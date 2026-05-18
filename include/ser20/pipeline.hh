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

template <class Head, class Input>
struct PipelineStepRejectedInput;

template <class ExistingStorage, class Head, class Input>
struct DuplicateStorageType;

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

template <auto Fn, class Context, class Input>
concept StepCallableWithConstContext =
    requires(const Context& ctx, Result<Input, Error> input) {
      Fn(ctx, std::move(input));
    };

template <auto Fn, class Context, class Input>
concept StepCallableWithMutableContext = requires(
    Context& ctx, Result<Input, Error> input) { Fn(ctx, std::move(input)); };

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
      if constexpr (std::same_as<NextStorage, detail::NoStorage>) {
        static_assert(sizeof(detail::MissingStorageType<Head, Input>) == 0,
                      "Decoder pipeline must define storage_type<Input> "
                      "exactly once");
      }

      return std::make_tuple(true, std::type_identity<NextStorage>{});
    } else {
      detail::require_after_type<Head, Input>();

      using Next = typename Head::template after_type<Input>;
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

    if constexpr (sizeof...(FnTail) == 0) {
      static_assert(
          !requires { typename Head::template after_type<Input>; },
          "The last encoder action must be terminal and must not define "
          "after_type<Input>");

      return std::make_tuple(true, std::type_identity<void>{});
    } else {
      detail::require_after_type<Head, Input>();

      using Next = typename Head::template after_type<Input>;
      return analyze_encoder_impl<Next, FnTail...>();
    }
  }

  template <class Context, class Input, auto FnHead, auto... FnTail>
  static consteval auto callable_with_const_context_impl() -> bool {
    using Head = std::remove_cvref_t<decltype(FnHead)>;

    if constexpr (!detail::StepCallableWithConstContext<FnHead, Context,
                                                        Input>) {  // NOLINT
      return false;
    } else if constexpr (sizeof...(FnTail) == 0) {
      return true;
    } else if constexpr (!requires {
                           typename Head::template after_type<Input>;
                         }) {
      return false;
    } else {
      using Next = typename Head::template after_type<Input>;
      return callable_with_const_context_impl<Context, Next, FnTail...>();
    }
  }

  template <class Context, class Input, auto FnHead, auto... FnTail>
  static consteval auto callable_with_mutable_context_impl() -> bool {
    using Head = std::remove_cvref_t<decltype(FnHead)>;

    if constexpr (!detail::StepCallableWithMutableContext<FnHead, Context,
                                                          Input>) {  // NOLINT
      return false;
    } else if constexpr (sizeof...(FnTail) == 0) {
      return true;
    } else if constexpr (!requires {
                           typename Head::template after_type<Input>;
                         }) {
      return false;
    } else {
      using Next = typename Head::template after_type<Input>;
      return callable_with_mutable_context_impl<Context, Next, FnTail...>();
    }
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

  template <class Context, class Input>
  static consteval auto callable_with_const_context() -> bool {
    return true;
  }

  template <class Context, class Input>
  static consteval auto callable_with_mutable_context() -> bool {
    return true;
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
