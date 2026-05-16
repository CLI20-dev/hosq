#pragma once

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <new>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "ser20/error.hh"
#include "ser20/parse_context.hh"
#include "ser20/to_tuple.hh"

namespace ser20 {

/**
 * @brief The result type produced and consumed at each step of an action
 * pipeline.
 *
 * An `PipelineResult<T>` is a 3-state type: ok (carrying a value of type `T`),
 * error (carrying a `ParseError`), or ignore (silent skip). The
 * `operator bool()`, `have_value()`, `has_value()`, `has_error()`, and
 * `is_ignored()` methods make it easy to check the state inline.
 *
 * Factory methods:
 * - `PipelineResult<T>::ok(value)` — create a success result.
 * - `PipelineResult<T>::fail(error)` — create a failure result.
 * - `PipelineResult<T>::ignore()` — create an ignore result.
 *
 * @tparam T The value type held on success.
 */
template <class T>
struct PipelineResult {
  using value_type = T;

  enum class State : std::uint8_t { ok, error, ignore };

  union Storage {
    char dummy;
    T value;
    Storage() : dummy{} {}
    ~Storage() {}
  };

  State state{State::ignore};
  Storage storage_{};
  std::unique_ptr<ParseError> error_{};

  PipelineResult() = default;
  PipelineResult(const PipelineResult& other) : state(other.state) {
    if (other.have_value()) new (&storage_.value) T(other.storage_.value);
    if (other.error_) error_ = std::make_unique<ParseError>(*other.error_);
  }
  PipelineResult(PipelineResult&& other) noexcept : state(other.state) {
    if (other.have_value())
      new (&storage_.value) T(std::move(other.storage_.value));
    if (other.error_) error_ = std::move(other.error_);
  }
  ~PipelineResult() {
    if (have_value()) storage_.value.~T();
  }
  auto operator=(const PipelineResult& other) -> PipelineResult& {
    if (this == &other) return *this;
    this->~PipelineResult();
    new (this) PipelineResult(other);
    return *this;
  }
  auto operator=(PipelineResult&& other) noexcept -> PipelineResult& {
    if (this == &other) return *this;
    this->~PipelineResult();
    new (this) PipelineResult(std::move(other));
    return *this;
  }

  /** @brief Returns `true` when the result represents success. */
  [[nodiscard]] constexpr explicit operator bool() const noexcept {
    return state == State::ok;
  }

  /** @brief Returns `true` when the result represents success. */
  [[nodiscard]] constexpr auto have_value() const noexcept -> bool {
    return state == State::ok;
  }

  /** @brief Returns `true` when the result represents success. */
  [[nodiscard]] constexpr auto has_value() const noexcept -> bool {
    return have_value();
  }

  /** @brief Returns `true` when the result represents failure. */
  [[nodiscard]] constexpr auto has_error() const noexcept -> bool {
    return state == State::error;
  }

  /** @brief Returns `true` when the result represents an ignored step. */
  [[nodiscard]] constexpr auto is_ignored() const noexcept -> bool {
    return state == State::ignore;
  }

  [[nodiscard]] auto value() & -> T& { return storage_.value; }
  [[nodiscard]] auto value() const& -> const T& { return storage_.value; }
  [[nodiscard]] auto value() && -> T&& { return std::move(storage_.value); }
  [[nodiscard]] auto error() const -> const ParseError& { return *error_; }

  /**
   * @brief Creates a success result holding `value`.
   *
   * @tparam U Type of the value (deduced).
   * @param v The value to store.
   * @return A successful `PipelineResult<T>`.
   */
  template <class U>
  [[nodiscard]] static auto ok(U&& v) -> PipelineResult {
    PipelineResult result;
    result.state = State::ok;
    new (&result.storage_.value) T(std::forward<U>(v));
    return result;
  }

  /**
   * @brief Creates a failure result carrying `error`.
   *
   * @param e The error to store.
   * @return A failed `PipelineResult<T>`.
   */
  [[nodiscard]] static auto fail(ParseError e) -> PipelineResult {
    PipelineResult result;
    result.state = State::error;
    result.error_ = std::make_unique<ParseError>(std::move(e));
    return result;
  }

  /**
   * @brief Creates an ignore result.
   *
   * @return An ignored `PipelineResult<T>`.
   */
  [[nodiscard]] static auto ignore() -> PipelineResult {
    PipelineResult result;
    result.state = State::ignore;
    return result;
  }
};

/**
 * @brief Specialisation of `PipelineResult` for terminal (pack) actions that
 * produce no value.
 *
 * Pack actions such as `SetTrue` or `Push` write their result directly into the
 * argument storage referenced by `PipelineCtx::arg`; they do not pass a value
 * downstream, so `PipelineResult<void>` carries only an error, success, or
 * ignore state.
 */
template <>
struct PipelineResult<void> {
  using value_type = void;

  enum class State : std::uint8_t { ok, error, ignore };

  State state{State::ok};
  std::unique_ptr<ParseError> error_{};

  PipelineResult() = default;
  PipelineResult(const PipelineResult& other) : state(other.state) {
    if (other.error_) error_ = std::make_unique<ParseError>(*other.error_);
  }
  PipelineResult(PipelineResult&&) noexcept = default;
  auto operator=(const PipelineResult& other) -> PipelineResult& {
    if (this == &other) return *this;
    state = other.state;
    error_ =
        other.error_ ? std::make_unique<ParseError>(*other.error_) : nullptr;
    return *this;
  }
  auto operator=(PipelineResult&&) noexcept -> PipelineResult& = default;

  /** @brief Returns `true` when the result represents success. */
  [[nodiscard]] constexpr explicit operator bool() const noexcept {
    return state == State::ok;
  }

  /** @brief Returns `true` when the result represents success. */
  [[nodiscard]] constexpr auto have_value() const noexcept -> bool {
    return state == State::ok;
  }

  /** @brief Returns `true` when the result represents success. */
  [[nodiscard]] constexpr auto has_value() const noexcept -> bool {
    return have_value();
  }

  /** @brief Returns `true` when the result represents failure. */
  [[nodiscard]] constexpr auto has_error() const noexcept -> bool {
    return state == State::error;
  }

  /** @brief Returns `true` when the result represents an ignored step. */
  [[nodiscard]] constexpr auto is_ignored() const noexcept -> bool {
    return state == State::ignore;
  }

  [[nodiscard]] auto error() const -> const ParseError& { return *error_; }

  /**
   * @brief Creates a success result.
   *
   * @return A successful `PipelineResult<void>`.
   */
  [[nodiscard]] static auto ok() -> PipelineResult<void> {
    PipelineResult result;
    result.state = State::ok;
    return result;
  }

  /**
   * @brief Creates a failure result carrying `error`.
   *
   * @param e The error to store.
   * @return A failed `PipelineResult<void>`.
   */
  [[nodiscard]] static auto fail(ParseError e) -> PipelineResult<void> {
    PipelineResult result;
    result.state = State::error;
    result.error_ = std::make_unique<ParseError>(std::move(e));
    return result;
  }

  /**
   * @brief Creates an ignore result.
   *
   * @return An ignored `PipelineResult<void>`.
   */
  [[nodiscard]] static auto ignore() -> PipelineResult<void> {
    PipelineResult result;
    result.state = State::ignore;
    return result;
  }
};

/**
 * @brief Helper functions for constructing `PipelineResult` objects with type
 * deduction.
 *
 * @tparam T The value type of the `PipelineResult` to construct.
 * @param value The value to store in a success result.
 * @return A successful `PipelineResult<T>` holding `value`.
 */
template <class T>
[[nodiscard]] auto ok(T&& value)
    -> PipelineResult<std::remove_cvref_t<T>> {
  return PipelineResult<std::remove_cvref_t<T>>::ok(std::forward<T>(value));
}

/**
 * @brief Helper function for constructing a successful `PipelineResult<void>`.
 *
 * @return A successful `PipelineResult<void>`.
 */
[[nodiscard]] auto ok() -> PipelineResult<void> {
  return PipelineResult<void>::ok();
}

/**
 * @brief Helper function for constructing a failure `PipelineResult` with type
 * deduction.
 *
 * @tparam T The value type of the `PipelineResult` to construct.
 * @param error The error to store in the failure result.
 * @return A failed `PipelineResult<T>` carrying `error`.
 */
template <class T>
[[nodiscard]] auto fail(ParseError error) -> PipelineResult<T> {
  return PipelineResult<T>::fail(std::move(error));
}

/**
 * @brief Helper function for constructing an ignore `PipelineResult` with type
 * deduction.
 *
 * @tparam T The value type of the `PipelineResult` to construct.
 * @return An ignored `PipelineResult<T>`.
 */
template <class T>
[[nodiscard]] auto ignore() -> PipelineResult<T> {
  return PipelineResult<T>::ignore();
}

/**
 * @brief Context passed to each action function during pipeline execution.
 *
 * Provides access to the current `argv` position, how many times the parent
 * option has been seen, how many values are in this occurrence, the 0-based
 * index of the current value, the total values across all occurrences, and
 * a reference to the argument's storage value.
 *
 * @tparam T The storage type of the argument; `void` for intermediate
 * (non-terminal) actions.
 */
template <class T = void>
struct PipelineCtx {
  size_t index{};  ///< Zero-based index from the string
  size_t
      occurrences{};  ///< Number of times the parent option token has appeared.
  std::string_view
      key{};  ///< The option token's text (empty for positional arguments).
  std::reference_wrapper<T>
      arg{};  ///< Reference to the argument's storage value.
};

/**
 * @brief Partial specialisation of `PipelineCtx` used for intermediate pipeline
 * steps.
 *
 * Conversion and validation actions do not have access to the final storage;
 * they receive `PipelineCtx<void>` which carries only the positional counters.
 * This specialisation also provides a converting constructor from
 * `PipelineCtx<T>` so that the parser can downcast when invoking non-terminal
 * actions.
 */
template <>
struct PipelineCtx<void> {
  size_t index{};  ///< Zero-based index from the string
  size_t
      occurrences{};  ///< Number of times the parent option token has appeared.
  std::string_view
      key{};  ///< The option token's text (empty for positional arguments).
  /**
   * @brief Constructs from a typed `PipelineCtx<T>`, copying the counters.
   *
   * @tparam T The storage type of the source context.
   * @param other The source context to copy counters from.
   */
  template <class T>
  PipelineCtx(const PipelineCtx<T>& other)
      : index(other.index), occurrences(other.occurrences) {}

  PipelineCtx() = default;
};

/**
 * @brief A compile-time pipeline of action functions applied sequentially to a
 * parsed token.
 *
 * Each element of `Fns` must be a constexpr-constructible callable that:
 * - Exposes `template<class I> static constexpr bool accepts_input` — whether it
 *   can handle input of type `I`.
 * - Exposes `template<class I> using after_type` — the output type when given
 * input `I`.
 * - Exposes `template<class I> using storage_type` — the argument storage type
 *   (non-void only for the last/terminal action in the pipeline).
 *
 * Pipelines are composed with `operator|`:
 * @code
 *   constexpr auto my_action = ser20::conversion::integer<int>
 *                            | ser20::validation::positive
 *                            | ser20::pack::set_once;
 * @endcode
 *
 * At pipeline invocation, `ignore` short-circuits the remaining steps while
 * `error` is passed through so downstream steps can preserve or react to it.
 *
 * @tparam Fns Pack of constexpr action objects forming the pipeline.
 */
template <auto... Fns>
  requires(requires {
    {
      std::remove_cvref_t<decltype(Fns)>::template accepts_input<void>
    } -> std::convertible_to<bool>;
    typename std::remove_cvref_t<decltype(Fns)>::template storage_type<void>;
  } && ...)
struct Pipeline {
  template <class Arg, class Result>
  [[nodiscard]]
  static constexpr auto invoke(PipelineCtx<Arg>& ctx,
                               PipelineResult<Result> input) {
    return []<auto FnHead, auto... FnTail>(
               PipelineCtx<Arg>& ctx,
               PipelineResult<Result> input) -> decltype(auto) {
      if (input.is_ignored()) {
        using FinalIdentity = std::remove_cvref_t<decltype(std::get<2>(
            Pipeline::template validate_impl<false, Result, FnHead,
                                             FnTail...>()))>;
        return PipelineResult<typename FinalIdentity::type>::ignore();
      }

      auto next = FnHead(ctx, std::move(input));
      if constexpr (sizeof...(FnTail) == 0) {
        return next;
      } else {
        return Pipeline<FnTail...>::invoke(ctx, std::move(next));
      }
    }.template operator()<Fns...>(ctx, std::move(input));
  }

  template <class Arg, class Result>
  [[nodiscard]]
  static constexpr auto invoke(const PipelineCtx<Arg>& ctx,
                               PipelineResult<Result> input) {
    return []<auto FnHead, auto... FnTail>(
               const PipelineCtx<Arg>& ctx,
               PipelineResult<Result> input) -> decltype(auto) {
      if (input.is_ignored()) {
        using FinalIdentity = std::remove_cvref_t<decltype(std::get<2>(
            Pipeline::template validate_impl<false, Result, FnHead,
                                             FnTail...>()))>;
        return PipelineResult<typename FinalIdentity::type>::ignore();
      }

      auto next = FnHead(ctx, std::move(input));
      if constexpr (sizeof...(FnTail) == 0) {
        return next;
      } else {
        return Pipeline<FnTail...>::invoke(ctx, std::move(next));
      }
    }.template operator()<Fns...>(ctx, std::move(input));
  }

  template <bool EnableTailCheck, class Prev, auto FnHead, auto... FnTail>
  static constexpr auto validate_impl() {
    static_assert(
        !EnableTailCheck ||
            (sizeof...(FnTail) != 0 ||
             !std::same_as<typename std::remove_cvref_t<
                               decltype(FnHead)>::template storage_type<Prev>,
                           void>),
        "The last action must have a non-void storage type");

    using Head = std::remove_cvref_t<decltype(FnHead)>;
    if constexpr (sizeof...(FnTail) == 0) {
      return std::make_tuple(
          Head::template accepts_input<Prev>,
          std::type_identity<typename Head::template storage_type<Prev>>{},
          std::type_identity<typename Head::template after_type<Prev>>{});
    } else {
      if constexpr (!Head::template accepts_input<Prev>) {
        static_assert(
            Head::template accepts_input<Prev>,
            "Pipeline step does not accept the previous step's output");
        return std::make_tuple(false, std::type_identity<void>{},
                               std::type_identity<void>{});
      } else {
        using Next = typename Head::template after_type<Prev>;
        return validate_impl<EnableTailCheck, Next, FnTail...>();
      }
    }
  }

 private:
  template <auto First, auto...>
  struct FirstFnType {
    using type = std::remove_cvref_t<decltype(First)>;
  };

 public:
  static constexpr bool entry_is_optional = FirstFnType<
      Fns...>::type::template accepts_input<std::optional<std::string_view>>;

  static constexpr auto validate() -> auto {
    using FirstFn = typename FirstFnType<Fns...>::type;
    if constexpr (FirstFn::template accepts_input<
                      std::optional<std::string_view>>) {
      return validate_impl<true, std::optional<std::string_view>, Fns...>();
    } else {
      return validate_impl<true, std::string_view, Fns...>();
    }
  }
};

template <>
struct Pipeline<> {
  static constexpr bool entry_is_optional = false;

  template <class Arg, class Result>
  [[nodiscard]]
  static constexpr auto invoke(PipelineCtx<Arg>&, PipelineResult<Result> input) {
    return input;
  }

  static constexpr auto validate() -> auto {
    return std::make_tuple(true, std::type_identity<std::monostate>{},
                           std::type_identity<std::monostate>{});
  }
};

template <auto Fn, auto... Fns>
constexpr auto operator|(Pipeline<Fns...>, Pipeline<Fn>) {
  return Pipeline<Fns..., Fn>{};
}

template <auto... Fns>
struct ParsePipeline;

template <>
struct ParsePipeline<> {
  template <class Ctx, class In>
  static constexpr auto invoke(Ctx&, PipelineResult<In> value) {
    return value;
  }
};

template <auto FnHead, auto... FnTail>
struct ParsePipeline<FnHead, FnTail...> {
  template <class Ctx, class In>
  static constexpr auto invoke(Ctx& ctx, PipelineResult<In> value)
      -> decltype(auto) {
    auto next = FnHead(ctx, std::move(value));
    if constexpr (sizeof...(FnTail) == 0) {
      return next;
    } else {
      return ParsePipeline<FnTail...>::invoke(ctx, std::move(next));
    }
  }
};

template <auto Fn, auto... Fns>
constexpr auto operator|(ParsePipeline<Fns...>, ParsePipeline<Fn>) {
  return ParsePipeline<Fns..., Fn>{};
}

template <class Entry, auto FnHead, auto... FnTail>
struct ParseStorageOf {
  using After =
      typename std::remove_cvref_t<decltype(FnHead)>::template after_type<Entry>;
  using type = typename ParseStorageOf<After, FnTail...>::type;
};

template <class Entry, auto FnLast>
struct ParseStorageOf<Entry, FnLast> {
  using type = typename std::remove_cvref_t<
      decltype(FnLast)>::template storage_type<Entry>;
};

template <class PipelineType>
struct ParsePipelineStorage;

template <auto... Fns>
struct ParsePipelineStorage<ParsePipeline<Fns...>> {
  using type = typename ParseStorageOf<void, Fns...>::type;
};

template <auto Pipe>
using parse_storage_t = typename ParsePipelineStorage<
    std::remove_cvref_t<decltype(Pipe)>>::type;

// ── whitespace utilities ──────────────────────────────────────────────────────
SER20_ALWAYS_INLINE auto json_skip_ws(std::string_view s, std::size_t p) noexcept
    -> std::size_t {
  while (p < s.size() && (s[p] == ' ' || s[p] == '\t' || s[p] == '\r' ||
                          s[p] == '\n')) [[unlikely]]
    ++p;
  return p;
}
SER20_ALWAYS_INLINE auto json_rtrim(std::string_view s, std::size_t e) noexcept
    -> std::size_t {
  while (e > 0 && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r' ||
                   s[e - 1] == '\n')) [[unlikely]]
    --e;
  return e;
}

// ── error helpers ─────────────────────────────────────────────────────────────
template <class T>
SER20_ALWAYS_INLINE auto json_fail(ErrorCode code, std::string_view subject = {})
    -> PipelineResult<T> {
  return PipelineResult<T>::fail(
      {.code = code, .subject = std::string(subject)});
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
  template <class I>
  using after_type = std::string_view;
  template <class I>
  using storage_type = void;
  template <class St>
  SER20_ALWAYS_INLINE auto operator()(ParseCtx<St>& ctx,
                                      PipelineResult<void>) const
      -> PipelineResult<std::string_view> {
    const auto& f = ctx.first_tok;
    if (f.kind == TokenKind::Quote || f.kind == TokenKind::LBrace ||
        f.kind == TokenKind::LBracket) [[unlikely]]
      return json_fail<std::string_view>(ErrorCode::invalid_value,
                                         "expected scalar");
    ctx.next_is_pending = true;
    ctx.pending_tok = f;
    auto s = json_skip_ws(ctx.input, ctx.colon_pos + 1);
    auto e = json_rtrim(ctx.input, f.pos);
    if (s >= e) [[unlikely]]
      return json_fail<std::string_view>(ErrorCode::invalid_value,
                                         "empty scalar");
    return PipelineResult<std::string_view>::ok(ctx.input.substr(s, e - s));
  }
};

// string ──────────────────────────────────────────────────────────────────────
// first_tok must be Quote (opening).  Consumes the closing Quote.
// Returns the content between the quotes (no escape resolution).
struct StringFn {
  template <class I>
  using after_type = std::string_view;
  template <class I>
  using storage_type = void;
  template <class St>
  SER20_ALWAYS_INLINE auto operator()(ParseCtx<St>& ctx,
                                      PipelineResult<void>) const
      -> PipelineResult<std::string_view> {
    if (ctx.first_tok.kind != TokenKind::Quote) [[unlikely]]
      return json_fail<std::string_view>(ErrorCode::invalid_value,
                                         "expected string");
    auto close = ctx.scanner->consume_next_token();
    if (close.is_end()) [[unlikely]]
      return json_fail<std::string_view>(ErrorCode::invalid_value,
                                         "unterminated string");
    auto s = ctx.first_tok.pos + 1;
    return PipelineResult<std::string_view>::ok(
        ctx.input.substr(s, close.pos > s ? close.pos - s : 0));
  }
};

// object_raw / array_raw ──────────────────────────────────────────────────────
struct ObjectRawFn {
  template <class I>
  using after_type = std::string_view;
  template <class I>
  using storage_type = void;
  template <class St>
  auto operator()(ParseCtx<St>& ctx, PipelineResult<void>) const
      -> PipelineResult<std::string_view> {
    if (ctx.first_tok.kind != TokenKind::LBrace)
      return json_fail<std::string_view>(ErrorCode::invalid_value, "expected {");
    auto start = ctx.first_tok.pos;
    auto end =
        json_consume_nested(*ctx.scanner, TokenKind::LBrace, TokenKind::RBrace);
    return PipelineResult<std::string_view>::ok(
        ctx.input.substr(start, end - start));
  }
};

struct ArrayRawFn {
  template <class I>
  using after_type = std::string_view;
  template <class I>
  using storage_type = void;
  template <class St>
  auto operator()(ParseCtx<St>& ctx, PipelineResult<void>) const
      -> PipelineResult<std::string_view> {
    if (ctx.first_tok.kind != TokenKind::LBracket)
      return json_fail<std::string_view>(ErrorCode::invalid_value, "expected [");
    auto start = ctx.first_tok.pos;
    auto end = json_consume_nested(*ctx.scanner, TokenKind::LBracket,
                                   TokenKind::RBracket);
    return PipelineResult<std::string_view>::ok(
        ctx.input.substr(start, end - start));
  }
};

inline constexpr auto scalar = ParsePipeline<ScalarFn{}>{};
inline constexpr auto string = ParsePipeline<StringFn{}>{};
inline constexpr auto object_raw = ParsePipeline<ObjectRawFn{}>{};
inline constexpr auto array_raw = ParsePipeline<ArrayRawFn{}>{};

}  // namespace scan

// ═══════════════════════════════════════════════════════════════════════════════
// Forward declarations needed for mutual recursion
// ═══════════════════════════════════════════════════════════════════════════════

namespace detail {
// json_parse_object_body: parse key-value pairs with sc positioned AFTER '{'.
template <class Tuple>
auto json_parse_object_body(ParserScanner& sc, std::string_view input,
                            Tuple& fields) -> PipelineResult<void>;

// json_parse_object: consume '{' then delegate to _body.
template <class Tuple>
auto json_parse_object(ParserScanner& sc, std::string_view input, Tuple& fields)
    -> PipelineResult<void>;
}  // namespace detail

// ═══════════════════════════════════════════════════════════════════════════════
// ser20::decode::*
// ═══════════════════════════════════════════════════════════════════════════════
namespace decode {

template <class T>
struct ParsedInPlace {};

// integer<T> ──────────────────────────────────────────────────────────────────
template <class T>
struct IntegerFn {
  template <class I>
  using after_type = T;
  template <class I>
  using storage_type = void;
  template <class St>
  SER20_ALWAYS_INLINE auto operator()(ParseCtx<St>&,
                                      PipelineResult<std::string_view> in) const
      -> PipelineResult<T> {
    if (!in.have_value()) [[unlikely]]
      return PipelineResult<T>::fail(in.error());
    T r{};
    auto [p, ec] =
        std::from_chars(in.value().data(), in.value().data() + in.value().size(), r);
    if (ec != std::errc{}) [[unlikely]]
      return json_fail<T>(ErrorCode::invalid_value, in.value());
    return PipelineResult<T>::ok(r);
  }
};

// floating<T> ─────────────────────────────────────────────────────────────────
template <class T>
struct FloatingFn {
  template <class I>
  using after_type = T;
  template <class I>
  using storage_type = void;
  template <class St>
  SER20_ALWAYS_INLINE auto operator()(ParseCtx<St>&,
                                      PipelineResult<std::string_view> in) const
      -> PipelineResult<T> {
    if (!in.have_value()) [[unlikely]]
      return PipelineResult<T>::fail(in.error());
    T r{};
    auto [p, ec] =
        std::from_chars(in.value().data(), in.value().data() + in.value().size(), r);
    if (ec != std::errc{}) [[unlikely]]
      return json_fail<T>(ErrorCode::invalid_value, in.value());
    return PipelineResult<T>::ok(r);
  }
};

// boolean ─────────────────────────────────────────────────────────────────────
struct BooleanFn {
  template <class I>
  using after_type = bool;
  template <class I>
  using storage_type = void;
  template <class St>
  SER20_ALWAYS_INLINE auto operator()(ParseCtx<St>&,
                                      PipelineResult<std::string_view> in) const
      -> PipelineResult<bool> {
    if (!in.have_value()) [[unlikely]]
      return PipelineResult<bool>::fail(in.error());
    if (in.value() == "true") [[likely]]
      return PipelineResult<bool>::ok(true);
    if (in.value() == "false") return PipelineResult<bool>::ok(false);
    return json_fail<bool>(ErrorCode::invalid_value, in.value());
  }
};

// string_view ─────────────────────────────────────────────────────────────────
struct StringViewFn {
  template <class I>
  using after_type = std::string_view;
  template <class I>
  using storage_type = void;
  template <class St>
  SER20_ALWAYS_INLINE auto operator()(ParseCtx<St>&,
                                      PipelineResult<std::string_view> in) const
      -> PipelineResult<std::string_view> {
    return in;
  }
};

// string (owned, escape-resolved TODO) ────────────────────────────────────────
struct OwnedStringFn {
  template <class I>
  using after_type = std::string;
  template <class I>
  using storage_type = void;
  template <class St>
  SER20_ALWAYS_INLINE auto operator()(ParseCtx<St>&,
                                      PipelineResult<std::string_view> in) const
      -> PipelineResult<std::string> {
    if (!in.have_value()) [[unlikely]]
      return PipelineResult<std::string>::fail(in.error());
    return PipelineResult<std::string>::ok(std::string(in.value()));
  }
};

// identity ────────────────────────────────────────────────────────────────────
struct IdentityFn {
  template <class I>
  using after_type = I;
  template <class I>
  using storage_type = void;
  template <class St, class In>
  auto operator()(ParseCtx<St>&, PipelineResult<In> in) const
      -> PipelineResult<In> {
    return in;
  }
};

// custom<F> ───────────────────────────────────────────────────────────────────
template <auto F>
struct CustomFn {
  template <class I>
  using after_type = std::invoke_result_t<decltype(F), const I&>;
  template <class I>
  using storage_type = void;
  template <class St, class In>
  SER20_ALWAYS_INLINE auto operator()(ParseCtx<St>&, PipelineResult<In> in) const
      -> PipelineResult<std::invoke_result_t<decltype(F), const In&>> {
    using Out = std::invoke_result_t<decltype(F), const In&>;
    if (!in.have_value()) [[unlikely]]
      return PipelineResult<Out>::fail(in.error());
    return PipelineResult<Out>::ok(F(in.value()));
  }
};

// parse_to<T> ─────────────────────────────────────────────────────────────────
// first_tok must be LBrace.  Sub-scans the object into T.
template <class T>
struct ParseToFn {
  template <class I>
  using after_type = T;
  template <class I>
  using storage_type = void;
  template <class St>
  SER20_ALWAYS_INLINE auto operator()(ParseCtx<St>& ctx,
                                      PipelineResult<void>) const {
    if (ctx.first_tok.kind != TokenKind::LBrace) [[unlikely]] {
      if constexpr (std::same_as<std::remove_cvref_t<St>, T>) {
        return json_fail<ParsedInPlace<T>>(ErrorCode::invalid_value,
                                           "expected {");
      } else {
        return json_fail<T>(ErrorCode::invalid_value, "expected {");
      }
    }
    if constexpr (std::same_as<std::remove_cvref_t<St>, T>) {
      auto fields = cli::as_tuple(ctx.arg.get());
      auto r = detail::json_parse_object_body(*ctx.scanner, ctx.input, fields);
      if (!r.have_value()) [[unlikely]]
        return PipelineResult<ParsedInPlace<T>>::fail(r.error());
      return PipelineResult<ParsedInPlace<T>>::ok(ParsedInPlace<T>{});
    } else {
      T result{};
      auto fields = cli::as_tuple(result);
      auto r = detail::json_parse_object_body(*ctx.scanner, ctx.input, fields);
      if (!r.have_value()) [[unlikely]]
        return PipelineResult<T>::fail(r.error());
      return PipelineResult<T>::ok(std::move(result));
    }
  }
};

// vector<Pipe> ────────────────────────────────────────────────────────────────
// first_tok must be LBracket.  Iterates elements, applying Pipe to each.
template <auto Pipe>
struct VectorFn {
  using elem_t = parse_storage_t<Pipe>;
  using pipe_t = std::remove_cvref_t<decltype(Pipe)>;
  template <class I>
  using after_type = std::vector<elem_t>;
  template <class I>
  using storage_type = void;

  template <class St>
  SER20_ALWAYS_INLINE auto operator()(ParseCtx<St>& ctx,
                                      PipelineResult<void>) const
      -> PipelineResult<std::vector<elem_t>> {
    if (ctx.first_tok.kind != TokenKind::LBracket) [[unlikely]]
      return json_fail<std::vector<elem_t>>(ErrorCode::invalid_value,
                                            "expected [");
    ParserScanner& sc = *ctx.scanner;
    std::vector<elem_t> out;

    // last_pos: start of the current potential scalar element (after '[' or
    // ','). just_saw_structured: true after consuming a Quote/LBrace/LBracket
    // element, so we skip the scalar check when the next ',' or ']' arrives.
    std::size_t last_pos = ctx.first_tok.pos + 1;
    bool just_saw_structured = false;

    auto process_scalar = [&](ScannerToken delim) {
      auto s = json_skip_ws(ctx.input, last_pos);
      auto e = json_rtrim(ctx.input, delim.pos);
      if (s >= e) return;
      elem_t val{};
      // colon_pos is set to last_pos-1 so colon_pos+1 == last_pos (ScalarFn
      // start)
      ParseCtxBase base{
          .scanner = &sc,
          .input = ctx.input,
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
          .scanner = &sc,
          .input = ctx.input,
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

// array<N, Pipe> ──────────────────────────────────────────────────────────────
// Like VectorFn but fills a fixed std::array<elem_t, N> — no heap allocation.
// Elements beyond N are skipped. Scalar arrays are not supported.
template <std::size_t N, auto Pipe>
struct ArrayFn {
  using elem_t = parse_storage_t<Pipe>;
  using pipe_t = std::remove_cvref_t<decltype(Pipe)>;
  template <class I>
  using after_type = std::array<elem_t, N>;
  template <class I>
  using storage_type = void;

  template <class St>
  auto operator()(ParseCtx<St>& ctx, PipelineResult<void>) const
      -> PipelineResult<std::array<elem_t, N>> {
    if (ctx.first_tok.kind != TokenKind::LBracket) [[unlikely]]
      return PipelineResult<std::array<elem_t, N>>::fail(
          {.code = ErrorCode::invalid_value, .subject = "expected ["});
    ParserScanner& sc = *ctx.scanner;
    std::array<elem_t, N> out{};
    std::size_t n = 0;

    while (true) {
      auto tok = sc.consume_next_token();
      if (tok.is_end()) [[unlikely]]
        break;
      if (tok.kind == TokenKind::RBracket) [[unlikely]]
        break;
      if (tok.kind == TokenKind::Comma) [[likely]]
        continue;

      if (n < N) [[likely]] {
        ParseCtxBase base{.scanner = &sc,
                          .input = ctx.input,
                          .first_tok = tok,
                          .colon_pos = 0};
        ParseCtx<elem_t> ectx{base, out[n]};
        auto r = pipe_t::invoke(ectx, PipelineResult<void>::ok());
        if (r.have_value()) [[likely]]
          ++n;
      } else {
        json_skip_value(sc, tok);
      }
    }

    return PipelineResult<std::array<elem_t, N>>::ok(std::move(out));
  }
};

// variant<Ts...> ──────────────────────────────────────────────────────────────
// first_tok must be LBrace.  Tries each T in order; picks the one whose
// Constant<> discriminant matches.
template <class... Ts>
struct VariantFn {
  template <class I>
  using after_type = std::variant<Ts...>;
  template <class I>
  using storage_type = void;

  // Scan a top-level string value for the given key, without allocating.
  static auto find_disc(std::string_view obj, std::string_view key)
      -> std::string_view {
    ParserScanner sc(obj);
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
      return json_fail<std::variant<Ts...>>(ErrorCode::invalid_value,
                                            "expected {");
    auto obj_start = ctx.first_tok.pos;
    auto obj_end =
        json_consume_nested(*ctx.scanner, TokenKind::LBrace, TokenKind::RBrace);
    std::string_view obj = ctx.input.substr(obj_start, obj_end - obj_start);

    std::optional<std::variant<Ts...>> result;

    auto try_one = [&]<class T>() -> bool {
      T candidate{};
      auto fields = cli::as_tuple(candidate);

      // Find discriminant by scanning field member types for Constant<>
      bool disc_ok = true;
      std::apply(
          [&](auto&... f) {
            (
                [&] {
                  using FT = std::remove_cvref_t<decltype(f)>;
                  if constexpr (requires {
                                  FT::discriminant_key;
                                  FT::discriminant_value;
                                }) {
                    if (find_disc(obj, FT::discriminant_key) !=
                        FT::discriminant_value)
                      disc_ok = false;
                  }
                }(),
                ...);
          },
          fields);
      if (!disc_ok) return false;

      ParserScanner sub(obj);
      auto r = detail::json_parse_object(sub, obj, fields);
      if (!r.have_value()) return false;
      result.emplace(std::in_place_type<T>, std::move(candidate));
      return true;
    };

    bool ok = (try_one.template operator()<Ts>() || ...);
    if (!ok)
      return json_fail<std::variant<Ts...>>(ErrorCode::invalid_value,
                                            "no variant match");
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
  template <class I>
  using after_type = void;
  template <class I>
  using storage_type = void;

  template <class St>
  SER20_ALWAYS_INLINE auto operator()(ParseCtx<St>& ctx,
                                      PipelineResult<void>) const
      -> PipelineResult<void> {
    const auto& f = ctx.first_tok;
    if (f.kind == TokenKind::Quote || f.kind == TokenKind::LBrace ||
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
inline constexpr auto integer = ParsePipeline<IntegerFn<T>{}>{};
template <class T>
inline constexpr auto floating = ParsePipeline<FloatingFn<T>{}>{};
inline constexpr auto boolean = ParsePipeline<BooleanFn{}>{};
inline constexpr auto string_view = ParsePipeline<StringViewFn{}>{};
inline constexpr auto string = ParsePipeline<OwnedStringFn{}>{};
inline constexpr auto identity = ParsePipeline<IdentityFn{}>{};
template <auto F>
inline constexpr auto custom = ParsePipeline<CustomFn<F>{}>{};
template <class T>
inline constexpr auto parse_to = ParsePipeline<ParseToFn<T>{}>{};
template <auto Pipe>
inline constexpr auto vector = ParsePipeline<VectorFn<Pipe>{}>{};
template <class... Ts>
inline constexpr auto variant = ParsePipeline<VariantFn<Ts...>{}>{};
template <auto P>
inline constexpr auto member = ParsePipeline<MemberFn<P>{}>{};
template <std::size_t N, auto Pipe>
inline constexpr auto array = ParsePipeline<ArrayFn<N, Pipe>{}>{};

}  // namespace decode

// ═══════════════════════════════════════════════════════════════════════════════
// ser20::pack::*
// ═══════════════════════════════════════════════════════════════════════════════
namespace pack {

// assign ──────────────────────────────────────────────────────────────────────
struct AssignFn {
  template <class I>
  using after_type = void;
  template <class I>
  using storage_type = std::remove_cvref_t<I>;
  template <class T>
  SER20_ALWAYS_INLINE auto operator()(ParseCtx<std::remove_cvref_t<T>>& ctx,
                                      PipelineResult<T> in) const
      -> PipelineResult<void> {
    if (!in.have_value()) [[unlikely]]
      return PipelineResult<void>::fail(in.error());
    ctx.arg.get() = std::move(in.value());
    return PipelineResult<void>::ok();
  }

  template <class T>
  SER20_ALWAYS_INLINE auto operator()(ParseCtx<T>&,
                                      PipelineResult<decode::ParsedInPlace<T>>
                                          in) const -> PipelineResult<void> {
    if (!in.have_value()) [[unlikely]]
      return PipelineResult<void>::fail(in.error());
    return PipelineResult<void>::ok();
  }
};

// once ────────────────────────────────────────────────────────────────────────
struct OnceFn {
  template <class I>
  using after_type = void;
  template <class I>
  using storage_type = std::remove_cvref_t<I>;
  template <class T>
  SER20_ALWAYS_INLINE auto operator()(ParseCtx<std::remove_cvref_t<T>>& ctx,
                                      PipelineResult<T> in) const
      -> PipelineResult<void> {
    if (!in.have_value()) [[unlikely]]
      return PipelineResult<void>::fail(in.error());
    if (ctx.occurrences > 0) [[unlikely]]
      return PipelineResult<void>::fail({.code = ErrorCode::duplicate_argument,
                                         .subject = std::string(ctx.key)});
    ctx.arg.get() = std::move(in.value());
    return PipelineResult<void>::ok();
  }

  template <class T>
  SER20_ALWAYS_INLINE auto operator()(ParseCtx<T>& ctx,
                                      PipelineResult<decode::ParsedInPlace<T>>
                                          in) const -> PipelineResult<void> {
    if (!in.have_value()) [[unlikely]]
      return PipelineResult<void>::fail(in.error());
    if (ctx.occurrences > 0) [[unlikely]]
      return PipelineResult<void>::fail({.code = ErrorCode::duplicate_argument,
                                         .subject = std::string(ctx.key)});
    return PipelineResult<void>::ok();
  }
};

// overwrite ───────────────────────────────────────────────────────────────────
struct OverwriteFn {
  template <class I>
  using after_type = void;
  template <class I>
  using storage_type = std::remove_cvref_t<I>;
  template <class T>
  SER20_ALWAYS_INLINE auto operator()(ParseCtx<std::remove_cvref_t<T>>& ctx,
                                      PipelineResult<T> in) const
      -> PipelineResult<void> {
    if (!in.have_value()) [[unlikely]]
      return PipelineResult<void>::fail(in.error());
    ctx.arg.get() = std::move(in.value());
    return PipelineResult<void>::ok();
  }

  template <class T>
  SER20_ALWAYS_INLINE auto operator()(ParseCtx<T>&,
                                      PipelineResult<decode::ParsedInPlace<T>>
                                          in) const -> PipelineResult<void> {
    if (!in.have_value()) [[unlikely]]
      return PipelineResult<void>::fail(in.error());
    return PipelineResult<void>::ok();
  }
};

// first_wins ──────────────────────────────────────────────────────────────────
struct FirstWinsFn {
  template <class I>
  using after_type = void;
  template <class I>
  using storage_type = std::remove_cvref_t<I>;
  template <class T>
  SER20_ALWAYS_INLINE auto operator()(ParseCtx<std::remove_cvref_t<T>>& ctx,
                                      PipelineResult<T> in) const
      -> PipelineResult<void> {
    if (!in.have_value()) [[unlikely]]
      return PipelineResult<void>::fail(in.error());
    if (ctx.occurrences == 0) [[likely]]
      ctx.arg.get() = std::move(in.value());
    return PipelineResult<void>::ok();
  }

  template <class T>
  SER20_ALWAYS_INLINE auto operator()(ParseCtx<T>&,
                                      PipelineResult<decode::ParsedInPlace<T>>
                                          in) const -> PipelineResult<void> {
    if (!in.have_value()) [[unlikely]]
      return PipelineResult<void>::fail(in.error());
    return PipelineResult<void>::ok();
  }
};

// ignore ──────────────────────────────────────────────────────────────────────
// Unconditionally succeeds — discards both value and any error from prior steps.
// Used as the terminal pack step for unknown-field handlers.
struct IgnoreFn {
  template <class I>
  using after_type = void;
  template <class I>
  using storage_type = std::monostate;
  template <class St, class T>
  SER20_ALWAYS_INLINE auto operator()(ParseCtx<St>&, PipelineResult<T>) const
      -> PipelineResult<void> {
    return PipelineResult<void>::ok();
  }
};

// reject_unknown_with_suggestion ──────────────────────────────────────────────
// Policy used inside decode::member<> — value already skipped by MemberFn.
struct RejectUnknownWithSuggestionFn {
  template <class I>
  using after_type = void;
  template <class I>
  using storage_type = std::monostate;
  template <class St>
  auto operator()(ParseCtx<St>& ctx, PipelineResult<void>) const
      -> PipelineResult<void> {
    return PipelineResult<void>::fail(
        {.code = ErrorCode::unknown_option, .subject = std::string(ctx.key)});
  }
};

inline constexpr auto assign = ParsePipeline<AssignFn{}>{};
inline constexpr auto once = ParsePipeline<OnceFn{}>{};
inline constexpr auto overwrite = ParsePipeline<OverwriteFn{}>{};
inline constexpr auto first_wins = ParsePipeline<FirstWinsFn{}>{};
inline constexpr auto ignore = ParsePipeline<IgnoreFn{}>{};
inline constexpr auto reject_unknown_with_suggestion =
    ParsePipeline<RejectUnknownWithSuggestionFn{}>{};

}  // namespace pack

namespace detail {

template <class T>
SER20_ALWAYS_INLINE auto parse_object_in_place(ParseCtx<T>& ctx)
    -> PipelineResult<void> {
  if (ctx.first_tok.kind != TokenKind::LBrace) [[unlikely]]
    return PipelineResult<void>::fail(
        {.code = ErrorCode::invalid_value, .subject = "expected {"});
  auto fields = cli::as_tuple(ctx.arg.get());
  return json_parse_object_body(*ctx.scanner, ctx.input, fields);
}

template <class T>
SER20_ALWAYS_INLINE auto skip_pending_value(ParseCtx<T>& ctx) -> void {
  const auto& first = ctx.first_tok;
  if (first.kind == TokenKind::Quote || first.kind == TokenKind::LBrace ||
      first.kind == TokenKind::LBracket) {
    json_skip_value(*ctx.scanner, first);
  } else {
    ctx.next_is_pending = true;
    ctx.pending_tok = first;
  }
}

}  // namespace detail


namespace unknown {

struct IgnoreUnknownFn {
  template <class I>
  using after_type = void;
  template <class I>
  using storage_type = std::monostate;
  template <class St>
  auto operator()(ParseCtx<St>& ctx, PipelineResult<void>) const
      -> PipelineResult<void> {
    if (!ctx.next_is_pending) json_skip_value(*ctx.scanner, ctx.first_tok);
    return PipelineResult<void>::ok();
  }
};

struct RejectUnknownFn {
  template <class I>
  using after_type = void;
  template <class I>
  using storage_type = std::monostate;
  template <class St>
  auto operator()(ParseCtx<St>& ctx, PipelineResult<void>) const
      -> PipelineResult<void> {
    if (!ctx.next_is_pending) json_skip_value(*ctx.scanner, ctx.first_tok);
    return PipelineResult<void>::fail(
        {.code = ErrorCode::unknown_option, .subject = std::string(ctx.key)});
  }
};

struct RejectWithSuggestionFn {
  template <class I>
  using after_type = void;
  template <class I>
  using storage_type = std::monostate;
  template <class St>
  auto operator()(ParseCtx<St>& ctx, PipelineResult<void>) const
      -> PipelineResult<void> {
    if (!ctx.next_is_pending) json_skip_value(*ctx.scanner, ctx.first_tok);
    return PipelineResult<void>::fail(
        {.code = ErrorCode::unknown_option, .subject = std::string(ctx.key)});
  }
};

inline constexpr auto ignore = ParsePipeline<IgnoreUnknownFn{}>{};
inline constexpr auto reject = ParsePipeline<RejectUnknownFn{}>{};
inline constexpr auto reject_with_suggestion =
    ParsePipeline<RejectWithSuggestionFn{}>{};

}  // namespace unknown

namespace conversion {

/** @brief Converts a `string_view` token to a `std::string` by copying. */
struct String {
  template <class Input>
  static constexpr bool accepts_input =
      std::same_as<std::remove_cvref_t<Input>, std::string_view> ||
      std::same_as<std::remove_cvref_t<Input>, std::string>;

  template <class Input>
  using after_type = std::string;

  template <class Prev>
  using storage_type = void;

  auto operator()(PipelineCtx<void>, PipelineResult<std::string_view> input) const
      -> PipelineResult<std::string> {
    if (!input.have_value()) return fail<std::string>(input.error());
    return ok(std::string(input.value()));
  }
};

};  // namespace conversion

namespace pack {

/** @brief Sets the `bool` storage to `true`. Used as the action for `Flag`. */
struct SetTrue {
  template <class Prev>
  static constexpr bool accepts_input = true;

  template <class Prev>
  using after_type = void;

  template <class Prev>
  using storage_type = bool;

  template <class T>
  auto operator()(PipelineCtx<bool> ctx, PipelineResult<T> input) const
      -> PipelineResult<void> {
    if (!input.have_value()) return fail<void>(input.error());
    ctx.arg.get() = true;
    return ok();
  }
};

/** @brief Sets the `bool` storage to `false`. */
struct SetFalse {
  template <class Prev>
  static constexpr bool accepts_input = true;

  template <class Prev>
  using after_type = void;

  template <class Prev>
  using storage_type = bool;

  template <class T>
  auto operator()(PipelineCtx<bool> ctx, PipelineResult<T> input) const
      -> PipelineResult<void> {
    if (!input.have_value()) return fail<void>(input.error());
    ctx.arg.get() = false;
    return ok();
  }
};

/** @brief Flips the `bool` storage each time the option is seen. */
struct Toggle {
  template <class Prev>
  static constexpr bool accepts_input = true;

  template <class Prev>
  using after_type = void;

  template <class Prev>
  using storage_type = bool;

  template <class T>
  auto operator()(PipelineCtx<bool> ctx, PipelineResult<T> input) const
      -> PipelineResult<void> {
    if (!input.have_value()) return fail<void>(input.error());
    ctx.arg.get() = !ctx.arg.get();
    return ok();
  }
};

/** @brief Increments a `std::size_t` counter each time the option is seen. */
struct Increment {
  template <class Prev>
  static constexpr bool accepts_input = true;

  template <class Prev>
  using after_type = void;

  template <class Prev>
  using storage_type = std::size_t;

  template <class T>
  auto operator()(PipelineCtx<std::size_t> ctx, PipelineResult<T> input) const
      -> PipelineResult<void> {
    if (!input.have_value()) return fail<void>(input.error());
    ++ctx.arg.get();
    return ok();
  }
};

/**
 * @brief Stores the input value and rejects subsequent occurrences.
 *
 * Storage type: `T`. Returns `ErrorCode::duplicate_argument` if the option
 * appears more than once. This is the default terminal action for
 * `Option<T, ...>`.
 */
struct SetOnce {
  template <class Prev>
  static constexpr bool accepts_input =
      !std::same_as<std::remove_cvref_t<Prev>, void>;

  template <class Prev>
  using after_type = void;

  template <class Prev>
  using storage_type = std::remove_cvref_t<Prev>;

  template <class T>
  auto operator()(PipelineCtx<std::remove_cvref_t<T>> ctx,
                  PipelineResult<T> input) const -> PipelineResult<void> {
    if (!input.have_value()) return fail<void>(input.error());
    // if (ctx.occurrences > 1) {
    //   return PipelineResult<void>::fail(detail::duplicate_argument_error(
    //       ctx.index, detail::to_error_subject(input.value())));
    // }
    ctx.arg.get() = std::move(input.value());
    return ok();
  }
};

/**
 * @brief Appends the value to a `std::vector<T>`.
 *
 * Storage type: `std::vector<T>`. This is the default terminal action for
 * `ListOption<T, ...>`.
 */
struct Push {
  template <class Prev>
  static constexpr bool accepts_input =
      !std::same_as<std::remove_cvref_t<Prev>, void>;

  template <class Prev>
  using after_type = void;

  template <class Prev>
  using storage_type = std::vector<std::remove_cvref_t<Prev>>;

  template <class T>
  auto operator()(PipelineCtx<std::vector<std::remove_cvref_t<T>>> ctx,
                  PipelineResult<T> input) const -> PipelineResult<void> {
    if (!input.have_value()) return fail<void>(input.error());
    ctx.arg.get().push_back(std::move(input.value()));
    return ok();
  }
};

/**
 * @brief Inserts the value into a `std::set<T>` (duplicates are silently ignored
 * by the set).
 *
 * Storage type: `std::set<T>`. Requires the value type to be totally ordered.
 */
struct Insert {
  template <class Prev>
  static constexpr bool accepts_input =
      !std::same_as<std::remove_cvref_t<Prev>, void> &&
      std::totally_ordered<std::remove_cvref_t<Prev>>;

  template <class Prev>
  using after_type = void;

  template <class Prev>
  using storage_type = std::set<std::remove_cvref_t<Prev>>;

  template <class T>
  auto operator()(PipelineCtx<std::set<std::remove_cvref_t<T>>> ctx,
                  PipelineResult<T> input) const -> PipelineResult<void> {
    if (!input.have_value()) return fail<void>(input.error());
    ctx.arg.get().insert(std::move(input.value()));
    return ok();
  }
};

// struct InsertOrAssign {
//   template <class Prev>
//   static constexpr bool accepts_input = detail::PairLike<Prev>;
//
//   template <class Prev>
//   using after_type = void;
//
//   template <class Prev, bool = detail::PairLike<Prev>>
//   struct StorageTypeImpl {
//     using type = void;
//   };
//   template <class Prev>
//   struct StorageTypeImpl<Prev, true> {
//     using type = std::map<
//         std::remove_cvref_t<std::tuple_element_t<0,
//         std::remove_cvref_t<Prev>>>,
//         std::remove_cvref_t<std::tuple_element_t<1,
//         std::remove_cvref_t<Prev>>>>;
//   };
//
//   template <class Prev>
//   using storage_type = typename StorageTypeImpl<Prev>::type;
//
//   template <class T>
//   auto operator()(PipelineCtx<storage_type<T>> ctx, PipelineResult<T> input)
//   const
//       -> PipelineResult<void> {
//     if (!input.have_value()) return fail<void>(input.error());
//     auto&& [key, value] = input.value();
//     ctx.arg.get().insert_or_assign(key, value);
//     return ok();
//   }
// };

// struct Extend {
//   template <class Prev>
//   static constexpr bool accepts_input =
//       std::ranges::input_range<std::remove_cvref_t<Prev>> &&
//       !detail::StringLike<Prev>;
//
//   template <class Prev>
//   using after_type = void;
//
//   template <class Prev>
//   using storage_type =
//       std::vector<std::ranges::range_value_t<std::remove_cvref_t<Prev>>>;
//
//   template <class T>
//   auto operator()(PipelineCtx<storage_type<T>> ctx, PipelineResult<T> input)
//   const
//       -> PipelineResult<void> {
//     if (!input.have_value()) return fail<void>(input.error());
//     auto& out = ctx.arg.get();
//     for (auto&& value : input.value()) {
//       out.emplace_back(value);
//     }
//     return ok();
//   }
// };

/**
 * @brief Sets a `bool` flag to `true` when the option is seen, regardless of
 * value.
 *
 * Storage type: `bool`. Useful for detecting option presence without caring
 * about the value of the option.
 */
struct MarkPresent {
  template <class Prev>
  static constexpr bool accepts_input = true;

  template <class Prev>
  using after_type = void;

  template <class Prev>
  using storage_type = bool;

  template <class T>
  auto operator()(PipelineCtx<bool> ctx, PipelineResult<T> input) const
      -> PipelineResult<void> {
    if (!input.have_value()) return fail<void>(input.error());
    ctx.arg.get() = true;
    return ok();
  }
};

/**
 * @brief Writes the value directly into an external variable via a `T*` pointer.
 *
 * Storage type: `T*`. The pointer must be non-null at the time of parsing;
 * returns `ErrorCode::validation_failed` with a descriptive message if the
 * pointer is null. Used as the action for `BoundOption`.
 *
 * @tparam T The type of the external variable.
 */
// template <class T>
// struct StoreInto {
//   template <class Prev>
//   static constexpr bool accepts_input =
//       std::same_as<std::remove_cvref_t<Prev>, T>;
//
//   template <class Prev>
//   using after_type = void;
//
//   template <class Prev>
//   using storage_type = T*;
//
//   auto operator()(PipelineCtx<T*> ctx, PipelineResult<T> input) const
//       -> PipelineResult<void> {
//     if (!input.have_value()) return fail<void>(input.error());
//     if (T* ptr = ctx.arg.get(); ptr != nullptr) {
//       *ptr = std::move(input.value());
//       return ok();
//     }
//     return PipelineResult<void>::fail(detail::validation_failed_error(
//         ctx.index, "write_to",
//         "target pointer is null; did you forget to call bind()?"));
//   }
// };

template <auto Fn, class T>
concept CallbackInvocable = requires(PipelineCtx<std::monostate> ctx, T& value) {
  { Fn(ctx, value) } -> std::same_as<void>;
} || requires(T& value) {
  { Fn(value) } -> std::same_as<void>;
} || requires {
  { Fn() } -> std::same_as<void>;
};

/**
 * @brief Invokes a compile-time callable `Fn` with the parsed value as a
 * terminal action.
 *
 * `Fn` may have any of the following signatures:
 * - `(PipelineCtx<std::monostate>, T&)` — receives context and value.
 * - `(T&)` — receives value only.
 * - `()` — receives nothing.
 *
 * Storage type: `std::monostate` (no value is stored; the callback is the only
 * effect).
 *
 * @tparam Fn A constexpr callable to invoke.
 */
template <auto Fn>
struct Callback {
  template <class Prev>
  static constexpr bool accepts_input = CallbackInvocable<Fn, Prev>;

  template <class Prev>
  using after_type = void;

  template <class Prev>
  using storage_type = std::monostate;

  template <class T>
    requires CallbackInvocable<Fn, T>
  auto operator()(PipelineCtx<std::monostate> ctx, PipelineResult<T> input) const
      -> PipelineResult<void> {
    if (!input.have_value()) return fail<void>(input.error());
    if constexpr (requires(T& value) { Fn(ctx, value); }) {
      Fn(ctx, input.value());
    } else if constexpr (requires(T& value) { Fn(value); }) {
      Fn(input.value());
    } else {
      Fn();
    }
    return ok();
  }
};

inline constexpr auto set_true = Pipeline<SetTrue{}>{};
inline constexpr auto set_false = Pipeline<SetFalse{}>{};
inline constexpr auto toggle = Pipeline<Toggle{}>{};
inline constexpr auto increment = Pipeline<Increment{}>{};
inline constexpr auto set_once = Pipeline<SetOnce{}>{};
// inline constexpr auto reject_duplicate = Pipeline<RejectDuplicate{}>{};
// inline constexpr auto push_unique = Pipeline<PushUnique{}>{};
inline constexpr auto push = Pipeline<Push{}>{};
inline constexpr auto insert = Pipeline<Insert{}>{};
// inline constexpr auto insert_or_assign = Pipeline<InsertOrAssign{}>{};
// inline constexpr auto extend = Pipeline<Extend{}>{};
inline constexpr auto mark_present = Pipeline<MarkPresent{}>{};
template <auto Fn>
inline constexpr auto callback = Pipeline<Callback<Fn>{}>{};
// template <class T>
// inline constexpr auto write_to = Pipeline<StoreInto<T>{}>{};

}  // namespace pack

}  // namespace ser20
