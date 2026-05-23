#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <string_view>

#include "hosq/context.hh"
#include "hosq/error.hh"
#include "hosq/pipeline/scan.hh"
#include "hosq/reader_base.hh"

#if defined(__GNUC__) || defined(__clang__)
#define HOSQ_ALWAYS_INLINE_ATTR __attribute__((always_inline))
#define HOSQ_FUNCTION_ALWAYS_INLINE inline __attribute__((always_inline))
#elif defined(_MSC_VER)
#define HOSQ_ALWAYS_INLINE_ATTR __forceinline
#define HOSQ_FUNCTION_ALWAYS_INLINE __forceinline
#else
#define HOSQ_ALWAYS_INLINE_ATTR
#define HOSQ_FUNCTION_ALWAYS_INLINE inline
#endif

namespace hosq {
namespace json {

enum class JsonFastExpect : std::uint8_t {
  value,
  value_or_end,
  key_or_end,
  colon_delimiter,
  comma_delimiter,
};

template <std::size_t MaxDepth>
struct JsonFastReaderCheckpoint {
  const char* pos;
  std::size_t ctx_size;
  JsonFastExpect expect;
};

// State for FastReader. Uses checkpoint()/rollback() that intentionally exclude
// `current` so that lazily-resolved string/literal lengths survive rollback and
// are reused by subsequent Try candidates without re-scanning.
template <std::size_t MaxDepth>
struct JsonFastReaderState {
  const char* begin{nullptr};
  const char* pos{nullptr};
  const char* end{nullptr};

  std::string_view input_view{};

  Token current{
      .offset = 0,
      .length = 0,
      .type = TokenType::End,
      .context = static_cast<std::uint8_t>(JsonTokenContext::none),
  };

  std::array<std::uint8_t, MaxDepth> ctx_stack{};
  std::size_t ctx_size{0};

  JsonFastExpect expect{JsonFastExpect::value};
  mutable Error last_error{};
  mutable bool error_flag{false};
  mutable Token error_token{
      .offset = 0,
      .length = 0,
      .type = TokenType::Error,
      .context = 0,
  };

  [[nodiscard]] auto checkpoint() const noexcept
      -> JsonFastReaderCheckpoint<MaxDepth> {
    return {pos, ctx_size, expect};
  }

  auto rollback(const JsonFastReaderCheckpoint<MaxDepth>& cp) noexcept -> void {
    pos = cp.pos;
    ctx_size = cp.ctx_size;
    expect = cp.expect;
    // `current` is intentionally NOT restored — cached lengths are reused
  }
};

template <std::size_t MaxDepth = 128>
class FastReader {
 public:
  using input_type = std::string_view;
  using state_type = JsonFastReaderState<MaxDepth>;

  FastReader() = default;

  auto reset(state_type& st, input_type input) const noexcept -> void {
    st.begin = input.data();
    st.pos = input.data();
    st.end = input.data() + input.size();
    st.input_view = input;
    st.ctx_size = 0;
    st.expect = JsonFastExpect::value;
    st.last_error = {};
    st.error_flag = false;
    st.current = {
        .offset = 0,
        .length = 0,
        .type = TokenType::End,
        .context = static_cast<std::uint8_t>(JsonTokenContext::none),
    };
  }

  auto next_token(state_type& st) const noexcept -> bool {
    if (st.error_flag) [[unlikely]]
      return false;
    Token tok{};
    if (!read_one(st, tok)) [[unlikely]] {
      return false;
    }
    st.current = tok;
    st.last_error = {};
    return true;
  }

  // Sets the error flag and records err (first call wins; subsequent calls are
  // no-ops). After this, current_token() always returns a TokenType::Error
  // token.
  void report_error(const state_type& st, Error err) const noexcept {
    if (!st.error_flag) {
      st.last_error = err;
      st.error_flag = true;
    }
  }

  [[nodiscard]] auto current_token(const state_type& st) const noexcept
      -> const Token& {
    if (st.error_flag) [[unlikely]]
      return st.error_token;
    return st.current;
  }

  // Mutable overload used by ext::ScanString/ScanLiteral specializations to
  // cache resolved lengths directly into the token.
  [[nodiscard]] auto current_token(state_type& st) const noexcept -> Token& {
    if (st.error_flag) [[unlikely]]
      return st.error_token;
    return st.current;
  }

  [[nodiscard]] auto error(const state_type& st) const noexcept -> Error {
    return st.last_error;
  }

  [[nodiscard]] auto input(const state_type& st) const noexcept
      -> const input_type& {
    return st.input_view;
  }

  // Advances st.pos past the current scalar token without caching its length.
  // Called by the state machine when a Scalar was never scanned by an ext::
  // step.
  auto resolve_skip(state_type& st) const noexcept -> bool {
    if (st.current.length != unresolved_length) return true;
    if (st.current.context ==
        static_cast<std::uint8_t>(JsonTokenContext::string)) {
      st.pos = st.begin + st.current.offset;
      while (st.pos < st.end) {
        if (*st.pos == '"') {
          ++st.pos;
          return true;
        }
        if (*st.pos == '\\') {
          st.pos += 2;
          continue;
        }
        ++st.pos;
      }
      st.last_error = {.code = ErrorCode::unexpected_eof,
                       .offset = static_cast<std::size_t>(st.end - st.begin)};
      return false;
    } else {
      // literal
      const char* p = st.begin + st.current.offset;
      while (p < st.end) {
        const auto c = static_cast<std::uint8_t>(*p);
        if (c <= ' ' || c == ',' || c == '}' || c == ']' || c == ':') break;
        ++p;
      }
      st.pos = p;
      return true;
    }
  }

 private:
  static constexpr auto kStrCtx =
      static_cast<std::uint8_t>(JsonTokenContext::string);
  static constexpr auto kLitCtx =
      static_cast<std::uint8_t>(JsonTokenContext::literal);
  static constexpr auto kNoneCtx =
      static_cast<std::uint8_t>(JsonTokenContext::none);

  static HOSQ_FUNCTION_ALWAYS_INLINE auto is_ws(char c) noexcept -> bool {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
  }

  static HOSQ_FUNCTION_ALWAYS_INLINE auto skip_ws(const char*& p,
                                                  const char* end) noexcept
      -> bool {
    while (p < end && is_ws(*p)) ++p;
    return p < end;
  }

  static HOSQ_FUNCTION_ALWAYS_INLINE auto off(const state_type& st,
                                              const char* p) noexcept
      -> std::size_t {
    return static_cast<std::size_t>(p - st.begin);
  }

  static HOSQ_FUNCTION_ALWAYS_INLINE auto is_num_start(char c) noexcept -> bool {
    return c == '-' || (c >= '0' && c <= '9');
  }

  static HOSQ_FUNCTION_ALWAYS_INLINE auto make_tok(std::size_t o,
                                                   std::size_t len,
                                                   TokenType type,
                                                   std::uint8_t ctx) noexcept
      -> Token {
    return {.offset = o, .length = len, .type = type, .context = ctx};
  }

  static HOSQ_FUNCTION_ALWAYS_INLINE auto lazy_scalar(std::size_t content_off,
                                                      std::uint8_t ctx) noexcept
      -> Token {
    return make_tok(content_off, unresolved_length, TokenType::Scalar, ctx);
  }

  HOSQ_FUNCTION_ALWAYS_INLINE auto push_ctx(state_type& st, std::uint8_t kind,
                                            std::size_t err_off) const noexcept
      -> bool {
    if (st.ctx_size >= MaxDepth) [[unlikely]] {
      st.last_error = {.code = ErrorCode::depth_limit_exceeded,
                       .offset = err_off};
      return false;
    }
    st.ctx_stack[st.ctx_size++] = kind;
    return true;
  }

  HOSQ_FUNCTION_ALWAYS_INLINE auto pop_ctx(state_type& st) const noexcept
      -> void {
    if (st.ctx_size > 0) --st.ctx_size;
  }

  HOSQ_FUNCTION_ALWAYS_INLINE auto in_object(const state_type& st) const noexcept
      -> bool {
    return st.ctx_size > 0 && st.ctx_stack[st.ctx_size - 1] == 1;
  }

  auto read_one(state_type& st, Token& out) const noexcept -> bool {
    switch (st.expect)
      [[likely]] {
        case JsonFastExpect::value:
          return read_value(st, out);
        case JsonFastExpect::value_or_end:
          return read_value_or_array_end(st, out);
        case JsonFastExpect::key_or_end:
          return read_key(st, true, out);
        case JsonFastExpect::colon_delimiter:
          return read_colon_then_value(st, out);
        case JsonFastExpect::comma_delimiter:
          return read_comma_or_close(st, out);
      }
    st.last_error = Error{.code = ErrorCode::invalid_state};
    return false;
  }

  auto read_value(state_type& st, Token& out) const noexcept -> bool {
    const char* p = st.pos;
    if (!skip_ws(p, st.end)) [[unlikely]] {
      st.pos = p;
      st.last_error =
          Error{.code = ErrorCode::unexpected_eof, .offset = off(st, p)};
      return false;
    }

    const char c = *p;
    const std::size_t o = off(st, p);

    if (c == '"') {
      st.expect = JsonFastExpect::comma_delimiter;
      st.pos = p + 1;
      out = lazy_scalar(o + 1, kStrCtx);
      return true;
    }

    if (c == '{') {
      if (!push_ctx(st, 1, o)) [[unlikely]]
        return false;
      st.expect = JsonFastExpect::key_or_end;
      st.pos = p + 1;
      out = make_tok(o, 1, TokenType::ObjectBegin, kNoneCtx);
      return true;
    }

    if (c == '[') {
      if (!push_ctx(st, 0, o)) [[unlikely]]
        return false;
      st.expect = JsonFastExpect::value_or_end;
      st.pos = p + 1;
      out = make_tok(o, 1, TokenType::ArrayBegin, kNoneCtx);
      return true;
    }

    if (is_num_start(c) || c == 't' || c == 'f' || c == 'n') {
      st.expect = JsonFastExpect::comma_delimiter;
      st.pos = p;
      out = lazy_scalar(o, kLitCtx);
      return true;
    }

    st.pos = p;
    st.last_error = Error{.code = ErrorCode::unexpected_token, .offset = o};
    return false;
  }

  auto read_value_or_array_end(state_type& st, Token& out) const noexcept
      -> bool {
    const char* p = st.pos;
    if (!skip_ws(p, st.end)) [[unlikely]] {
      st.pos = p;
      st.last_error =
          Error{.code = ErrorCode::unexpected_eof, .offset = off(st, p)};
      return false;
    }
    if (*p == ']') {
      pop_ctx(st);
      st.expect = JsonFastExpect::comma_delimiter;
      st.pos = p + 1;
      out = make_tok(off(st, p), 1, TokenType::ArrayEnd, kNoneCtx);
      return true;
    }
    st.pos = p;
    return read_value(st, out);
  }

  auto read_key(state_type& st, bool allow_end, Token& out) const noexcept
      -> bool {
    const char* p = st.pos;
    if (!skip_ws(p, st.end)) [[unlikely]] {
      st.pos = p;
      st.last_error =
          Error{.code = ErrorCode::unexpected_eof, .offset = off(st, p)};
      return false;
    }

    const char c = *p;
    const std::size_t o = off(st, p);

    if (c == '"') {
      st.expect = JsonFastExpect::colon_delimiter;
      st.pos = p + 1;
      out = lazy_scalar(o + 1, kStrCtx);
      return true;
    }

    if (allow_end && c == '}') {
      pop_ctx(st);
      st.expect = JsonFastExpect::comma_delimiter;
      st.pos = p + 1;
      out = make_tok(o, 1, TokenType::ObjectEnd, kNoneCtx);
      return true;
    }

    st.pos = p;
    st.last_error = Error{.code = ErrorCode::unexpected_token, .offset = o};
    return false;
  }

  // After a key string token: skip it if unresolved, find ':', then read value.
  auto read_colon_then_value(state_type& st, Token& out) const noexcept -> bool {
    if (st.current.length == unresolved_length) {
      if (!resolve_skip(st)) [[unlikely]]
        return false;
    }
    const char* p = st.pos;
    if (!skip_ws(p, st.end)) [[unlikely]] {
      st.pos = p;
      st.last_error =
          Error{.code = ErrorCode::unexpected_eof, .offset = off(st, p)};
      return false;
    }
    if (*p != ':') [[unlikely]] {
      st.pos = p;
      st.last_error =
          Error{.code = ErrorCode::unexpected_token, .offset = off(st, p)};
      return false;
    }
    ++p;
    st.expect = JsonFastExpect::value;
    st.pos = p;
    return read_value(st, out);
  }

  // After a value token: skip it if unresolved, then find ',' or '}' or ']'.
  auto read_comma_or_close(state_type& st, Token& out) const noexcept -> bool {
    if (st.current.length == unresolved_length) {
      if (!resolve_skip(st)) [[unlikely]]
        return false;
    }
    const char* p = st.pos;
    if (!skip_ws(p, st.end)) {
      st.pos = p;
      if (st.ctx_size == 0) {
        out = make_tok(off(st, p), 0, TokenType::End, kNoneCtx);
        return true;
      }
      st.last_error =
          Error{.code = ErrorCode::unexpected_eof, .offset = off(st, p)};
      return false;
    }

    const char c = *p;
    const std::size_t o = off(st, p);

    if (c == ',') {
      st.pos = p + 1;
      if (in_object(st)) {
        return read_key(st, false, out);
      } else {
        return read_value(st, out);
      }
    }

    if (c == '}') {
      if (!in_object(st)) [[unlikely]] {
        st.pos = p;
        st.last_error = Error{.code = ErrorCode::unexpected_token, .offset = o};
        return false;
      }
      pop_ctx(st);
      st.expect = JsonFastExpect::comma_delimiter;
      st.pos = p + 1;
      out = make_tok(o, 1, TokenType::ObjectEnd, kNoneCtx);
      return true;
    }

    if (c == ']') {
      if (in_object(st)) [[unlikely]] {
        st.pos = p;
        st.last_error = Error{.code = ErrorCode::unexpected_token, .offset = o};
        return false;
      }
      pop_ctx(st);
      st.expect = JsonFastExpect::comma_delimiter;
      st.pos = p + 1;
      out = make_tok(o, 1, TokenType::ArrayEnd, kNoneCtx);
      return true;
    }

    st.pos = p;
    st.last_error = Error{.code = ErrorCode::unexpected_token, .offset = o};
    return false;
  }
};

}  // namespace json

template <std::size_t MaxDepth>
struct ReaderTraits<json::FastReader<MaxDepth>> {
  static constexpr Format format = Format::json;
  static constexpr bool is_binary_format = false;
};

static_assert(ReaderConcept<json::FastReader<>>);

}  // namespace hosq

// ── ext specializations ──────────────────────────────────────────────────────

namespace hosq::ext {

// ext::ScanString<FastReader<N>>: mutable-only overload.
// On first access (length == unresolved_length): scan to find closing '"', cache
// length. On re-access after rollback (length already set): just re-advance pos.
// Returns std::string_view directly; errors go to st.last_error (no unexpected).
template <std::size_t MaxDepth>
struct ScanString<hosq::json::FastReader<MaxDepth>> {
  using Reader = hosq::json::FastReader<MaxDepth>;

  template <class T, TryPhase Phase>
  HOSQ_FUNCTION_ALWAYS_INLINE auto operator()(
      DecodeContext<T, Reader, Phase>& ctx) const -> std::string_view {
    auto& st = ctx.reader_state();
    auto& tok = ctx.reader().current_token(st);

    if (tok.type != TokenType::Scalar ||
        tok.context != static_cast<std::uint8_t>(JsonTokenContext::string))
        [[unlikely]] {
      st.last_error = {.code = ErrorCode::type_mismatch, .offset = tok.offset};
      return {};
    }

    if (tok.length == unresolved_length) {
      const char* const content = st.begin + tok.offset;
      const char* p = content;
      while (p < st.end) {
        if (*p == '"') {
          tok.length = static_cast<std::size_t>(p - content);
          st.pos = p + 1;
          return st.input_view.substr(tok.offset, tok.length);
        }
        if (*p == '\\') {
          p += 2;
          continue;
        }
        ++p;
      }
      st.last_error = {.code = ErrorCode::unexpected_eof,
                       .offset = static_cast<std::size_t>(st.end - st.begin)};
      return {};
    }

    st.pos = st.begin + tok.offset + tok.length + 1;
    return st.input_view.substr(tok.offset, tok.length);
  }
};

// ext::ScanLiteral<FastReader<N>>: mutable-only overload.
// Same lazy-resolve + cache pattern as ScanString.
// Returns std::string_view directly; errors go to st.last_error (no unexpected).
template <std::size_t MaxDepth>
struct ScanLiteral<hosq::json::FastReader<MaxDepth>> {
  using Reader = hosq::json::FastReader<MaxDepth>;

  template <class T, TryPhase Phase>
  HOSQ_FUNCTION_ALWAYS_INLINE auto operator()(
      DecodeContext<T, Reader, Phase>& ctx) const -> std::string_view {
    auto& st = ctx.reader_state();
    auto& tok = ctx.reader().current_token(st);

    if (tok.type != TokenType::Scalar ||
        tok.context != static_cast<std::uint8_t>(JsonTokenContext::literal))
        [[unlikely]] {
      st.last_error = {.code = ErrorCode::type_mismatch, .offset = tok.offset};
      return {};
    }

    if (tok.length == unresolved_length) {
      const char* const lit_start = st.begin + tok.offset;
      const char* p = lit_start;
      while (p < st.end) {
        const auto c = static_cast<std::uint8_t>(*p);
        if (c <= ' ' || c == ',' || c == '}' || c == ']' || c == ':') break;
        ++p;
      }
      tok.length = static_cast<std::size_t>(p - lit_start);
      st.pos = p;
    } else {
      st.pos = st.begin + tok.offset + tok.length;
    }

    return st.input_view.substr(tok.offset, tok.length);
  }
};

}  // namespace hosq::ext

#undef HOSQ_ALWAYS_INLINE_ATTR
#undef HOSQ_FUNCTION_ALWAYS_INLINE
