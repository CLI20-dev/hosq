#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include "hosq/error.hh"
#include "hosq/reader_base.hh"
#include "hosq/result.hh"

namespace hosq {

namespace json {

class SimpleReader {
 public:
  using input_type = std::string_view;

  SimpleReader() = default;
  ~SimpleReader() = default;

  SimpleReader(const SimpleReader&) = delete;
  auto operator=(const SimpleReader&) -> SimpleReader& = delete;

  SimpleReader(SimpleReader&&) = delete;
  auto operator=(SimpleReader&&) -> SimpleReader& = delete;

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
    Error last_error{};
  };

  using state_type = StateType;

  void reset(StateType& st, input_type input) {
    st = {};
    st.input = input;
  }

  auto next_token(StateType& st) -> bool {
    const auto r = next(st);
    if (!r) {
      st.last_error = r.error();
      return false;
    }

    st.current = *r;
    st.last_error = {};
    return true;
  }

  [[nodiscard]] auto current_token(const StateType& st) const -> const Token& {
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

  static auto token(Token t) -> Result<Token, Error> {
    return Result<Token, Error>{t};
  }

  static auto error(ErrorCode code, std::size_t offset) -> Result<Token, Error> {
    return Result<Token, Error>{
        Unexpected{Error{.code = code, .offset = offset}}};
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

  auto next(StateType& st) const -> Result<Token, Error> {
    skip_ws(st);

    if (st.end_done) {
      return error(ErrorCode::invalid_state, st.pos);
    }

    if (st.stack.empty()) {
      if (!st.root_done) {
        st.root_done = true;
        return value(st);
      }

      if (st.pos != st.input.size()) {
        return error(ErrorCode::trailing_data, st.pos);
      }

      st.end_done = true;
      return token(Token{
          .offset = st.pos,
          .length = 0,
          .type = TokenType::End,
          .context = context(JsonTokenContext::none),
      });
    }

    auto& frame = st.stack.back();

    if (frame.kind == StateType::Kind::object) {
      return object_next(st, frame);
    }

    return array_next(st, frame);
  }

  auto object_next(StateType& st, StateType::Frame& frame) const
      -> Result<Token, Error> {
    skip_ws(st);

    if (st.pos >= st.input.size()) {
      return error(ErrorCode::unexpected_eof, st.pos);
    }

    switch (frame.expect) {
      case StateType::Expect::key_or_end:
        if (st.input[st.pos] == '}') {
          return close(st, TokenType::ObjectEnd);
        }

        if (st.input[st.pos] != '"') {
          return error(ErrorCode::unexpected_token, st.pos);
        }

        frame.expect = StateType::Expect::colon;
        return string(st);

      case StateType::Expect::colon:
        if (st.input[st.pos] != ':') {
          return error(ErrorCode::unexpected_token, st.pos);
        }

        ++st.pos;
        frame.expect = StateType::Expect::value;
        return next(st);

      case StateType::Expect::value:
        frame.expect = StateType::Expect::comma_or_end;
        return value(st);

      case StateType::Expect::comma_or_end:
        if (st.input[st.pos] == ',') {
          ++st.pos;
          frame.expect = StateType::Expect::key_or_end;
          return next(st);
        }

        if (st.input[st.pos] == '}') {
          return close(st, TokenType::ObjectEnd);
        }

        return error(ErrorCode::unexpected_token, st.pos);

      case StateType::Expect::value_or_end:
        return error(ErrorCode::invalid_state, st.pos);
    }

    return error(ErrorCode::invalid_state, st.pos);
  }

  auto array_next(StateType& st, StateType::Frame& frame) const
      -> Result<Token, Error> {
    skip_ws(st);

    if (st.pos >= st.input.size()) {
      return error(ErrorCode::unexpected_eof, st.pos);
    }

    switch (frame.expect) {
      case StateType::Expect::value_or_end:
        if (st.input[st.pos] == ']') {
          return close(st, TokenType::ArrayEnd);
        }

        frame.expect = StateType::Expect::comma_or_end;
        return value(st);

      case StateType::Expect::comma_or_end:
        if (st.input[st.pos] == ',') {
          ++st.pos;
          frame.expect = StateType::Expect::value_or_end;
          return next(st);
        }

        if (st.input[st.pos] == ']') {
          return close(st, TokenType::ArrayEnd);
        }

        return error(ErrorCode::unexpected_token, st.pos);

      case StateType::Expect::key_or_end:
      case StateType::Expect::colon:
      case StateType::Expect::value:
        return error(ErrorCode::invalid_state, st.pos);
    }

    return error(ErrorCode::invalid_state, st.pos);
  }

  auto value(StateType& st) const -> Result<Token, Error> {
    skip_ws(st);

    if (st.pos >= st.input.size()) {
      return error(ErrorCode::unexpected_eof, st.pos);
    }

    const auto offset = st.pos;
    const char c = st.input[st.pos];

    if (c == '{') {
      ++st.pos;
      st.stack.push_back({
          StateType::Kind::object,
          StateType::Expect::key_or_end,
      });

      return token(Token{
          .offset = offset,
          .length = 1,
          .type = TokenType::ObjectBegin,
          .context = context(JsonTokenContext::none),
      });
    }

    if (c == '[') {
      ++st.pos;
      st.stack.push_back({
          StateType::Kind::array,
          StateType::Expect::value_or_end,
      });

      return token(Token{
          .offset = offset,
          .length = 1,
          .type = TokenType::ArrayBegin,
          .context = context(JsonTokenContext::none),
      });
    }

    if (c == '"') {
      return string(st);
    }

    if (c == '-' || is_digit(c)) {
      return number(st);
    }

    if (starts_with(st, "true")) {
      st.pos += 4;
      return token(Token{
          .offset = offset,
          .length = 4,
          .type = TokenType::Scalar,
          .context = context(JsonTokenContext::literal),
      });
    }

    if (starts_with(st, "false")) {
      st.pos += 5;
      return token(Token{
          .offset = offset,
          .length = 5,
          .type = TokenType::Scalar,
          .context = context(JsonTokenContext::literal),
      });
    }

    if (starts_with(st, "null")) {
      st.pos += 4;
      return token(Token{
          .offset = offset,
          .length = 4,
          .type = TokenType::Scalar,
          .context = context(JsonTokenContext::literal),
      });
    }

    return error(ErrorCode::unexpected_token, st.pos);
  }

  auto close(StateType& st, TokenType type) const -> Result<Token, Error> {
    const auto offset = st.pos;

    ++st.pos;
    st.stack.pop_back();

    return token(Token{
        .offset = offset,
        .length = 1,
        .type = type,
        .context = context(JsonTokenContext::none),
    });
  }

  auto string(StateType& st) const -> Result<Token, Error> {
    const auto quote_offset = st.pos;

    ++st.pos;  // opening quote

    const auto content_offset = st.pos;

    while (st.pos < st.input.size()) {
      const char c = st.input[st.pos++];

      if (c == '"') {
        const auto closing_quote_offset = st.pos - 1;

        return token(Token{
            .offset = content_offset,
            .length = closing_quote_offset - content_offset,
            .type = TokenType::Scalar,
            .context = context(JsonTokenContext::string),
        });
      }

      if (c == '\\') {
        if (st.pos >= st.input.size()) {
          return error(ErrorCode::unexpected_eof, st.pos);
        }

        ++st.pos;
      }
    }

    return error(ErrorCode::unexpected_eof, quote_offset);
  }

  auto number(StateType& st) const -> Result<Token, Error> {
    const auto offset = st.pos;

    while (st.pos < st.input.size() && is_number_char(st.input[st.pos])) {
      ++st.pos;
    }

    return token(Token{
        .offset = offset,
        .length = st.pos - offset,
        .type = TokenType::Scalar,
        .context = context(JsonTokenContext::literal),
    });
  }
};

}  // namespace json

template <>
struct ReaderTraits<json::SimpleReader> {
  static constexpr Format format = Format::json;
  static constexpr bool is_binary_format = false;
};

}  // namespace hosq
