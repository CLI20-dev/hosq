#pragma once

#include <cstddef>
#include <functional>
#include <string_view>

#ifdef __ARM_NEON
#include "ser20/scanner_neon.hh"
#else
#include "ser20/scanner.hh"
#endif

#ifndef SER20_ALWAYS_INLINE
#if defined(__clang__) || defined(__GNUC__)
#define SER20_ALWAYS_INLINE [[gnu::always_inline]] inline
#else
#define SER20_ALWAYS_INLINE inline
#endif
#endif

namespace ser20 {

#ifdef __ARM_NEON
using ParserScanner = ScannerNeon;
#else
using ParserScanner = Scanner;
#endif

// ── ParseCtxBase ──────────────────────────────────────────────────────────────
// first_pos  : absolute input position of the first value token
// colon_pos  : absolute position of ':' (used by ScalarFn for range extraction)
// pending_pos: set by ScalarFn to the delimiter (,  }  ]) it consumed
struct ParseCtxBase {
  ParserScanner* scanner{nullptr};
  std::string_view input{};
  std::size_t first_pos{0};
  std::size_t colon_pos{0};
  std::string_view key{};
  std::size_t occurrences{0};
  std::size_t pending_pos{0};
  bool next_is_pending{false};
};

template <class T>
struct ParseCtx : ParseCtxBase {
  std::reference_wrapper<T> arg;
  explicit ParseCtx(ParseCtxBase base, T& value)
      : ParseCtxBase(base), arg(value) {}
};

template <>
struct ParseCtx<void> : ParseCtxBase {
  template <class T>
  /*implicit*/ ParseCtx(ParseCtx<T>& other)
      : ParseCtxBase(static_cast<ParseCtxBase&>(other)) {}
  explicit ParseCtx(ParseCtxBase base) : ParseCtxBase(base) {}
  ParseCtx() = default;
};

// ── String and block skipping ─────────────────────────────────────────────────

// Skip past the closing '"' of a string.
// Call after the opening '"' position has been obtained via next().
// Returns the position of the closing '"', or input.size() on EOF.
SER20_ALWAYS_INLINE auto json_skip_string(ParserScanner& scanner) -> size_t {
  const std::string_view input = scanner.input();
  while (true) {
    const size_t pos = scanner.next();
    if (pos >= input.size()) return pos;
    if (input[pos] == '"') {
      size_t count = 0;
      while (count < pos && input[pos - 1 - count] == '\\') ++count;
      if ((count & 1u) == 0) return pos;  // unescaped
    }
  }
}

// Skip past the matching closing bracket/brace.
// `close` must be '}' or ']'; the paired open char is derived automatically.
// Returns the position just after the closing bracket.
SER20_ALWAYS_INLINE auto json_skip_block(ParserScanner& scanner, char close)
    -> size_t {
  const std::string_view input = scanner.input();
  const char open = (close == '}') ? '{' : '[';
  int depth = 1;
  while (true) {
    const size_t pos = scanner.next();
    if (pos >= input.size()) return pos;
    const char c = input[pos];
    if (c == '"') {
      (void)json_skip_string(scanner);
    } else if (c == open) {
      ++depth;
    } else if (c == close && --depth == 0) {
      return pos + 1;
    }
  }
}

// Skip any JSON value starting at first_pos.
SER20_ALWAYS_INLINE auto json_skip_value(ParserScanner& scanner,
                                         size_t first_pos) -> void {
  const std::string_view input = scanner.input();
  if (first_pos >= input.size()) return;
  const char c = input[first_pos];
  if (c == '"') {
    (void)json_skip_string(scanner);
  } else if (c == '{') {
    (void)json_skip_block(scanner, '}');
  } else if (c == '[') {
    (void)json_skip_block(scanner, ']');
  }
  // scalars: no extra candidates to consume
}

}  // namespace ser20
