#pragma once

#include <string_view>
#include <tuple>
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

inline constexpr std::size_t npos = static_cast<std::size_t>(-1);

template <class Tuple, std::size_t I = 0>
consteval auto unknown_handler_index() -> std::size_t {
  if constexpr (I >= std::tuple_size_v<std::remove_cvref_t<Tuple>>) {
    return npos;
  } else {
    using Field =
        std::remove_cvref_t<std::tuple_element_t<I, std::remove_cvref_t<Tuple>>>;
    if constexpr (Field::is_unknown_handler) {
      return I;
    } else {
      return unknown_handler_index<Tuple, I + 1>();
    }
  }
}

template <class Tuple>
SER20_ALWAYS_INLINE auto json_parse_object_body(ParserScanner& sc,
                                                std::string_view input,
                                                Tuple& fields)
    -> PipelineResult<void> {
  // '{' already consumed by caller
  while (true) {
    const size_t pos = sc.next();
    if (pos >= input.size()) [[unlikely]]
      break;
    const char c = input[pos];
    if (c == '}') [[unlikely]]
      return PipelineResult<void>::ok();
    if (c == ',') [[likely]]
      continue;
    if (c != '"') [[unlikely]]
      continue;  // unexpected, skip

    // pos = opening '"' of key name
    size_t key_close{};
    size_t colon_pos{};
    sc.next_two(key_close, colon_pos);
    if (key_close >= input.size()) [[unlikely]]
      break;
    const size_t kstart = pos + 1;
    std::string_view key =
        input.substr(kstart, key_close > kstart ? key_close - kstart : 0);

    if (colon_pos >= input.size() || input[colon_pos] != ':') [[unlikely]]
      break;

    const size_t first_pos = sc.next();
    if (first_pos >= input.size()) [[unlikely]]
      break;

    ParseCtxBase base{
        .scanner = &sc,
        .input = input,
        .first_pos = first_pos,
        .colon_pos = colon_pos,
        .key = key,
        .occurrences = 0,
        .next_is_pending = false,
    };

    auto field_result = PipelineResult<void>::ok();
    bool matched = Matcher::find(key, fields, [&](auto& field) -> auto {
      field_result = field.parse(base);
    });

    if (matched && field_result.has_error()) [[unlikely]]
      return field_result;

    if (!matched) {
      constexpr auto unknown_index = unknown_handler_index<Tuple>();
      if constexpr (unknown_index != npos) {
        auto unknown_result = std::get<unknown_index>(fields).parse(base);
        if (unknown_result.has_error()) [[unlikely]]
          return unknown_result;
      } else {
        if (!base.next_is_pending) json_skip_value(sc, first_pos);
      }
    }

    // After field parse: if scalar, pending_pos holds the delimiter position.
    if (base.next_is_pending && input[base.pending_pos] == '}')
      return PipelineResult<void>::ok();
  }
  return PipelineResult<void>::ok();
}

template <class Tuple>
auto json_parse_object(ParserScanner& sc, std::string_view input, Tuple& fields)
    -> PipelineResult<void> {
  const size_t pos = sc.next();
  if (pos >= input.size() || input[pos] != '{')
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
template <StringLiteral Key, auto Val>
struct Constant {
  static constexpr std::string_view name{Key.data(), Key.size()};
  static constexpr std::string_view discriminant_key{Key.data(), Key.size()};
  static constexpr std::string_view discriminant_value = decltype(Val)::value;
  static constexpr bool is_unknown_handler = false;

  SER20_ALWAYS_INLINE auto parse(ParseCtxBase& base) -> PipelineResult<void> {
    const char c =
        base.first_pos < base.input.size() ? base.input[base.first_pos] : '\0';
    if (c == '"') {
      (void)json_skip_string(*base.scanner);
    } else if (c == '{') {
      (void)json_skip_block(*base.scanner, '}');
    } else if (c == '[') {
      (void)json_skip_block(*base.scanner, ']');
    } else {
      base.next_is_pending = true;
      base.pending_pos = base.first_pos;
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
auto parse_result(std::string_view text) -> PipelineResult<T> {
  T result{};
  ParserScanner sc(text);
  auto fields = cli::as_tuple(result);
  auto parsed = detail::json_parse_object(sc, text, fields);
  if (!parsed.have_value()) [[unlikely]]
    return PipelineResult<T>::fail(parsed.error());
  return PipelineResult<T>::ok(std::move(result));
}

template <class T>
auto parse(std::string_view text) -> T {
  T result{};
  ParserScanner sc(text);
  auto fields = cli::as_tuple(result);
  auto parsed = detail::json_parse_object(sc, text, fields);
  if (!parsed.have_value()) [[unlikely]]
    return T{};
  return result;
}

#undef SER20_ALWAYS_INLINE

}  // namespace ser20

// ── _json_string UDL ─────────────────────────────────────────────────────────
template <ser20::StringLiteral S>
constexpr auto operator""_json_string() {
  return ser20::JsonString<S>{};
}
