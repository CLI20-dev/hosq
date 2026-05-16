#pragma once

#include <algorithm>
#include <cctype>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "ser20/error.hh"

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

  State state{State::ok};
  ParseError error{};
  T value{};

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

  /**
   * @brief Creates a success result holding `value`.
   *
   * @tparam U Type of the value (deduced).
   * @param v The value to store.
   * @return A successful `PipelineResult<T>`.
   */
  template <class U>
  [[nodiscard]] static constexpr auto ok(U&& v) -> PipelineResult {
    return {.state = State::ok, .value = std::forward<U>(v)};
  }

  /**
   * @brief Creates a failure result carrying `error`.
   *
   * @param e The error to store.
   * @return A failed `PipelineResult<T>`.
   */
  [[nodiscard]] static constexpr auto fail(ParseError e) -> PipelineResult {
    return {.state = State::error, .error = std::move(e)};
  }

  /**
   * @brief Creates an ignore result.
   *
   * @return An ignored `PipelineResult<T>`.
   */
  [[nodiscard]] static constexpr auto ignore() -> PipelineResult {
    return {.state = State::ignore};
  }
};

/**
 * @brief Specialisation of `PipelineResult` for terminal (pack) actions that
 * produce no value.
 *
 * Pack actions such as `SetTrue` or `Push` write their result directly into the
 * argument storage referenced by `PipelineCtx::arg`; they do not pass a value
 * downstream, so `PipelineResult<void>` carries only an error, success, or ignore
 * state.
 */
template <>
struct PipelineResult<void> {
  using value_type = void;

  enum class State : std::uint8_t { ok, error, ignore };

  State state{State::ok};
  ParseError error{};

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

  /**
   * @brief Creates a success result.
   *
   * @return A successful `PipelineResult<void>`.
   */
  [[nodiscard]] static constexpr auto ok() -> PipelineResult<void> {
    return {.state = State::ok};
  }

  /**
   * @brief Creates a failure result carrying `error`.
   *
   * @param e The error to store.
   * @return A failed `PipelineResult<void>`.
   */
  [[nodiscard]] static constexpr auto fail(ParseError e) -> PipelineResult<void> {
    return {.state = State::error, .error = std::move(e)};
  }

  /**
   * @brief Creates an ignore result.
   *
   * @return An ignored `PipelineResult<void>`.
   */
  [[nodiscard]] static constexpr auto ignore() -> PipelineResult<void> {
    return {.state = State::ignore};
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
[[nodiscard]] constexpr auto ok(T&& value)
    -> PipelineResult<std::remove_cvref_t<T>> {
  return PipelineResult<std::remove_cvref_t<T>>::ok(std::forward<T>(value));
}

/**
 * @brief Helper function for constructing a successful `PipelineResult<void>`.
 *
 * @return A successful `PipelineResult<void>`.
 */
[[nodiscard]] constexpr auto ok() -> PipelineResult<void> {
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
[[nodiscard]] constexpr auto fail(ParseError error) -> PipelineResult<T> {
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
[[nodiscard]] constexpr auto ignore() -> PipelineResult<T> {
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
 * This specialisation also provides a converting constructor from `PipelineCtx<T>`
 * so that the parser can downcast when invoking non-terminal actions.
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
  static constexpr auto invoke(PipelineCtx<Arg>& ctx, PipelineResult<Result> input) {
    return
        []<auto FnHead, auto... FnTail>(
            PipelineCtx<Arg>& ctx, PipelineResult<Result> input) -> decltype(auto) {
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
        static_assert(Head::template accepts_input<Prev>,
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

  constexpr auto operator()(PipelineCtx<void>,
                            PipelineResult<std::string_view> input) const
      -> PipelineResult<std::string> {
    if (!input.have_value()) return fail<std::string>(input.error);
    return ok(std::string(input.value));
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
    if (!input.have_value()) return fail<void>(input.error);
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
    if (!input.have_value()) return fail<void>(input.error);
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
    if (!input.have_value()) return fail<void>(input.error);
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
    if (!input.have_value()) return fail<void>(input.error);
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
    if (!input.have_value()) return fail<void>(input.error);
    // if (ctx.occurrences > 1) {
    //   return PipelineResult<void>::fail(detail::duplicate_argument_error(
    //       ctx.index, detail::to_error_subject(input.value)));
    // }
    ctx.arg.get() = std::move(input.value);
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
    if (!input.have_value()) return fail<void>(input.error);
    ctx.arg.get().push_back(std::move(input.value));
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
    if (!input.have_value()) return fail<void>(input.error);
    ctx.arg.get().insert(std::move(input.value));
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
//   auto operator()(PipelineCtx<storage_type<T>> ctx, PipelineResult<T> input) const
//       -> PipelineResult<void> {
//     if (!input.have_value()) return fail<void>(input.error);
//     auto&& [key, value] = input.value;
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
//   auto operator()(PipelineCtx<storage_type<T>> ctx, PipelineResult<T> input) const
//       -> PipelineResult<void> {
//     if (!input.have_value()) return fail<void>(input.error);
//     auto& out = ctx.arg.get();
//     for (auto&& value : input.value) {
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
    if (!input.have_value()) return fail<void>(input.error);
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
//     if (!input.have_value()) return fail<void>(input.error);
//     if (T* ptr = ctx.arg.get(); ptr != nullptr) {
//       *ptr = std::move(input.value);
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
    if (!input.have_value()) return fail<void>(input.error);
    if constexpr (requires(T& value) { Fn(ctx, value); }) {
      Fn(ctx, input.value);
    } else if constexpr (requires(T& value) { Fn(value); }) {
      Fn(input.value);
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
