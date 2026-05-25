#pragma once

#include <concepts>
#include <functional>
#include <type_traits>
#include <utility>
#include <variant>

#include "hosq/pipeline.hh"

namespace hosq {

namespace detail {

template <class In, class Out, auto Fn>
struct ActionFn {
  using input_type = std::remove_cvref_t<In>;
  using output_type = std::remove_cvref_t<Out>;

  static_assert(!std::same_as<input_type, void>,
                "ActionFn<void, Out, Fn> must use the void specialization");

  template <class Context>
    requires requires(input_type in, const Context& ctx) {
      { std::invoke(Fn, std::move(in), ctx) } -> std::same_as<output_type>;
    }
  [[nodiscard]]
  constexpr auto operator()(input_type input, const Context& ctx) const
      -> output_type {
    return std::invoke(Fn, std::move(input), ctx);
  }

  [[nodiscard]]
  constexpr auto operator()(input_type input) const -> output_type
    requires requires(input_type in) {
      { std::invoke(Fn, std::move(in)) } -> std::same_as<output_type>;
    }
  {
    return std::invoke(Fn, std::move(input));
  }
};

template <class Out, auto Fn>
struct ActionFn<void, Out, Fn> {
  using input_type = void;
  using output_type = std::remove_cvref_t<Out>;

  template <class Context>
    requires requires(const Context& ctx) {
      { std::invoke(Fn, ctx) } -> std::same_as<output_type>;
    }
  [[nodiscard]]
  constexpr auto operator()(const Context& ctx) const -> output_type {
    return std::invoke(Fn, ctx);
  }

  [[nodiscard]]
  constexpr auto operator()() const -> output_type
    requires requires {
      { std::invoke(Fn) } -> std::same_as<output_type>;
    }
  {
    return std::invoke(Fn);
  }
};

template <class In, class Out, auto Fn>
struct MutableActionFn {
  using input_type = std::remove_cvref_t<In>;
  using output_type = std::remove_cvref_t<Out>;

  static_assert(
      !std::same_as<input_type, void>,
      "MutableActionFn<void, Out, Fn> must use the void specialization");

  template <class Context>
    requires requires(input_type in, Context& ctx) {
      { std::invoke(Fn, std::move(in), ctx) } -> std::same_as<output_type>;
    }
  [[nodiscard]]
  constexpr auto operator()(input_type input, Context& ctx) const
      -> output_type {
    return std::invoke(Fn, std::move(input), ctx);
  }

  [[nodiscard]]
  constexpr auto operator()(input_type input) const -> output_type
    requires requires(input_type in) {
      { std::invoke(Fn, std::move(in)) } -> std::same_as<output_type>;
    }
  {
    return std::invoke(Fn, std::move(input));
  }
};

template <class Out, auto Fn>
struct MutableActionFn<void, Out, Fn> {
  using input_type = void;
  using output_type = std::remove_cvref_t<Out>;

  template <class Context>
    requires requires(Context& ctx) {
      { std::invoke(Fn, ctx) } -> std::same_as<output_type>;
    }
  [[nodiscard]]
  constexpr auto operator()(Context& ctx) const -> output_type {
    return std::invoke(Fn, ctx);
  }

  [[nodiscard]]
  constexpr auto operator()() const -> output_type
    requires requires {
      { std::invoke(Fn) } -> std::same_as<output_type>;
    }
  {
    return std::invoke(Fn);
  }
};

struct NoopDecoder {
  template <class Input>
  using storage_type = std::monostate;

  template <class Context>
  constexpr auto operator()(Context&) const -> void {}

  template <class Context>
  constexpr auto operator()(const Context&) const -> void {}
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
inline constexpr auto action = step<detail::ActionFn<In, Out, Fn>>;

template <class In, class Out, auto Fn>
inline constexpr auto mutable_action =
    step<detail::MutableActionFn<In, Out, Fn>>;

inline constexpr auto noop_decoder = step<detail::NoopDecoder>;

inline constexpr auto noop_encoder = step<detail::NoopEncoder>;

}  // namespace hosq
