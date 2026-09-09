#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include "hosq/bytes_view.hh"
#include "hosq/error.hh"
#include "hosq/reader_base.hh"

namespace hosq {

namespace json {

class SimpleReader : public ReaderBase<SimpleReader> {
 public:
  using input_type = std::string_view;

  SimpleReader() = default;

  enum class Context : uint8_t {
    None = 0b0000'0000,
    ArrayValue = 0b0000'0010,
    ArrayValue2 = 0b0000'0011,
    ObjectKey = 0b0000'0100,
    ObjectVlaue = 0b0000'0101,
  };

  struct state_type {  // NOLINT
    std::string_view input = {};
    size_t pos = 0;
    bool end_flag = false;
    std::vector<Context> context;
    Token token{.offset = 0, .length = 0, .type = TokenType::End, .context = 0};

    mutable bool has_error = false;
    mutable Error error;

    struct Checkpoint {
      size_t pos;
      bool end_flag;
      std::vector<Context> context;
      Token token;
    };

    [[nodiscard]] auto checkpoint() const -> Checkpoint {
      return Checkpoint{
          .pos = pos, .end_flag = end_flag, .context = context, .token = token};
    }

    auto rollback(Checkpoint checkpoint) -> void {
      pos = checkpoint.pos;
      end_flag = checkpoint.end_flag;
      context = std::move(checkpoint.context);
      token = checkpoint.token;
    }
  };

  static_assert(ReaderStateConcept<state_type>);

  auto reset(state_type& state, input_type input) -> void {
    state.input = input;
    state.pos = 0;
    state.end_flag = false;
    state.context.clear();
    state.token = Token{.offset = 0,
                        .length = 0,
                        .type = TokenType::End,
                        .context = static_cast<uint8_t>(JsonTokenContext::none)};
    state.has_error = false;
    state.error = Error::ok();
  }

  [[nodiscard]]
  auto next_token_impl(state_type& state) -> bool {
    if (state.has_error) [[unlikely]] {
      return false;
    }
    if (state.end_flag) [[unlikely]] {
      state.end_flag = false;
      if (static_cast<uint8_t>(state.context.back()) & 0b0000'0010) {
        // Array End
        state.token.type = TokenType::ArrayEnd;
        state.token.length = 1;
        state.token.offset = state.pos;
        state.token.context = 0;
        state.context.pop_back();
        state.pos++;
        return after_container_end(state);
      } else if (static_cast<uint8_t>(state.context.back()) & 0b0000'0100) {
        // Object End
        state.token.type = TokenType::ObjectEnd;
        state.token.length = 1;
        state.token.offset = state.pos;
        state.token.context = 0;
        state.context.pop_back();
        state.pos++;
        return after_container_end(state);
      }
      return false;
    }

    while (state.pos < state.input.size() && is_ws(state.input[state.pos])) {
      state.pos++;
    }

    if (state.pos >= state.input.size()) {
      if (state.token.type == TokenType::End) {
        state.has_error = true;
        state.error = Error{
            .code = ErrorCode::unexpected_eof,
            .offset = state.pos,
            .detail = "expected JSON value",
        };
        return false;
      }
      if (state.context.empty()) {
        state.token = Token{.offset = state.pos,
                            .length = 0,
                            .type = TokenType::End,
                            .context = 0};
        return true;
      }
      state.has_error = true;
      state.error = Error{
          .code = ErrorCode::unexpected_eof,
          .offset = state.pos,
          .detail = "Unexpected end of input",
      };
      return false;
    }

    while (state.pos < state.input.size()) {
      switch (state.input[state.pos]) {
        case '{':
          if (!state.context.empty() &&
              state.context.back() == Context::ObjectKey) [[unlikely]] {
            state.has_error = true;
            state.error = Error{
                .code = ErrorCode::unexpected_token,
                .offset = state.pos,
                .detail = "Unexpected token '{'",
            };
            return false;
          }
          state.token.offset = state.pos;
          state.token.length = 1;
          state.token.type = TokenType::ObjectBegin;
          state.token.context = 0;
          state.context.push_back(Context::ObjectKey);

          do {  // NOLINT
            state.pos++;
            if (state.pos >= state.input.size()) [[unlikely]] {
              state.has_error = true;
              state.error = Error{
                  .code = ErrorCode::unexpected_eof,
                  .offset = state.pos,
                  .detail = "Unexpected end of input after '{'",
              };
              return false;
            }
          } while (is_ws(state.input[state.pos]));

          if (state.input[state.pos] == '}') {
            state.end_flag = true;
          }

          return true;
        case '}':
          // This should not be happened because the end_flag should be set when
          // we encounter '}' in parse_string or parse_literal, thus this is
          // unexpected token.
          state.has_error = true;
          state.error = Error{
              .code = ErrorCode::unexpected_token,
              .offset = state.pos,
              .detail = "Unexpected token '}'",
          };
          return false;
        case '[':
          if (!state.context.empty() &&
              state.context.back() == Context::ObjectKey) [[unlikely]] {
            state.has_error = true;
            state.error = Error{
                .code = ErrorCode::unexpected_token,
                .offset = state.pos,
                .detail = "Unexpected token '['",
            };
            return false;
          }
          state.token.offset = state.pos;
          state.token.length = 1;
          state.token.type = TokenType::ArrayBegin;
          state.token.context = 0;
          state.context.push_back(Context::ArrayValue);

          do {  // NOLINT
            state.pos++;
            if (state.pos >= state.input.size()) [[unlikely]] {
              state.has_error = true;
              state.error = Error{
                  .code = ErrorCode::unexpected_eof,
                  .offset = state.pos,
                  .detail = "Unexpected end of input after '['",
              };
              return false;
            }
          } while (is_ws(state.input[state.pos]));

          if (state.input[state.pos] == ']') {
            state.end_flag = true;
          }

          return true;
        case ']':
          // This should not be happened because the end_flag should be set when
          // we encounter ']' in parse_string or parse_literal, thus this is
          // unexpected token.
          state.has_error = true;
          state.error = Error{
              .code = ErrorCode::unexpected_token,
              .offset = state.pos,
              .detail = "Unexpected token ']'",
          };
          return false;
        default:
          state.token.offset = state.pos;
          if (!parse_value(state)) [[unlikely]] {
            return false;
          }
          break;
      }
      return true;
    }
    state.token = Token{
        .offset = state.pos, .length = 0, .type = TokenType::End, .context = 0};
    return true;
  }

  [[nodiscard]] auto current_token(const state_type& state) const
      -> const Token& {
    return state.token;
  }

  [[nodiscard]] auto is_ws(char c) const -> bool {
    return c == ' ' || c == '\n' || c == '\t' || c == '\r';
  }

  auto parse_value(state_type& state) -> bool {
    while (is_ws(state.input[state.pos])) {
      state.pos++;
      if (state.pos >= state.input.size()) [[unlikely]] {
        state.has_error = true;
        return false;
      }
      state.token.offset = state.pos;
    }
    switch (state.input[state.pos]) {
      case '"':
        state.pos++;
        state.token.offset = state.pos;
        state.token.type = TokenType::Scalar;
        state.token.context = static_cast<uint8_t>(JsonTokenContext::string);
        return parse_string(state);
      case ',':
      case ':':
      case ']':
      case '}':
        state.has_error = true;
        state.error = Error{
            .code = ErrorCode::unexpected_token,
            .offset = state.pos,
            .detail = "Unexpected token while parsing value",
        };
        return false;
      default:
        state.token.type = TokenType::Scalar;
        state.token.context = static_cast<uint8_t>(JsonTokenContext::literal);
        return parse_literal(state);
    }
  }

  auto parse_string(state_type& state) -> bool {
    while (state.pos < state.input.size()) [[likely]] {
      switch (state.input[state.pos]) {
        case '"':
          state.token.length = state.pos - state.token.offset;
          if (state.context.empty()) {
            do {  // NOLINT
              state.pos++;
            } while (state.pos < state.input.size() &&
                     is_ws(state.input[state.pos]));
            return true;
          }
          do {  // NOLINT
            state.pos++;
            if (state.pos >= state.input.size()) [[unlikely]] {
              if (state.context.empty()) {
                return true;
              }
              state.has_error = true;
              state.error = Error{
                  .code = ErrorCode::unexpected_eof,
                  .offset = state.pos,
                  .detail = "Unexpected end of input after string",
              };
              return false;
            }
          } while (is_ws(state.input[state.pos]));
          switch (state.input[state.pos]) {
            case ':':
              if (state.context.back() != Context::ObjectKey) [[unlikely]] {
                state.has_error = true;
                state.error = Error{
                    .code = ErrorCode::unexpected_token,
                    .offset = state.pos,
                    .detail = "Unexpected token ':' after string",
                };
                return false;
              }
              state.pos++;
              state.context.back() = static_cast<Context>(
                  static_cast<uint8_t>(state.context.back()) ^
                  1);  // ObjectKey -> ObjectValue
              break;
            case ',':
              if ((static_cast<uint8_t>(state.context.back()) & 0b0000'0011) ==
                  0) [[unlikely]] {
                state.has_error = true;
                state.error = Error{
                    .code = ErrorCode::unexpected_token,
                    .offset = state.pos,
                    .detail = "Unexpected token ',' after string",
                };
                return false;
              }
              state.pos++;
              state.context.back() = static_cast<Context>(
                  static_cast<uint8_t>(state.context.back()) ^
                  1);  // ObjectKey -> ObjectValue
              break;
            case ']':
              if ((static_cast<uint8_t>(state.context.back()) & 0b0000'0010) ==
                  0) [[unlikely]] {
                state.has_error = true;
                state.error = Error{
                    .code = ErrorCode::unexpected_token,
                    .offset = state.pos,
                    .detail = "Unexpected token ']' after string",
                };
                return false;
              }
              state.end_flag = true;
              break;
            case '}':
              if (state.context.back() == Context::ObjectKey ||
                  (static_cast<uint8_t>(state.context.back()) & 0b0000'0100) ==
                      0) [[unlikely]] {
                state.has_error = true;
                state.error = Error{
                    .code = ErrorCode::unexpected_token,
                    .offset = state.pos,
                    .detail = "Unexpected token '}' after string",
                };
                return false;
              }
              state.end_flag = true;
              break;
            default:
              state.has_error = true;
              state.error = Error{
                  .code = ErrorCode::unexpected_token,
                  .offset = state.pos,
                  .detail = "Invalid token after string",
              };
              return false;
          }
          return true;
        case '\\':
          state.pos += 2;
          break;
        default:
          state.pos++;
          break;
      }
    }
    state.has_error = true;
    state.error = Error{
        .code = ErrorCode::unexpected_eof,
        .offset =
            state.pos > state.input.size() ? state.input.size() : state.pos,
        .detail = "Unexpected end of input while parsing string",
    };
    return false;
  }

  auto report_error(const state_type& state, Error error) const -> void {
    state.has_error = true;
    state.error = std::move(error);
  }

  [[nodiscard]] auto error(const state_type& state) const -> Error {
    return state.error;
  }

  [[nodiscard]] auto input(const state_type& state) const -> BytesView {
    return BytesView::from_string_view(state.input);
  }

  auto after_container_end(state_type& state) -> bool {
    while (state.pos < state.input.size() && is_ws(state.input[state.pos])) {
      state.pos++;
    }
    if (state.context.empty()) {
      if (state.pos < state.input.size()) {
        state.has_error = true;
        state.error = Error{
            .code = ErrorCode::trailing_data,
            .offset = state.pos,
            .detail = "non-whitespace data after root value",
        };
        return false;
      }
      return true;
    }
    if (state.pos >= state.input.size()) [[unlikely]] {
      state.has_error = true;
      state.error = Error{
          .code = ErrorCode::unexpected_eof,
          .offset = state.pos,
          .detail = "Unexpected end of input after container",
      };
      return false;
    }
    switch (state.input[state.pos]) {
      case ',':
        if (state.context.back() == Context::ObjectKey) [[unlikely]] {
          state.has_error = true;
          state.error = Error{
              .code = ErrorCode::unexpected_token,
              .offset = state.pos,
              .detail = "Unexpected token ',' after container",
          };
          return false;
        }
        state.pos++;
        state.context.back() =
            static_cast<Context>(static_cast<uint8_t>(state.context.back()) ^ 1);
        return true;
      case ']':
        if ((static_cast<uint8_t>(state.context.back()) & 0b0000'0010) == 0)
            [[unlikely]] {
          state.has_error = true;
          state.error = Error{
              .code = ErrorCode::unexpected_token,
              .offset = state.pos,
              .detail = "Unexpected token ']' after container",
          };
          return false;
        }
        state.end_flag = true;
        return true;
      case '}':
        if (static_cast<uint8_t>(state.context.back()) & 0b0000'0010)
            [[unlikely]] {
          state.has_error = true;
          state.error = Error{
              .code = ErrorCode::unexpected_token,
              .offset = state.pos,
              .detail = "Unexpected token '}' after container",
          };
          return false;
        }
        state.end_flag = true;
        return true;
      default:
        state.has_error = true;
        state.error = Error{
            .code = ErrorCode::unexpected_token,
            .offset = state.pos,
            .detail = "Invalid token after container",
        };
        return false;
    }
  }

  auto parse_literal(state_type& state) -> bool {
    while (state.pos < state.input.size()) {
      switch (state.input[state.pos]) {
        case ']':
          if ((static_cast<uint8_t>(state.context.back()) & 0b0000'0010) == 0)
              [[unlikely]] {
            state.has_error = true;
            state.error = Error{
                .code = ErrorCode::unexpected_token,
                .offset = state.pos,
                .detail = "Unexpected token ']' after literal",
            };
            return false;
          }
          state.token.length = state.pos - state.token.offset;
          state.end_flag = true;
          return true;
        case '}':
          if (static_cast<uint8_t>(state.context.back()) & 0b0000'0010)
              [[unlikely]] {
            state.has_error = true;
            state.error = Error{
                .code = ErrorCode::unexpected_token,
                .offset = state.pos,
                .detail = "Unexpected token '}' after literal",
            };
            return false;
          }
          state.token.length = state.pos - state.token.offset;
          state.end_flag = true;
          return true;
        case ',':
          if (state.context.back() == Context::ObjectKey) [[unlikely]] {
            state.has_error = true;
            state.error = Error{
                .code = ErrorCode::unexpected_token,
                .offset = state.pos,
                .detail = "Unexpected token ',' after literal",
            };
            return false;
          }
          state.token.length = state.pos - state.token.offset;
          state.pos++;
          state.context.back() = static_cast<Context>(
              static_cast<uint8_t>(state.context.back()) ^
              1);  // ArrayValue -> ArrayValue2, ObjectValue -> ObjectKey
          return true;
        case ' ':
        case '\n':
        case '\t':
        case '\r':
          state.token.length = state.pos - state.token.offset;
          if (state.context.empty()) {
            do {  // NOLINT
              state.pos++;
            } while (state.pos < state.input.size() &&
                     is_ws(state.input[state.pos]));
            return true;
          }
          do {  // NOLINT
            state.pos++;
            if (state.pos >= state.input.size()) [[unlikely]] {
              state.has_error = true;
              state.error = Error{
                  .code = ErrorCode::unexpected_eof,
                  .offset = state.pos,
                  .detail = "Unexpected end of input while parsing literal",
              };
              return false;
            }
          } while (is_ws(state.input[state.pos]));
          switch (state.input[state.pos]) {
            case ']':
              if ((static_cast<uint8_t>(state.context.back()) & 0b0000'0010) ==
                  0) [[unlikely]] {
                state.has_error = true;
                state.error = Error{
                    .code = ErrorCode::unexpected_token,
                    .offset = state.pos,
                    .detail = "Unexpected token ']' after literal",
                };

                return false;
              }
              state.end_flag = true;
              return true;
            case '}':
              if (static_cast<uint8_t>(state.context.back()) & 0b0000'0010)
                  [[unlikely]] {
                state.has_error = true;
                state.error = Error{
                    .code = ErrorCode::unexpected_token,
                    .offset = state.pos,
                    .detail = "Unexpected token '}' after literal",
                };
                return false;
              }
              state.end_flag = true;
              return true;
            case ',':
              if (state.context.back() == Context::ObjectKey) [[unlikely]] {
                state.has_error = true;
                state.error = Error{
                    .code = ErrorCode::unexpected_token,
                    .offset = state.pos,
                    .detail = "Unexpected token ',' after literal",
                };
                return false;
              }
              state.pos++;
              state.context.back() = static_cast<Context>(
                  static_cast<uint8_t>(state.context.back()) ^
                  1);  // ArrayValue -> ArrayValue2, ObjectValue -> ObjectKey
              return true;
            default:
              state.has_error = true;
              state.error = Error{
                  .code = ErrorCode::unexpected_token,
                  .offset = state.pos,
                  .detail = "Invalid token after literal",
              };
              return false;
          }
        default:
          break;
      }
      state.pos++;
    }
    if (state.context.empty()) {
      state.token.length = state.pos - state.token.offset;
      return true;
    }
    state.has_error = true;
    state.error = Error{
        .code = ErrorCode::unexpected_eof,
        .offset = state.pos,
        .detail = "Unexpected end of input while parsing literal",
    };
    return false;
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
