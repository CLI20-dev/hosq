#pragma once

#include <cstdint>
#include <tuple>
#include <type_traits>
#include <utility>

#include "hosq/detail/always_false.hh"
#include "hosq/error.hh"
#include "hosq/result.hh"

namespace hosq {

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

enum class PipelineVerificationMode : std::uint8_t {
  none,
  exactly_one_storage,
  no_storage,
};

template <PipelineVerificationMode Mode, class ProbeContext, class Input,
          class Storage, auto FnHead, auto... FnTail>
constexpr auto analyze_pipeline_impl() {
  using Head = std::remove_cvref_t<decltype(FnHead)>;
  using Context = ProbeContext;
  using In = std::remove_cvref_t<Input>;

  constexpr bool has_storage =
      requires { typename Head::template storage_type<In>; };

  using NextStorage =
      typename detail::NextStorage<Head, In, Storage, has_storage>::type;

  if constexpr (Mode == PipelineVerificationMode::no_storage &&
                requires { typename Head::template storage_type<In>; }) {
    static_assert(sizeof(detail::UnexpectedStorageType<Head, In>) == 0,
                  "Pipeline step must not define storage_type<Input>");
  }
  if constexpr (Mode == PipelineVerificationMode::exactly_one_storage &&
                (has_storage && !std::same_as<Storage, detail::NoStorage>)) {
    static_assert(sizeof(detail::DuplicateStorageType<Storage, Head, In>) == 0,
                  "Pipeline defines storage_type<Input> more than once");
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
          "Pipeline step is not callable. It must be callable as "
          "Fn(ctx) or Fn() when input is void, or Fn(input, ctx) or Fn(input) "
          "when input is non-void.");
    }
  }())::type;

  using after_type = detail::step_output_t<raw_return_type>;

  if constexpr (sizeof...(FnTail) == 0) {
    if constexpr (Mode == PipelineVerificationMode::exactly_one_storage) {
      static_assert(!std::same_as<NextStorage, detail::NoStorage>,
                    "Pipeline must define exactly one storage_type<Input>");
    }
    return std::make_tuple(true, std::type_identity<NextStorage>{},
                           std::type_identity<after_type>{});
  } else {
    return analyze_pipeline_impl<Mode, ProbeContext, after_type, NextStorage,
                                 FnTail...>();
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

template <PipelineVerificationMode V, class ProbeContext, class Start, class T>
struct PipelineAnalysis {
  static_assert(always_false<T>,
                "pipeline analysis requires hosq::Pipeline<...>");
};

template <PipelineVerificationMode Mode, class ProbeContext, class Start,
          auto... Fns>
struct PipelineAnalysis<Mode, ProbeContext, Start, Pipeline<Fns...>> {
  using type = decltype(analyze_pipeline_impl<Mode, ProbeContext, Start,
                                              NoStorage, Fns...>());
};

template <PipelineVerificationMode V, class ProbeContext, class Start, class T>
using pipeline_analysis_t =
    typename PipelineAnalysis<V, ProbeContext, Start,
                              std::remove_cvref_t<T>>::type;

template <class Pipeline, class Context, class Input>
struct PipelineCallableWithConstContext {
  static_assert(always_false<Pipeline>,
                "callability probe requires hosq::Pipeline<...>");
};

template <auto... Fns, class Context, class Input>
struct PipelineCallableWithConstContext<hosq::Pipeline<Fns...>, Context, Input>
    : std::bool_constant<
          callable_with_const_context_impl<Context, Input, Fns...>()> {};

template <class Pipeline, class Context, class Input>
inline constexpr bool pipeline_callable_with_const_context_v =
    PipelineCallableWithConstContext<std::remove_cvref_t<Pipeline>, Context,
                                     Input>::value;

template <class Pipeline, class Context, class Input>
struct PipelineCallableWithMutableContext {
  static_assert(always_false<Pipeline>,
                "callability probe requires hosq::Pipeline<...>");
};

template <auto... Fns, class Context, class Input>
struct PipelineCallableWithMutableContext<hosq::Pipeline<Fns...>, Context, Input>
    : std::bool_constant<
          callable_with_mutable_context_impl<Context, Input, Fns...>()> {};

template <class Pipeline, class Context, class Input>
inline constexpr bool pipeline_callable_with_mutable_context_v =
    PipelineCallableWithMutableContext<std::remove_cvref_t<Pipeline>, Context,
                                       Input>::value;

template <class Analysis>
using pipeline_storage_t = typename std::tuple_element_t<1, Analysis>::type;

template <class Analysis>
using pipeline_terminal_t = typename std::tuple_element_t<2, Analysis>::type;

}  // namespace detail

}  // namespace hosq
