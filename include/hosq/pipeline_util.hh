#pragma once

#include <concepts>
#include <functional>
#include <type_traits>
#include <utility>
#include <variant>

#include "hosq/context.hh"
#include "hosq/detail/always_false.hh"
#include "hosq/pipeline.hh"

namespace hosq {

namespace detail {

template <class In, class Out, auto Fn>
struct ActionFn {
  static_assert(detail::always_false<ActionFn<In, Out, Fn>>,
                "Invalid action<In, Out, Fn>: Fn must be callable as one of: "
                "Fn(input) -> Out, "
                "Fn(input, const AnyContext&) -> Out, "
                "or, when In is void: "
                "Fn() -> Out, "
                "Fn(const AnyContext&) -> Out.");
};

template <class In, class Out, auto Fn>
  requires(
      !std::same_as<std::remove_cvref_t<In>, void> &&
      (
          requires(std::remove_cvref_t<In> in) {
            { Fn(std::move(in)) } -> std::same_as<std::remove_cvref_t<Out>>;
          }  ||
          requires(std::remove_cvref_t<In> in, const detail::AnyContext& ctx) {
            { Fn(std::move(in), ctx) }
                -> std::same_as<std::remove_cvref_t<Out>>;
          }       ))
struct ActionFn<In, Out, Fn> {
  using input_type = std::remove_cvref_t<In>;
  using output_type = std::remove_cvref_t<Out>;

  template <class Context>
    requires(requires(input_type in, const Context& ctx) {
      { Fn(std::move(in), ctx) } -> std::same_as<output_type>;
    })
  [[nodiscard]]
  constexpr auto operator()(input_type input, const Context& ctx) const
      -> std::invoke_result_t<decltype(Fn), input_type, const Context&> {
    return std::invoke(Fn, std::move(input), ctx);
  }

  [[nodiscard]]
  constexpr auto operator()(input_type input) const {
    static_assert(
        requires(input_type in) {
          { Fn(std::move(in)) } -> std::same_as<output_type>;
        },
        "Action function must be callable as Fn(input) and return output_type");

    return std::invoke(Fn, std::move(input));
  }
};

template <class Out, auto Fn>
  requires((
      requires() {
        { Fn() } -> std::same_as<std::remove_cvref_t<Out>>;
      } ||
      requires(const detail::AnyContext& ctx) {
        { Fn(ctx) } -> std::same_as<std::remove_cvref_t<Out>>;
      }))
struct ActionFn<void, Out, Fn> {
  using input_type = void;
  using output_type = std::remove_cvref_t<Out>;

  template <class Context>
    requires(requires(const Context& ctx) {
      { Fn(ctx) } -> std::same_as<output_type>;
    })
  [[nodiscard]]
  constexpr auto operator()(const Context& ctx) const
      -> std::invoke_result_t<decltype(Fn), const Context&> {
    return std::invoke(Fn, ctx);
  }

  [[nodiscard]]
  constexpr auto operator()() const {
    if constexpr (requires {
                    { Fn() } -> std::same_as<output_type>;
                  }) {
      return std::invoke(Fn);
    } else {
      static_assert(
          detail::always_false<decltype(Fn)>,
          "Action function must be callable as Fn() and return output_type.");
    }
  }
};

template <class In, class Out, auto Fn>
struct MutableActionFn {
  static_assert(detail::always_false<MutableActionFn<In, Out, Fn>>,
                "Invalid mutable_action<In, Out, Fn>: Fn must be callable as "
                "one of: "
                "Fn(input) -> Out, "
                "Fn(input, AnyContext&) -> Out, "
                "or, when In is void: "
                "Fn() -> Out, "
                "Fn(AnyContext&) -> Out.");
};

template <class In, class Out, auto Fn>
  requires(
      !std::same_as<std::remove_cvref_t<In>, void> &&
      (
          requires(std::remove_cvref_t<In> in) {
            { Fn(std::move(in)) } -> std::same_as<std::remove_cvref_t<Out>>;
          }  ||
          requires(std::remove_cvref_t<In> in, detail::AnyContext& ctx) {
            { Fn(std::move(in), ctx) } -> std::same_as<std::remove_cvref_t<Out>>;
          }
      ))
struct MutableActionFn<In, Out, Fn> {
  using input_type = std::remove_cvref_t<In>;
  using output_type = std::remove_cvref_t<Out>;

  template <class Context>
    requires(requires(input_type in, Context& ctx) {
      { Fn(std::move(in), ctx) } -> std::same_as<output_type>;
    })
  [[nodiscard]]
  constexpr auto operator()(input_type input, Context& ctx) const
      -> std::invoke_result_t<decltype(Fn), input_type, Context&> {
    return std::invoke(Fn, std::move(input), ctx);
  }

  [[nodiscard]]
  constexpr auto operator()(input_type input) const {
    static_assert(
        requires(input_type in) {
          { Fn(std::move(in)) } -> std::same_as<output_type>;
        },
        "Action function must be callable as Fn(input) and return output_type.");

    return std::invoke(Fn, std::move(input));
  }
};

template <class Out, auto Fn>
  requires((
      requires() {
        { Fn() } -> std::same_as<std::remove_cvref_t<Out>>;
      } ||
      requires(detail::AnyContext& ctx) {
        { Fn(ctx) } -> std::same_as<std::remove_cvref_t<Out>>;
      }))
struct MutableActionFn<void, Out, Fn> {
  using input_type = void;
  using output_type = std::remove_cvref_t<Out>;

  template <class Context>
    requires(requires(Context& ctx) {
      { Fn(ctx) } -> std::same_as<output_type>;
    })
  [[nodiscard]]
  constexpr auto operator()(Context& ctx) const
      -> std::invoke_result_t<decltype(Fn), Context&> {
    return std::invoke(Fn, ctx);
  }

  [[nodiscard]]
  constexpr auto operator()() const {
    if constexpr (requires {
                    { Fn() } -> std::same_as<output_type>;
                  }) {
      return std::invoke(Fn);
    } else {
      static_assert(
          detail::always_false<decltype(Fn)>,
          "Action function must be callable as Fn() and return output_type.");
    }
  }
};

struct NoopDecoder {
  template <class>
  using storage_type = std::monostate;

  constexpr auto operator()(auto&) const -> void {}

  constexpr auto operator()(const auto&) const -> void {}
};

struct NoopEncoder {
  template <class T, class Context>
  constexpr auto operator()(T&&, Context&) const -> void {}

  template <class T, class Context>
  constexpr auto operator()(T&&, const Context&) const -> void {}

  template <class T>
  constexpr auto operator()(T&&) const -> void {}
};

}  // namespace detail

template <class In, class Out, auto Fn>
inline constexpr detail::ActionFn<In, Out, Fn> action{};

template <class In, class Out, auto Fn>
inline constexpr detail::MutableActionFn<In, Out, Fn> mutable_action{};

inline constexpr auto noop_decoder = step<detail::NoopDecoder>;

inline constexpr auto noop_encoder = step<detail::NoopEncoder>;

}  // namespace hosq
