#pragma once

#include <algorithm>
#include <cctype>
#include <concepts>
#include <cstddef>
#include <tuple>
#include <type_traits>
#include <utility>

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

template <class Input>
struct MissingAcceptsInput;

template <class Head, class Input>
struct MissingAfterType;

template <class Head, class Input>
struct MissingStorageType;

template <class Head, class Input>
struct PipelineStepRejectedInput;

template <class ExistingStorage, class Head, class Input>
struct DuplicateStorageType;

template <class Head, class Input>
struct UnexpectedStorageType;

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

struct NoStorage {};

template <class Head, class Input, class Storage, bool HasStorage>
struct NextStorage {
  using type = Storage;
};

template <class Head, class Input, class Storage>
struct NextStorage<Head, Input, Storage, true> {
  using type = typename Head::template storage_type<Input>;
};

template <auto Fn>
consteval auto require_pipeline_callable() -> void {
  using FnType = std::remove_cvref_t<decltype(Fn)>;
  require_step_impl<FnType>();
}

enum class PipelineMode {
  decoder,
  encoder,
};

template <auto Fn>
concept PipelineCallable = detail::HasAcceptsInput<decltype(Fn)>;

template <class Head>
static constexpr bool always_false = false;

}  // namespace detail

template <auto... Fns>
  requires(sizeof...(Fns) > 0 && (detail::PipelineCallable<Fns> && ...))
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

template <class Context, auto... FnTail>
static constexpr auto continue_void_result(Result<void, Error> next,
                                           Context& ctx) {
  if (!next) [[unlikely]] {
    using TailResult = detail::tail_result_t<void, Context, FnTail...>;
    return TailResult{unexpected(std::move(next).error())};
  }
  return Pipeline<FnTail...>::invoke(ctx);
}

template <class Context, auto... FnTail>
static constexpr auto continue_void_result(Result<void, Error> next,
                                           const Context& ctx) {
  if (!next) [[unlikely]] {
    using TailResult = detail::tail_result_t<void, const Context, FnTail...>;
    return TailResult{unexpected(std::move(next).error())};
  }
  return Pipeline<FnTail...>::invoke(ctx);
}

template <class Output, class Context, auto... FnTail>
static constexpr auto continue_result(Result<Output, Error> next, Context& ctx) {
  if (!next) [[unlikely]] {
    using TailResult = detail::tail_result_t<Output, Context, FnTail...>;
    return TailResult{unexpected(std::move(next).error())};
  }
  return Pipeline<FnTail...>::invoke(std::move(*next), ctx);
}

template <class Output, class Context, auto... FnTail>
static constexpr auto continue_result(Result<Output, Error> next,
                                      const Context& ctx) {
  if (!next) [[unlikely]] {
    using TailResult = detail::tail_result_t<Output, const Context, FnTail...>;
    return TailResult{unexpected(std::move(next).error())};
  }
  return Pipeline<FnTail...>::invoke(std::move(*next), ctx);
}

}  // namespace detail

/**
 * @brief A compile-time pipeline of action functions applied sequentially.
 *
 * Each step must expose:
 * - `template<class I> static constexpr bool accepts_input`
 * - `template<class I> using after_type`
 *
 * Decoder pipelines must define `storage_type<I>` exactly once across the
 * whole pipeline. The step that defines it determines the decoder storage type;
 * it does not have to be the terminal step.
 *
 * Encoder pipelines must not define `storage_type<I>`.
 *
 * At invocation, steps may return either a plain value, `void`,
 * `Result<T, Error>`, or `Result<void, Error>`. `Result` errors are propagated
 * immediately and remaining steps are not invoked.
 *
 * @tparam Fns Pack of constexpr action objects forming the pipeline.
 */
template <auto... Fns>
  requires(sizeof...(Fns) > 0 && (detail::PipelineCallable<Fns> && ...))
struct Pipeline {
  /**
   * @brief Invokes this pipeline from an explicit input value using a mutable
   * context.
   *
   * Each step is dispatched according to its declared
   * `after_type<Input>` and the call form it supports. Context-aware overloads
   * are preferred over input-only overloads.
   *
   * Supported step signatures are:
   * - `Fn(input, ctx) -> void`
   * - `Fn(input, ctx) -> Result<void, Error>`
   * - `Fn(input) -> void`
   * - `Fn(input) -> Result<void, Error>`
   * - `Fn(input, ctx) -> after_type<Input>`
   * - `Fn(input, ctx) -> Result<after_type<Input>, Error>`
   * - `Fn(input) -> after_type<Input>`
   * - `Fn(input) -> Result<after_type<Input>, Error>`
   *
   * If a step returns `Result<..., Error>`, errors are propagated immediately
   * and the remaining steps are not invoked. Plain return values are forwarded
   * to the next step without error checking. A terminal plain value is
   * normalized to `Result<value_type, Error>`, and a terminal `void` return is
   * normalized to `Result<void, Error>`.
   *
   * @tparam Input Input value type for the first step.
   * @tparam Context Mutable context type used by context-aware steps.
   * @param input Input value consumed by the first step.
   * @param ctx Mutable pipeline context.
   * @return The final pipeline result as `Result<Final, Error>`.
   */
  template <class Input, class Context>
  [[nodiscard]]
  static constexpr auto invoke(Input input, Context& ctx) {
    return []<auto FnHead, auto... FnTail>(Input input,
                                           Context& ctx) -> decltype(auto) {
      using Head = std::remove_cvref_t<decltype(FnHead)>;
      using After = typename Head::template after_type<Input>;
      constexpr auto Fn = FnHead;
      constexpr bool last_function = sizeof...(FnTail) == 0;

      // 1. Fn(input, ctx) -> void
      if constexpr (  //
          requires(Input i, Context& c) {
            { Fn(std::move(i), c) } -> std::same_as<void>;
          } && std::same_as<After, void>) {
        if constexpr (last_function) {
          return Fn(std::move(input), ctx), Result<void, Error>{};
        } else {
          return Fn(std::move(input), ctx), Pipeline<FnTail...>::invoke(ctx);
        }
      }
      // 2. Fn(input, ctx) -> Result<void, Error>
      else if constexpr (  //
          requires(Input i, Context& c) {
            { Fn(std::move(i), c) } -> std::same_as<Result<void, Error>>;
          } && std::same_as<After, void>) {
        if constexpr (last_function) {
          return Fn(std::move(input), ctx);
        } else {
          return detail::continue_void_result<Context, FnTail...>(
              Fn(std::move(input), ctx), ctx);
        }
      }
      // 3. Fn(input) -> void
      else if constexpr (  //
          requires(Input i) {
            { Fn(std::move(i)) } -> std::same_as<void>;
          } && std::same_as<After, void>) {
        if constexpr (last_function) {
          return Fn(std::move(input)), Result<void, Error>{};
        } else {
          return Fn(std::move(input)), Pipeline<FnTail...>::invoke(ctx);
        }
      }
      // 4. Fn(input) -> Result<void, Error>
      else if constexpr (  //
          requires(Input i) {
            { Fn(std::move(i)) } -> std::same_as<Result<void, Error>>;
          } && std::same_as<After, void>) {
        if constexpr (last_function) {
          return Fn(std::move(input));
        } else {
          return detail::continue_void_result<Context, FnTail...>(
              Fn(std::move(input)), ctx);
        }
      }
      // 5. Fn(input, ctx) -> AfterType
      else if constexpr (  //
          requires(Input i, Context& c) {
            { Fn(std::move(i), c) } -> std::same_as<After>;
          } && !std::same_as<After, void>) {
        if constexpr (last_function) {
          return Result<After, Error>{Fn(std::move(input), ctx)};
        } else {
          return Pipeline<FnTail...>::invoke(Fn(std::move(input), ctx), ctx);
        }
      }
      // 6. Fn(input, ctx) -> Result<AfterType, Error>
      else if constexpr (  //
          requires(Input i, Context& c) {
            { Fn(std::move(i), c) } -> std::same_as<Result<After, Error>>;
          } && !std::same_as<After, void>) {
        if constexpr (last_function) {
          return Fn(std::move(input), ctx);
        } else {
          return detail::continue_result<After, Context, FnTail...>(
              Fn(std::move(input), ctx), ctx);
        }
      }
      // 7. Fn(input) -> AfterType
      else if constexpr (  //
          requires(Input i) {
            { Fn(std::move(i)) } -> std::same_as<After>;
          } && !std::same_as<After, void>) {
        if constexpr (last_function) {
          return Result<After, Error>{Fn(std::move(input))};
        } else {
          return Pipeline<FnTail...>::invoke(Fn(std::move(input)), ctx);
        }
      }
      // 8. Fn(input) -> Result<AfterType, Error>
      else if constexpr (  //
          requires(Input i) {
            { Fn(std::move(i)) } -> std::same_as<Result<After, Error>>;
          } && !std::same_as<After, void>) {
        if constexpr (last_function) {
          return Fn(std::move(input));
        } else {
          return detail::continue_result<After, Context, FnTail...>(
              Fn(std::move(input)), ctx);
        }
      } else if constexpr (std::same_as<After, void>) {
        static_assert(
            detail::always_false<Head>,
            "Pipeline step return type mismatch: after_type<Input> is void, "
            "so the step must be callable as one of: "
            "Fn(input, ctx) -> void, "
            "Fn(input, ctx) -> Result<void, Error>, "
            "Fn(input) -> void, "
            "Fn(input) -> Result<void, Error>.");
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
   * Each step is dispatched according to its declared `after_type<Input>`.
   * Context-aware overloads taking `const Context&` are preferred over
   * input-only overloads.
   *
   * Supported step signatures are:
   * - `Fn(input, ctx) -> void`
   * - `Fn(input, ctx) -> Result<void, Error>`
   * - `Fn(input) -> void`
   * - `Fn(input) -> Result<void, Error>`
   * - `Fn(input, ctx) -> after_type<Input>`
   * - `Fn(input, ctx) -> Result<after_type<Input>, Error>`
   * - `Fn(input) -> after_type<Input>`
   * - `Fn(input) -> Result<after_type<Input>, Error>`
   *
   * If a step returns `Result<..., Error>`, errors are propagated immediately
   * and the remaining steps are not invoked. Plain return values are forwarded
   * to the next step without error checking. A terminal plain value is
   * normalized to `Result<value_type, Error>`, and a terminal `void` return is
   * normalized to `Result<void, Error>`.
   *
   * @tparam Input Input value type for the first step.
   * @tparam Context Const context type used by context-aware steps.
   * @param input Input value consumed by the first step.
   * @param ctx Const pipeline context.
   * @return The final pipeline result as `Result<Final, Error>`.
   */
  template <class Input, class Context>
  [[nodiscard]]
  static constexpr auto invoke(Input input, const Context& ctx) {
    return []<auto FnHead, auto... FnTail>(
               Input input, const Context& ctx) -> decltype(auto) {
      using Head = std::remove_cvref_t<decltype(FnHead)>;
      using After = typename Head::template after_type<Input>;
      constexpr auto Fn = FnHead;
      constexpr bool last_function = sizeof...(FnTail) == 0;

      // 1. Fn(input, ctx) -> void
      if constexpr (  //
          requires(Input i, const Context& c) {
            { Fn(std::move(i), c) } -> std::same_as<void>;
          } && std::same_as<After, void>) {
        if constexpr (last_function) {
          return Fn(std::move(input), ctx), Result<void, Error>{};
        } else {
          return Fn(std::move(input), ctx), Pipeline<FnTail...>::invoke(ctx);
        }
      }
      // 2. Fn(input, ctx) -> Result<void, Error>
      else if constexpr (  //
          requires(Input i, const Context& c) {
            { Fn(std::move(i), c) } -> std::same_as<Result<void, Error>>;
          } && std::same_as<After, void>) {
        if constexpr (last_function) {
          return Fn(std::move(input), ctx);
        } else {
          return detail::continue_void_result<const Context, FnTail...>(
              Fn(std::move(input), ctx), ctx);
        }
      }
      // 3. Fn(input) -> void
      else if constexpr (  //
          requires(Input i) {
            { Fn(std::move(i)) } -> std::same_as<void>;
          } && std::same_as<After, void>) {
        if constexpr (last_function) {
          return Fn(std::move(input)), Result<void, Error>{};
        } else {
          return Fn(std::move(input)), Pipeline<FnTail...>::invoke(ctx);
        }
      }
      // 4. Fn(input) -> Result<void, Error>
      else if constexpr (  //
          requires(Input i) {
            { Fn(std::move(i)) } -> std::same_as<Result<void, Error>>;
          } && std::same_as<After, void>) {
        if constexpr (last_function) {
          return Fn(std::move(input));
        } else {
          return detail::continue_void_result<const Context, FnTail...>(
              Fn(std::move(input)), ctx);
        }
      }
      // 5. Fn(input, ctx) -> AfterType
      else if constexpr (  //
          requires(Input i, const Context& c) {
            { Fn(std::move(i), c) } -> std::same_as<After>;
          } && !std::same_as<After, void>) {
        if constexpr (last_function) {
          return Result<After, Error>{Fn(std::move(input), ctx)};
        } else {
          return Pipeline<FnTail...>::invoke(Fn(std::move(input), ctx), ctx);
        }
      }
      // 6. Fn(input, ctx) -> Result<AfterType, Error>
      else if constexpr (  //
          requires(Input i, const Context& c) {
            { Fn(std::move(i), c) } -> std::same_as<Result<After, Error>>;
          } && !std::same_as<After, void>) {
        if constexpr (last_function) {
          return Fn(std::move(input), ctx);
        } else {
          return detail::continue_result<After, Context, FnTail...>(
              Fn(std::move(input), ctx), ctx);
        }
      }
      // 7. Fn(input) -> AfterType
      else if constexpr (  //
          requires(Input i) {
            { Fn(std::move(i)) } -> std::same_as<After>;
          } && !std::same_as<After, void>) {
        if constexpr (last_function) {
          return Result<After, Error>{Fn(std::move(input))};
        } else {
          return Pipeline<FnTail...>::invoke(Fn(std::move(input)), ctx);
        }
      }
      // 8. Fn(input) -> Result<AfterType, Error>
      else if constexpr (  //
          requires(Input i) {
            { Fn(std::move(i)) } -> std::same_as<Result<After, Error>>;
          } && !std::same_as<After, void>) {
        if constexpr (last_function) {
          return Fn(std::move(input));
        } else {
          return detail::continue_result<After, Context, FnTail...>(
              Fn(std::move(input)), ctx);
        }
      } else if constexpr (std::same_as<After, void>) {
        static_assert(
            detail::always_false<Head>,
            "Pipeline step return type mismatch: after_type<Input> is void, "
            "so the step must be callable as one of: "
            "Fn(input, ctx) -> void, "
            "Fn(input, ctx) -> Result<void, Error>, "
            "Fn(input) -> void, "
            "Fn(input) -> Result<void, Error>.");
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
   * This overload is used when the first step's input type is `void`. Each step
   * is dispatched according to its declared `after_type<void>` or the previous
   * step's `after_type`. Context-aware overloads are preferred over no-argument
   * overloads.
   *
   * Supported first-step signatures are:
   * - `Fn(ctx) -> void`
   * - `Fn(ctx) -> Result<void, Error>`
   * - `Fn() -> void`
   * - `Fn() -> Result<void, Error>`
   * - `Fn(ctx) -> after_type<void>`
   * - `Fn(ctx) -> Result<after_type<void>, Error>`
   * - `Fn() -> after_type<void>`
   * - `Fn() -> Result<after_type<void>, Error>`
   *
   * If a step returns `Result<..., Error>`, errors are propagated immediately
   * and the remaining steps are not invoked. Plain return values are forwarded
   * to the next step without error checking. A terminal plain value is
   * normalized to `Result<value_type, Error>`, and a terminal `void` return is
   * normalized to `Result<void, Error>`.
   *
   * @tparam Context Mutable context type used by context-aware steps.
   * @param ctx Mutable pipeline context.
   * @return The final pipeline result as `Result<Final, Error>`.
   */
  template <class Context>
  [[nodiscard]]
  static constexpr auto invoke(Context& ctx) {
    return []<auto FnHead, auto... FnTail>(Context& ctx) -> decltype(auto) {
      using Head = std::remove_cvref_t<decltype(FnHead)>;
      using After = typename Head::template after_type<void>;
      constexpr auto Fn = FnHead;
      constexpr bool last_function = sizeof...(FnTail) == 0;

      // 1. Fn(ctx) -> void
      if constexpr (  //
          requires(Context& c) {
            { Fn(c) } -> std::same_as<void>;
          } && std::same_as<After, void>) {
        if constexpr (last_function) {
          return Fn(ctx), Result<void, Error>{};
        } else {
          return Fn(ctx), Pipeline<FnTail...>::invoke(ctx);
        }
      }
      // 2. Fn(ctx) -> Result<void, Error>
      else if constexpr (  //
          requires(Context& c) {
            { Fn(c) } -> std::same_as<Result<void, Error>>;
          } && std::same_as<After, void>) {
        if constexpr (last_function) {
          return Fn(ctx);
        } else {
          return detail::continue_void_result<Context, FnTail...>(Fn(ctx), ctx);
        }
      }
      // 3. Fn() -> void
      else if constexpr (  //
          requires() {
            { Fn() } -> std::same_as<void>;
          } && std::same_as<After, void>) {
        if constexpr (last_function) {
          return Fn(), Result<void, Error>{};
        } else {
          return Fn(), Pipeline<FnTail...>::invoke(ctx);
        }
      }
      // 4. Fn() -> Result<void, Error>
      else if constexpr (  //
          requires() {
            { Fn() } -> std::same_as<Result<void, Error>>;
          } && std::same_as<After, void>) {
        if constexpr (last_function) {
          return Fn();
        } else {
          return detail::continue_void_result<Context, FnTail...>(Fn(), ctx);
        }
      }
      // 5. Fn(ctx) -> AfterType
      else if constexpr (  //
          requires(Context& c) {
            { Fn(c) } -> std::same_as<After>;
          } && !std::same_as<After, void>) {
        if constexpr (last_function) {
          return Result<After, Error>{Fn(ctx)};
        } else {
          return Pipeline<FnTail...>::invoke(Fn(ctx), ctx);
        }
      }
      // 6. Fn(ctx) -> Result<AfterType, Error>
      else if constexpr (  //
          requires(Context& c) {
            { Fn(c) } -> std::same_as<Result<After, Error>>;
          } && !std::same_as<After, void>) {
        if constexpr (last_function) {
          return Fn(ctx);
        } else {
          return detail::continue_result<After, Context, FnTail...>(Fn(ctx),
                                                                    ctx);
        }
      }
      // 7. Fn() -> AfterType
      else if constexpr (  //
          requires() {
            { Fn() } -> std::same_as<After>;
          } && !std::same_as<After, void>) {
        if constexpr (last_function) {
          return Result<After, Error>{Fn()};
        } else {
          return Pipeline<FnTail...>::invoke(Fn(), ctx);
        }
      }
      // 8. Fn() -> Result<AfterType, Error>
      else if constexpr (  //
          requires() {
            { Fn() } -> std::same_as<Result<After, Error>>;
          } && !std::same_as<After, void>) {
        if constexpr (last_function) {
          return Fn();
        } else {
          return detail::continue_result<After, Context, FnTail...>(Fn(), ctx);
        }
      } else if constexpr (std::same_as<After, void>) {
        static_assert(
            detail::always_false<Head>,
            "Pipeline step return type mismatch: after_type<void> is void, "
            "so the step must be callable as one of: "
            "Fn(ctx) -> void, "
            "Fn(ctx) -> Result<void, Error>, "
            "Fn() -> void, "
            "Fn() -> Result<void, Error>.");
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
   * This overload is used when the first step's input type is `void`. Each step
   * is dispatched according to its declared `after_type<void>` or the previous
   * step's `after_type`. Context-aware overloads taking `const Context&` are
   * preferred over no-argument overloads.
   *
   * Supported first-step signatures are:
   * - `Fn(ctx) -> void`
   * - `Fn(ctx) -> Result<void, Error>`
   * - `Fn() -> void`
   * - `Fn() -> Result<void, Error>`
   * - `Fn(ctx) -> after_type<void>`
   * - `Fn(ctx) -> Result<after_type<void>, Error>`
   * - `Fn() -> after_type<void>`
   * - `Fn() -> Result<after_type<void>, Error>`
   *
   * If a step returns `Result<..., Error>`, errors are propagated immediately
   * and the remaining steps are not invoked. Plain return values are forwarded
   * to the next step without error checking. A terminal plain value is
   * normalized to `Result<value_type, Error>`, and a terminal `void` return is
   * normalized to `Result<void, Error>`.
   *
   * @tparam Context Const context type used by context-aware steps.
   * @param ctx Const pipeline context.
   * @return The final pipeline result as `Result<Final, Error>`.
   */
  template <class Context>
  [[nodiscard]]
  static constexpr auto invoke(const Context& ctx) {
    return []<auto FnHead, auto... FnTail>(
               const Context& ctx) -> decltype(auto) {
      using Head = std::remove_cvref_t<decltype(FnHead)>;
      using After = typename Head::template after_type<void>;
      constexpr auto Fn = FnHead;
      constexpr bool last_function = sizeof...(FnTail) == 0;

      // 1. Fn(ctx) -> void
      if constexpr (  //
          requires(const Context& c) {
            { Fn(c) } -> std::same_as<void>;
          } && std::same_as<After, void>) {
        if constexpr (last_function) {
          return Fn(ctx), Result<void, Error>{};
        } else {
          return Fn(ctx), Pipeline<FnTail...>::invoke(ctx);
        }
      }
      // 2. Fn(ctx) -> Result<void, Error>
      else if constexpr (  //
          requires(const Context& c) {
            { Fn(c) } -> std::same_as<Result<void, Error>>;
          } && std::same_as<After, void>) {
        if constexpr (last_function) {
          return Fn(ctx);
        } else {
          return detail::continue_void_result<const Context, FnTail...>(Fn(ctx),
                                                                        ctx);
        }
      }
      // 3. Fn() -> void
      else if constexpr (  //
          requires() {
            { Fn() } -> std::same_as<void>;
          } && std::same_as<After, void>) {
        if constexpr (last_function) {
          return Fn(), Result<void, Error>{};
        } else {
          return Fn(), Pipeline<FnTail...>::invoke(ctx);
        }
      }
      // 4. Fn() -> Result<void, Error>
      else if constexpr (  //
          requires() {
            { Fn() } -> std::same_as<Result<void, Error>>;
          } && std::same_as<After, void>) {
        if constexpr (last_function) {
          return Fn();
        } else {
          return detail::continue_void_result<const Context, FnTail...>(Fn(),
                                                                        ctx);
        }
      }
      // 5. Fn(ctx) -> AfterType
      else if constexpr (  //
          requires(const Context& c) {
            { Fn(c) } -> std::same_as<After>;
          } && !std::same_as<After, void>) {
        if constexpr (last_function) {
          return Result<After, Error>{Fn(ctx)};
        } else {
          return Pipeline<FnTail...>::invoke(Fn(ctx), ctx);
        }
      }
      // 6. Fn(ctx) -> Result<AfterType, Error>
      else if constexpr (  //
          requires(const Context& c) {
            { Fn(c) } -> std::same_as<Result<After, Error>>;
          } && !std::same_as<After, void>) {
        if constexpr (last_function) {
          return Fn(ctx);
        } else {
          return detail::continue_result<After, Context, FnTail...>(Fn(ctx),
                                                                    ctx);
        }
      }
      // 7. Fn() -> AfterType
      else if constexpr (  //
          requires() {
            { Fn() } -> std::same_as<After>;
          } && !std::same_as<After, void>) {
        if constexpr (last_function) {
          return Result<After, Error>{Fn()};
        } else {
          return Pipeline<FnTail...>::invoke(Fn(), ctx);
        }
      }
      // 8. Fn() -> Result<AfterType, Error>
      else if constexpr (  //
          requires() {
            { Fn() } -> std::same_as<Result<After, Error>>;
          } && !std::same_as<After, void>) {
        if constexpr (last_function) {
          return Fn();
        } else {
          return detail::continue_result<After, Context, FnTail...>(Fn(), ctx);
        }
      } else if constexpr (std::same_as<After, void>) {
        static_assert(
            detail::always_false<Head>,
            "Pipeline step return type mismatch: after_type<void> is void, "
            "so the step must be callable as one of: "
            "Fn(ctx) -> void, "
            "Fn(ctx) -> Result<void, Error>, "
            "Fn() -> void, "
            "Fn() -> Result<void, Error>.");
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

  static constexpr auto analyze_decoder() {
    return analyze_decoder_impl<void, detail::NoStorage, Fns...>();
  }

  template <class Input>
  static constexpr auto analyze_encoder() {
    return analyze_encoder_impl<Input, Fns...>();
  }

  template <class Context, class Input>
  static consteval auto callable_with_const_context() -> bool {
    return callable_with_const_context_impl<Context, Input, Fns...>();
  }

  template <class Context, class Input>
  static consteval auto callable_with_mutable_context() -> bool {
    return callable_with_mutable_context_impl<Context, Input, Fns...>();
  }

 private:
  template <class Input, class Storage, auto FnHead, auto... FnTail>
  static constexpr auto analyze_decoder_impl() {
    using Head = std::remove_cvref_t<decltype(FnHead)>;
    if constexpr (!Head::template accepts_input<Input>) {
      static_assert(sizeof(detail::PipelineStepRejectedInput<Head, Input>) == 0,
                    "Pipeline step does not accept the previous output type");
    }
    detail::require_after_type<Head, Input>();
    using Next = typename Head::template after_type<Input>;
    constexpr bool has_storage =
        requires { typename Head::template storage_type<Input>; };
    if constexpr (has_storage && !std::same_as<Storage, detail::NoStorage>) {
      static_assert(
          sizeof(detail::DuplicateStorageType<Storage, Head, Input>) == 0,
          "Decoder pipeline defines storage_type<Input> more than once");
    }
    using NextStorage =
        typename detail::NextStorage<Head, Input, Storage, has_storage>::type;
    if constexpr (sizeof...(FnTail) == 0) {
      static_assert(
          std::same_as<Next, void>,
          "Decoder terminal action must have after_type<Input> = void");
      if constexpr (std::same_as<NextStorage, detail::NoStorage>) {
        static_assert(sizeof(detail::MissingStorageType<Head, Input>) == 0,
                      "Decoder pipeline must define storage_type<Input> "
                      "exactly once");
      }
      return std::make_tuple(true, std::type_identity<NextStorage>{});
    } else {
      return analyze_decoder_impl<Next, NextStorage, FnTail...>();
    }
  }

  template <class Input, auto FnHead, auto... FnTail>
  static constexpr auto analyze_encoder_impl() {
    using Head = std::remove_cvref_t<decltype(FnHead)>;

    if constexpr (!Head::template accepts_input<Input>) {
      static_assert(sizeof(detail::PipelineStepRejectedInput<Head, Input>) == 0,
                    "Pipeline step does not accept the previous output type");
    }
    if constexpr (requires { typename Head::template storage_type<Input>; }) {
      static_assert(sizeof(detail::UnexpectedStorageType<Head, Input>) == 0,
                    "Encoder pipeline step must not define storage_type<Input>");
    }
    detail::require_after_type<Head, Input>();
    using Next = typename Head::template after_type<Input>;

    if constexpr (sizeof...(FnTail) == 0) {
      static_assert(
          std::same_as<Next, void>,
          "The last encoder action must have after_type<Input> = void");

      return std::make_tuple(true, std::type_identity<void>{});
    } else {
      return analyze_encoder_impl<Next, FnTail...>();
    }
  }

  template <class Context, class Input, auto FnHead, auto... FnTail>
  static consteval auto callable_with_const_context_impl() -> bool {
    using Head = std::remove_cvref_t<decltype(FnHead)>;

    if constexpr (!requires { typename Head::template after_type<Input>; }) {
      return false;
    } else {
      using After = typename Head::template after_type<Input>;

      constexpr bool callable = []() consteval -> bool {
        if constexpr (std::same_as<Input, void>) {
          if constexpr (std::same_as<After, void>) {
            return requires(const Context& ctx) {
              { FnHead(ctx) } -> std::same_as<void>;
            } || requires(const Context& ctx) {
              { FnHead(ctx) } -> std::same_as<Result<void, Error>>;
            } || requires {
              { FnHead() } -> std::same_as<void>;
            } || requires {
              { FnHead() } -> std::same_as<Result<void, Error>>;
            };
          } else {
            return requires(const Context& ctx) {
              { FnHead(ctx) } -> std::same_as<After>;
            } || requires(const Context& ctx) {
              { FnHead(ctx) } -> std::same_as<Result<After, Error>>;
            } || requires {
              { FnHead() } -> std::same_as<After>;
            } || requires {
              { FnHead() } -> std::same_as<Result<After, Error>>;
            };
          }
        } else {
          if constexpr (std::same_as<After, void>) {
            return requires(Input input, const Context& ctx) {
              { FnHead(std::move(input), ctx) } -> std::same_as<void>;
            } || requires(Input input, const Context& ctx) {
              {
                FnHead(std::move(input), ctx)
              } -> std::same_as<Result<void, Error>>;
            } || requires(Input input) {
              { FnHead(std::move(input)) } -> std::same_as<void>;
            } || requires(Input input) {
              { FnHead(std::move(input)) } -> std::same_as<Result<void, Error>>;
            };
          } else {
            return requires(Input input, const Context& ctx) {
              { FnHead(std::move(input), ctx) } -> std::same_as<After>;
            } || requires(Input input, const Context& ctx) {
              {
                FnHead(std::move(input), ctx)
              } -> std::same_as<Result<After, Error>>;
            } || requires(Input input) {
              { FnHead(std::move(input)) } -> std::same_as<After>;
            } || requires(Input input) {
              { FnHead(std::move(input)) } -> std::same_as<Result<After, Error>>;
            };
          }
        }
      }();

      if constexpr (!callable) {
        return false;
      } else if constexpr (sizeof...(FnTail) == 0) {
        return true;
      } else {
        return callable_with_const_context_impl<Context, After, FnTail...>();
      }
    }
  }

  template <class Context, class Input, auto FnHead, auto... FnTail>
  static consteval auto callable_with_mutable_context_impl() -> bool {
    using Head = std::remove_cvref_t<decltype(FnHead)>;

    if constexpr (!requires { typename Head::template after_type<Input>; }) {
      return false;
    } else {
      using After = typename Head::template after_type<Input>;

      constexpr bool callable = []() consteval -> bool {
        if constexpr (std::same_as<Input, void>) {
          if constexpr (std::same_as<After, void>) {
            return requires(Context& ctx) {
              { FnHead(ctx) } -> std::same_as<void>;
            } || requires(Context& ctx) {
              { FnHead(ctx) } -> std::same_as<Result<void, Error>>;
            } || requires {
              { FnHead() } -> std::same_as<void>;
            } || requires {
              { FnHead() } -> std::same_as<Result<void, Error>>;
            };
          } else {
            return requires(Context& ctx) {
              { FnHead(ctx) } -> std::same_as<After>;
            } || requires(Context& ctx) {
              { FnHead(ctx) } -> std::same_as<Result<After, Error>>;
            } || requires {
              { FnHead() } -> std::same_as<After>;
            } || requires {
              { FnHead() } -> std::same_as<Result<After, Error>>;
            };
          }
        } else {
          if constexpr (std::same_as<After, void>) {
            return requires(Input input, Context& ctx) {
              { FnHead(std::move(input), ctx) } -> std::same_as<void>;
            } || requires(Input input, Context& ctx) {
              {
                FnHead(std::move(input), ctx)
              } -> std::same_as<Result<void, Error>>;
            } || requires(Input input) {
              { FnHead(std::move(input)) } -> std::same_as<void>;
            } || requires(Input input) {
              { FnHead(std::move(input)) } -> std::same_as<Result<void, Error>>;
            };
          } else {
            return requires(Input input, Context& ctx) {
              { FnHead(std::move(input), ctx) } -> std::same_as<After>;
            } || requires(Input input, Context& ctx) {
              {
                FnHead(std::move(input), ctx)
              } -> std::same_as<Result<After, Error>>;
            } || requires(Input input) {
              { FnHead(std::move(input)) } -> std::same_as<After>;
            } || requires(Input input) {
              { FnHead(std::move(input)) } -> std::same_as<Result<After, Error>>;
            };
          }
        }
      }();

      if constexpr (!callable) {
        return false;
      } else if constexpr (sizeof...(FnTail) == 0) {
        return true;
      } else {
        return callable_with_mutable_context_impl<Context, After, FnTail...>();
      }
    }
  }
};

}  // namespace ser20
