#pragma once

#include <ser20/pipeline.hh>

namespace ser20 {

template <class In, class Out, auto Fn>
struct ActionFn {
  using input_type = std::remove_cvref_t<In>;
  using output_type = std::remove_cvref_t<Out>;

  template <class Input>
  static constexpr bool accepts_input =
      std::same_as<std::remove_cvref_t<Input>, input_type>;

  template <class Input>
  using after_type = output_type;

  template <class Context>
    requires requires(const Context& ctx, Result<input_type, Error> input) {
      Fn(ctx, std::move(input));
    }
  [[nodiscard]]
  constexpr auto operator()(const Context& ctx,
                            Result<input_type, Error> input) const
      -> decltype(Fn(ctx, std::move(input))) {
    return Fn(Context(ctx), std::move(input));
  }
};

template <class In, class Out, auto Fn>
inline constexpr ActionFn<In, Out, Fn> action{};

template <class In, class Storage, auto Fn>
struct TerminalActionFn {
  using input_type = std::remove_cvref_t<In>;
  using storage_type_ = std::remove_cvref_t<Storage>;

  template <class Input>
  static constexpr bool accepts_input =
      std::same_as<std::remove_cvref_t<Input>, input_type>;

  template <class Input>
  using storage_type = storage_type_;

  template <class Context>
    requires requires(const Context& ctx, Result<input_type, Error> input) {
      Fn(ctx, std::move(input));
    }
  [[nodiscard]]
  constexpr auto operator()(const Context& ctx,
                            Result<input_type, Error> input) const
      -> decltype(Fn(ctx, std::move(input))) {
    return Fn(ctx, std::move(input));
  }
};

template <class In, class Storage, auto Fn>
inline constexpr TerminalActionFn<In, Storage, Fn> terminal_action{};

template <class In, class Out, auto Fn>
struct MutableActionFn {
  using input_type = std::remove_cvref_t<In>;
  using output_type = std::remove_cvref_t<Out>;

  template <class Input>
  static constexpr bool accepts_input =
      std::same_as<std::remove_cvref_t<Input>, input_type>;

  template <class Input>
  using after_type = output_type;

  template <class Context>
    requires requires(Context& ctx, Result<input_type, Error> input) {
      Fn(ctx, std::move(input));
    }
  [[nodiscard]]
  constexpr auto operator()(Context& ctx, Result<input_type, Error> input) const
      -> decltype(Fn(ctx, std::move(input))) {
    return Fn(ctx, std::move(input));
  }
};

template <class In, class Out, auto Fn>
inline constexpr MutableActionFn<In, Out, Fn> mutable_action{};

template <class In, class Storage, auto Fn>
struct MutableTerminalActionFn {
  using input_type = std::remove_cvref_t<In>;
  using storage_type_ = std::remove_cvref_t<Storage>;

  template <class Input>
  static constexpr bool accepts_input =
      std::same_as<std::remove_cvref_t<Input>, input_type>;

  template <class Input>
  using storage_type = storage_type_;

  template <class Context>
    requires requires(Context& ctx, Result<input_type, Error> input) {
      Fn(ctx, std::move(input));
    }
  [[nodiscard]]
  constexpr auto operator()(Context& ctx, Result<input_type, Error> input) const
      -> decltype(Fn(ctx, std::move(input))) {
    return Fn(ctx, std::move(input));
  }
};

template <class In, class Storage, auto Fn>
inline constexpr MutableTerminalActionFn<In, Storage, Fn>
    mutable_terminal_action{};

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
};  // namespace ser20
