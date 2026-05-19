#pragma once

#include <array>

#include "ser20/error.hh"
#include "ser20/pipeline_util.hh"
#include "ser20/reader_base.hh"
#include "ser20/result.hh"

namespace ser20::scan {

namespace json::detail {

[[nodiscard]]
constexpr auto is_literal_end(char c) noexcept -> bool {
  return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == ',' ||
         c == ']' || c == '}';
}

template <class Reader>
auto find_string_end(const Reader& reader, std::size_t quote_offset)
    -> Result<std::size_t, Error> {
  const auto input = reader.input();
  if (quote_offset >= input.size() || input[quote_offset] != '"') [[unlikely]] {
    return unexpected(Error{
        .code = ErrorCode::invalid_token,
        .offset = quote_offset,
    });
  }
  std::size_t i = quote_offset + 1;
  while (i < input.size()) {
    const char c = input[i];
    if (c == '\\') {
      i += 2;
      continue;
    }
    if (c == '"') {
      return i;
    }
    ++i;
  }
  return unexpected(Error{
      .code = ErrorCode::unexpected_eof,
      .offset = quote_offset,
  });
}

template <class Reader>
auto scan_literal(const Reader& reader, const Token& token)
    -> Result<std::string_view, Error> {
  const auto input = reader.input();
  if (token.length == unresolved_length) {
    std::size_t end = token.offset;
    while (end < input.size() && !is_literal_end(input[end])) {
      ++end;
    }
    token.length = end - token.offset;
    auto advanced = reader.advance(token.length);
    if (!advanced) [[unlikely]] {
      token.length = unresolved_length;
      return unexpected(advanced.error());
    }
  }
  return input.substr(token.offset, token.length);
}

template <class Reader>
auto scan_string(const Reader& reader, const Token& token)
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

inline constexpr std::size_t raw_scan_max_depth = 1024;

[[nodiscard]]
constexpr auto is_open_bracket(char c) noexcept -> bool {
  return c == '{' || c == '[';
}

[[nodiscard]]
constexpr auto is_close_bracket(char c) noexcept -> bool {
  return c == '}' || c == ']';
}

[[nodiscard]]
constexpr auto is_close_for(char open, char close) noexcept -> bool {
  return (open == '{' && close == '}') || (open == '[' && close == ']');
}

template <class Reader>
auto scan_compound(const Reader& reader, const Token& token, char expected_open)
    -> Result<std::string_view, Error> {
  const auto input = reader.input();

  if (token.offset >= input.size() || input[token.offset] != expected_open)
      [[unlikely]] {
    return unexpected(Error{
        .code = ErrorCode::invalid_token,
        .offset = token.offset,
    });
  }

  if (token.length == unresolved_length) {
    std::array<char, raw_scan_max_depth> stack{};
    std::size_t depth = 0;

    stack[depth++] = expected_open;

    std::size_t i = token.offset + 1;

    while (i < input.size()) {
      const char c = input[i];

      if (c == '"') {
        auto end = find_string_end(reader, i);
        if (!end) [[unlikely]] {
          return unexpected(end.error());
        }

        i = *end + 1;
        continue;
      }

      if (is_open_bracket(c)) {
        if (depth >= raw_scan_max_depth) [[unlikely]] {
          return unexpected(Error{
              .code = ErrorCode::depth_limit_exceeded,
              .offset = i,
          });
        }

        stack[depth++] = c;
        ++i;
        continue;
      }

      if (is_close_bracket(c)) {
        if (depth == 0 || !is_close_for(stack[depth - 1], c)) [[unlikely]] {
          return unexpected(Error{
              .code = ErrorCode::unexpected_token,
              .offset = i,
          });
        }

        --depth;
        ++i;

        if (depth == 0) {
          token.length = i - token.offset;

          auto advanced = reader.advance(token.length);
          if (!advanced) [[unlikely]] {
            token.length = unresolved_length;
            return unexpected(advanced.error());
          }

          return input.substr(token.offset, token.length);
        }

        continue;
      }

      ++i;
    }

    return unexpected(Error{
        .code = ErrorCode::unexpected_eof,
        .offset = token.offset,
    });
  }

  return input.substr(token.offset, token.length);
}

[[nodiscard]]
constexpr auto is_literal_token(const Token& token) noexcept -> bool {
  return token.type == TokenType::ScalarBegin &&
         static_cast<JsonTokenContext>(token.context) ==
             JsonTokenContext::literal;
}

[[nodiscard]]
constexpr auto is_string_token(const Token& token) noexcept -> bool {
  return token.type == TokenType::ScalarBegin &&
         static_cast<JsonTokenContext>(token.context) ==
             JsonTokenContext::string;
}

[[nodiscard]]
constexpr auto is_scalar_token(const Token& token) noexcept -> bool {
  return is_literal_token(token) || is_string_token(token);
}

[[nodiscard]]
constexpr auto is_object_token(const Token& token) noexcept -> bool {
  return token.type == TokenType::ObjectBegin;
}

[[nodiscard]]
constexpr auto is_array_token(const Token& token) noexcept -> bool {
  return token.type == TokenType::ArrayBegin;
}

}  // namespace json::detail

namespace detail {

template <class>
inline constexpr bool always_false = false;

template <class Reader>
auto scan_literal_for(const Reader& reader, const Token& token)
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
                  "ser20::scan::literal is not implemented for this reader");
  }
}

template <class Reader>
auto scan_string_for(const Reader& reader, const Token& token)
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
                  "ser20::scan::string is not implemented for this reader");
  }
}

template <class Reader>
auto scan_scalar_for(const Reader& reader, const Token& token)
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
                  "ser20::scan::scalar is not implemented for this reader");
  }
}

template <class Reader>
auto scan_object_for(const Reader& reader, const Token& token)
    -> Result<std::string_view, Error> {
  using R = std::remove_cvref_t<Reader>;
  if constexpr (ReaderTraits<R>::format == Format::json) {
    if (!json::detail::is_object_token(token)) [[unlikely]] {
      return unexpected(Error{
          .code = ErrorCode::type_mismatch,
          .offset = token.offset,
      });
    }
    return json::detail::scan_compound(reader, token, '{');
  } else {
    static_assert(always_false<R>,
                  "ser20::scan::object is not implemented for this reader");
  }
}

template <class Reader>
auto scan_array_for(const Reader& reader, const Token& token)
    -> Result<std::string_view, Error> {
  using R = std::remove_cvref_t<Reader>;
  if constexpr (ReaderTraits<R>::format == Format::json) {
    if (!json::detail::is_array_token(token)) [[unlikely]] {
      return unexpected(Error{
          .code = ErrorCode::type_mismatch,
          .offset = token.offset,
      });
    }
    return json::detail::scan_compound(reader, token, '[');
  } else {
    static_assert(always_false<R>,
                  "ser20::scan::array is not implemented for this reader");
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
  auto operator()(Context& ctx) const -> Result<std::string_view, Error> {
    auto& reader = ctx.reader();
    return scan_object_for(reader, reader.current_token());
  }
};

struct ScanArrayImpl {
  template <class Context>
  auto operator()(Context& ctx) const -> Result<std::string_view, Error> {
    auto& reader = ctx.reader();
    return scan_array_for(reader, reader.current_token());
  }
};

}  // namespace detail

inline constexpr auto literal = ser20::step<detail::ScanLiteralImpl>;
inline constexpr auto string = ser20::step<detail::ScanStringImpl>;
inline constexpr auto scalar = ser20::step<detail::ScanScalarImpl>;
inline constexpr auto object = ser20::step<detail::ScanObjectImpl>;
inline constexpr auto array = ser20::step<detail::ScanArrayImpl>;

}  // namespace ser20::scan
