#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include "hosq/error.hh"
#include "hosq/reader_base.hh"

namespace hosq {

namespace json {

class SimpleReader {
 public:
  using input_type = std::string_view;

  SimpleReader() = default;

  struct StateType {
    input_type input{};
    std::size_t pos{0};

    Token current{
        .offset = 0,
        .length = 0,
        .type = TokenType::End,
        .context = static_cast<std::uint8_t>(JsonTokenContext::none),
    };

    enum class Kind : std::uint8_t {
      object,
      array,
    };

    enum class Expect : std::uint8_t {
      key_or_end,
      colon,
      value_or_end,
      value,
      comma_or_end,
    };

    struct Frame {
      Kind kind;
      Expect expect;
    };

    std::vector<Frame> stack{};

    bool root_done{false};
    bool end_done{false};
    mutable Error last_error{};
    mutable bool error_flag{false};
    mutable Token error_token{
        .offset = 0,
        .length = 0,
        .type = TokenType::Error,
        .context = 0,
    };

    struct Checkpoint {
      std::size_t pos;
      Token current;
      std::vector<Frame> stack;
      bool root_done;
      bool end_done;
    };

    [[nodiscard]] auto checkpoint() const -> Checkpoint {
      return {pos, current, stack, root_done, end_done};
    }

    auto rollback(const Checkpoint& cp) -> void {
      pos = cp.pos;
      current = cp.current;
      stack = cp.stack;
      root_done = cp.root_done;
      end_done = cp.end_done;
      // error_flag and last_error intentionally NOT restored
    }
  };

  using state_type = StateType;

  void reset(StateType& st, input_type input) {
    st = {};
    st.input = input;
  }

  auto next_token(StateType& st) -> bool {
    if (st.error_flag) [[unlikely]]
      return false;
    Token tok{};
    if (!next(st, tok)) {
      return false;
    }
    st.current = tok;
    st.last_error = {};
    return true;
  }

  // Sets the error flag and records err (first call wins; subsequent calls are
  // no-ops). After this, current_token() always returns a TokenType::Error
  // token.
  void report_error(const StateType& st, Error err) const {
    if (!st.error_flag) {
      st.last_error = err;
      st.error_flag = true;
    }
  }

  [[nodiscard]] auto current_token(const StateType& st) const -> const Token& {
    if (st.error_flag) return st.error_token;
    return st.current;
  }

  [[nodiscard]] auto error(const StateType& st) const -> Error {
    return st.last_error;
  }

  [[nodiscard]] auto input(const StateType& st) const -> const input_type& {
    return st.input;
  }

 private:
  static auto context(JsonTokenContext c) -> std::uint8_t {
    return static_cast<std::uint8_t>(c);
  }

  static auto is_ws(char c) -> bool {
    return c == ' ' || c == '\n' || c == '\r' || c == '\t';
  }

  static auto is_digit(char c) -> bool { return '0' <= c && c <= '9'; }

  static auto is_number_char(char c) -> bool {
    return is_digit(c) || c == '-' || c == '+' || c == '.' || c == 'e' ||
           c == 'E';
  }

  static void skip_ws(StateType& st) {
    while (st.pos < st.input.size() && is_ws(st.input[st.pos])) {
      ++st.pos;
    }
  }

  static auto starts_with(const StateType& st, std::string_view s) -> bool {
    return st.input.substr(st.pos, s.size()) == s;
  }

  auto next(StateType& st, Token& out) const -> bool {
    skip_ws(st);

    if (st.end_done) {
      st.last_error = Error{.code = ErrorCode::invalid_state, .offset = st.pos};
      return false;
    }

    if (st.stack.empty()) {
      if (!st.root_done) {
        st.root_done = true;
        return value(st, out);
      }

      if (st.pos != st.input.size()) {
        st.last_error =
            Error{.code = ErrorCode::trailing_data, .offset = st.pos};
        return false;
      }

      st.end_done = true;
      out = Token{
          .offset = st.pos,
          .length = 0,
          .type = TokenType::End,
          .context = context(JsonTokenContext::none),
      };
      return true;
    }

    auto& frame = st.stack.back();

    if (frame.kind == StateType::Kind::object) {
      return object_next(st, frame, out);
    }

    return array_next(st, frame, out);
  }

  auto object_next(StateType& st, StateType::Frame& frame, Token& out) const
      -> bool {
    skip_ws(st);

    if (st.pos >= st.input.size()) {
      st.last_error = Error{.code = ErrorCode::unexpected_eof, .offset = st.pos};
      return false;
    }

    switch (frame.expect) {
      case StateType::Expect::key_or_end:
        if (st.input[st.pos] == '}') {
          return close(st, TokenType::ObjectEnd, out);
        }

        if (st.input[st.pos] != '"') {
          st.last_error =
              Error{.code = ErrorCode::unexpected_token, .offset = st.pos};
          return false;
        }

        frame.expect = StateType::Expect::colon;
        return string(st, out);

      case StateType::Expect::colon:
        if (st.input[st.pos] != ':') {
          st.last_error =
              Error{.code = ErrorCode::unexpected_token, .offset = st.pos};
          return false;
        }

        ++st.pos;
        frame.expect = StateType::Expect::value;
        return next(st, out);

      case StateType::Expect::value:
        frame.expect = StateType::Expect::comma_or_end;
        return value(st, out);

      case StateType::Expect::comma_or_end:
        if (st.input[st.pos] == ',') {
          ++st.pos;
          frame.expect = StateType::Expect::key_or_end;
          return next(st, out);
        }

        if (st.input[st.pos] == '}') {
          return close(st, TokenType::ObjectEnd, out);
        }

        st.last_error =
            Error{.code = ErrorCode::unexpected_token, .offset = st.pos};
        return false;

      case StateType::Expect::value_or_end:
        st.last_error =
            Error{.code = ErrorCode::invalid_state, .offset = st.pos};
        return false;
    }

    st.last_error = Error{.code = ErrorCode::invalid_state, .offset = st.pos};
    return false;
  }

  auto array_next(StateType& st, StateType::Frame& frame, Token& out) const
      -> bool {
    skip_ws(st);

    if (st.pos >= st.input.size()) {
      st.last_error = Error{.code = ErrorCode::unexpected_eof, .offset = st.pos};
      return false;
    }

    switch (frame.expect) {
      case StateType::Expect::value_or_end:
        if (st.input[st.pos] == ']') {
          return close(st, TokenType::ArrayEnd, out);
        }

        frame.expect = StateType::Expect::comma_or_end;
        return value(st, out);

      case StateType::Expect::comma_or_end:
        if (st.input[st.pos] == ',') {
          ++st.pos;
          frame.expect = StateType::Expect::value_or_end;
          return next(st, out);
        }

        if (st.input[st.pos] == ']') {
          return close(st, TokenType::ArrayEnd, out);
        }

        st.last_error =
            Error{.code = ErrorCode::unexpected_token, .offset = st.pos};
        return false;

      case StateType::Expect::key_or_end:
      case StateType::Expect::colon:
      case StateType::Expect::value:
        st.last_error =
            Error{.code = ErrorCode::invalid_state, .offset = st.pos};
        return false;
    }

    st.last_error = Error{.code = ErrorCode::invalid_state, .offset = st.pos};
    return false;
  }

  auto value(StateType& st, Token& out) const -> bool {
    skip_ws(st);

    if (st.pos >= st.input.size()) {
      st.last_error = Error{.code = ErrorCode::unexpected_eof, .offset = st.pos};
      return false;
    }

    const auto offset = st.pos;
    const char c = st.input[st.pos];

    if (c == '{') {
      ++st.pos;
      st.stack.push_back({
          StateType::Kind::object,
          StateType::Expect::key_or_end,
      });
      out = Token{
          .offset = offset,
          .length = 1,
          .type = TokenType::ObjectBegin,
          .context = context(JsonTokenContext::none),
      };
      return true;
    }

    if (c == '[') {
      ++st.pos;
      st.stack.push_back({
          StateType::Kind::array,
          StateType::Expect::value_or_end,
      });
      out = Token{
          .offset = offset,
          .length = 1,
          .type = TokenType::ArrayBegin,
          .context = context(JsonTokenContext::none),
      };
      return true;
    }

    if (c == '"') {
      return string(st, out);
    }

    if (c == '-' || is_digit(c)) {
      return number(st, out);
    }

    if (starts_with(st, "true")) {
      st.pos += 4;
      out = Token{
          .offset = offset,
          .length = 4,
          .type = TokenType::Scalar,
          .context = context(JsonTokenContext::literal),
      };
      return true;
    }

    if (starts_with(st, "false")) {
      st.pos += 5;
      out = Token{
          .offset = offset,
          .length = 5,
          .type = TokenType::Scalar,
          .context = context(JsonTokenContext::literal),
      };
      return true;
    }

    if (starts_with(st, "null")) {
      st.pos += 4;
      out = Token{
          .offset = offset,
          .length = 4,
          .type = TokenType::Scalar,
          .context = context(JsonTokenContext::literal),
      };
      return true;
    }

    st.last_error = Error{.code = ErrorCode::unexpected_token, .offset = st.pos};
    return false;
  }

  auto close(StateType& st, TokenType type, Token& out) const -> bool {
    const auto offset = st.pos;

    ++st.pos;
    st.stack.pop_back();

    out = Token{
        .offset = offset,
        .length = 1,
        .type = type,
        .context = context(JsonTokenContext::none),
    };
    return true;
  }

  auto string(StateType& st, Token& out) const -> bool {
    const auto quote_offset = st.pos;

    ++st.pos;  // opening quote

    const auto content_offset = st.pos;

    while (st.pos < st.input.size()) {
      const char c = st.input[st.pos++];

      if (c == '"') {
        const auto closing_quote_offset = st.pos - 1;

        out = Token{
            .offset = content_offset,
            .length = closing_quote_offset - content_offset,
            .type = TokenType::Scalar,
            .context = context(JsonTokenContext::string),
        };
        return true;
      }

      if (c == '\\') {
        if (st.pos >= st.input.size()) {
          st.last_error =
              Error{.code = ErrorCode::unexpected_eof, .offset = st.pos};
          return false;
        }

        ++st.pos;
      }
    }

    st.last_error =
        Error{.code = ErrorCode::unexpected_eof, .offset = quote_offset};
    return false;
  }

  auto number(StateType& st, Token& out) const -> bool {
    const auto offset = st.pos;

    while (st.pos < st.input.size() && is_number_char(st.input[st.pos])) {
      ++st.pos;
    }

    out = Token{
        .offset = offset,
        .length = st.pos - offset,
        .type = TokenType::Scalar,
        .context = context(JsonTokenContext::literal),
    };
    return true;
  }
};

}  // namespace json

template <>
struct ReaderTraits<json::SimpleReader> {
  static constexpr Format format = Format::json;
  static constexpr bool is_binary_format = false;
};

static_assert(ReaderConcept<json::SimpleReader>);

}  // namespace hosq
