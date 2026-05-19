#pragma once

#include <bitset>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <type_traits>

#include "ser20/error.hh"
#include "ser20/reader_base.hh"
#include "ser20/result.hh"

namespace ser20 {
namespace json {

template <size_t MaxDepth = 1024>
class Reader {
 public:
  using input_type = std::string_view;
  explicit Reader(std::string_view input) noexcept : input_(input) {}

  Reader(const Reader&) = default;
  auto operator=(const Reader&) -> Reader& = default;

  Reader(Reader&&) = delete;
  auto operator=(Reader&&) -> Reader& = delete;

  ~Reader() = default;

  [[nodiscard]]
  auto next_token() -> Result<void, Error> {
    auto result = read_one();
    if (!result) {
      return unexpected(result.error());
    }
    current_ = *result;
    return {};
  }

  [[nodiscard]]
  auto current_token() noexcept -> Token& {
    return current_;
  }

  [[nodiscard]]
  auto current_token() const noexcept -> const Token& {
    return current_;
  }

  [[nodiscard]]
  auto input() const noexcept -> std::string_view {
    return input_;
  }

  auto advance(std::size_t n) const -> Result<void, Error> {
    switch (current_.type) {
      case TokenType::ScalarBegin:
      case TokenType::ObjectBegin:
      case TokenType::ArrayBegin:
        break;

      case TokenType::ObjectEnd:
      case TokenType::ArrayEnd:
      case TokenType::End:
        return unexpected(Error{
            .code = ErrorCode::unexpected_token,
            .offset = current_.offset,
        });
    }

    if (n > input_.size() - pos_) {
      return unexpected(Error{
          .code = ErrorCode::unexpected_eof,
          .offset = pos_,
      });
    }

    pos_ += n;
    return {};
  }

 private:
  enum class ObjectContext : bool { object, array };
  enum class Expect : uint8_t {
    key,
    key_or_end,
    value,
    value_or_end,
    comma_delimiter,
    colon_delimiter,
  };

  [[nodiscard]]
  static constexpr auto is_ws(char c) noexcept -> bool {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
  }

  [[nodiscard]]
  static constexpr auto is_delim(char c) noexcept -> bool {
    return c == ',' || c == ']' || c == '}';
  }

  [[nodiscard]]
  auto read_one() -> Result<Token, Error> {
    while (pos_ < input_.size()) {
      const std::size_t pos = pos_;
      const char c = input_[pos_++];

      if (is_ws(c)) {
        continue;
      }

      switch (c) {
        case '{': {
          if (expect_ != Expect::value && expect_ != Expect::value_or_end) {
            return unexpected(
                Error{.code = ErrorCode::unexpected_token, .offset = pos});
          }
          auto pushed = push_context(ObjectContext::object, pos);
          if (!pushed) {
            return unexpected(pushed.error());
          }
          expect_ = Expect::key_or_end;
          pos_ = pos;
          return Token{
              .offset = pos,
              .length = unresolved_length,
              .type = TokenType::ObjectBegin,
              .context = static_cast<std::uint8_t>(JsonTokenContext::none)};
        }

        case '[': {
          if (expect_ != Expect::value && expect_ != Expect::value_or_end) {
            return unexpected(
                Error{.code = ErrorCode::unexpected_token, .offset = pos});
          }
          auto pushed = push_context(ObjectContext::array, pos);
          if (!pushed) {
            return unexpected(pushed.error());
          }
          expect_ = Expect::value_or_end;
          pos_ = pos;
          return Token{
              .offset = pos,
              .length = unresolved_length,
              .type = TokenType::ArrayBegin,
              .context = static_cast<std::uint8_t>(JsonTokenContext::none)};
        }
        case '}':
          if (ctx_size_ == 0 || current_ctx() != ObjectContext::object ||
              (expect_ != Expect::key_or_end &&
               expect_ != Expect::comma_delimiter)) {
            return unexpected(
                Error{.code = ErrorCode::unexpected_token, .offset = pos});
          }

          pop_context();
          expect_ = Expect::comma_delimiter;

          return Token{
              .offset = pos,
              .length = 0,
              .type = TokenType::ObjectEnd,
              .context = static_cast<std::uint8_t>(JsonTokenContext::none)};

        case ']':
          if (ctx_size_ == 0 || current_ctx() != ObjectContext::array ||
              (expect_ != Expect::value_or_end &&
               expect_ != Expect::comma_delimiter)) {
            return unexpected(
                Error{.code = ErrorCode::unexpected_token, .offset = pos});
          }

          pop_context();
          expect_ = Expect::comma_delimiter;
          return Token{
              .offset = pos,
              .length = 0,
              .type = TokenType::ArrayEnd,
              .context = static_cast<std::uint8_t>(JsonTokenContext::none)};

        case ',':
          if (ctx_size_ == 0 || expect_ != Expect::comma_delimiter) {
            return unexpected(
                Error{.code = ErrorCode::unexpected_token, .offset = pos});
          }

          expect_ = current_ctx() == ObjectContext::object ? Expect::key
                                                           : Expect::value;
          continue;

        case ':':
          if (ctx_size_ == 0 || current_ctx() != ObjectContext::object ||
              expect_ != Expect::colon_delimiter) {
            return unexpected(
                Error{.code = ErrorCode::unexpected_token, .offset = pos});
          }
          expect_ = Expect::value;
          continue;

        case '"': {
          if (expect_ != Expect::key && expect_ != Expect::key_or_end &&
              expect_ != Expect::value && expect_ != Expect::value_or_end) {
            return unexpected(
                Error{.code = ErrorCode::unexpected_token, .offset = pos});
          }

          const bool is_key =
              ctx_size_ != 0 && current_ctx() == ObjectContext::object &&
              (expect_ == Expect::key || expect_ == Expect::key_or_end);

          expect_ = is_key ? Expect::colon_delimiter : Expect::comma_delimiter;
          pos_ = pos;
          return Token{
              .offset = pos,
              .length = unresolved_length,
              .type = TokenType::ScalarBegin,
              .context = static_cast<std::uint8_t>(JsonTokenContext::string),
          };
        }

        default:
          if (expect_ != Expect::value && expect_ != Expect::value_or_end) {
            return unexpected(
                Error{.code = ErrorCode::unexpected_token, .offset = pos});
          }
          expect_ = Expect::comma_delimiter;
          pos_ = pos;
          return Token{
              .offset = pos,
              .length = unresolved_length,
              .type = TokenType::ScalarBegin,
              .context = static_cast<std::uint8_t>(JsonTokenContext::literal),
          };
      }
    }

    if (ctx_size_ != 0 || expect_ != Expect::comma_delimiter) {
      return unexpected(Error{
          .code = ErrorCode::unexpected_eof,
          .offset = input_.size(),
      });
    }

    return Token{
        .offset = input_.size(),
        .length = 0,
        .type = TokenType::End,
        .context = static_cast<std::uint8_t>(JsonTokenContext::none),
    };
  }

  [[nodiscard]]
  auto current_ctx() const noexcept -> ObjectContext {
    return ctx_.test(ctx_size_ - 1) ? ObjectContext::object
                                    : ObjectContext::array;
  }

  auto push_context(ObjectContext type, std::size_t offset)
      -> Result<void, Error> {
    if (ctx_size_ >= MaxDepth) {
      return unexpected(Error{
          .code = ErrorCode::depth_limit_exceeded,
          .offset = offset,
      });
    }

    ctx_.set(ctx_size_++, type == ObjectContext::object);
    return {};
  }

  auto pop_context() noexcept -> void {
    --ctx_size_;
    ctx_.reset(ctx_size_);
  }

 private:
  std::bitset<MaxDepth> ctx_{};
  std::size_t ctx_size_{0};

  std::string_view input_{};
  Expect expect_{Expect::value};

  Token current_{
      .offset = 0,
      .length = 0,
      .type = TokenType::End,
      .context = static_cast<std::uint8_t>(JsonTokenContext::none),
  };

  mutable std::size_t pos_{0};
};

static_assert(std::is_trivially_copyable_v<Reader<>>);
static_assert(ReaderConcept<Reader<>>);

}  // namespace json

template <std::size_t MaxDepth>
struct ReaderTraits<json::Reader<MaxDepth>> {
  static constexpr Format format = Format::json;
  static constexpr bool is_binary_format = false;
};

}  // namespace ser20
