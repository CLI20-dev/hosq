#pragma once

#include <charconv>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <variant>
#include <vector>

#ifdef __ARM_NEON
#include "ser20/scanner_neon.hh"
#else
#include "ser20/scanner.hh"
#endif

#include "ser20/error.hh"
#include "ser20/matcher.hh"
#include "ser20/pipeline.hh"
#include "ser20/string_literal.hh"
#include "ser20/to_tuple.hh"

// ── Force-inline helper ───────────────────────────────────────────────────────
#if defined(__clang__) || defined(__GNUC__)
#  define SER20_ALWAYS_INLINE [[gnu::always_inline]] inline
#else
#  define SER20_ALWAYS_INLINE inline
#endif

namespace ser20 {

// ── Scanner alias ─────────────────────────────────────────────────────────────
#ifdef __ARM_NEON
using JsonScanner = ScannerNeon;
#else
using JsonScanner = Scanner;
#endif

// ── ParseCtx ──────────────────────────────────────────────────────────────────
// Context threaded through every JSON pipeline step.
//
// `first_tok`: first structural token after the current key's ':'.
//   Quote / LBrace / LBracket → value type
//   Comma / RBrace / RBracket → scalar (the delimiter IS first_tok)
//
// When first_tok is a delimiter, scan::scalar sets next_is_pending = true so
// the outer object-loop re-uses pending_tok instead of consuming a new token.

struct ParseCtxBase {
  JsonScanner*     scanner{nullptr};
  std::string_view input{};
  ScannerToken     first_tok{};
  std::size_t      colon_pos{0};   // position of ':' preceding the value
  std::string_view key{};
  std::size_t      occurrences{0};
  ScannerToken     pending_tok{};
  bool             next_is_pending{false};
};

// ParseCtx<T>  — terminal steps use ctx.arg to write the parsed value.
// ParseCtx<void> — intermediate steps; upcast from ParseCtx<T>& via inheritance.
template <class T>
struct ParseCtx : ParseCtxBase {
  T& arg;
  explicit ParseCtx(ParseCtxBase b, T& a) : ParseCtxBase(b), arg(a) {}
};

template <>
struct ParseCtx<void> : ParseCtxBase {
  template <class T>
  /*implicit*/ ParseCtx(ParseCtx<T>& o)  // NOLINT(google-explicit-constructor)
      : ParseCtxBase(static_cast<ParseCtxBase&>(o)) {}
  explicit ParseCtx(ParseCtxBase b) : ParseCtxBase(b) {}
  ParseCtx() = default;
};

// ── JsonPipe<Fns...> ──────────────────────────────────────────────────────────
// Compile-time chain of JSON functors.
// Each functor must expose:
//   template<class I> using after_type    = <output type>;
//   template<class I> using storage_type  = void | <terminal storage type>;
// and be callable as:
//   operator()(ParseCtx<Storage>& ctx, PipelineResult<In>) -> PipelineResult<Out>
//
// Composed with operator|.  Storage deduced by scanning after_type chain.

template <auto... Fns> struct JsonPipe;

template <>
struct JsonPipe<> {
  template <class St, class In>
  static auto invoke(ParseCtx<St>&, PipelineResult<In> v) { return v; }
};

template <auto FnH, auto... FnT>
struct JsonPipe<FnH, FnT...> {
  template <class St, class In>
  static auto invoke(ParseCtx<St>& ctx, PipelineResult<In> v) -> decltype(auto) {
    auto next = FnH(ctx, std::move(v));
    if constexpr (sizeof...(FnT) == 0) return next;
    else return JsonPipe<FnT...>::invoke(ctx, std::move(next));
  }
};

template <auto Fn, auto... Fns>
constexpr auto operator|(JsonPipe<Fns...>, JsonPipe<Fn>) {
  return JsonPipe<Fns..., Fn>{};
}

// ── storage_type computation ──────────────────────────────────────────────────
// Walk the after_type chain to find the terminal storage_type.

template <class Entry, auto FnH, auto... FnT>
struct JsonStorageOf {
  using After = typename std::remove_cvref_t<decltype(FnH)>::template after_type<Entry>;
  using type  = typename JsonStorageOf<After, FnT...>::type;
};
template <class Entry, auto FnL>
struct JsonStorageOf<Entry, FnL> {
  using type = typename std::remove_cvref_t<decltype(FnL)>::template storage_type<Entry>;
};

// Type-level helper so partial spec can see the Fns pack (value-level NTTP
// partial specs are ill-formed; we specialize on the TYPE JsonPipe<Fns...>).
template <class PipeType> struct JsonPipeStorageHelper;
template <auto... Fns>
struct JsonPipeStorageHelper<JsonPipe<Fns...>> {
  using type = typename JsonStorageOf<void, Fns...>::type;
};

template <auto Pipe>
using json_storage_t =
    typename JsonPipeStorageHelper<std::remove_cvref_t<decltype(Pipe)>>::type;

// ── whitespace utilities ──────────────────────────────────────────────────────
SER20_ALWAYS_INLINE auto json_skip_ws(std::string_view s, std::size_t p) noexcept
    -> std::size_t {
  while (p < s.size() &&
         (s[p] == ' ' || s[p] == '\t' || s[p] == '\r' || s[p] == '\n')) [[unlikely]]
    ++p;
  return p;
}
SER20_ALWAYS_INLINE auto json_rtrim(std::string_view s, std::size_t e) noexcept
    -> std::size_t {
  while (e > 0 &&
         (s[e-1] == ' ' || s[e-1] == '\t' || s[e-1] == '\r' || s[e-1] == '\n')) [[unlikely]]
    --e;
  return e;
}

// Consume tokens until matching close (depth starts at 1 = caller already past open).
// Returns position just past the closing char.
SER20_ALWAYS_INLINE auto json_consume_nested(JsonScanner& sc,
                                              TokenKind open, TokenKind close)
    -> std::size_t {
  int depth = 1;
  while (true) {
    auto tok = sc.consume_next_token();
    if (tok.is_end()) [[unlikely]] return tok.pos;
    if (tok.kind == open)  [[unlikely]] { ++depth; continue; }
    if (tok.kind == close) { if (--depth == 0) [[unlikely]] return tok.pos + 1; }
  }
}

// Skip a value whose first_tok has already been consumed.
SER20_ALWAYS_INLINE auto json_skip_value(JsonScanner& sc, ScannerToken first) -> void {
  switch (first.kind) {
    case TokenKind::Quote:
      (void)sc.consume_next_token();  // closing quote
      return;
    case TokenKind::LBrace:
      json_consume_nested(sc, TokenKind::LBrace, TokenKind::RBrace);
      return;
    case TokenKind::LBracket:
      json_consume_nested(sc, TokenKind::LBracket, TokenKind::RBracket);
      return;
    default:
      return;  // scalar: delimiter already consumed
  }
}

// ── error helpers ─────────────────────────────────────────────────────────────
template <class T>
SER20_ALWAYS_INLINE auto json_fail(ErrorCode code, std::string_view subject = {})
    -> PipelineResult<T> {
  return PipelineResult<T>::fail({.code = code, .subject = std::string(subject)});
}

// ═══════════════════════════════════════════════════════════════════════════════
// ser20::scan::*
// ═══════════════════════════════════════════════════════════════════════════════
namespace scan {

// scalar ──────────────────────────────────────────────────────────────────────
// first_tok must be a delimiter (Comma / RBrace / RBracket).
// Extracts input[colon_pos+1 .. first_tok.pos) trimmed.
// Sets next_is_pending so the outer loop re-uses the delimiter.
struct ScalarFn {
  template <class I> using after_type   = std::string_view;
  template <class I> using storage_type = void;
  template <class St>
  SER20_ALWAYS_INLINE auto operator()(ParseCtx<St>& ctx, PipelineResult<void>) const
      -> PipelineResult<std::string_view> {
    const auto& f = ctx.first_tok;
    if (f.kind == TokenKind::Quote || f.kind == TokenKind::LBrace ||
        f.kind == TokenKind::LBracket) [[unlikely]]
      return json_fail<std::string_view>(ErrorCode::invalid_value, "expected scalar");
    ctx.next_is_pending = true;
    ctx.pending_tok     = f;
    auto s = json_skip_ws(ctx.input, ctx.colon_pos + 1);
    auto e = json_rtrim(ctx.input, f.pos);
    if (s >= e) [[unlikely]]
      return json_fail<std::string_view>(ErrorCode::invalid_value, "empty scalar");
    return PipelineResult<std::string_view>::ok(ctx.input.substr(s, e - s));
  }
};

// string ──────────────────────────────────────────────────────────────────────
// first_tok must be Quote (opening).  Consumes the closing Quote.
// Returns the content between the quotes (no escape resolution).
struct StringFn {
  template <class I> using after_type   = std::string_view;
  template <class I> using storage_type = void;
  template <class St>
  SER20_ALWAYS_INLINE auto operator()(ParseCtx<St>& ctx, PipelineResult<void>) const
      -> PipelineResult<std::string_view> {
    if (ctx.first_tok.kind != TokenKind::Quote) [[unlikely]]
      return json_fail<std::string_view>(ErrorCode::invalid_value, "expected string");
    auto close = ctx.scanner->consume_next_token();
    if (close.is_end()) [[unlikely]]
      return json_fail<std::string_view>(ErrorCode::invalid_value, "unterminated string");
    auto s = ctx.first_tok.pos + 1;
    return PipelineResult<std::string_view>::ok(
        ctx.input.substr(s, close.pos > s ? close.pos - s : 0));
  }
};

// object_raw / array_raw ──────────────────────────────────────────────────────
struct ObjectRawFn {
  template <class I> using after_type   = std::string_view;
  template <class I> using storage_type = void;
  template <class St>
  auto operator()(ParseCtx<St>& ctx, PipelineResult<void>) const
      -> PipelineResult<std::string_view> {
    if (ctx.first_tok.kind != TokenKind::LBrace)
      return json_fail<std::string_view>(ErrorCode::invalid_value, "expected {");
    auto start = ctx.first_tok.pos;
    auto end   = json_consume_nested(*ctx.scanner,
                                      TokenKind::LBrace, TokenKind::RBrace);
    return PipelineResult<std::string_view>::ok(ctx.input.substr(start, end - start));
  }
};

struct ArrayRawFn {
  template <class I> using after_type   = std::string_view;
  template <class I> using storage_type = void;
  template <class St>
  auto operator()(ParseCtx<St>& ctx, PipelineResult<void>) const
      -> PipelineResult<std::string_view> {
    if (ctx.first_tok.kind != TokenKind::LBracket)
      return json_fail<std::string_view>(ErrorCode::invalid_value, "expected [");
    auto start = ctx.first_tok.pos;
    auto end   = json_consume_nested(*ctx.scanner,
                                      TokenKind::LBracket, TokenKind::RBracket);
    return PipelineResult<std::string_view>::ok(ctx.input.substr(start, end - start));
  }
};

inline constexpr auto scalar     = JsonPipe<ScalarFn{}>{};
inline constexpr auto string     = JsonPipe<StringFn{}>{};
inline constexpr auto object_raw = JsonPipe<ObjectRawFn{}>{};
inline constexpr auto array_raw  = JsonPipe<ArrayRawFn{}>{};

}  // namespace scan

// ═══════════════════════════════════════════════════════════════════════════════
// Forward declarations needed for mutual recursion
// ═══════════════════════════════════════════════════════════════════════════════

namespace detail {
// json_parse_object_body: parse key-value pairs with sc positioned AFTER '{'.
template <class Tuple>
auto json_parse_object_body(JsonScanner& sc, std::string_view input, Tuple& fields)
    -> PipelineResult<void>;

// json_parse_object: consume '{' then delegate to _body.
template <class Tuple>
auto json_parse_object(JsonScanner& sc, std::string_view input, Tuple& fields)
    -> PipelineResult<void>;
}

// ═══════════════════════════════════════════════════════════════════════════════
// ser20::decode::*
// ═══════════════════════════════════════════════════════════════════════════════
namespace decode {

// integer<T> ──────────────────────────────────────────────────────────────────
template <class T>
struct IntegerFn {
  template <class I> using after_type   = T;
  template <class I> using storage_type = void;
  template <class St>
  SER20_ALWAYS_INLINE auto operator()(ParseCtx<St>&, PipelineResult<std::string_view> in) const
      -> PipelineResult<T> {
    if (!in.have_value()) [[unlikely]] return PipelineResult<T>::fail(in.error);
    T r{};
    auto [p, ec] = std::from_chars(in.value.data(), in.value.data() + in.value.size(), r);
    if (ec != std::errc{}) [[unlikely]] return json_fail<T>(ErrorCode::invalid_value, in.value);
    return PipelineResult<T>::ok(r);
  }
};

// floating<T> ─────────────────────────────────────────────────────────────────
template <class T>
struct FloatingFn {
  template <class I> using after_type   = T;
  template <class I> using storage_type = void;
  template <class St>
  SER20_ALWAYS_INLINE auto operator()(ParseCtx<St>&, PipelineResult<std::string_view> in) const
      -> PipelineResult<T> {
    if (!in.have_value()) [[unlikely]] return PipelineResult<T>::fail(in.error);
    T r{};
    auto [p, ec] = std::from_chars(in.value.data(), in.value.data() + in.value.size(), r);
    if (ec != std::errc{}) [[unlikely]] return json_fail<T>(ErrorCode::invalid_value, in.value);
    return PipelineResult<T>::ok(r);
  }
};

// boolean ─────────────────────────────────────────────────────────────────────
struct BooleanFn {
  template <class I> using after_type   = bool;
  template <class I> using storage_type = void;
  template <class St>
  SER20_ALWAYS_INLINE auto operator()(ParseCtx<St>&, PipelineResult<std::string_view> in) const
      -> PipelineResult<bool> {
    if (!in.have_value()) [[unlikely]] return PipelineResult<bool>::fail(in.error);
    if (in.value == "true")  [[likely]]   return PipelineResult<bool>::ok(true);
    if (in.value == "false")              return PipelineResult<bool>::ok(false);
    return json_fail<bool>(ErrorCode::invalid_value, in.value);
  }
};

// string_view ─────────────────────────────────────────────────────────────────
struct StringViewFn {
  template <class I> using after_type   = std::string_view;
  template <class I> using storage_type = void;
  template <class St>
  SER20_ALWAYS_INLINE auto operator()(ParseCtx<St>&, PipelineResult<std::string_view> in) const
      -> PipelineResult<std::string_view> { return in; }
};

// string (owned, escape-resolved TODO) ────────────────────────────────────────
struct OwnedStringFn {
  template <class I> using after_type   = std::string;
  template <class I> using storage_type = void;
  template <class St>
  SER20_ALWAYS_INLINE auto operator()(ParseCtx<St>&, PipelineResult<std::string_view> in) const
      -> PipelineResult<std::string> {
    if (!in.have_value()) [[unlikely]] return PipelineResult<std::string>::fail(in.error);
    return PipelineResult<std::string>::ok(std::string(in.value));
  }
};

// identity ────────────────────────────────────────────────────────────────────
struct IdentityFn {
  template <class I> using after_type   = I;
  template <class I> using storage_type = void;
  template <class St, class In>
  auto operator()(ParseCtx<St>&, PipelineResult<In> in) const
      -> PipelineResult<In> { return in; }
};

// custom<F> ───────────────────────────────────────────────────────────────────
template <auto F>
struct CustomFn {
  template <class I>
  using after_type = std::invoke_result_t<decltype(F), const I&>;
  template <class I> using storage_type = void;
  template <class St, class In>
  SER20_ALWAYS_INLINE auto operator()(ParseCtx<St>&, PipelineResult<In> in) const
      -> PipelineResult<std::invoke_result_t<decltype(F), const In&>> {
    using Out = std::invoke_result_t<decltype(F), const In&>;
    if (!in.have_value()) [[unlikely]] return PipelineResult<Out>::fail(in.error);
    return PipelineResult<Out>::ok(F(in.value));
  }
};

// parse_to<T> ─────────────────────────────────────────────────────────────────
// first_tok must be LBrace.  Sub-scans the object into T.
template <class T>
struct ParseToFn {
  template <class I> using after_type   = T;
  template <class I> using storage_type = void;
  template <class St>
  SER20_ALWAYS_INLINE auto operator()(ParseCtx<St>& ctx, PipelineResult<void>) const
      -> PipelineResult<T> {
    if (ctx.first_tok.kind != TokenKind::LBrace) [[unlikely]]
      return json_fail<T>(ErrorCode::invalid_value, "expected {");
    T result{};
    auto fields = cli::as_tuple(result);
    auto r = detail::json_parse_object_body(*ctx.scanner, ctx.input, fields);
    if (!r.have_value()) [[unlikely]] return PipelineResult<T>::fail(r.error);
    return PipelineResult<T>::ok(std::move(result));
  }
};

// vector<Pipe> ────────────────────────────────────────────────────────────────
// first_tok must be LBracket.  Iterates elements, applying Pipe to each.
template <auto Pipe>
struct VectorFn {
  using elem_t   = json_storage_t<Pipe>;
  using pipe_t   = std::remove_cvref_t<decltype(Pipe)>;
  template <class I> using after_type   = std::vector<elem_t>;
  template <class I> using storage_type = void;

  template <class St>
  SER20_ALWAYS_INLINE auto operator()(ParseCtx<St>& ctx, PipelineResult<void>) const
      -> PipelineResult<std::vector<elem_t>> {
    if (ctx.first_tok.kind != TokenKind::LBracket) [[unlikely]]
      return json_fail<std::vector<elem_t>>(ErrorCode::invalid_value, "expected [");
    JsonScanner& sc = *ctx.scanner;
    std::vector<elem_t> out;

    // last_pos: start of the current potential scalar element (after '[' or ',').
    // just_saw_structured: true after consuming a Quote/LBrace/LBracket element,
    // so we skip the scalar check when the next ',' or ']' arrives.
    std::size_t last_pos = ctx.first_tok.pos + 1;
    bool just_saw_structured = false;

    auto process_scalar = [&](ScannerToken delim) {
      auto s = json_skip_ws(ctx.input, last_pos);
      auto e = json_rtrim(ctx.input, delim.pos);
      if (s >= e) return;
      elem_t val{};
      // colon_pos is set to last_pos-1 so colon_pos+1 == last_pos (ScalarFn start)
      ParseCtxBase base{
          .scanner   = &sc,
          .input     = ctx.input,
          .first_tok = delim,
          .colon_pos = last_pos > 0 ? last_pos - 1 : 0,
      };
      ParseCtx<elem_t> ectx{base, val};
      auto r = pipe_t::invoke(ectx, PipelineResult<void>::ok());
      if (r.have_value()) out.push_back(std::move(val));
    };

    while (true) {
      auto tok = sc.consume_next_token();
      if (tok.is_end()) break;

      if (tok.kind == TokenKind::RBracket) [[unlikely]] {
        if (!just_saw_structured) process_scalar(tok);
        break;
      }

      if (tok.kind == TokenKind::Comma) [[likely]] {
        if (!just_saw_structured) process_scalar(tok);
        last_pos = tok.pos + 1;
        just_saw_structured = false;
        continue;
      }

      // Structured element: Quote, LBrace, LBracket
      just_saw_structured = true;
      elem_t val{};
      ParseCtxBase base{
          .scanner   = &sc,
          .input     = ctx.input,
          .first_tok = tok,
          .colon_pos = 0,
      };
      ParseCtx<elem_t> ectx{base, val};
      auto r = pipe_t::invoke(ectx, PipelineResult<void>::ok());
      if (r.have_value()) out.push_back(std::move(val));
    }

    return PipelineResult<std::vector<elem_t>>::ok(std::move(out));
  }
};

// variant<Ts...> ──────────────────────────────────────────────────────────────
// first_tok must be LBrace.  Tries each T in order; picks the one whose
// Constant<> discriminant matches.
template <class... Ts>
struct VariantFn {
  template <class I> using after_type   = std::variant<Ts...>;
  template <class I> using storage_type = void;

  // Scan a top-level string value for the given key, without allocating.
  static auto find_disc(std::string_view obj, std::string_view key)
      -> std::string_view {
    JsonScanner sc(obj);
    auto tok = sc.consume_next_token();
    if (tok.is_end() || tok.kind != TokenKind::LBrace) return {};
    while (true) {
      tok = sc.consume_next_token();
      if (tok.is_end() || tok.kind == TokenKind::RBrace) return {};
      if (tok.kind == TokenKind::Comma) continue;
      if (tok.kind != TokenKind::Quote) continue;
      auto kc = sc.consume_next_token();
      if (kc.is_end()) return {};
      auto ks = tok.pos + 1;
      std::string_view k = obj.substr(ks, kc.pos > ks ? kc.pos - ks : 0);
      auto colon = sc.consume_next_token();
      if (colon.is_end()) return {};
      auto first = sc.consume_next_token();
      if (first.is_end()) return {};
      if (k == key) {
        if (first.kind == TokenKind::Quote) {
          auto cl = sc.consume_next_token();
          auto vs = first.pos + 1;
          return obj.substr(vs, cl.pos > vs ? cl.pos - vs : 0);
        }
        // scalar discriminant
        auto nxt = sc.consume_next_token();
        auto vs = json_skip_ws(obj, colon.pos + 1);
        auto ve = json_rtrim(obj, nxt.pos);
        return obj.substr(vs, ve > vs ? ve - vs : 0);
      }
      json_skip_value(sc, first);
    }
  }

  template <class St>
  auto operator()(ParseCtx<St>& ctx, PipelineResult<void>) const
      -> PipelineResult<std::variant<Ts...>> {
    if (ctx.first_tok.kind != TokenKind::LBrace)
      return json_fail<std::variant<Ts...>>(ErrorCode::invalid_value, "expected {");
    auto obj_start = ctx.first_tok.pos;
    auto obj_end   = json_consume_nested(*ctx.scanner,
                                          TokenKind::LBrace, TokenKind::RBrace);
    std::string_view obj = ctx.input.substr(obj_start, obj_end - obj_start);

    std::optional<std::variant<Ts...>> result;

    auto try_one = [&]<class T>() -> bool {
      T candidate{};
      auto fields = cli::as_tuple(candidate);

      // Find discriminant by scanning field member types for Constant<>
      bool disc_ok = true;
      std::apply([&](auto&... f) {
        ([&] {
          using FT = std::remove_cvref_t<decltype(f)>;
          if constexpr (requires { FT::discriminant_key; FT::discriminant_value; }) {
            if (find_disc(obj, FT::discriminant_key) != FT::discriminant_value)
              disc_ok = false;
          }
        }(), ...);
      }, fields);
      if (!disc_ok) return false;

      JsonScanner sub(obj);
      auto r = detail::json_parse_object(sub, obj, fields);
      if (!r.have_value()) return false;
      result.emplace(std::in_place_type<T>, std::move(candidate));
      return true;
    };

    bool ok = (try_one.template operator()<Ts>() || ...);
    if (!ok)
      return json_fail<std::variant<Ts...>>(ErrorCode::invalid_value, "no variant match");
    return PipelineResult<std::variant<Ts...>>::ok(std::move(*result));
  }
};

// member<P> ───────────────────────────────────────────────────────────────────
// Always skips the current value, then applies P as a policy.
// P = pack::ignore       → silently skip (always ok)
// P = pack::reject_*     → skip + signal error (outer pack::ignore eats it)
template <auto P>
struct MemberFn {
  using p_type = std::remove_cvref_t<decltype(P)>;
  template <class I> using after_type   = void;
  template <class I> using storage_type = void;

  template <class St>
  SER20_ALWAYS_INLINE auto operator()(ParseCtx<St>& ctx, PipelineResult<void>) const
      -> PipelineResult<void> {
    const auto& f = ctx.first_tok;
    if (f.kind == TokenKind::Quote ||
        f.kind == TokenKind::LBrace ||
        f.kind == TokenKind::LBracket) {
      json_skip_value(*ctx.scanner, f);
    } else {
      ctx.next_is_pending = true;
      ctx.pending_tok = f;
    }
    return p_type::invoke(ctx, PipelineResult<void>::ok());
  }
};

template <class T>
inline constexpr auto integer    = JsonPipe<IntegerFn<T>{}>{};
template <class T>
inline constexpr auto floating   = JsonPipe<FloatingFn<T>{}>{};
inline constexpr auto boolean    = JsonPipe<BooleanFn{}>{};
inline constexpr auto string_view = JsonPipe<StringViewFn{}>{};
inline constexpr auto string     = JsonPipe<OwnedStringFn{}>{};
inline constexpr auto identity   = JsonPipe<IdentityFn{}>{};
template <auto F>
inline constexpr auto custom     = JsonPipe<CustomFn<F>{}>{};
template <class T>
inline constexpr auto parse_to   = JsonPipe<ParseToFn<T>{}>{};
template <auto Pipe>
inline constexpr auto vector     = JsonPipe<VectorFn<Pipe>{}>{};
template <class... Ts>
inline constexpr auto variant    = JsonPipe<VariantFn<Ts...>{}>{};
template <auto P>
inline constexpr auto member     = JsonPipe<MemberFn<P>{}>{};

}  // namespace decode

// ═══════════════════════════════════════════════════════════════════════════════
// ser20::pack::*  (JSON pack — names not in pipeline.hh)
// ═══════════════════════════════════════════════════════════════════════════════
namespace pack {

// assign ──────────────────────────────────────────────────────────────────────
struct JsonAssignFn {
  template <class I> using after_type   = void;
  template <class I> using storage_type = std::remove_cvref_t<I>;
  template <class T>
  SER20_ALWAYS_INLINE auto operator()(ParseCtx<std::remove_cvref_t<T>>& ctx, PipelineResult<T> in) const
      -> PipelineResult<void> {
    if (!in.have_value()) [[unlikely]] return PipelineResult<void>::fail(in.error);
    ctx.arg = std::move(in.value);
    return PipelineResult<void>::ok();
  }
};

// once ────────────────────────────────────────────────────────────────────────
struct JsonOnceFn {
  template <class I> using after_type   = void;
  template <class I> using storage_type = std::remove_cvref_t<I>;
  template <class T>
  SER20_ALWAYS_INLINE auto operator()(ParseCtx<std::remove_cvref_t<T>>& ctx, PipelineResult<T> in) const
      -> PipelineResult<void> {
    if (!in.have_value()) [[unlikely]] return PipelineResult<void>::fail(in.error);
    if (ctx.occurrences > 0) [[unlikely]]
      return PipelineResult<void>::fail(
          {.code = ErrorCode::duplicate_argument, .subject = std::string(ctx.key)});
    ctx.arg = std::move(in.value);
    return PipelineResult<void>::ok();
  }
};

// overwrite ───────────────────────────────────────────────────────────────────
struct JsonOverwriteFn {
  template <class I> using after_type   = void;
  template <class I> using storage_type = std::remove_cvref_t<I>;
  template <class T>
  SER20_ALWAYS_INLINE auto operator()(ParseCtx<std::remove_cvref_t<T>>& ctx, PipelineResult<T> in) const
      -> PipelineResult<void> {
    if (!in.have_value()) [[unlikely]] return PipelineResult<void>::fail(in.error);
    ctx.arg = std::move(in.value);
    return PipelineResult<void>::ok();
  }
};

// first_wins ──────────────────────────────────────────────────────────────────
struct JsonFirstWinsFn {
  template <class I> using after_type   = void;
  template <class I> using storage_type = std::remove_cvref_t<I>;
  template <class T>
  SER20_ALWAYS_INLINE auto operator()(ParseCtx<std::remove_cvref_t<T>>& ctx, PipelineResult<T> in) const
      -> PipelineResult<void> {
    if (!in.have_value()) [[unlikely]] return PipelineResult<void>::fail(in.error);
    if (ctx.occurrences == 0) [[likely]] ctx.arg = std::move(in.value);
    return PipelineResult<void>::ok();
  }
};

// ignore ──────────────────────────────────────────────────────────────────────
// Unconditionally succeeds — discards both value and any error from prior steps.
// Used as the terminal pack step for unknown-field handlers.
struct JsonIgnoreFn {
  template <class I> using after_type   = void;
  template <class I> using storage_type = std::monostate;
  template <class St, class T>
  SER20_ALWAYS_INLINE auto operator()(ParseCtx<St>&, PipelineResult<T>) const
      -> PipelineResult<void> {
    return PipelineResult<void>::ok();
  }
};

// reject_unknown_with_suggestion ──────────────────────────────────────────────
// Policy used inside decode::member<> — value already skipped by MemberFn.
struct JsonRejectUnknownWithSuggestionFn {
  template <class I> using after_type   = void;
  template <class I> using storage_type = std::monostate;
  template <class St>
  auto operator()(ParseCtx<St>& ctx, PipelineResult<void>) const
      -> PipelineResult<void> {
    return PipelineResult<void>::fail(
        {.code = ErrorCode::unknown_option, .subject = std::string(ctx.key)});
  }
};

inline constexpr auto assign                       = JsonPipe<JsonAssignFn{}>{};
inline constexpr auto once                         = JsonPipe<JsonOnceFn{}>{};
inline constexpr auto overwrite                    = JsonPipe<JsonOverwriteFn{}>{};
inline constexpr auto first_wins                   = JsonPipe<JsonFirstWinsFn{}>{};
inline constexpr auto ignore                       = JsonPipe<JsonIgnoreFn{}>{};
inline constexpr auto reject_unknown_with_suggestion =
    JsonPipe<JsonRejectUnknownWithSuggestionFn{}>{};

}  // namespace pack

// ═══════════════════════════════════════════════════════════════════════════════
// ser20::unknown::*
// ═══════════════════════════════════════════════════════════════════════════════
namespace unknown {

struct IgnoreUnknownFn {
  template <class I> using after_type   = void;
  template <class I> using storage_type = std::monostate;
  template <class St>
  auto operator()(ParseCtx<St>& ctx, PipelineResult<void>) const
      -> PipelineResult<void> {
    if (!ctx.next_is_pending) json_skip_value(*ctx.scanner, ctx.first_tok);
    return PipelineResult<void>::ok();
  }
};

struct RejectUnknownFn {
  template <class I> using after_type   = void;
  template <class I> using storage_type = std::monostate;
  template <class St>
  auto operator()(ParseCtx<St>& ctx, PipelineResult<void>) const
      -> PipelineResult<void> {
    if (!ctx.next_is_pending) json_skip_value(*ctx.scanner, ctx.first_tok);
    return PipelineResult<void>::fail(
        {.code = ErrorCode::unknown_option, .subject = std::string(ctx.key)});
  }
};

struct RejectWithSuggestionFn {
  template <class I> using after_type   = void;
  template <class I> using storage_type = std::monostate;
  template <class St>
  auto operator()(ParseCtx<St>& ctx, PipelineResult<void>) const
      -> PipelineResult<void> {
    if (!ctx.next_is_pending) json_skip_value(*ctx.scanner, ctx.first_tok);
    // TODO: levenshtein suggestion from known field names
    return PipelineResult<void>::fail(
        {.code = ErrorCode::unknown_option, .subject = std::string(ctx.key)});
  }
};

inline constexpr auto ignore                 = JsonPipe<IgnoreUnknownFn{}>{};
inline constexpr auto reject                 = JsonPipe<RejectUnknownFn{}>{};
inline constexpr auto reject_with_suggestion = JsonPipe<RejectWithSuggestionFn{}>{};

}  // namespace unknown

// ═══════════════════════════════════════════════════════════════════════════════
// ser20::ElemImpl<Name, Pipe>
// ═══════════════════════════════════════════════════════════════════════════════

template <StringLiteral Name, auto Pipe>
struct ElemImpl {
  using pipe_type    = std::remove_cvref_t<decltype(Pipe)>;
  using storage_type = json_storage_t<Pipe>;

  static constexpr std::string_view name{Name.data(), Name.size()};
  static constexpr bool is_unknown_handler = name.empty();

  storage_type value_{};
  std::size_t  occurrences_{0};

  // Implicit conversions for ergonomic field access (e.g. `c.r` → int).
  operator const storage_type&() const noexcept { return value_; }
  operator storage_type&() noexcept             { return value_; }

  // Called by the JSON parser when this field's key is matched.
  // Also called (for is_unknown_handler == true) on unmatched keys.
  SER20_ALWAYS_INLINE auto parse(ParseCtxBase& base) -> PipelineResult<void> {
    ParseCtx<storage_type> ctx{base, value_};
    ctx.occurrences = occurrences_;
    auto r = pipe_type::invoke(ctx, PipelineResult<void>::ok());
    base.next_is_pending = ctx.next_is_pending;
    base.pending_tok     = ctx.pending_tok;
    if (r.have_value()) [[likely]] ++occurrences_;
    return r;
  }
};

// ═══════════════════════════════════════════════════════════════════════════════
// json_parse_object — core recursive object parser
// ═══════════════════════════════════════════════════════════════════════════════

namespace detail {

template <class Tuple>
auto json_parse_object_body(JsonScanner& sc, std::string_view input, Tuple& fields)
    -> PipelineResult<void> {
  // '{' already consumed by caller
  while (true) {
    auto tok = sc.consume_next_token();
    if (tok.is_end()) [[unlikely]] break;
    if (tok.kind == TokenKind::RBrace) [[unlikely]] return PipelineResult<void>::ok();
    if (tok.kind == TokenKind::Comma)  [[likely]]   continue;
    if (tok.kind != TokenKind::Quote)  [[unlikely]]  continue;  // unexpected, skip

    auto key_close = sc.consume_next_token();
    if (key_close.is_end()) [[unlikely]] break;
    auto kstart = tok.pos + 1;
    std::string_view key = input.substr(kstart,
        key_close.pos > kstart ? key_close.pos - kstart : 0);

    auto colon_tok = sc.consume_next_token();
    if (colon_tok.is_end() || colon_tok.kind != TokenKind::Colon) [[unlikely]] break;

    auto first_tok = sc.consume_next_token();
    if (first_tok.is_end()) [[unlikely]] break;

    ParseCtxBase base{
        .scanner         = &sc,
        .input           = input,
        .first_tok       = first_tok,
        .colon_pos       = colon_tok.pos,
        .key             = key,
        .occurrences     = 0,
        .next_is_pending = false,
    };

    bool matched = Matcher::find(key, fields, [&](auto& field) {
      field.parse(base);
    });

    if (!matched) {
      // Invoke the catch-all field (empty name) if present; else skip.
      bool handled = false;
      std::apply(
          [&](auto&... f) {
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
    if (base.next_is_pending &&
        base.pending_tok.kind == TokenKind::RBrace)
      return PipelineResult<void>::ok();
  }
  return PipelineResult<void>::ok();
}

template <class Tuple>
auto json_parse_object(JsonScanner& sc, std::string_view input, Tuple& fields)
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
      json_consume_nested(*base.scanner, TokenKind::LBracket, TokenKind::RBracket);
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
  JsonScanner sc(text);
  auto fields = cli::as_tuple(result);
  detail::json_parse_object(sc, text, fields);
  return result;
}

}  // namespace ser20

// ── _json_string UDL ─────────────────────────────────────────────────────────
template <ser20::StringLiteral S>
constexpr auto operator""_json_string() {
  return ser20::JsonString<S>{};
}
