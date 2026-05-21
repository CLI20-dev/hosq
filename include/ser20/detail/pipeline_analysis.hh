#pragma once

#include <type_traits>

#include "ser20/context.hh"
#include "ser20/detail/always_false.hh"

namespace ser20 {

template <auto... Fns>
  requires(sizeof...(Fns) > 0)
struct Pipeline;

namespace detail {

/// Incomplete type; instantiating it triggers a "duplicate storage_type"
/// static_assert diagnostic.
template <class ExistingStorage, class Head, class Input>
struct DuplicateStorageType;

/// Incomplete type; instantiating it triggers an "unexpected storage_type in
/// encoder" static_assert diagnostic.
template <class Head, class Input>
struct UnexpectedStorageType;

/// Sentinel: no step in the pipeline has declared a `storage_type` yet.
struct NoStorage {};

/// Sentinel returned by callability probes when a step cannot be invoked with
/// the given arguments.
struct NotCallable {};

/// Computes the active storage type after a pipeline step.
/// Adopts `Head::storage_type<Input>` when `HasStorage` is true; otherwise
/// forwards `Storage` unchanged.
template <class Head, class Input, class Storage, bool HasStorage>
struct NextStorage {
  using type = Storage;
};

template <class Head, class Input, class Storage>
struct NextStorage<Head, Input, Storage, true> {
  using type = typename Head::template storage_type<Input>;
};

/// Distinguishes decoder pipelines (which read/deserialize data) from encoder
/// pipelines (which write/serialize data).
enum class PipelineMode {
  decoder,
  encoder,
};

/// Unwraps the value type `T` from `Result<T, Error>`; passes `R` through
/// unchanged for non-Result types.
template <class R>
struct StepOutput {
  using type = std::remove_cvref_t<R>;
};

template <class T>
struct StepOutput<Result<T, Error>> {
  using type = T;
};

/// The unwrapped output type of a pipeline step: `T` for `Result<T, Error>`, `R`
/// for everything else.
template <class R>
using step_output_t = typename StepOutput<std::remove_cvref_t<R>>::type;

template <class Input, class Storage, auto FnHead, auto... FnTail>
constexpr auto analyze_decoder_impl() {
  using Head = std::remove_cvref_t<decltype(FnHead)>;
  using Context = detail::AnyDecodeContext;
  using In = std::remove_cvref_t<Input>;

  constexpr bool has_storage =
      requires { typename Head::template storage_type<In>; };

  using NextStorage =
      typename detail::NextStorage<Head, In, Storage, has_storage>::type;

  if constexpr (has_storage && !std::same_as<Storage, detail::NoStorage>) {
    static_assert(sizeof(detail::DuplicateStorageType<Storage, Head, In>) == 0,
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
      return std::type_identity<std::invoke_result_t<decltype(FnHead), In&&>>{};
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
        std::same_as<after_type, void> || std::same_as<after_type, bool>,
        "Decoder terminal action must return void, bool, Result<void, Error>, "
        "or Result<bool, Error>");
    static_assert(
        !std::same_as<NextStorage, detail::NoStorage>,
        "At least one decoder pipeline step must define storage_type<Input>");
    return std::make_tuple(true, std::type_identity<NextStorage>{},
                           std::type_identity<after_type>{});
  } else {
    return analyze_decoder_impl<after_type, NextStorage, FnTail...>();
  }
}

template <class Input, auto FnHead, auto... FnTail>
constexpr auto analyze_encoder_impl() {
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
      return std::type_identity<std::invoke_result_t<decltype(FnHead), In&&>>{};
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
        "Encoder terminal action must return void, Result<void, Error>");
    return std::make_tuple(true, std::type_identity<void>{},
                           std::type_identity<after_type>{});
  } else {
    return analyze_encoder_impl<after_type, FnTail...>();
  }
}

template <class Context, class Input, auto FnHead, auto... FnTail>
consteval auto callable_with_const_context_impl() -> bool {
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
consteval auto callable_with_mutable_context_impl() -> bool {
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

template <class T>
struct DecoderPipelineAnalysis {
  static_assert(always_false<T>,
                "decoder pipeline analysis requires ser20::Pipeline<...>");
};

template <auto... Fns>
struct DecoderPipelineAnalysis<ser20::Pipeline<Fns...>> {
  using type = decltype(analyze_decoder_impl<void, NoStorage, Fns...>());
};

template <class T>
using decoder_pipeline_analysis_t =
    typename DecoderPipelineAnalysis<std::remove_cvref_t<T>>::type;

template <class T, class Input>
struct EncoderPipelineAnalysis {
  static_assert(always_false<T>,
                "encoder pipeline analysis requires ser20::Pipeline<...>");
};

template <auto... Fns, class Input>
struct EncoderPipelineAnalysis<ser20::Pipeline<Fns...>, Input> {
  using type = decltype(analyze_encoder_impl<Input, Fns...>());
};

template <class T, class Input>
using encoder_pipeline_analysis_t =
    typename EncoderPipelineAnalysis<std::remove_cvref_t<T>, Input>::type;

template <class Pipeline, class Context, class Input>
struct PipelineCallableWithConstContext {
  static_assert(always_false<Pipeline>,
                "callability probe requires ser20::Pipeline<...>");
};

template <auto... Fns, class Context, class Input>
struct PipelineCallableWithConstContext<ser20::Pipeline<Fns...>, Context, Input>
    : std::bool_constant<
          callable_with_const_context_impl<Context, Input, Fns...>()> {};

template <class Pipeline, class Context, class Input>
inline constexpr bool pipeline_callable_with_const_context_v =
    PipelineCallableWithConstContext<std::remove_cvref_t<Pipeline>, Context,
                                     Input>::value;

template <class Pipeline, class Context, class Input>
struct PipelineCallableWithMutableContext {
  static_assert(always_false<Pipeline>,
                "callability probe requires ser20::Pipeline<...>");
};

template <auto... Fns, class Context, class Input>
struct PipelineCallableWithMutableContext<ser20::Pipeline<Fns...>, Context,
                                          Input>
    : std::bool_constant<
          callable_with_mutable_context_impl<Context, Input, Fns...>()> {};

template <class Pipeline, class Context, class Input>
inline constexpr bool pipeline_callable_with_mutable_context_v =
    PipelineCallableWithMutableContext<std::remove_cvref_t<Pipeline>, Context,
                                       Input>::value;

}  // namespace detail

}  // namespace ser20
