
#pragma once

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <ranges>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>
#include <variant>

#include "ser20/error.hh"

namespace ser20 {

/**
 * @brief The result type produced and consumed at each step of an action
 * pipeline.
 *
 * An `ActionResult<T>` is a 3-state type: ok (carrying a value of type `T`),
 * error (carrying a `ParseError`), or ignore (silent skip). The
 * `operator bool()`, `have_value()`, `has_value()`, `has_error()`, and
 * `is_ignored()` methods make it easy to check the state inline.
 *
 * Factory methods:
 * - `ActionResult<T>::ok(value)` — create a success result.
 * - `ActionResult<T>::fail(error)` — create a failure result.
 * - `ActionResult<T>::ignore()` — create an ignore result.
 *
 * @tparam T The value type held on success.
 */
template <class T>
struct ActionResult {
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
   * @return A successful `ActionResult<T>`.
   */
  template <class U>
  [[nodiscard]] static constexpr auto ok(U&& v) -> ActionResult {
    return {.state = State::ok, .value = std::forward<U>(v)};
  }

  /**
   * @brief Creates a failure result carrying `error`.
   *
   * @param e The error to store.
   * @return A failed `ActionResult<T>`.
   */
  [[nodiscard]] static constexpr auto fail(ParseError e) -> ActionResult {
    return {.state = State::error, .error = std::move(e)};
  }

  /**
   * @brief Creates an ignore result.
   *
   * @return An ignored `ActionResult<T>`.
   */
  [[nodiscard]] static constexpr auto ignore() -> ActionResult {
    return {.state = State::ignore};
  }
};

/**
 * @brief Specialisation of `ActionResult` for terminal (pack) actions that
 * produce no value.
 *
 * Pack actions such as `SetTrue` or `Push` write their result directly into the
 * argument storage referenced by `ActionCtx::arg`; they do not pass a value
 * downstream, so `ActionResult<void>` carries only an error, success, or ignore
 * state.
 */
template <>
struct ActionResult<void> {
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
   * @return A successful `ActionResult<void>`.
   */
  [[nodiscard]] static constexpr auto ok() -> ActionResult<void> {
    return {.state = State::ok};
  }

  /**
   * @brief Creates a failure result carrying `error`.
   *
   * @param e The error to store.
   * @return A failed `ActionResult<void>`.
   */
  [[nodiscard]] static constexpr auto fail(ParseError e) -> ActionResult<void> {
    return {.state = State::error, .error = std::move(e)};
  }

  /**
   * @brief Creates an ignore result.
   *
   * @return An ignored `ActionResult<void>`.
   */
  [[nodiscard]] static constexpr auto ignore() -> ActionResult<void> {
    return {.state = State::ignore};
  }
};

/**
 * @brief Helper functions for constructing `ActionResult` objects with type
 * deduction.
 *
 * @tparam T The value type of the `ActionResult` to construct.
 * @param value The value to store in a success result.
 * @return A successful `ActionResult<T>` holding `value`.
 */
template <class T>
[[nodiscard]] constexpr auto ok(T&& value)
    -> ActionResult<std::remove_cvref_t<T>> {
  return ActionResult<std::remove_cvref_t<T>>::ok(std::forward<T>(value));
}

/**
 * @brief Helper function for constructing a successful `ActionResult<void>`.
 *
 * @return A successful `ActionResult<void>`.
 */
[[nodiscard]] constexpr auto ok() -> ActionResult<void> {
  return ActionResult<void>::ok();
}

/**
 * @brief Helper function for constructing a failure `ActionResult` with type
 * deduction.
 *
 * @tparam T The value type of the `ActionResult` to construct.
 * @param error The error to store in the failure result.
 * @return A failed `ActionResult<T>` carrying `error`.
 */
template <class T>
[[nodiscard]] constexpr auto fail(ParseError error) -> ActionResult<T> {
  return ActionResult<T>::fail(std::move(error));
}

/**
 * @brief Helper function for constructing an ignore `ActionResult` with type
 * deduction.
 *
 * @tparam T The value type of the `ActionResult` to construct.
 * @return An ignored `ActionResult<T>`.
 */
template <class T>
[[nodiscard]] constexpr auto ignore() -> ActionResult<T> {
  return ActionResult<T>::ignore();
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
struct ActionCtx {
  size_t index{};  ///< Zero-based index into `argv` of the current token.
  size_t
      occurrences{};  ///< Number of times the parent option token has appeared.
  size_t nargs{};     ///< Number of values in this option occurrence.
  size_t value_index{};  ///< 0-based index of current value in this occurrence.
  size_t
      total_values{};  ///< Total values across all occurrences of this option.
  std::reference_wrapper<T>
      arg{};  ///< Reference to the argument's storage value.
};

/**
 * @brief Partial specialisation of `ActionCtx` used for intermediate pipeline
 * steps.
 *
 * Conversion and validation actions do not have access to the final storage;
 * they receive `ActionCtx<void>` which carries only the positional counters.
 * This specialisation also provides a converting constructor from `ActionCtx<T>`
 * so that the parser can downcast when invoking non-terminal actions.
 */
template <>
struct ActionCtx<void> {
  size_t index{};  ///< Zero-based index into `argv` of the current token.
  size_t
      occurrences{};  ///< Number of times the parent option token has appeared.
  size_t nargs{};     ///< Number of values in this option occurrence.
  size_t value_index{};  ///< 0-based index of current value in this occurrence.
  size_t
      total_values{};  ///< Total values across all occurrences of this option.

  /**
   * @brief Constructs from a typed `ActionCtx<T>`, copying the counters.
   *
   * @tparam T The storage type of the source context.
   * @param other The source context to copy counters from.
   */
  template <class T>
  ActionCtx(const ActionCtx<T>& other)
      : index(other.index),
        occurrences(other.occurrences),
        nargs(other.nargs),
        value_index(other.value_index),
        total_values(other.total_values) {}

  ActionCtx() = default;
};

namespace detail {

template <class T>
[[nodiscard]]
auto to_chars_string(T value) -> std::string {
  std::array<char, 128> buffer{};
  const auto [ptr, ec] = [&]() -> auto {
    if constexpr (std::floating_point<T>) {
      return std::to_chars(buffer.data(), buffer.data() + buffer.size(), value,
                           std::chars_format::general);
    } else {
      return std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
    }
  }();
  if (ec != std::errc{}) return "<value>";
  return std::string(buffer.data(), ptr);
}

template <class T>
[[nodiscard]]
auto to_error_subject(const T& value) -> std::string {
  using U = std::remove_cvref_t<T>;

  if constexpr (std::same_as<U, std::string>) {
    return value;
  } else if constexpr (std::same_as<U, std::string_view>) {
    return std::string(value);
  } else if constexpr (std::same_as<U, const char*>) {
    return value == nullptr ? std::string{} : std::string(value);
  } else if constexpr (std::is_same_v<U, bool>) {
    return value ? "true" : "false";
  } else if constexpr (std::integral<U> || std::floating_point<U>) {
    return to_chars_string(value);
  } else if constexpr (std::same_as<U, std::filesystem::path>) {
    return value.string();
  } else if constexpr (requires(std::ostream& os, const U& v) { os << v; }) {
    std::ostringstream out;
    out << value;
    return out.str();
  } else {
    return "<value>";
  }
}

[[nodiscard]]
inline auto invalid_value_error(ErrorKind kind, std::size_t index,
                                std::string subject, std::string detail = {})
    -> ParseError {
  return ParseError{
      .code = ErrorCode::invalid_value,
      .kind = kind,
      .position = static_cast<int>(index),
      .subject = std::move(subject),
      .detail = std::move(detail),
  };
}

[[nodiscard]]
inline auto duplicate_argument_error(std::size_t index, std::string subject,
                                     std::string detail = {}) -> ParseError {
  return ParseError{
      .code = ErrorCode::duplicate_argument,
      .kind = ErrorKind::validation,
      .position = static_cast<int>(index),
      .subject = std::move(subject),
      .detail = std::move(detail),
  };
}

[[nodiscard]]
inline auto validation_failed_error(std::size_t index, std::string subject,
                                    std::string detail) -> ParseError {
  return ParseError{
      .code = ErrorCode::validation_failed,
      .kind = ErrorKind::validation,
      .position = static_cast<int>(index),
      .subject = std::move(subject),
      .detail = std::move(detail),
  };
}

[[nodiscard]]
inline auto invalid_choice_error(std::size_t index, std::string subject,
                                 std::string detail = {}) -> ParseError {
  return ParseError{
      .code = ErrorCode::invalid_choice,
      .kind = ErrorKind::conversion,
      .position = static_cast<int>(index),
      .subject = std::move(subject),
      .detail = std::move(detail),
  };
}

[[nodiscard]]
inline auto is_blank(std::string_view text) -> bool {
  return std::ranges::all_of(
      text, [](unsigned char ch) -> bool { return std::isspace(ch) != 0; });
}

template <class T>
using decay_t = std::remove_cvref_t<T>;

template <class T>
struct HelpRequested {
  T value;
};

template <class T>
struct IsHelpRequested : std::false_type {};

template <class T>
struct IsHelpRequested<HelpRequested<T>> : std::true_type {};

template <class T>
concept PairLike = requires {
  typename std::tuple_size<decay_t<T>>::type;
  requires std::tuple_size_v<decay_t<T>> == 2;
};

template <class T>
concept StringLike = std::same_as<decay_t<T>, std::string> ||
                     std::same_as<decay_t<T>, std::string_view>;

}  // namespace detail

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
struct Action {
  template <class Arg, class Result>
  [[nodiscard]]
  static constexpr auto invoke(ActionCtx<Arg>& ctx, ActionResult<Result> input) {
    return
        []<auto FnHead, auto... FnTail>(
            ActionCtx<Arg>& ctx, ActionResult<Result> input) -> decltype(auto) {
          if (input.is_ignored()) {
            using FinalIdentity = std::remove_cvref_t<decltype(std::get<2>(
                Action::template validate_impl<false, Result, FnHead,
                                               FnTail...>()))>;
            return ActionResult<typename FinalIdentity::type>::ignore();
          }

          auto next = FnHead(ctx, std::move(input));
          if constexpr (sizeof...(FnTail) == 0) {
            return next;
          } else {
            return Action<FnTail...>::invoke(ctx, std::move(next));
          }
        }.template operator()<Fns...>(ctx, std::move(input));
  }

  template <class Arg, class Result>
  [[nodiscard]]
  static constexpr auto invoke(const ActionCtx<Arg>& ctx,
                               ActionResult<Result> input) {
    return []<auto FnHead, auto... FnTail>(
               const ActionCtx<Arg>& ctx,
               ActionResult<Result> input) -> decltype(auto) {
      if (input.is_ignored()) {
        using FinalIdentity = std::remove_cvref_t<decltype(std::get<2>(
            Action::template validate_impl<false, Result, FnHead,
                                           FnTail...>()))>;
        return ActionResult<typename FinalIdentity::type>::ignore();
      }

      auto next = FnHead(ctx, std::move(input));
      if constexpr (sizeof...(FnTail) == 0) {
        return next;
      } else {
        return Action<FnTail...>::invoke(ctx, std::move(next));
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
                      "Action step does not accept the previous step's output");
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
struct Action<> {
  static constexpr bool entry_is_optional = false;

  template <class Arg, class Result>
  [[nodiscard]]
  static constexpr auto invoke(ActionCtx<Arg>&, ActionResult<Result> input) {
    return input;
  }

  static constexpr auto validate() -> auto {
    return std::make_tuple(true, std::type_identity<std::monostate>{},
                           std::type_identity<std::monostate>{});
  }
};

template <auto Fn, auto... Fns>
constexpr auto operator|(Action<Fns...>, Action<Fn>) {
  return Action<Fns..., Fn>{};
}

}  // namespace ser20
