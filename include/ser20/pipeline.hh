#pragma once

#include <algorithm>
#include <cctype>
#include <concepts>
#include <cstddef>
#include <tuple>
#include <type_traits>
#include <utility>

#include "ser20/context.hh"
#include "ser20/error.hh"
#include "ser20/result.hh"

namespace ser20 {

namespace detail {

/// Placeholder input type used during static pipeline analysis when the true input is not yet known.
struct AnyInput {};

/// Incomplete type; instantiating it triggers a "missing storage_type" static_assert diagnostic.
template <class Head, class Input>
struct MissingStorageType;

/// Incomplete type; instantiating it triggers a "duplicate storage_type" static_assert diagnostic.
template <class ExistingStorage, class Head, class Input>
struct DuplicateStorageType;

/// Incomplete type; instantiating it triggers an "unexpected storage_type in encoder" static_assert diagnostic.
template <class Head, class Input>
struct UnexpectedStorageType;

/// Sentinel: no step in the pipeline has declared a `storage_type` yet.
struct NoStorage {};

/// Sentinel returned by callability probes when a step cannot be invoked with the given arguments.
struct NotCallable {};

/// Computes the active storage type after a pipeline step.
/// Adopts `Head::storage_type<Input>` when `HasStorage` is true; otherwise forwards `Storage` unchanged.
template <class Head, class Input, class Storage, bool HasStorage>
struct NextStorage {
  using type = Storage;
};

template <class Head, class Input, class Storage>
struct NextStorage<Head, Input, Storage, true> {
  using type = typename Head::template storage_type<Input>;
};

/// Distinguishes decoder pipelines (which read/deserialize data) from encoder pipelines (which write/serialize data).
enum class PipelineMode {
  decoder,
  encoder,
};

/// Always-false helper for static_assert inside unreachable `constexpr if` branches.
template <class Head>
static constexpr bool always_false = false;

/// Unwraps the value type `T` from `Result<T, Error>`; passes `R` through unchanged for non-Result types.
template <class R>
struct StepOutput {
  using type = std::remove_cvref_t<R>;
};

template <class T>
struct StepOutput<Result<T, Error>> {
  using type = T;
};

/// The unwrapped output type of a pipeline step: `T` for `Result<T, Error>`, `R` for everything else.
template <class R>
using step_output_t = typename StepOutput<std::remove_cvref_t<R>>::type;

/// Wraps `value` in a successful `Result<T, Error>`, for use as a normalized pipeline step output.
template <class T>
[[nodiscard]]
constexpr auto expected(T&& value) -> Result<std::remove_cvref_t<T>, Error> {
  return Result<std::remove_cvref_t<T>, Error>{std::forward<T>(value)};
}

/// Returns a successful `Result<void, Error>`, for use as a normalized pipeline step output.
[[nodiscard]]
constexpr auto expected() -> Result<void, Error> {
  return Result<void, Error>{};
}

}  // namespace detail

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

template <auto... FnTail, class Output, class Context>
static constexpr auto continue_result(Result<Output, Error> next, Context& ctx) {
  if (!next) [[unlikely]] {
    using TailResult = detail::tail_result_t<Output, Context, FnTail...>;
    return TailResult{unexpected(std::move(next).error())};
  }

  if constexpr (std::same_as<Output, void>) {
    return Pipeline<FnTail...>::invoke(ctx);
  } else {
    return Pipeline<FnTail...>::invoke(std::move(*next), ctx);
  }
}

template <auto... FnTail, class Output, class Context>
static constexpr auto continue_result(Result<Output, Error> next,
                                      const Context& ctx) {
  if (!next) [[unlikely]] {
    using TailResult = detail::tail_result_t<Output, const Context, FnTail...>;
    return TailResult{unexpected(std::move(next).error())};
  }

  if constexpr (std::same_as<Output, void>) {
    return Pipeline<FnTail...>::invoke(ctx);
  } else {
    return Pipeline<FnTail...>::invoke(std::move(*next), ctx);
  }
}

}  // namespace detail

/**
 * @brief A compile-time pipeline of constexpr action objects applied sequentially.
 *
 * Each step receives the output of the previous step as its input. Steps may
 * return a plain value, `void`, `Result<Out, Error>`, or `Result<void, Error>`.
 * When a step returns a `Result` error it is propagated immediately and
 * the remaining steps are not invoked.
 *
 * **Decoder role** (validated by `analyze_decoder()`): exactly one step across
 * the pipeline must declare `storage_type<Input>`. That type becomes the
 * decoder's storage type; it need not be the terminal step.
 *
 * **Encoder role** (validated by `analyze_encoder()`): no step may declare
 * `storage_type<Input>`.
 *
 * @tparam Fns Pack of constexpr action objects forming the pipeline.
 */
template <auto... Fns>
  requires(sizeof...(Fns) > 0)
struct Pipeline {
  /**
   * @brief Invokes this pipeline from an explicit input value using a mutable context.
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
   * invoked. Plain return values are forwarded without error-checking. A
   * terminal plain value is normalized to `Result<Out, Error>`; a terminal
   * `void` return is normalized to `Result<void, Error>`.
   *
   * @tparam Input  Input value type for the first step.
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
      constexpr auto Fn = FnHead;
      constexpr bool last_function = sizeof...(FnTail) == 0;

      // 1. Fn(input, ctx) -> void
      if constexpr (  //
          requires(Input i, Context& c) {
            { Fn(std::move(i), c) } -> std::same_as<void>;
          }) {
        if constexpr (last_function) {
          return Fn(std::move(input), ctx), Result<void, Error>{};
        } else {
          return Fn(std::move(input), ctx), Pipeline<FnTail...>::invoke(ctx);
        }
      }
      // 2. Fn(input, ctx) -> Result<AfterType, Error>
      else if constexpr (  //
          requires(Input i, Context& c) {
            { Fn(std::move(i), c) } -> detail::Ser20ResultType;
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
          return detail::expected(Fn(std::move(input), ctx));
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
          return Fn(std::move(input)), Result<void, Error>{};
        } else {
          return Fn(std::move(input)), Pipeline<FnTail...>::invoke(ctx);
        }
      }
      // 5. Fn(input) -> Result<AfterType, Error>
      else if constexpr (  //
          requires(Input i) {
            { Fn(std::move(i)) } -> detail::Ser20ResultType;
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
          return detail::expected(Fn(std::move(input)));
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
   * @brief Invokes this pipeline from an explicit input value using a const context.
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
   * invoked. Plain return values are forwarded without error-checking. A
   * terminal plain value is normalized to `Result<Out, Error>`; a terminal
   * `void` return is normalized to `Result<void, Error>`.
   *
   * @tparam Input  Input value type for the first step.
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
      constexpr auto Fn = FnHead;
      constexpr bool last_function = sizeof...(FnTail) == 0;

      // 1. Fn(input, ctx) -> void
      if constexpr (  //
          requires(Input i, const Context& c) {
            { Fn(std::move(i), c) } -> std::same_as<void>;
          }) {
        if constexpr (last_function) {
          return Fn(std::move(input), ctx), Result<void, Error>{};
        } else {
          return Fn(std::move(input), ctx), Pipeline<FnTail...>::invoke(ctx);
        }
      }
      // 2. Fn(input, ctx) -> Result<AfterType, Error>
      else if constexpr (  //
          requires(Input i, const Context& c) {
            { Fn(std::move(i), c) } -> detail::Ser20ResultType;
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
          return detail::expected(Fn(std::move(input), ctx));
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
          return Fn(std::move(input)), Result<void, Error>{};
        } else {
          return Fn(std::move(input)), Pipeline<FnTail...>::invoke(ctx);
        }
      }
      // 5. Fn(input) -> Result<AfterType, Error>
      else if constexpr (  //
          requires(Input i) {
            { Fn(std::move(i)) } -> detail::Ser20ResultType;
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
          return detail::expected(Fn(std::move(input)));
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
   * @brief Invokes this pipeline without an explicit input value using a mutable context.
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
   * invoked. Plain return values are forwarded without error-checking. A
   * terminal plain value is normalized to `Result<Out, Error>`; a terminal
   * `void` return is normalized to `Result<void, Error>`.
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
      constexpr auto Fn = FnHead;
      constexpr bool last_function = sizeof...(FnTail) == 0;

      // 1. Fn(ctx) -> void
      if constexpr (  //
          requires(Context& c) {
            { Fn(c) } -> std::same_as<void>;
          }) {
        if constexpr (last_function) {
          return Fn(ctx), Result<void, Error>{};
        } else {
          return Fn(ctx), Pipeline<FnTail...>::invoke(ctx);
        }
      }
      // 2. Fn(ctx) -> Result<AfterType, Error>
      else if constexpr (  //
          requires(Context& c) {
            { Fn(c) } -> detail::Ser20ResultType;
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
          return detail::expected(Fn(ctx));
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
          return Fn(), Result<void, Error>{};
        } else {
          return Fn(), Pipeline<FnTail...>::invoke(ctx);
        }
      }
      // 5. Fn() -> Result<AfterType, Error>
      else if constexpr (  //
          requires() {
            { Fn() } -> detail::Ser20ResultType;
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
          return detail::expected(Fn());
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
   * @brief Invokes this pipeline without an explicit input value using a const context.
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
   * invoked. Plain return values are forwarded without error-checking. A
   * terminal plain value is normalized to `Result<Out, Error>`; a terminal
   * `void` return is normalized to `Result<void, Error>`.
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
      constexpr auto Fn = FnHead;
      constexpr bool last_function = sizeof...(FnTail) == 0;

      // 1. Fn(ctx) -> void
      if constexpr (  //
          requires(const Context& c) {
            { Fn(c) } -> std::same_as<void>;
          }) {
        if constexpr (last_function) {
          return Fn(ctx), Result<void, Error>{};
        } else {
          return Fn(ctx), Pipeline<FnTail...>::invoke(ctx);
        }
      }
      // 2. Fn(ctx) -> Result<AfterType, Error>
      else if constexpr (  //
          requires(const Context& c) {
            { Fn(c) } -> detail::Ser20ResultType;
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
          return detail::expected(Fn(ctx));
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
          return Fn(), Result<void, Error>{};
        } else {
          return Fn(), Pipeline<FnTail...>::invoke(ctx);
        }
      }
      // 5. Fn() -> Result<AfterType, Error>
      else if constexpr (  //
          requires() {
            { Fn() } -> detail::Ser20ResultType;
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
          return detail::expected(Fn());
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
   * @brief Statically validates this pipeline for use as a decoder.
   *
   * Checks that exactly one step declares `storage_type<Input>` and that
   * the terminal step returns `void` or `Result<void, Error>`. Returns a
   * tuple `{true, type_identity<StorageType>}` on success; fails via
   * static_assert otherwise.
   */
  static constexpr auto analyze_decoder() {
    return analyze_decoder_impl<void, detail::NoStorage, Fns...>();
  }

  /**
   * @brief Statically validates this pipeline for use as an encoder.
   *
   * Checks that no step declares `storage_type<Input>` and that the
   * terminal step returns `void` or `Result<void, Error>`. Returns
   * `{true, type_identity<void>}` on success; fails via static_assert otherwise.
   *
   * @tparam Input The value type the encoder pipeline will receive.
   */
  template <class Input>
  static constexpr auto analyze_encoder() {
    return analyze_encoder_impl<Input, Fns...>();
  }

  /**
   * @brief Returns `true` if this pipeline can be invoked with a `const Context&`
   * starting from the given `Input` type.
   *
   * Walks the step chain at compile time, checking that each step is callable
   * with the appropriate input and `const Context&` (or no context).
   *
   * @tparam Context Context type to probe.
   * @tparam Input   Input type for the first step.
   */
  template <class Context, class Input>
  static consteval auto callable_with_const_context() -> bool {
    return callable_with_const_context_impl<Context, Input, Fns...>();
  }

  /**
   * @brief Returns `true` if this pipeline can be invoked with a mutable `Context&`
   * starting from the given `Input` type.
   *
   * Walks the step chain at compile time, checking that each step is callable
   * with the appropriate input and `Context&` (or no context).
   *
   * @tparam Context Context type to probe.
   * @tparam Input   Input type for the first step.
   */
  template <class Context, class Input>
  static consteval auto callable_with_mutable_context() -> bool {
    return callable_with_mutable_context_impl<Context, Input, Fns...>();
  }

 private:
  template <class Input, class Storage, auto FnHead, auto... FnTail>
  static constexpr auto analyze_decoder_impl() {
    using Head = std::remove_cvref_t<decltype(FnHead)>;
    using Context = detail::AnyDecodeContext;
    using In = std::remove_cvref_t<Input>;

    constexpr bool has_storage =
        requires { typename Head::template storage_type<In>; };

    using NextStorage =
        typename detail::NextStorage<Head, Input, Storage, has_storage>::type;

    if constexpr (has_storage && !std::same_as<Storage, detail::NoStorage>) {
      static_assert(
          sizeof(detail::DuplicateStorageType<Storage, Head, Input>) == 0,
          "Decoder pipeline defines storage_type<Input> more than once");
    }

    using raw_return_type = typename decltype([]() -> auto {
      if constexpr (std::same_as<In, void> &&
                    requires(Context& ctx) { FnHead(ctx); }) {
        return std::type_identity<
            std::invoke_result_t<decltype(FnHead), Context&>>{};
      } else if constexpr (std::same_as<In, void> && requires { FnHead(); }) {
        return std::type_identity<std::invoke_result_t<decltype(FnHead)>>{};
      } else if constexpr (!std::same_as<In, void> &&
                           requires(In in, Context& ctx) {
                             FnHead(std::move(in), ctx);
                           }) {
        return std::type_identity<
            std::invoke_result_t<decltype(FnHead), In&&, Context&>>{};
      } else if constexpr (!std::same_as<In, void> &&
                           requires(In in) { FnHead(std::move(in)); }) {
        return std::type_identity<
            std::invoke_result_t<decltype(FnHead), In&&>>{};
      } else {
        static_assert(
            detail::always_false<Head>,
            "Decoder pipeline step is not callable. It must be callable as "
            "Fn(ctx) or Fn() when input is void, or Fn(input, ctx) or Fn(input) "
            "when input is non-void.");
      }
    }())::type;

    using after_type = detail::step_output_t<raw_return_type>;

    if constexpr (sizeof...(FnTail) == 0) {
      static_assert(
          std::same_as<after_type, void>,
          "Decoder terminal action must return void or Result<void, Error>");
      static_assert(
          !std::same_as<NextStorage, detail::NoStorage>,
          "At least one decoder pipeline step must define storage_type<Input>");
      return std::make_tuple(true, std::type_identity<NextStorage>{});
    } else {
      return analyze_decoder_impl<after_type, NextStorage, FnTail...>();
    }
  }

  template <class Input, auto FnHead, auto... FnTail>
  static constexpr auto analyze_encoder_impl() {
    using Head = std::remove_cvref_t<decltype(FnHead)>;
    using Context = detail::AnyEncodeContext;
    using In = std::remove_cvref_t<Input>;

    if constexpr (requires { typename Head::template storage_type<In>; }) {
      static_assert(sizeof(detail::UnexpectedStorageType<Head, Input>) == 0,
                    "Encoder pipeline step must not define storage_type<Input>");
    }

    using raw_return_type = typename decltype([]() -> auto {
      if constexpr (std::same_as<In, void> &&
                    requires(Context& ctx) { FnHead(ctx); }) {
        return std::type_identity<
            std::invoke_result_t<decltype(FnHead), Context&>>{};
      } else if constexpr (std::same_as<In, void> && requires { FnHead(); }) {
        return std::type_identity<std::invoke_result_t<decltype(FnHead)>>{};
      } else if constexpr (!std::same_as<In, void> &&
                           requires(In in, Context& ctx) {
                             FnHead(std::move(in), ctx);
                           }) {
        return std::type_identity<
            std::invoke_result_t<decltype(FnHead), In&&, Context&>>{};
      } else if constexpr (!std::same_as<In, void> &&
                           requires(In in) { FnHead(std::move(in)); }) {
        return std::type_identity<
            std::invoke_result_t<decltype(FnHead), In&&>>{};
      } else {
        static_assert(
            detail::always_false<Head>,
            "Encoder pipeline step is not callable. It must be callable as "
            "Fn(ctx) or Fn() when input is void, or Fn(input, ctx) or Fn(input) "
            "when input is non-void.");
      }
    }())::type;

    using after_type = detail::step_output_t<raw_return_type>;

    if constexpr (sizeof...(FnTail) == 0) {
      static_assert(
          std::same_as<after_type, void>,
          "Encoder terminal action must return void or Result<void, Error>");

      return std::make_tuple(true, std::type_identity<void>{});
    } else {
      return analyze_encoder_impl<after_type, FnTail...>();
    }
  }

  template <class Context, class Input, auto FnHead, auto... FnTail>
  static consteval auto callable_with_const_context_impl() -> bool {
    using In = std::remove_cvref_t<Input>;

    using probe = decltype([]() consteval -> auto {
      if constexpr (std::same_as<In, void>) {
        if constexpr (requires(const Context& ctx) { FnHead(ctx); }) {
          return std::type_identity<
              std::invoke_result_t<decltype(FnHead), const Context&>>{};
        } else if constexpr (requires { FnHead(); }) {
          return std::type_identity<std::invoke_result_t<decltype(FnHead)>>{};
        } else {
          return std::type_identity<detail::NotCallable>{};
        }
      } else {
        if constexpr (requires(In input, const Context& ctx) {
                        FnHead(std::move(input), ctx);
                      }) {
          return std::type_identity<
              std::invoke_result_t<decltype(FnHead), In&&, const Context&>>{};
        } else if constexpr (requires(In input) { FnHead(std::move(input)); }) {
          return std::type_identity<
              std::invoke_result_t<decltype(FnHead), In&&>>{};
        } else {
          return std::type_identity<detail::NotCallable>{};
        }
      }
    }());

    using raw_return_type = typename probe::type;

    if constexpr (std::same_as<raw_return_type, detail::NotCallable>) {
      return false;
    } else if constexpr (sizeof...(FnTail) == 0) {
      return true;
    } else {
      using After = detail::step_output_t<raw_return_type>;
      return callable_with_const_context_impl<Context, After, FnTail...>();
    }
  }

  template <class Context, class Input, auto FnHead, auto... FnTail>
  static consteval auto callable_with_mutable_context_impl() -> bool {
    using In = std::remove_cvref_t<Input>;

    using probe = decltype([]() consteval -> auto {
      if constexpr (std::same_as<In, void>) {
        if constexpr (requires(Context& ctx) { FnHead(ctx); }) {
          return std::type_identity<
              std::invoke_result_t<decltype(FnHead), Context&>>{};
        } else if constexpr (requires { FnHead(); }) {
          return std::type_identity<std::invoke_result_t<decltype(FnHead)>>{};
        } else {
          return std::type_identity<detail::NotCallable>{};
        }
      } else {
        if constexpr (requires(In input, Context& ctx) {
                        FnHead(std::move(input), ctx);
                      }) {
          return std::type_identity<
              std::invoke_result_t<decltype(FnHead), In&&, Context&>>{};
        } else if constexpr (requires(In input) { FnHead(std::move(input)); }) {
          return std::type_identity<
              std::invoke_result_t<decltype(FnHead), In&&>>{};
        } else {
          return std::type_identity<detail::NotCallable>{};
        }
      }
    }());

    using raw_return_type = typename probe::type;

    if constexpr (std::same_as<raw_return_type, detail::NotCallable>) {
      return false;
    } else if constexpr (sizeof...(FnTail) == 0) {
      return true;
    } else {
      using After = detail::step_output_t<raw_return_type>;
      return callable_with_mutable_context_impl<Context, After, FnTail...>();
    }
  }
};

/// Convenience variable template that wraps a default-constructed `T` as a single-step `Pipeline<T{}>`.
template <class T>
inline constexpr auto step = []() -> auto { return Pipeline<T{}>{}; }();

/// Concatenates two pipelines into a single `Pipeline` containing all steps of `Lhs` followed by `Rhs`.
template <auto... Lhs, auto... Rhs>
constexpr auto operator|(Pipeline<Lhs...>, Pipeline<Rhs...>) {
  return Pipeline<Lhs..., Rhs...>{};
}

}  // namespace ser20
