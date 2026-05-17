#pragma once

#include <algorithm>
#include <cctype>
#include <concepts>
#include <cstddef>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>

#include "ser20/error.hh"
#include "ser20/result.hh"

namespace ser20 {

namespace detail {

struct AnyInput {};

template <class Fn>
concept HasAcceptsInput = requires {
  {
    std::remove_cvref_t<Fn>::template accepts_input<AnyInput>
  } -> std::convertible_to<bool>;
};

template <class Fn>
concept HasAfterType = requires {
  typename std::remove_cvref_t<Fn>::template after_type<AnyInput>;
};

template <class Fn>
concept HasStorageType = requires {
  typename std::remove_cvref_t<Fn>::template storage_type<AnyInput>;
};

template <class Input>
struct MissingAcceptsInput;

template <class Head, class Input>
struct MissingAfterType;

template <class Head, class Input>
struct MissingStorageType;

template <class T>
consteval auto require_step_impl() -> void {
  if constexpr (!detail::HasAcceptsInput<T>) {
    static_assert(sizeof(MissingAcceptsInput<T>) == 0,
                  "ser20 step error: implementation type must define "
                  "template<class Input> "
                  "static constexpr bool accepts_input");
  }
}

template <class Head, class Input>
consteval auto require_after_type() -> void {
  if constexpr (!requires { typename Head::template after_type<Input>; }) {
    static_assert(sizeof(MissingAfterType<Head, Input>) == 0,
                  "A non-terminal action must define after_type<Input>");
  }
}

template <class Head, class Input>
consteval auto require_storage_type() -> void {
  if constexpr (!requires { typename Head::template storage_type<Input>; }) {
    static_assert(sizeof(MissingStorageType<Head, Input>) == 0,
                  "The last action must define storage_type<Input>");
  }
}

template <auto Fn>
consteval auto require_pipeline_callable() -> void {
  using FnType = std::remove_cvref_t<decltype(Fn)>;
  require_step_impl<FnType>();
}

enum class PipelineMode {
  decoder,
  encoder,
};

}  // namespace detail

template <auto Fn>
concept PipelineCallable = detail::HasAcceptsInput<decltype(Fn)>;

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
  requires(PipelineCallable<Fns> && ...)
struct Pipeline {
  template <class Context, class Input>
  [[nodiscard]]
  static constexpr auto invoke(Context& ctx, Result<Input, Error> input) {
    return []<auto FnHead, auto... FnTail>(
               Context& ctx, Result<Input, Error> input) -> decltype(auto) {
      auto next = FnHead(ctx, std::move(input));

      if constexpr (sizeof...(FnTail) == 0) {
        return next;
      } else {
        return Pipeline<FnTail...>::invoke(ctx, std::move(next));
      }
    }.template operator()<Fns...>(ctx, std::move(input));
  }

  template <class Context, class Input>
  [[nodiscard]]
  static constexpr auto invoke(const Context& ctx, Result<Input, Error> input) {
    return
        []<auto FnHead, auto... FnTail>(
            const Context& ctx, Result<Input, Error> input) -> decltype(auto) {
          auto next = FnHead(ctx, std::move(input));

          if constexpr (sizeof...(FnTail) == 0) {
            return next;
          } else {
            return Pipeline<FnTail...>::invoke(ctx, std::move(next));
          }
        }.template operator()<Fns...>(ctx, std::move(input));
  }

  template <detail::PipelineMode Mode, class Input, auto FnHead, auto... FnTail>
  static constexpr auto analyze_impl() {
    using Head = std::remove_cvref_t<decltype(FnHead)>;

    static_assert(Head::template accepts_input<Input>,
                  "Pipeline step does not accept the previous output type");

    if constexpr (sizeof...(FnTail) == 0) {
      if constexpr (Mode == detail::PipelineMode::decoder) {
        detail::require_storage_type<Head, Input>();

        return std::make_tuple(
            true,
            std::type_identity<typename Head::template storage_type<Input>>{});
      } else {
        static_assert(
            !requires { typename Head::template after_type<Input>; },
            "The last encoder action must be terminal and must not define "
            "after_type<Input>");

        return std::make_tuple(true, std::type_identity<void>{});
      }
    } else {
      detail::require_after_type<Head, Input>();

      using Next = typename Head::template after_type<Input>;
      return analyze_impl<Mode, Next, FnTail...>();
    }
  }

  static constexpr auto analyze_decoder() {
    return analyze_impl<detail::PipelineMode::decoder, void, Fns...>();
  }

  template <class Input>
  static constexpr auto analyze_encoder() {
    return analyze_impl<detail::PipelineMode::encoder, Input, Fns...>();
  }
};

template <>
struct Pipeline<> {
  template <class Context, class Input>
  [[nodiscard]]
  static constexpr auto invoke(Context&, Result<Input, Error> input) {
    return input;
  }

  template <class Context, class Input>
  [[nodiscard]]
  static constexpr auto invoke(const Context&, Result<Input, Error> input) {
    return input;
  }

  static constexpr auto analyze_decoder() {
    return std::make_tuple(true, std::type_identity<void>{});
  }

  template <class Input>
  static constexpr auto analyze_encoder() {
    return std::make_tuple(true, std::type_identity<void>{});
  }
};

template <class T>
inline constexpr auto step = []() -> auto {
  detail::require_step_impl<T>();
  return Pipeline<T{}>{};
}();

template <auto... Lhs, auto... Rhs>
constexpr auto operator|(Pipeline<Lhs...>, Pipeline<Rhs...>) {
  (detail::require_pipeline_callable<Lhs>(), ...);
  (detail::require_pipeline_callable<Rhs>(), ...);
  return Pipeline<Lhs..., Rhs...>{};
}

}  // namespace ser20
