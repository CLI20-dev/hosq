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

/**
 * @brief Pull-style JSON tokeniser that satisfies @ref ReaderConcept.
 *
 * Reader walks a JSON byte string one structural token at a time.  It
 * intentionally performs only structural parsing — it does **not** validate
 * string escape sequences, UTF-8 encoding, or the spelling of literal values
 * such as `true`, `false`, and `null`.  Scalar content validation is
 * delegated to the caller.
 *
 * ### Typical usage
 * ```cpp
 * ser20::json::Reader<> r;
 * r.reset(json_text);
 *
 * while (true) {
 *     if (!r.next_token()) { break; }  // handle error
 *     const auto& tok = r.current_token();
 *     if (tok.type == ser20::TokenType::End) break;
 *
 *     if (tok.type == ser20::TokenType::ScalarBegin) {
 *         // Scan content, then advance past it.
 *         r.advance(my_scalar_length(r.input(), tok));
 *     } else if (tok.type == ser20::TokenType::ObjectBegin ||
 *                tok.type == ser20::TokenType::ArrayBegin) {
 *         r.advance(1);   // skip the opening brace / bracket
 *     }
 *     // ObjectEnd / ArrayEnd / End require no advance.
 * }
 * ```
 *
 * ### Snapshot and speculative reads
 * Because Reader is trivially copyable, the full parser state — token,
 * context stack, and read position — can be snapshotted cheaply:
 * ```cpp
 * auto saved = r;          // snapshot
 * r.advance(3);
 * if (!r.next_token() || rejected) {
 *     r = saved;           // full rollback
 * }
 * ```
 * For position-only probing (without changing token state) a `const Reader&`
 * can be used directly; see @ref advance for details.
 *
 * @tparam MaxDepth  Maximum nesting depth of JSON objects and arrays.
 *                   Exceeding this limit yields @ref
 * ErrorCode::depth_limit_exceeded.
 */
template <size_t MaxDepth = 1024>
class Reader {
 public:
  using input_type = std::string_view;

  /// @brief Default-constructs a Reader with no input attached.
  ///        Call @ref reset before invoking any other method.
  explicit Reader() noexcept = default;

  Reader(const Reader&) = default;
  auto operator=(const Reader&) -> Reader& = default;

  Reader(Reader&&) = delete;
  auto operator=(Reader&&) -> Reader& = delete;

  ~Reader() = default;

  /**
   * @brief Attach a new input and reset all parser state.
   *
   * After reset(), the reader is positioned before the first token.
   * A subsequent call to @ref next_token() will return the first token of
   * @p input.
   *
   * @param input  The JSON text to tokenise.  The caller must keep @p input
   *               alive for the lifetime of this Reader (or until the next
   *               reset() call).
   */
  auto reset(std::string_view input) noexcept -> void {
    input_ = input;
    pos_ = 0;
    ctx_.reset();
    ctx_size_ = 0;
    expect_ = Expect::value;
    current_ = Token{
        .offset = 0,
        .length = 0,
        .type = TokenType::End,
        .context = static_cast<std::uint8_t>(JsonTokenContext::none),
    };
  }

  /**
   * @brief Advance to the next token and store it as the current token.
   *
   * Must be called before @ref current_token() can return a meaningful value.
   * After a successful call, retrieve the token via @ref current_token().
   *
   * @note For `ScalarBegin`, `ObjectBegin`, and `ArrayBegin` tokens, the read
   *       cursor is left at the start of the token (i.e. on the opening
   *       character).  The caller **must** call @ref advance() to move past
   *       the token's content before calling next_token() again.
   *
   * @return An empty Result on success, or an Error whose @c code is:
   *   - @ref ErrorCode::unexpected_token  if a structurally invalid character
   *     is encountered.
   *   - @ref ErrorCode::unexpected_eof    if the input ends while a container
   *     is still open or a value is expected.
   *   - @ref ErrorCode::depth_limit_exceeded  if nesting exceeds @c MaxDepth.
   */
  [[nodiscard]]
  auto next_token() -> Result<void, Error> {
    auto result = read_one();
    if (!result) {
      return unexpected(result.error());
    }
    current_ = *result;
    return {};
  }

  /**
   * @brief Return a reference to the most recently produced token.
   *
   * The returned reference is valid until the next call to @ref next_token()
   * or @ref reset().
   *
   * @note The initial current token (before the first @ref next_token() call)
   *       is a synthetic `End` token at offset 0.
   */
  [[nodiscard]]
  auto current_token() noexcept -> Token& {
    return current_;
  }

  /// @copydoc current_token()
  [[nodiscard]]
  auto current_token() const noexcept -> const Token& {
    return current_;
  }

  /**
   * @brief Return the full input string as supplied to @ref reset().
   */
  [[nodiscard]]
  auto input() const noexcept -> std::string_view {
    return input_;
  }

  /**
   * @brief Advance the read cursor by @p n bytes past the start of the
   *        current token.
   *
   * ### When to call
   * After @ref next_token() returns `ScalarBegin`, `ObjectBegin`, or
   * `ArrayBegin`.  Calling advance() when the current token is `ObjectEnd`,
   * `ArrayEnd`, or `End` returns @ref ErrorCode::unexpected_token.
   *
   * | Token type    | Typical argument                                    |
   * |---------------|-----------------------------------------------------|
   * | `ObjectBegin` | `1`  — skips the opening `{`                        |
   * | `ArrayBegin`  | `1`  — skips the opening `[`                        |
   * | `ScalarBegin` | byte count of the full scalar text (see below)      |
   *
   * For `ScalarBegin` with `context == JsonTokenContext::string`, @p n must
   * cover the opening `"`, all content bytes, and the closing `"`.  For
   * `JsonTokenContext::literal`, @p n covers only the non-whitespace content
   * bytes.  The token's `length` field is always `unresolved_length` for
   * scalars — the caller is responsible for determining the correct count by
   * scanning `input()`.
   *
   * ### Why `const` with `mutable pos_`
   * advance() modifies only `pos_`, the raw byte cursor.  It leaves the token
   * state (current token, container context stack, and parser expectation)
   * **unchanged**. This separation enables two complementary patterns:
   *
   * **1. Speculative position probing on a `const Reader&`:**
   * A decoder that receives a `const Reader&` can call advance() multiple
   * times to try different parse lengths for a scalar without altering the
   * token stream.  Only the final accepted position is visible to the next
   * @ref next_token() call (which requires a non-const Reader).
   * ```cpp
   * // Try two candidate lengths; accept the first that passes validation.
   * const auto& cr = r;
   * cr.advance(len_a);   // tentative — updates pos_ only
   * if (validate_a()) { return; }  // done
   * else { cr.advance(len_b - len_a); }
   * ```
   *
   * **2. Full rollback via copy (snapshot pattern):**
   * Because Reader is trivially copyable, the entire parser state — including
   * the context stack and `pos_` — can be captured cheaply.  Discarding the
   * copy reverts all state to the snapshot point.  Use this to undo a
   * @ref next_token() call or any other stateful change.
   * ```cpp
   * auto snap = r;              // snapshot complete state
   * r.advance(n);
   * if (!r.next_token()) r = snap;  // full rollback
   * ```
   *
   * ### Thread safety
   * advance() is **not** thread-safe even though it is `const`.  The `const`
   * qualifier expresses *logical* const-ness of the token stream, not physical
   * immutability of the object.  Do not call advance() concurrently on the
   * same Reader instance from multiple threads.
   *
   * @param n  Number of bytes to advance.
   * @return   An empty Result on success, or:
   *   - @ref ErrorCode::unexpected_token  — current token does not support
   * advance.
   *   - @ref ErrorCode::unexpected_eof    — @p n exceeds the remaining input.
   */
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

    if (current_.length == unresolved_length) {
      current_.length = n;
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

  friend auto allow_next_top_level_value_after_scan(Reader& reader) noexcept
      -> void {
    reader.allow_top_level_value_after_scan_ = true;
  }

  [[nodiscard]]
  static constexpr auto is_ws(char c) noexcept -> bool {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
  }

  [[nodiscard]]
  static constexpr auto is_delim(char c) noexcept -> bool {
    return c == ',' || c == ']' || c == '}';
  }

  [[nodiscard]]
  auto can_read_value() const noexcept -> bool {
    return expect_ == Expect::value || expect_ == Expect::value_or_end ||
           (ctx_size_ == 0 && allow_top_level_value_after_scan_);
  }

  auto consume_value_start() noexcept -> void {
    allow_top_level_value_after_scan_ = false;
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
          if (!can_read_value()) {
            return unexpected(
                Error{.code = ErrorCode::unexpected_token, .offset = pos});
          }
          consume_value_start();
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
          if (!can_read_value()) {
            return unexpected(
                Error{.code = ErrorCode::unexpected_token, .offset = pos});
          }
          consume_value_start();
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
              !can_read_value()) {
            return unexpected(
                Error{.code = ErrorCode::unexpected_token, .offset = pos});
          }

          const bool is_key =
              ctx_size_ != 0 && current_ctx() == ObjectContext::object &&
              (expect_ == Expect::key || expect_ == Expect::key_or_end);

          if (!is_key) {
            consume_value_start();
          }
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
          if (!can_read_value()) {
            return unexpected(
                Error{.code = ErrorCode::unexpected_token, .offset = pos});
          }
          consume_value_start();
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
  bool allow_top_level_value_after_scan_{false};

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
