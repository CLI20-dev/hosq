#pragma once

#include <string_view>
#include <variant>

#include "ser20/element.hh"
#include "ser20/matcher.hh"
#include "ser20/parse_context.hh"
#include "ser20/pipeline.hh"
#include "ser20/string_literal.hh"
#include "ser20/to_tuple.hh"

namespace ser20 {

// ═══════════════════════════════════════════════════════════════════════════════
// json_parse_object — core recursive object parser
// ═══════════════════════════════════════════════════════════════════════════════

namespace detail {

#if defined(__GNUC__) || defined(__clang__)
#define SER20_ALWAYS_INLINE inline __attribute__((always_inline))
#elif defined(_MSC_VER)
#define SER20_ALWAYS_INLINE __forceinline
#else
#define SER20_ALWAYS_INLINE inline
#endif

template <class Tuple>
SER20_ALWAYS_INLINE auto json_parse_object_body(ParserScanner& sc,
                                                std::string_view input,
                                                Tuple& fields)
    -> PipelineResult<void> {
  // '{' already consumed by caller
  while (true) {
    auto tok = sc.consume_next_token();
    if (tok.is_end()) [[unlikely]]
      break;
    if (tok.kind == TokenKind::RBrace) [[unlikely]]
      return PipelineResult<void>::ok();
    if (tok.kind == TokenKind::Comma) [[likely]]
      continue;
    if (tok.kind != TokenKind::Quote) [[unlikely]]
      continue;  // unexpected, skip

    auto key_close = sc.consume_next_token();
    if (key_close.is_end()) [[unlikely]]
      break;
    auto kstart = tok.pos + 1;
    std::string_view key = input.substr(
        kstart, key_close.pos > kstart ? key_close.pos - kstart : 0);

    auto colon_tok = sc.consume_next_token();
    if (colon_tok.is_end() || colon_tok.kind != TokenKind::Colon) [[unlikely]]
      break;

    auto first_tok = sc.consume_next_token();
    if (first_tok.is_end()) [[unlikely]]
      break;

    ParseCtxBase base{
        .scanner = &sc,
        .input = input,
        .first_tok = first_tok,
        .colon_pos = colon_tok.pos,
        .key = key,
        .occurrences = 0,
        .next_is_pending = false,
    };

    bool matched = Matcher::find(
        key, fields, [&](auto& field) -> auto { field.parse(base); });

    if (!matched) {
      // Invoke the catch-all field (empty name) if present; else skip.
      bool handled = false;
      std::apply(
          [&](auto&... f) -> auto {
            ((!handled && f.is_unknown_handler
                  ? (handled = true, f.parse(base), void())
                  : void()),
             ...);
          },
          fields);
      if (!handled) {
        if (!base.next_is_pending) json_skip_value(sc, first_tok);
      }
    }

    // After field parse: if scalar, pending_tok is the delimiter.
    if (base.next_is_pending && base.pending_tok.kind == TokenKind::RBrace)
      return PipelineResult<void>::ok();
  }
  return PipelineResult<void>::ok();
}

template <class Tuple>
auto json_parse_object(ParserScanner& sc, std::string_view input, Tuple& fields)
    -> PipelineResult<void> {
  auto tok = sc.consume_next_token();
  if (tok.is_end() || tok.kind != TokenKind::LBrace)
    return PipelineResult<void>::fail(
        {.code = ErrorCode::invalid_value, .subject = "{"});
  return json_parse_object_body(sc, input, fields);
}

}  // namespace detail

// ═══════════════════════════════════════════════════════════════════════════════
// ser20::JsonString, ser20::Constant, ser20::Variant
// ═══════════════════════════════════════════════════════════════════════════════

template <StringLiteral S>
struct JsonString {
  static constexpr std::string_view value{S.data(), S.size()};
};

// Constant<Key, Val> — member field that acts as a discriminant for Variant.
// Placed as a data member (not base class) so cli::as_tuple counts it correctly.
// When the JSON parser encounters the matching key, parse() validates/consumes
// the value. VariantFn detects it by checking field types for discriminant_key.
template <StringLiteral Key, auto Val>
struct Constant {
  static constexpr std::string_view name{Key.data(), Key.size()};
  static constexpr std::string_view discriminant_key{Key.data(), Key.size()};
  static constexpr std::string_view discriminant_value = decltype(Val)::value;
  static constexpr bool is_unknown_handler = false;

  SER20_ALWAYS_INLINE auto parse(ParseCtxBase& base) -> PipelineResult<void> {
    const auto& f = base.first_tok;
    if (f.kind == TokenKind::Quote) [[likely]] {
      (void)base.scanner->consume_next_token();
    } else if (f.kind == TokenKind::LBrace) {
      json_consume_nested(*base.scanner, TokenKind::LBrace, TokenKind::RBrace);
    } else if (f.kind == TokenKind::LBracket) {
      json_consume_nested(*base.scanner, TokenKind::LBracket,
                          TokenKind::RBracket);
    } else {
      base.next_is_pending = true;
      base.pending_tok = f;
    }
    return PipelineResult<void>::ok();
  }
};

template <class... Ts>
using Variant = std::variant<Ts...>;

// ═══════════════════════════════════════════════════════════════════════════════
// ser20::parse<T>
// ═══════════════════════════════════════════════════════════════════════════════

template <class T>
auto parse(std::string_view text) -> T {
  T result{};
  ParserScanner sc(text);
  auto fields = cli::as_tuple(result);
  detail::json_parse_object(sc, text, fields);
  return result;
}

#undef SER20_ALWAYS_INLINE

}  // namespace ser20

// ── _json_string UDL ─────────────────────────────────────────────────────────
template <ser20::StringLiteral S>
constexpr auto operator""_json_string() {
  return ser20::JsonString<S>{};
}
