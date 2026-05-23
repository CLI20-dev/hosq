#pragma once

#include <algorithm>
#include <cctype>
#include <concepts>
#include <cstddef>
#include <type_traits>
#include <utility>

#include "hosq/detail/always_false.hh"
#include "hosq/detail/pipeline_analysis.hh"

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

/**
 * @brief A compile-time pipeline of constexpr action objects applied
 * sequentially.
 *
 * Each step receives the output of the previous step as its input.
 * Steps may return a plain value, a `MatchResult`, or `void`.
 *
 * Plain values are forwarded directly to the next pipeline step.
 * Terminal plain values are preserved as the pipeline result.
 *
 * If a step returns a `MatchResult` that is empty (mismatch), the pipeline
 * short-circuits and returns `false` immediately. If the result holds a value
 * (match), the inner value is unwrapped and forwarded to the next step.
 * A terminal `MatchResult` step yields `bool`.
 *
 * Pipelines do not propagate hard errors through return values.
 * Instead, pipeline steps may report hard errors through the decoding
 * context or reader state. Once a reader enters the error state,
 * subsequent operations are required to remain safe and non-throwing.
 *
 * This allows hot decoding paths to avoid per-step error branching while
 * still preserving rich error information at the top-level decoder API.
 *
 * Supported step signatures are matched left-to-right:
 *
 * - `Fn(input, ctx) -> void`
 * - `Fn(input, ctx) -> MatchResult`
 * - `Fn(input, ctx) -> Out`
 * - `Fn(input) -> void`
 * - `Fn(input) -> MatchResult`
 * - `Fn(input) -> Out`
 *
 * For pipelines without an explicit input value:
 *
 * - `Fn(ctx) -> void`
 * - `Fn(ctx) -> MatchResult`
 * - `Fn(ctx) -> Out`
 * - `Fn() -> void`
 * - `Fn() -> MatchResult`
 * - `Fn() -> Out`
 *
 * **Decoder role** (validated by `analyze_decoder()`):
 * exactly one step across the pipeline must declare `storage_type<Input>`.
 * That type becomes the decoder's storage type; it need not be the terminal
 * step.
 *
 * **Encoder role** (validated by `analyze_encoder()`):
 * no step may declare `storage_type<Input>`.
 *
 * @tparam Fns Pack of constexpr action objects forming the pipeline.
 */
template <auto... Fns>
  requires(sizeof...(Fns) > 0)
struct Pipeline {
  template <class Context, class Input>
  static consteval auto callable_with_const_context() -> bool {
    return detail::pipeline_callable_with_const_context_v<Pipeline, Context,
                                                          Input>;
  }

  template <class Context, class Input>
  static consteval auto callable_with_mutable_context() -> bool {
    return detail::pipeline_callable_with_mutable_context_v<Pipeline, Context,
                                                            Input>;
  }

  template <class Input, class Context>
  using info =
      detail::pipeline_analysis_t<detail::PipelineVerificationMode::none,
                                  Context, Input, Pipeline<Fns...>>;

  template <class Input, class Context>
  using final_output_type = detail::pipeline_terminal_t<info<Input, Context>>;

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
   * - `Fn(input, ctx) -> MatchResult`
   * - `Fn(input, ctx) -> Out`
   * - `Fn(input) -> void`
   * - `Fn(input) -> MatchResult`
   * - `Fn(input) -> Out`
   *
   * Plain return values are forwarded directly. If a step returns `void`,
   * the next step is invoked without an explicit input value. If a step
   * returns a `MatchResult` that is empty, the pipeline short-circuits and
   * returns `false`; otherwise the inner value is unwrapped and forwarded.
   *
   * Pipeline invocation does not propagate hard errors through return values.
   * Steps that encounter hard errors should report them through the context
   * or reader state. The top-level decoder is responsible for checking that
   * error state after pipeline execution.
   *
   * @tparam Input Input value type for the first step.
   * @tparam Context Mutable context type used by context-aware steps.
   * @param input Input value consumed by the first step.
   * @param ctx Mutable pipeline context.
   * @return The final pipeline result. Terminal plain values are returned
   *         as-is; a terminal `MatchResult` step yields `bool`.
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
          using Pipe = Pipeline<FnTail...>;
          return Fn(std::move(input), ctx), Pipe::invoke(ctx);
        }
      }
      // 2. Fn(input, ctx) -> MatchResult
      else if constexpr (  //
          requires(Input i, Context& c) {
            { Fn(std::move(i), c) } -> detail::MatchResultT;
          }) {
        if constexpr (last_function) {
          return bool(Fn(std::move(input), ctx));
        } else {
          using Pipe = Pipeline<FnTail...>;
          auto ret = Fn(std::move(input), ctx);
          if (!ret) {
            return false;
          }
          if constexpr (  //
              std::same_as<decltype(Pipe::invoke(std::move(ret).value(), ctx)), void>) {
            Pipe::invoke(std::move(ret).value(), ctx);
            return true;
          } else {
            return Pipe::invoke(std::move(ret).value(), ctx);
          }
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
          using Pipe = Pipeline<FnTail...>;
          return Pipe::invoke(Fn(std::move(input), ctx), ctx);
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
          using Pipe = Pipeline<FnTail...>;
          return Fn(std::move(input)), Pipe::invoke(ctx);
        }
      }
      // 5. Fn(input) -> MatchResult
      else if constexpr (  //
          requires(Input i) {
            { Fn(std::move(i)) } -> detail::MatchResultT;
          }) {
        if constexpr (last_function) {
          return bool(Fn(std::move(input)));
        } else {
          using Pipe = Pipeline<FnTail...>;
          auto ret = Fn(std::move(input));
          if (!ret) {
            return false;
          }
          if constexpr (  //
              std::same_as<decltype(Pipe::invoke(std::move(ret).value(), ctx)), void>) {
            Pipe::invoke(std::move(ret).value(), ctx);
            return true;
          } else {
            return Pipe::invoke(std::move(ret).value(), ctx);
          }
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
          using Pipe = Pipeline<FnTail...>;
          return Pipe::invoke(Fn(std::move(input)), ctx);
        }
      } else {
        static_assert(
            detail::always_false<Head>,
            "Pipeline step return type mismatch: after_type<Input> is non-void, "
            "so the step must be callable as one of: "
            "Fn(input, ctx) -> after_type<Input>, "
            "Fn(input) -> after_type<Input>, ");
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
   * - `Fn(input, ctx) -> MatchResult`
   * - `Fn(input, ctx) -> Out`
   * - `Fn(input) -> void`
   * - `Fn(input) -> MatchResult`
   * - `Fn(input) -> Out`
   *
   * Plain return values are forwarded directly. If a step returns `void`,
   * the next step is invoked without an explicit input value. If a step
   * returns a `MatchResult` that is empty, the pipeline short-circuits and
   * returns `false`; otherwise the inner value is unwrapped and forwarded.
   *
   * Pipeline invocation does not propagate hard errors through return values.
   * Steps that encounter hard errors should report them through the context
   * or reader state. The top-level decoder is responsible for checking that
   * error state after pipeline execution.
   *
   * @tparam Input Input value type for the first step.
   * @tparam Context Const context type used by context-aware steps.
   * @param input Input value consumed by the first step.
   * @param ctx Const pipeline context.
   * @return The final pipeline result. Terminal plain values are returned
   *         as-is; a terminal `MatchResult` step yields `bool`.
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
          using Pipe = Pipeline<FnTail...>;
          return Fn(std::move(input), ctx), Pipe::invoke(ctx);
        }
      }
      // 2. Fn(input, ctx) -> MatchResult
      else if constexpr (  //
          requires(Input i, const Context& c) {
            { Fn(std::move(i), c) } -> detail::MatchResultT;
          }) {
        if constexpr (last_function) {
          return bool(Fn(std::move(input), ctx));
        } else {
          using Pipe = Pipeline<FnTail...>;
          auto ret = Fn(std::move(input), ctx);
          if (!ret) {
            return false;
          }
          if constexpr (  //
              std::same_as<decltype(Pipe::invoke(std::move(ret).value(), ctx)), void>) {
            Pipe::invoke(std::move(ret).value(), ctx);
            return true;
          } else {
            return Pipe::invoke(std::move(ret).value(), ctx);
          }
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
          using Pipe = Pipeline<FnTail...>;
          return Pipe::invoke(Fn(std::move(input), ctx), ctx);
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
          using Pipe = Pipeline<FnTail...>;
          return Fn(std::move(input)), Pipe::invoke(ctx);
        }
      }
      // 5. Fn(input) -> MatchResult
      else if constexpr (  //
          requires(Input i) {
            { Fn(std::move(i)) } -> detail::MatchResultT;
          }) {
        if constexpr (last_function) {
          return bool(Fn(std::move(input)));
        } else {
          using Pipe = Pipeline<FnTail...>;
          auto ret = Fn(std::move(input));
          if (!ret) {
            return false;
          }
          if constexpr (  //
              std::same_as<decltype(Pipe::invoke(std::move(ret).value(), ctx)), void>) {
            Pipe::invoke(std::move(ret).value(), ctx);
            return true;
          } else {
            return Pipe::invoke(std::move(ret).value(), ctx);
          }
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
          using Pipe = Pipeline<FnTail...>;
          return Pipe::invoke(Fn(std::move(input)), ctx);
        }
      } else {
        static_assert(
            detail::always_false<Head>,
            "Pipeline step return type mismatch: after_type<Input> is non-void, "
            "so the step must be callable as one of: "
            "Fn(input, ctx) -> after_type<Input>, "
            "Fn(input) -> after_type<Input>, ");
      }
    }.template operator()<Fns...>(std::move(input), ctx);
  };

  /**
   * @brief Invokes this pipeline without an explicit input value using a mutable
   * context.
   *
   * Used when the first step takes no data input. Steps are matched by their
   * call signature; context-aware forms are tried before no-argument forms.
   * The return value of each step flows as the input to the next step.
   *
   * Supported step signatures, tried in priority order:
   * - `Fn(ctx) -> void`
   * - `Fn(ctx) -> MatchResult`
   * - `Fn(ctx) -> Out`
   * - `Fn() -> void`
   * - `Fn() -> MatchResult`
   * - `Fn() -> Out`
   *
   * Plain return values are forwarded directly. If a step returns `void`,
   * the next step is invoked without an explicit input value. If a step
   * returns a `MatchResult` that is empty, the pipeline short-circuits and
   * returns `false`; otherwise the inner value is unwrapped and forwarded.
   *
   * Pipeline invocation does not propagate hard errors through return values.
   * Steps that encounter hard errors should report them through the context
   * or reader state. The top-level decoder is responsible for checking that
   * error state after pipeline execution.
   *
   * @tparam Context Mutable context type used by context-aware steps.
   * @param ctx Mutable pipeline context.
   * @return The final pipeline result. Terminal plain values are returned
   *         as-is; a terminal `MatchResult` step yields `bool`.
   */
  template <class Context>
  [[nodiscard]]
  HOSQ_FUNCTION_ALWAYS_INLINE static constexpr auto invoke(Context& ctx) {
    return []<auto FnHead, auto... FnTail>(Context& ctx) -> decltype(auto) {
      using Head = std::remove_cvref_t<decltype(FnHead)>;
      constexpr auto Fn = FnHead;
      constexpr bool last_function = sizeof...(FnTail) == 0;

      // 1. Fn(input, ctx) -> void
      if constexpr (  //
          requires(Context& c) {
            { Fn(c) } -> std::same_as<void>;
          }) {
        if constexpr (last_function) {
          return Fn(ctx);
        } else {
          using Pipe = Pipeline<FnTail...>;
          return Fn(ctx), Pipe::invoke(ctx);
        }
      }
      // 2. Fn(input, ctx) -> MatchResult
      else if constexpr (  //
          requires(Context& c) {
            { Fn(c) } -> detail::MatchResultT;
          }) {
        if constexpr (last_function) {
          return bool(Fn(ctx));
        } else {
          using Pipe = Pipeline<FnTail...>;
          auto ret = Fn(ctx);
          if (!ret) {
            return false;
          }
          if constexpr (  //
              std::same_as<decltype(Pipe::invoke(std::move(ret).value(), ctx)), void>) {
            Pipe::invoke(std::move(ret).value(), ctx);
            return true;
          } else {
            return Pipe::invoke(std::move(ret).value(), ctx);
          }
        }
      }
      // 3. Fn(input, ctx) -> AfterType
      else if constexpr (  //
          requires(Context& c) {
            { Fn(c) };
          }) {
        if constexpr (last_function) {
          return Fn(ctx);
        } else {
          using Pipe = Pipeline<FnTail...>;
          return Pipe::invoke(Fn(ctx), ctx);
        }
      }
      // 4. Fn(input) -> void
      else if constexpr (  //
          requires() {
            { Fn() } -> std::same_as<void>;
          }) {
        if constexpr (last_function) {
          return Fn();
        } else {
          using Pipe = Pipeline<FnTail...>;
          return Fn(), Pipe::invoke(ctx);
        }
      }
      // 5. Fn(input) -> MatchResult
      else if constexpr (  //
          requires() {
            { Fn() } -> detail::MatchResultT;
          }) {
        if constexpr (last_function) {
          return bool(Fn());
        } else {
          using Pipe = Pipeline<FnTail...>;
          auto ret = Fn();
          if (!ret) {
            return false;
          }
          if constexpr (  //
              std::same_as<decltype(Pipe::invoke(std::move(ret).value(), ctx)), void>) {
            Pipe::invoke(std::move(ret).value(), ctx);
            return true;
          } else {
            return Pipe::invoke(std::move(ret).value(), ctx);
          }
        }
      }
      // 6. Fn(input) -> AfterType
      else if constexpr (  //
          requires() {
            { Fn() };
          }) {
        if constexpr (last_function) {
          return Fn();
        } else {
          using Pipe = Pipeline<FnTail...>;
          return Pipe::invoke(Fn(), ctx);
        }
      } else {
        static_assert(
            detail::always_false<Head>,
            "Pipeline step return type mismatch: after_type<Input> is non-void, "
            "so the step must be callable as one of: "
            "Fn(input, ctx) -> after_type<Input>, "
            "Fn(input) -> after_type<Input>, ");
      }
    }.template operator()<Fns...>(ctx);
  };

  /**
   * @brief Invokes this pipeline without an explicit input value using a const
   * context.
   *
   * Used when the pipeline is invoked without an explicit input value.
   * Steps are matched by their call signature; context-aware forms taking
   * `const Context&` are tried before no-argument forms. The return value of
   * each step flows as the input to the next step.
   *
   * Supported step signatures, tried in priority order:
   * - `Fn(ctx) -> void`
   * - `Fn(ctx) -> MatchResult`
   * - `Fn(ctx) -> Out`
   * - `Fn() -> void`
   * - `Fn() -> MatchResult`
   * - `Fn() -> Out`
   *
   * Plain return values are forwarded directly. If a step returns `void`,
   * the next step is invoked without an explicit input value. If a step
   * returns a `MatchResult` that is empty, the pipeline short-circuits and
   * returns `false`; otherwise the inner value is unwrapped and forwarded.
   *
   * Pipeline invocation does not propagate hard errors through return values.
   * Steps that encounter hard errors should report them through the context
   * or reader state. The top-level decoder is responsible for checking the
   * final reader error state after pipeline execution.
   *
   * @tparam Context Const context type used by context-aware steps.
   * @param ctx Const pipeline context.
   * @return The final pipeline result. Terminal plain values are returned
   *         as-is; a terminal `MatchResult` step yields `bool`.
   */
  template <class Context>
  [[nodiscard]]
  HOSQ_FUNCTION_ALWAYS_INLINE static constexpr auto invoke(const Context& ctx) {
    return []<auto FnHead, auto... FnTail>(
               const Context& ctx) -> decltype(auto) {
      using Head = std::remove_cvref_t<decltype(FnHead)>;
      constexpr auto Fn = FnHead;
      constexpr bool last_function = sizeof...(FnTail) == 0;

      // 1. Fn(input, ctx) -> void
      if constexpr (  //
          requires(const Context& c) {
            { Fn(c) } -> std::same_as<void>;
          }) {
        if constexpr (last_function) {
          return Fn(ctx);
        } else {
          using Pipe = Pipeline<FnTail...>;
          return Fn(ctx), Pipe::invoke(ctx);
        }
      }
      // 2. Fn(input, ctx) -> MatchResult
      else if constexpr (  //
          requires(const Context& c) {
            { Fn(c) } -> detail::MatchResultT;
          }) {
        if constexpr (last_function) {
          return bool(Fn(ctx));
        } else {
          using Pipe = Pipeline<FnTail...>;
          auto ret = Fn(ctx);
          if (!ret) {
            return false;
          }
          if constexpr (  //
              std::same_as<decltype(Pipe::invoke(std::move(ret).value(), ctx)), void>) {
            Pipe::invoke(std::move(ret).value(), ctx);
            return true;
          } else {
            return Pipe::invoke(std::move(ret).value(), ctx);
          }
        }
      }
      // 3. Fn(input, ctx) -> AfterType
      else if constexpr (  //
          requires(const Context& c) {
            { Fn(c) };
          }) {
        if constexpr (last_function) {
          return Fn(ctx);
        } else {
          using Pipe = Pipeline<FnTail...>;
          return Pipe::invoke(Fn(ctx), ctx);
        }
      }
      // 4. Fn(input) -> void
      else if constexpr (  //
          requires() {
            { Fn() } -> std::same_as<void>;
          }) {
        if constexpr (last_function) {
          return Fn();
        } else {
          using Pipe = Pipeline<FnTail...>;
          return Fn(), Pipe::invoke(ctx);
        }
      }
      // 5. Fn(input) -> MatchResult
      else if constexpr (  //
          requires() {
            { Fn() } -> detail::MatchResultT;
          }) {
        if constexpr (last_function) {
          return bool(Fn());
        } else {
          using Pipe = Pipeline<FnTail...>;
          auto ret = Fn();
          if (!ret) {
            return false;
          }
          if constexpr (  //
              std::same_as<decltype(Pipe::invoke(std::move(ret).value(), ctx)), void>) {
            Pipe::invoke(std::move(ret).value(), ctx);
            return true;
          } else {
            return Pipe::invoke(std::move(ret).value(), ctx);
          }
        }
      }
      // 6. Fn(input) -> AfterType
      else if constexpr (  //
          requires() {
            { Fn() };
          }) {
        if constexpr (last_function) {
          return Fn();
        } else {
          using Pipe = Pipeline<FnTail...>;
          return Pipe::invoke(Fn(), ctx);
        }
      } else {
        static_assert(
            detail::always_false<Head>,
            "Pipeline step return type mismatch: after_type<Input> is non-void, "
            "so the step must be callable as one of: "
            "Fn(input, ctx) -> after_type<Input>, "
            "Fn(input) -> after_type<Input>, ");
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
