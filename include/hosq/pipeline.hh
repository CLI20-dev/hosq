#pragma once

#include <algorithm>
#include <cctype>
#include <concepts>
#include <cstddef>
#include <type_traits>
#include <utility>

#include "hosq/detail/always_false.hh"
#include "hosq/error.hh"
#include "hosq/result.hh"

#if defined(__GNUC__) || defined(__clang__)
#define HOSQ_ALWAYS_INLINE_ATTR __attribute__((always_inline))
#define HOSQ_FUNCTION_ALWAYS_INLINE inline __attribute__((always_inline))
#elif defined(_MSC_VER)
#define HOSQ_ALWAYS_INLINE_ATTR
#define HOSQ_FUNCTION_ALWAYS_INLINE __forceinline
#else
#define HOSQ_ALWAYS_INLINE_ATTR
#define HOSQ_FUNCTION_ALWAYS_INLINE inline
#endif

namespace hosq {

template <auto... Fns>
  requires(sizeof...(Fns) > 0)
struct Pipeline;

namespace detail {

template <class Value, class Context, auto... Fn>
struct TailResultType {
  using type = decltype(Pipeline<Fn...>::invoke(std::declval<Value>(),
                                                std::declval<Context&>()));
};

template <class Context, auto... Fn>
struct TailResultType<void, Context, Fn...> {
  using type = decltype(Pipeline<Fn...>::invoke(std::declval<Context&>()));
};

template <class Value, class Context, auto... Fn>
using tail_result_t = typename TailResultType<Value, Context, Fn...>::type;

template <class R>
struct LiftResult {
  using type = Result<std::remove_cvref_t<R>, Error>;
};

template <>
struct LiftResult<void> {
  using type = Result<void, Error>;
};

template <class T>
struct LiftResult<Result<T, Error>> {
  using type = Result<T, Error>;
};

template <class R>
using lift_result_t = typename LiftResult<std::remove_cvref_t<R>>::type;

template <class R>
constexpr auto lift_plain_success(R&& value) {
  using Clean = std::remove_cvref_t<R>;

  if constexpr (HosqResultType<Clean>) {
    return std::forward<R>(value);
  } else {
    return Result<Clean, Error>{std::forward<R>(value)};
  }
}

constexpr auto lift_plain_success() -> Result<void, Error> { return {}; }

template <class R>
constexpr auto lift_error(Error error) {
  using Out = lift_result_t<R>;
  return Out{unexpected(std::move(error))};
}

template <auto... FnTail, class Output, class Context>
static constexpr auto continue_result(Result<Output, Error> next, Context& ctx) {
  using TailRaw = detail::tail_result_t<Output, Context, FnTail...>;
  using TailOut = detail::lift_result_t<TailRaw>;

  if (!next) [[unlikely]] {
    return detail::lift_error<TailRaw>(std::move(next).error());
  }

  if constexpr (std::same_as<Output, void>) {
    if constexpr (std::same_as<TailRaw, void>) {
      Pipeline<FnTail...>::invoke(ctx);
      return TailOut{};
    } else {
      return detail::lift_plain_success(Pipeline<FnTail...>::invoke(ctx));
    }
  } else {
    if constexpr (std::same_as<TailRaw, void>) {
      Pipeline<FnTail...>::invoke(std::move(*next), ctx);
      return TailOut{};
    } else {
      return detail::lift_plain_success(
          Pipeline<FnTail...>::invoke(std::move(*next), ctx));
    }
  }
}

template <auto... FnTail, class Output, class Context>
static constexpr auto continue_result(Result<Output, Error> next,
                                      const Context& ctx) {
  using TailRaw = detail::tail_result_t<Output, const Context, FnTail...>;
  using TailOut = detail::lift_result_t<TailRaw>;

  if (!next) [[unlikely]] {
    return detail::lift_error<TailRaw>(std::move(next).error());
  }

  if constexpr (std::same_as<Output, void>) {
    if constexpr (std::same_as<TailRaw, void>) {
      Pipeline<FnTail...>::invoke(ctx);
      return TailOut{};
    } else {
      return detail::lift_plain_success(Pipeline<FnTail...>::invoke(ctx));
    }
  } else {
    if constexpr (std::same_as<TailRaw, void>) {
      Pipeline<FnTail...>::invoke(std::move(*next), ctx);
      return TailOut{};
    } else {
      return detail::lift_plain_success(
          Pipeline<FnTail...>::invoke(std::move(*next), ctx));
    }
  }
}

}  // namespace detail

/**
 * @brief A compile-time pipeline of constexpr action objects applied
 * sequentially.
 *
 * Each step receives the output of the previous step as its input. Steps may
 * return a plain value, `void`, `Result<Out, Error>`, or `Result<void, Error>`.
 *
 * Plain values are forwarded directly. Terminal plain values are preserved as
 * the pipeline result; they are not normalized to `Result<T, Error>`.
 *
 * When a step returns `Result<T, Error>`, errors are propagated immediately.
 * If the result is successful, the unwrapped value `T` is passed to the tail
 * pipeline. The tail result is then lifted into `Result<U, Error>`, unless it
 * is already a `Result<U, Error>`.
 *
 * This means:
 * - a pure match pipeline may return `bool`;
 * - a scan-then-match pipeline may return `Result<bool, Error>`;
 * - a pure action pipeline may return `void`;
 * - an error-capable action pipeline may return `Result<void, Error>`.
 *
 * A terminal `bool` is commonly used by decoder pipelines as a match/retry
 * result: `true` means matched, `false` means not matched.
 *
 * **Decoder role** (validated by `analyze_decoder()`): exactly one step across
 * the pipeline must declare `storage_type<Input>`. That type becomes the
 * decoder's storage type; it need not be the terminal step. The terminal output
 * must be `void`, `bool`, `Result<void, Error>`, or `Result<bool, Error>`.
 *
 * **Encoder role** (validated by `analyze_encoder()`): no step may declare
 * `storage_type<Input>`. The terminal output must be `void` or
 * `Result<void, Error>`.
 *
 * @tparam Fns Pack of constexpr action objects forming the pipeline.
 */
template <auto... Fns>
  requires(sizeof...(Fns) > 0)
struct Pipeline {
  /**
   * @brief Invokes this pipeline from an explicit input value using a mutable
   * context.
   *
   * Steps are matched left-to-right by their call signature. Context-aware
   * forms are tried before no-context forms. The return value of each step
   * flows as the input to the next step.
   *
   * Supported step signatures, tried in priority order:
   * - `Fn(input, ctx) -> void`
   * - `Fn(input, ctx) -> Result<Out, Error>`
   * - `Fn(input, ctx) -> Out`
   * - `Fn(input) -> void`
   * - `Fn(input) -> Result<Out, Error>`
   * - `Fn(input) -> Out`
   *
   * `Result` errors are propagated immediately; remaining steps are not
   * invoked. Plain return values are forwarded without error-checking.
   *
   * @tparam Input  Input value type for the first step.
   * @tparam Context Mutable context type used by context-aware steps.
   * @param input Input value consumed by the first step.
   * @param ctx Mutable pipeline context.
   * @return The final pipeline result. Plain terminal values are returned as-is.
   * If any earlier step returns `Result<T, Error>`, errors are propagated and
   * the tail result is lifted to `Result<U, Error>`.
   */
  template <class Input, class Context>
  [[nodiscard]]
  HOSQ_FUNCTION_ALWAYS_INLINE static constexpr auto invoke(Input input,
                                                           Context& ctx) {
    return []<auto FnHead, auto... FnTail>(Input input,
                                           Context& ctx) -> decltype(auto) {
      using Head = std::remove_cvref_t<decltype(FnHead)>;
      constexpr auto Fn = FnHead;
      constexpr bool last_function = sizeof...(FnTail) == 0;

      // 1. Fn(input, ctx) -> void
      if constexpr (  //
          requires(Input i, Context& c) {
            { Fn(std::move(i), c) } -> std::same_as<void>;
          }) {
        if constexpr (last_function) {
          return Fn(std::move(input), ctx);
        } else {
          return Fn(std::move(input), ctx), Pipeline<FnTail...>::invoke(ctx);
        }
      }
      // 2. Fn(input, ctx) -> Result<AfterType, Error>
      else if constexpr (  //
          requires(Input i, Context& c) {
            { Fn(std::move(i), c) } -> detail::HosqResultType;
          }) {
        if constexpr (last_function) {
          return Fn(std::move(input), ctx);
        } else {
          return detail::continue_result<FnTail...>(Fn(std::move(input), ctx),
                                                    ctx);
        }
      }
      // 3. Fn(input, ctx) -> AfterType
      else if constexpr (  //
          requires(Input i, Context& c) {
            { Fn(std::move(i), c) };
          }) {
        if constexpr (last_function) {
          return Fn(std::move(input), ctx);
        } else {
          return Pipeline<FnTail...>::invoke(Fn(std::move(input), ctx), ctx);
        }
      }
      // 3. Fn(input) -> void
      else if constexpr (  //
          requires(Input i) {
            { Fn(std::move(i)) } -> std::same_as<void>;
          }) {
        if constexpr (last_function) {
          return Fn(std::move(input));
        } else {
          return Fn(std::move(input)), Pipeline<FnTail...>::invoke(ctx);
        }
      }
      // 5. Fn(input) -> Result<AfterType, Error>
      else if constexpr (  //
          requires(Input i) {
            { Fn(std::move(i)) } -> detail::HosqResultType;
          }) {
        if constexpr (last_function) {
          return Fn(std::move(input));
        } else {
          return detail::continue_result<FnTail...>(Fn(std::move(input)), ctx);
        }
      }
      // 6. Fn(input) -> AfterType
      else if constexpr (  //
          requires(Input i) {
            { Fn(std::move(i)) };
          }) {
        if constexpr (last_function) {
          return Fn(std::move(input));
        } else {
          return Pipeline<FnTail...>::invoke(Fn(std::move(input)), ctx);
        }
      } else {
        static_assert(
            detail::always_false<Head>,
            "Pipeline step return type mismatch: after_type<Input> is non-void, "
            "so the step must be callable as one of: "
            "Fn(input, ctx) -> after_type<Input>, "
            "Fn(input, ctx) -> Result<after_type<Input>, Error>, "
            "Fn(input) -> after_type<Input>, "
            "Fn(input) -> Result<after_type<Input>, Error>.");
      }
    }.template operator()<Fns...>(std::move(input), ctx);
  };

  /**
   * @brief Invokes this pipeline from an explicit input value using a const
   * context.
   *
   * Steps are matched left-to-right by their call signature. Context-aware
   * forms taking `const Context&` are tried before no-context forms. The
   * return value of each step flows as the input to the next step.
   *
   * Supported step signatures, tried in priority order:
   * - `Fn(input, ctx) -> void`
   * - `Fn(input, ctx) -> Result<Out, Error>`
   * - `Fn(input, ctx) -> Out`
   * - `Fn(input) -> void`
   * - `Fn(input) -> Result<Out, Error>`
   * - `Fn(input) -> Out`
   *
   * `Result` errors are propagated immediately; remaining steps are not
   * invoked. Plain return values are forwarded without error-checking.
   *
   * @tparam Input  Input value type for the first step.
   * @tparam Context Const context type used by context-aware steps.
   * @param input Input value consumed by the first step.
   * @param ctx Const pipeline context.
   * @return The final pipeline result. Plain terminal values are returned as-is.
   * If any earlier step returns `Result<T, Error>`, errors are propagated and
   * the tail result is lifted to `Result<U, Error>`.
   *
   */
  template <class Input, class Context>
  [[nodiscard]]
  HOSQ_FUNCTION_ALWAYS_INLINE static constexpr auto invoke(Input input,
                                                           const Context& ctx) {
    return []<auto FnHead, auto... FnTail>(
               Input input, const Context& ctx) -> decltype(auto) {
      using Head = std::remove_cvref_t<decltype(FnHead)>;
      constexpr auto Fn = FnHead;
      constexpr bool last_function = sizeof...(FnTail) == 0;

      // 1. Fn(input, ctx) -> void
      if constexpr (  //
          requires(Input i, const Context& c) {
            { Fn(std::move(i), c) } -> std::same_as<void>;
          }) {
        if constexpr (last_function) {
          return Fn(std::move(input), ctx);
        } else {
          return Fn(std::move(input), ctx), Pipeline<FnTail...>::invoke(ctx);
        }
      }
      // 2. Fn(input, ctx) -> Result<AfterType, Error>
      else if constexpr (  //
          requires(Input i, const Context& c) {
            { Fn(std::move(i), c) } -> detail::HosqResultType;
          }) {
        if constexpr (last_function) {
          return Fn(std::move(input), ctx);
        } else {
          return detail::continue_result<FnTail...>(Fn(std::move(input), ctx),
                                                    ctx);
        }
      }
      // 3. Fn(input, ctx) -> AfterType
      else if constexpr (  //
          requires(Input i, const Context& c) {
            { Fn(std::move(i), c) };
          }) {
        if constexpr (last_function) {
          return Fn(std::move(input), ctx);
        } else {
          return Pipeline<FnTail...>::invoke(Fn(std::move(input), ctx), ctx);
        }
      }
      // 4. Fn(input) -> void
      else if constexpr (  //
          requires(Input i) {
            { Fn(std::move(i)) } -> std::same_as<void>;
          }) {
        if constexpr (last_function) {
          return Fn(std::move(input));
        } else {
          return Fn(std::move(input)), Pipeline<FnTail...>::invoke(ctx);
        }
      }
      // 5. Fn(input) -> Result<AfterType, Error>
      else if constexpr (  //
          requires(Input i) {
            { Fn(std::move(i)) } -> detail::HosqResultType;
          }) {
        if constexpr (last_function) {
          return Fn(std::move(input));
        } else {
          return detail::continue_result<FnTail...>(Fn(std::move(input)), ctx);
        }
      }
      // 6. Fn(input) -> AfterType
      else if constexpr (  //
          requires(Input i) {
            { Fn(std::move(i)) };
          }) {
        if constexpr (last_function) {
          return Fn(std::move(input));
        } else {
          return Pipeline<FnTail...>::invoke(Fn(std::move(input)), ctx);
        }
      } else {
        static_assert(
            detail::always_false<Head>,
            "Pipeline step return type mismatch: after_type<Input> is non-void, "
            "so the step must be callable as one of: "
            "Fn(input, ctx) -> after_type<Input>, "
            "Fn(input, ctx) -> Result<after_type<Input>, Error>, "
            "Fn(input) -> after_type<Input>, "
            "Fn(input) -> Result<after_type<Input>, Error>.");
      }
    }.template operator()<Fns...>(std::move(input), ctx);
  };

  /**
   * @brief Invokes this pipeline without an explicit input value using a mutable
   * context.
   *
   * Used when the first step takes no data input (input type is effectively
   * `void`). Steps are matched by their call signature; context-aware forms
   * are tried before no-argument forms. The return value of each step flows
   * as the input to the next step.
   *
   * Supported step signatures, tried in priority order:
   * - `Fn(ctx) -> void`
   * - `Fn(ctx) -> Result<Out, Error>`
   * - `Fn(ctx) -> Out`
   * - `Fn() -> void`
   * - `Fn() -> Result<Out, Error>`
   * - `Fn() -> Out`
   *
   * `Result` errors are propagated immediately; remaining steps are not
   * invoked. Plain return values are forwarded without error-checking.
   *
   * @tparam Context Mutable context type used by context-aware steps.
   * @param ctx Mutable pipeline context.
   * @return The final pipeline result. Plain terminal values are returned as-is.
   * If any earlier step returns `Result<T, Error>`, errors are propagated and
   * the tail result is lifted to `Result<U, Error>`.
   */
  template <class Context>
  [[nodiscard]]
  HOSQ_FUNCTION_ALWAYS_INLINE static constexpr auto invoke(Context& ctx) {
    return []<auto FnHead, auto... FnTail>(Context& ctx) -> decltype(auto) {
      using Head = std::remove_cvref_t<decltype(FnHead)>;
      constexpr auto Fn = FnHead;
      constexpr bool last_function = sizeof...(FnTail) == 0;

      // 1. Fn(ctx) -> void
      if constexpr (  //
          requires(Context& c) {
            { Fn(c) } -> std::same_as<void>;
          }) {
        if constexpr (last_function) {
          return Fn(ctx);
        } else {
          return Fn(ctx), Pipeline<FnTail...>::invoke(ctx);
        }
      }
      // 2. Fn(ctx) -> Result<AfterType, Error>
      else if constexpr (  //
          requires(Context& c) {
            { Fn(c) } -> detail::HosqResultType;
          }) {
        if constexpr (last_function) {
          return Fn(ctx);
        } else {
          return detail::continue_result<FnTail...>(Fn(ctx), ctx);
        }
      }
      // 3. Fn(ctx) -> AfterType
      else if constexpr (  //
          requires(Context& c) {
            { Fn(c) };
          }) {
        if constexpr (last_function) {
          return Fn(ctx);
        } else {
          return Pipeline<FnTail...>::invoke(Fn(ctx), ctx);
        }
      }
      // 4. Fn() -> void
      else if constexpr (  //
          requires() {
            { Fn() } -> std::same_as<void>;
          }) {
        if constexpr (last_function) {
          return Fn();
        } else {
          return Fn(), Pipeline<FnTail...>::invoke(ctx);
        }
      }
      // 5. Fn() -> Result<AfterType, Error>
      else if constexpr (  //
          requires() {
            { Fn() } -> detail::HosqResultType;
          }) {
        if constexpr (last_function) {
          return Fn();
        } else {
          return detail::continue_result<FnTail...>(Fn(), ctx);
        }
      }
      // 6. Fn() -> AfterType
      else if constexpr (  //
          requires() {
            { Fn() };
          }) {
        if constexpr (last_function) {
          return Fn();
        } else {
          return Pipeline<FnTail...>::invoke(Fn(), ctx);
        }
      } else {
        static_assert(
            detail::always_false<Head>,
            "Pipeline step return type mismatch: after_type<void> is non-void, "
            "so the step must be callable as one of: "
            "Fn(ctx) -> after_type<void>, "
            "Fn(ctx) -> Result<after_type<void>, Error>, "
            "Fn() -> after_type<void>, "
            "Fn() -> Result<after_type<void>, Error>.");
      }
    }.template operator()<Fns...>(ctx);
  };

  /**
   * @brief Invokes this pipeline without an explicit input value using a const
   * context.
   *
   * Used when the first step takes no data input (input type is effectively
   * `void`). Steps are matched by their call signature; context-aware forms
   * taking `const Context&` are tried before no-argument forms. The return
   * value of each step flows as the input to the next step.
   *
   * Supported step signatures, tried in priority order:
   * - `Fn(ctx) -> void`
   * - `Fn(ctx) -> Result<Out, Error>`
   * - `Fn(ctx) -> Out`
   * - `Fn() -> void`
   * - `Fn() -> Result<Out, Error>`
   * - `Fn() -> Out`
   *
   * `Result` errors are propagated immediately; remaining steps are not
   * invoked. Plain return values are forwarded without error-checking.
   *
   * @tparam Context Const context type used by context-aware steps.
   * @param ctx Const pipeline context.
   * @return The final pipeline result. Plain terminal values are returned as-is.
   * If any earlier step returns `Result<T, Error>`, errors are propagated and
   * the tail result is lifted to `Result<U, Error>`.
   */
  template <class Context>
  [[nodiscard]]
  HOSQ_FUNCTION_ALWAYS_INLINE static constexpr auto invoke(const Context& ctx) {
    return []<auto FnHead, auto... FnTail>(
               const Context& ctx) HOSQ_ALWAYS_INLINE_ATTR -> decltype(auto) {
      using Head = std::remove_cvref_t<decltype(FnHead)>;
      constexpr auto Fn = FnHead;
      constexpr bool last_function = sizeof...(FnTail) == 0;

      // 1. Fn(ctx) -> void
      if constexpr (  //
          requires(const Context& c) {
            { Fn(c) } -> std::same_as<void>;
          }) {
        if constexpr (last_function) {
          return Fn(ctx);
        } else {
          return Fn(ctx), Pipeline<FnTail...>::invoke(ctx);
        }
      }
      // 2. Fn(ctx) -> Result<AfterType, Error>
      else if constexpr (  //
          requires(const Context& c) {
            { Fn(c) } -> detail::HosqResultType;
          }) {
        if constexpr (last_function) {
          return Fn(ctx);
        } else {
          return detail::continue_result<FnTail...>(Fn(ctx), ctx);
        }
      }
      // 3. Fn(ctx) -> AfterType
      else if constexpr (  //
          requires(const Context& c) {
            { Fn(c) };
          }) {
        if constexpr (last_function) {
          return Fn(ctx);
        } else {
          return Pipeline<FnTail...>::invoke(Fn(ctx), ctx);
        }
      }
      // 4. Fn() -> void
      else if constexpr (  //
          requires() {
            { Fn() } -> std::same_as<void>;
          }) {
        if constexpr (last_function) {
          return Fn();
        } else {
          return Fn(), Pipeline<FnTail...>::invoke(ctx);
        }
      }
      // 5. Fn() -> Result<AfterType, Error>
      else if constexpr (  //
          requires() {
            { Fn() } -> detail::HosqResultType;
          }) {
        if constexpr (last_function) {
          return Fn();
        } else {
          return detail::continue_result<FnTail...>(Fn(), ctx);
        }
      }
      // 6. Fn() -> AfterType
      else if constexpr (  //
          requires() {
            { Fn() };
          }) {
        if constexpr (last_function) {
          return Fn();
        } else {
          return Pipeline<FnTail...>::invoke(Fn(), ctx);
        }
      } else {
        static_assert(
            detail::always_false<Head>,
            "Pipeline step return type mismatch: after_type<void> is non-void, "
            "so the step must be callable as one of: "
            "Fn(ctx) -> after_type<void>, "
            "Fn(ctx) -> Result<after_type<void>, Error>, "
            "Fn() -> after_type<void>, "
            "Fn() -> Result<after_type<void>, Error>.");
      }
    }.template operator()<Fns...>(ctx);
  };
};

/// Convenience variable template that wraps a default-constructed `T` as a
/// single-step `Pipeline<T{}>`.
template <class T>
inline constexpr auto step =
    []() HOSQ_ALWAYS_INLINE_ATTR -> auto { return Pipeline<T{}>{}; }();

/// Concatenates two pipelines into a single `Pipeline` containing all steps of
/// `Lhs` followed by `Rhs`.
template <auto... Lhs, auto... Rhs>
constexpr auto operator|(Pipeline<Lhs...>, Pipeline<Rhs...>) {
  return Pipeline<Lhs..., Rhs...>{};
}

namespace detail {

template <class T>
struct IsPipeline : std::false_type {};

template <auto... Fns>
struct IsPipeline<Pipeline<Fns...>> : std::true_type {};

template <class T>
concept PipelineObject = IsPipeline<std::remove_cvref_t<T>>::value;

}  // namespace detail

}  // namespace hosq

#undef HOSQ_ALWAYS_INLINE_ATTR
#undef HOSQ_FUNCTION_ALWAYS_INLINE
