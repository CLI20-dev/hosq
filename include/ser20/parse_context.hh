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

struct ParseCtxBase {
  ParserScanner* scanner{nullptr};
  std::string_view input{};
  ScannerToken first_tok{};
  std::size_t colon_pos{0};
  std::string_view key{};
  std::size_t occurrences{0};
  ScannerToken pending_tok{};
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

SER20_ALWAYS_INLINE auto json_consume_nested(ParserScanner& scanner,
                                             TokenKind open, TokenKind close)
    -> std::size_t {
  int depth = 1;
  while (true) {
    auto token = scanner.consume_next_token();
    if (token.is_end()) [[unlikely]]
      return token.pos;
    if (token.kind == open) [[unlikely]] {
      ++depth;
      continue;
    }
    if (token.kind == close) {
      if (--depth == 0) [[unlikely]]
        return token.pos + 1;
    }
  }
}

SER20_ALWAYS_INLINE auto json_skip_value(ParserScanner& scanner,
                                         ScannerToken first) -> void {
  switch (first.kind) {
    case TokenKind::Quote:
      (void)scanner.consume_next_token();
      return;
    case TokenKind::LBrace:
      json_consume_nested(scanner, TokenKind::LBrace, TokenKind::RBrace);
      return;
    case TokenKind::LBracket:
      json_consume_nested(scanner, TokenKind::LBracket, TokenKind::RBracket);
      return;
    default:
      return;
  }
}

}  // namespace ser20
