#pragma once

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string_view>
#include <vector>

#include "hosq/bytes_view.hh"
#include "hosq/error.hh"
#include "hosq/reader_base.hh"

namespace hosq {

namespace json {

class SimpleReader {
 public:
  using input_type = std::string_view;

  SimpleReader() = default;

  struct StateType {
    StateType() { stack.reserve(32); };
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

    mutable Error last_error{Error::ok()};
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
      // std::cout << "Checkpointing at pos " << pos << ", stack size "
      //           << stack.size() << ", root_done " << root_done << ", end_done
      //           "
      //           << end_done << "\n";
      return Checkpoint{
          .pos = pos,
          .current = current,
          .stack = stack,
          .root_done = root_done,
          .end_done = end_done,
      };
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
    st.last_error = Error::ok();
  }

  auto next_token(StateType& st) -> bool {
    if (st.error_flag) [[unlikely]] {
      return false;
    }
    if (!next(st)) {
      return false;
    }
    st.last_error = Error::ok();
    return true;
  }

  void report_error(const StateType& st, Error err) const {
    if (!st.error_flag) {
      st.last_error = err;
      st.error_flag = true;
    }
  }

  [[nodiscard]] auto current_token(const StateType& st) const -> const Token& {
    if (st.error_flag) {
      return st.error_token;
    }
    return st.current;
  }

  [[nodiscard]] auto error(const StateType& st) const -> Error {
    return st.last_error;
  }

  [[nodiscard]] auto input(const StateType& st) const -> BytesView {
    return BytesView::from_string_view(st.input);
  }

 private:
  enum class StepResult : std::uint8_t {
    emitted,
    consumed,
    error,
  };

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

  static auto emit(Token& out, std::size_t offset, std::size_t length,
                   TokenType type, JsonTokenContext ctx) -> bool {
    out = Token{
        .offset = offset,
        .length = length,
        .type = type,
        .context = context(ctx),
    };
    return true;
  }

  static void skip_ws(StateType& st) {
    while (st.pos < st.input.size() && is_ws(st.input[st.pos])) {
      ++st.pos;
    }
  }

  static auto starts_with(const StateType& st, std::string_view s) -> bool {
    const auto rest = st.input.size() - st.pos;
    return rest >= s.size() &&
           std::char_traits<char>::compare(st.input.data() + st.pos, s.data(),
                                           s.size()) == 0;
  }

  auto fail(StateType& st, ErrorCode code) const -> bool {
    st.last_error = Error{
        .code = code,
        .offset = st.pos,
    };
    return false;
  }

  auto fail(StateType& st, ErrorCode code, std::size_t offset) const -> bool {
    st.last_error = Error{
        .code = code,
        .offset = offset,
    };
    return false;
  }

  auto next(StateType& st) const -> bool {
    while (true) {
      skip_ws(st);

      if (st.end_done) [[unlikely]] {
        return fail(st, ErrorCode::invalid_state);
      }

      if (st.stack.empty()) {
        if (!st.root_done) {
          st.root_done = true;
          return value(st, st.current);
        }
        if (st.pos != st.input.size()) [[unlikely]] {
          return fail(st, ErrorCode::trailing_data);
        }

        st.end_done = true;

        return emit(st.current, st.pos, 0, TokenType::End,
                    JsonTokenContext::none);
      }
      auto& frame = st.stack.back();
      const auto result = frame.kind == StateType::Kind::object
                              ? object_step(st, frame, st.current)
                              : array_step(st, frame, st.current);

      switch (result) {
        case StepResult::emitted:
          return true;

        case StepResult::consumed:
          continue;

        case StepResult::error:
          return false;
      }
    }
  }

  auto object_step(StateType& st, StateType::Frame& frame, Token& out) const
      -> StepResult {
    skip_ws(st);

    if (st.pos >= st.input.size()) [[unlikely]] {
      fail(st, ErrorCode::unexpected_eof);
      return StepResult::error;
    }

    switch (frame.expect) {
      case StateType::Expect::key_or_end:
        if (st.input[st.pos] == '}') {
          close(st, TokenType::ObjectEnd, out);
          return StepResult::emitted;
        }

        if (st.input[st.pos] != '"') {
          fail(st, ErrorCode::unexpected_token);
          return StepResult::error;
        }

        frame.expect = StateType::Expect::colon;
        if (!string(st, out)) {
          return StepResult::error;
        }
        return StepResult::emitted;

      case StateType::Expect::colon:
        if (st.input[st.pos] != ':') {
          fail(st, ErrorCode::unexpected_token);
          return StepResult::error;
        }

        ++st.pos;
        frame.expect = StateType::Expect::value;
        return StepResult::consumed;

      case StateType::Expect::value:
        frame.expect = StateType::Expect::comma_or_end;
        if (!value(st, out)) {
          return StepResult::error;
        }
        return StepResult::emitted;

      case StateType::Expect::comma_or_end:
        if (st.input[st.pos] == ',') {
          ++st.pos;
          frame.expect = StateType::Expect::key_or_end;
          return StepResult::consumed;
        }

        if (st.input[st.pos] == '}') {
          close(st, TokenType::ObjectEnd, out);
          return StepResult::emitted;
        }

        fail(st, ErrorCode::unexpected_token);
        return StepResult::error;

      case StateType::Expect::value_or_end:
        fail(st, ErrorCode::invalid_state);
        return StepResult::error;
    }

    fail(st, ErrorCode::invalid_state);
    return StepResult::error;
  }

  auto array_step(StateType& st, StateType::Frame& frame, Token& out) const
      -> StepResult {
    skip_ws(st);

    if (st.pos >= st.input.size()) [[unlikely]] {
      fail(st, ErrorCode::unexpected_eof);
      return StepResult::error;
    }

    switch (frame.expect) {
      case StateType::Expect::value_or_end:
        if (st.input[st.pos] == ']') {
          close(st, TokenType::ArrayEnd, out);
          return StepResult::emitted;
        }

        frame.expect = StateType::Expect::comma_or_end;
        if (!value(st, out)) {
          return StepResult::error;
        }
        return StepResult::emitted;

      case StateType::Expect::comma_or_end:
        if (st.input[st.pos] == ',') {
          ++st.pos;
          frame.expect = StateType::Expect::value_or_end;
          return StepResult::consumed;
        }

        if (st.input[st.pos] == ']') {
          close(st, TokenType::ArrayEnd, out);
          return StepResult::emitted;
        }

        fail(st, ErrorCode::unexpected_token);
        return StepResult::error;

      case StateType::Expect::key_or_end:
      case StateType::Expect::colon:
      case StateType::Expect::value:
        fail(st, ErrorCode::invalid_state);
        return StepResult::error;
    }

    fail(st, ErrorCode::invalid_state);
    return StepResult::error;
  }

  auto value(StateType& st, Token& out) const -> bool {
    skip_ws(st);

    if (st.pos >= st.input.size()) [[unlikely]] {
      return fail(st, ErrorCode::unexpected_eof);
    }
    const auto offset = st.pos;
    const char c = st.input[st.pos];
    switch (c) {
      case '{':
        ++st.pos;
        st.stack.push_back({
            StateType::Kind::object,
            StateType::Expect::key_or_end,
        });
        return emit(out, offset, 1, TokenType::ObjectBegin,
                    JsonTokenContext::none);
      case '[':
        ++st.pos;
        st.stack.push_back({
            StateType::Kind::array,
            StateType::Expect::value_or_end,
        });
        return emit(out, offset, 1, TokenType::ArrayBegin,
                    JsonTokenContext::none);
      case '"':
        return string(st, out);
      case 't':
        if (starts_with(st, "true")) {
          st.pos += 4;
          return emit(out, offset, 4, TokenType::Scalar,
                      JsonTokenContext::literal);
        }
        break;
      case 'f':
        if (starts_with(st, "false")) {
          st.pos += 5;
          return emit(out, offset, 5, TokenType::Scalar,
                      JsonTokenContext::literal);
        }
        break;
      case 'n':
        if (starts_with(st, "null")) {
          st.pos += 4;
          return emit(out, offset, 4, TokenType::Scalar,
                      JsonTokenContext::literal);
        }
        break;
      case '-':
        return number(st, out);
      default:
        if (is_digit(c)) {
          return number(st, out);
        }
        break;
    }
    return fail(st, ErrorCode::unexpected_token);
  }

  auto close(StateType& st, TokenType type, Token& out) const -> bool {
    const auto offset = st.pos;
    ++st.pos;
    st.stack.pop_back();
    return emit(out, offset, 1, type, JsonTokenContext::none);
  }

  auto string(StateType& st, Token& out) const -> bool {
    const auto quote_offset = st.pos;
    ++st.pos;  // opening quote
    const auto content_offset = st.pos;
    while (st.pos < st.input.size()) {
      const char c = st.input[st.pos++];
      if (c == '"') {
        const auto closing_quote_offset = st.pos - 1;
        return emit(out, content_offset, closing_quote_offset - content_offset,
                    TokenType::Scalar, JsonTokenContext::string);
      }
      if (c == '\\') {
        if (st.pos >= st.input.size()) [[unlikely]] {
          return fail(st, ErrorCode::unexpected_eof);
        }
        ++st.pos;
      }
    }
    return fail(st, ErrorCode::unexpected_eof, quote_offset);
  }

  auto number(StateType& st, Token& out) const -> bool {
    const auto offset = st.pos;
    while (st.pos < st.input.size() && is_number_char(st.input[st.pos])) {
      ++st.pos;
    }
    return emit(out, offset, st.pos - offset, TokenType::Scalar,
                JsonTokenContext::literal);
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
