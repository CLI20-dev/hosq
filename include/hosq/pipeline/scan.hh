#pragma once

#include <type_traits>

#include "hosq/error.hh"
#include "hosq/pipeline_util.hh"
#include "hosq/reader_base.hh"
#include "hosq/result.hh"

namespace hosq::scan {

namespace json::detail {

#if defined(__GNUC__) || defined(__clang__)
#define HOSQ_ALWAYS_INLINE_ATTR __attribute__((always_inline))
#define HOSQ_FUNCTION_ALWAYS_INLINE inline __attribute__((always_inline))
#elif defined(_MSC_VER)
#define HOSQ_ALWAYS_INLINE_ATTR
#define HOSQ_FUNCTION_ALWAYS_INLINE __forceinline
#else
#define HOSQ_ALWAYS_INLINE_ATTR
#define HOSQ_FUNCTION_ALWAYS_INLINE inline
#endif

[[nodiscard]]
constexpr auto is_literal_end(char c) noexcept -> bool {
  return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == ',' ||
         c == ']' || c == '}';
}

[[nodiscard]]
HOSQ_FUNCTION_ALWAYS_INLINE auto load_u64(const char* p) noexcept
    -> std::uint64_t {
  std::uint64_t v = 0;
  std::memcpy(&v, p, sizeof(v));
  return v;
}

[[nodiscard]]
HOSQ_FUNCTION_ALWAYS_INLINE constexpr auto repeat_byte(unsigned char c) noexcept
    -> std::uint64_t {
  return std::uint64_t{0x0101010101010101ull} * c;
}

[[nodiscard]]
HOSQ_FUNCTION_ALWAYS_INLINE constexpr auto has_zero_byte(
    std::uint64_t x) noexcept -> bool {
  return ((x - 0x0101010101010101ull) & ~x & 0x8080808080808080ull) != 0;
}

[[nodiscard]]
HOSQ_FUNCTION_ALWAYS_INLINE constexpr auto has_byte(std::uint64_t x,
                                                     unsigned char c) noexcept
    -> bool {
  return has_zero_byte(x ^ repeat_byte(c));
}

[[nodiscard]]
HOSQ_FUNCTION_ALWAYS_INLINE constexpr auto has_quote_or_backslash(
    std::uint64_t x) noexcept -> bool {
  return has_byte(x, static_cast<unsigned char>('"')) ||
         has_byte(x, static_cast<unsigned char>('\\'));
}

template <class Reader>
HOSQ_FUNCTION_ALWAYS_INLINE auto find_string_end(const Reader& reader,
                                                  std::size_t quote_offset)
    -> Result<std::size_t, Error> {
  const auto input = reader.input();
  const char* const base = input.data();
  const char* const end = base + input.size();

  if (quote_offset >= input.size() || base[quote_offset] != '"') [[unlikely]] {
    return unexpected(Error{
        .code = ErrorCode::invalid_token,
        .offset = quote_offset,
    });
  }

  const char* p = base + quote_offset + 1;

  while (static_cast<std::size_t>(end - p) >= 8) {
    const std::uint64_t x = detail::load_u64(p);

    if (!detail::has_quote_or_backslash(x)) [[likely]] {
      p += 8;
      continue;
    }

    while (p < end) {
      const char c = *p++;

      if (c == '"') {
        return static_cast<std::size_t>((p - 1) - base);
      }

      if (c == '\\') {
        if (p >= end) [[unlikely]] {
          goto unexpected_eof;
        }
        ++p;
      }
    }

    goto unexpected_eof;
  }

  while (p < end) {
    const char c = *p++;

    if (c == '"') {
      return static_cast<std::size_t>((p - 1) - base);
    }

    if (c == '\\') {
      if (p >= end) [[unlikely]] {
        goto unexpected_eof;
      }
      ++p;
    }
  }

unexpected_eof:
  return unexpected(Error{
      .code = ErrorCode::unexpected_eof,
      .offset = quote_offset,
  });
}

template <class Reader>
HOSQ_FUNCTION_ALWAYS_INLINE auto scan_literal(const Reader& reader,
                                               const Token& token)
    -> Result<std::string_view, Error> {
  const auto input = reader.input();

  if (token.length == unresolved_length) {
    const std::size_t off = token.offset;

    if (off >= input.size()) [[unlikely]] {
      return unexpected(Error{
          .code = ErrorCode::unexpected_eof,
          .offset = off,
      });
    }

    const char c = input[off];

    std::size_t len = 0;

    if (c == 't' || c == 'n') {
      // true / null
      len = 4;
    } else if (c == 'f') {
      // false
      len = 5;
    } else {
      std::size_t end = off;
      while (end < input.size() && !is_literal_end(input[end])) {
        ++end;
      }
      len = end - off;
    }

    token.length = len;
    auto advanced = reader.advance(len);
    if (!advanced) [[unlikely]] {
      token.length = unresolved_length;
      return unexpected(advanced.error());
    }
  }

  return input.substr(token.offset, token.length);
}

template <class Reader>
HOSQ_FUNCTION_ALWAYS_INLINE auto scan_string(const Reader& reader,
                                              const Token& token)
    -> Result<std::string_view, Error> {
  const auto input = reader.input();
  if (token.length == unresolved_length) {
    auto end = find_string_end(reader, token.offset);
    if (!end) [[unlikely]] {
      return unexpected(end.error());
    }
    token.length = *end - token.offset + 1;
    auto advanced = reader.advance(token.length);
    if (!advanced) [[unlikely]] {
      token.length = unresolved_length;
      return unexpected(advanced.error());
    }
  }
  if (token.length < 2) [[unlikely]] {
    return unexpected(Error{
        .code = ErrorCode::invalid_length,
        .offset = token.offset,
    });
  }
  return input.substr(token.offset + 1, token.length - 2);
}

[[nodiscard]]
HOSQ_FUNCTION_ALWAYS_INLINE constexpr auto is_literal_token(
    const Token& token) noexcept -> bool {
  return token.type == TokenType::Scalar &&
         static_cast<JsonTokenContext>(token.context) ==
             JsonTokenContext::literal;
}

[[nodiscard]]
HOSQ_FUNCTION_ALWAYS_INLINE constexpr auto is_string_token(
    const Token& token) noexcept -> bool {
  return token.type == TokenType::Scalar &&
         static_cast<JsonTokenContext>(token.context) ==
             JsonTokenContext::string;
}

[[nodiscard]]
HOSQ_FUNCTION_ALWAYS_INLINE constexpr auto is_scalar_token(
    const Token& token) noexcept -> bool {
  return is_literal_token(token) || is_string_token(token);
}

}  // namespace json::detail

namespace detail {

template <class>
inline constexpr bool always_false = false;

template <class Reader>
HOSQ_FUNCTION_ALWAYS_INLINE auto scan_literal_for(const Reader& reader,
                                                   const Token& token)
    -> Result<std::string_view, Error> {
  using R = std::remove_cvref_t<Reader>;

  if constexpr (ReaderTraits<R>::format == Format::json) {
    if (!json::detail::is_literal_token(token)) [[unlikely]] {
      return unexpected(Error{
          .code = ErrorCode::type_mismatch,
          .offset = token.offset,
      });
    }

    return json::detail::scan_literal(reader, token);
  } else {
    static_assert(always_false<R>,
                  "hosq::scan::literal is not implemented for this reader");
  }
}

template <class Reader>
HOSQ_FUNCTION_ALWAYS_INLINE auto scan_string_for(const Reader& reader,
                                                  const Token& token)
    -> Result<std::string_view, Error> {
  using R = std::remove_cvref_t<Reader>;

  if constexpr (ReaderTraits<R>::format == Format::json) {
    if (!json::detail::is_string_token(token)) [[unlikely]] {
      return unexpected(Error{
          .code = ErrorCode::type_mismatch,
          .offset = token.offset,
      });
    }
    return json::detail::scan_string(reader, token);
  } else {
    static_assert(always_false<R>,
                  "hosq::scan::string is not implemented for this reader");
  }
}

template <class Reader>
HOSQ_FUNCTION_ALWAYS_INLINE auto scan_scalar_for(const Reader& reader,
                                                  const Token& token)
    -> Result<std::string_view, Error> {
  using R = std::remove_cvref_t<Reader>;
  if constexpr (ReaderTraits<R>::format == Format::json) {
    if (json::detail::is_literal_token(token)) {
      return json::detail::scan_literal(reader, token);
    }
    if (json::detail::is_string_token(token)) {
      return json::detail::scan_string(reader, token);
    }
    return unexpected(Error{
        .code = ErrorCode::type_mismatch,
        .offset = token.offset,
    });
  } else {
    static_assert(always_false<R>,
                  "hosq::scan::scalar is not implemented for this reader");
  }
}

template <class Reader>
HOSQ_FUNCTION_ALWAYS_INLINE auto scan_compound_by_tokens(
    Reader& reader, const Token& token, TokenType expected_begin)
    -> Result<std::string_view, Error> {
  if (token.type != expected_begin) [[unlikely]] {
    return unexpected(Error{
        .code = ErrorCode::type_mismatch,
        .offset = token.offset,
    });
  }

  const auto input = reader.input();
  const std::size_t start = token.offset;

  auto advanced = reader.advance(1);
  if (!advanced) [[unlikely]] {
    return unexpected(advanced.error());
  }

  std::size_t depth = 1;
  while (depth > 0) {
    auto next = reader.next_token();
    if (!next) [[unlikely]] {
      return unexpected(next.error());
    }

    const auto& current_token = reader.current_token();
    switch (current_token.type) {
      case TokenType::Scalar: {
        auto scalar = scan_scalar_for(reader, current_token);
        if (!scalar) [[unlikely]] {
          return unexpected(scalar.error());
        }
        break;
      }

      case TokenType::ObjectBegin:
      case TokenType::ArrayBegin: {
        auto nested_advanced = reader.advance(1);
        if (!nested_advanced) [[unlikely]] {
          return unexpected(nested_advanced.error());
        }
        ++depth;
        break;
      }

      case TokenType::ObjectEnd:
      case TokenType::ArrayEnd: {
        const std::size_t end = current_token.offset + 1;
        --depth;
        if (depth == 0) {
          token.length = end - start;
          if constexpr (requires(Reader& r) {
                          allow_next_top_level_value_after_scan(r);
                        }) {
            allow_next_top_level_value_after_scan(reader);
          }
          return input.substr(start, end - start);
        }
        break;
      }

      case TokenType::End:
        return unexpected(Error{
            .code = ErrorCode::unexpected_eof,
            .offset = current_token.offset,
        });
    }
  }

  return unexpected(Error{
      .code = ErrorCode::unexpected_eof,
      .offset = start,
  });
}

template <class Reader>
HOSQ_FUNCTION_ALWAYS_INLINE auto scan_object_for(Reader& reader,
                                                  const Token& token)
    -> Result<std::string_view, Error> {
  using R = std::remove_cvref_t<Reader>;
  if constexpr (ReaderTraits<R>::format == Format::json) {
    return scan_compound_by_tokens(reader, token, TokenType::ObjectBegin);
  } else {
    static_assert(always_false<R>,
                  "hosq::scan::object is not implemented for this reader");
  }
}

template <class Reader>
HOSQ_FUNCTION_ALWAYS_INLINE auto scan_array_for(Reader& reader,
                                                 const Token& token)
    -> Result<std::string_view, Error> {
  using R = std::remove_cvref_t<Reader>;
  if constexpr (ReaderTraits<R>::format == Format::json) {
    return scan_compound_by_tokens(reader, token, TokenType::ArrayBegin);
  } else {
    static_assert(always_false<R>,
                  "hosq::scan::array is not implemented for this reader");
  }
}

}  // namespace detail

namespace detail {

struct ScanLiteralImpl {
  template <class Context>
  auto operator()(const Context& ctx) const -> Result<std::string_view, Error> {
    const auto& reader = ctx.reader();
    return scan_literal_for(reader, reader.current_token());
  }
};

struct ScanStringImpl {
  template <class Context>
  auto operator()(Context& ctx) const -> Result<std::string_view, Error> {
    const auto& reader = ctx.reader();
    return scan_string_for(reader, reader.current_token());
  }
};

struct ScanScalarImpl {
  template <class Context>
  auto operator()(const Context& ctx) const -> Result<std::string_view, Error> {
    const auto& reader = ctx.reader();
    return scan_scalar_for(reader, reader.current_token());
  }
};

struct ScanObjectImpl {
  template <class Context>
    requires(!std::is_const_v<std::remove_reference_t<Context>>)
  auto operator()(Context& ctx) const -> Result<std::string_view, Error> {
    auto& reader = ctx.reader();
    return scan_object_for(reader, reader.current_token());
  }
};

struct ScanArrayImpl {
  template <class Context>
    requires(!std::is_const_v<std::remove_reference_t<Context>>)
  auto operator()(Context& ctx) const -> Result<std::string_view, Error> {
    auto& reader = ctx.reader();
    return scan_array_for(reader, reader.current_token());
  }
};

}  // namespace detail

inline constexpr auto literal = hosq::step<detail::ScanLiteralImpl>;
inline constexpr auto string = hosq::step<detail::ScanStringImpl>;
inline constexpr auto scalar = hosq::step<detail::ScanScalarImpl>;
inline constexpr auto object = hosq::step<detail::ScanObjectImpl>;
inline constexpr auto array = hosq::step<detail::ScanArrayImpl>;

}  // namespace hosq::scan

#undef HOSQ_ALWAYS_INLINE_ATTR
#undef HOSQ_FUNCTION_ALWAYS_INLINE
