#pragma once

#include <ser20/context.hh>
#include <ser20/pipeline.hh>

namespace ser20 {

namespace detail {

template <class In, class Out, auto Fn>
struct ActionFn {
  static_assert(detail::always_false<ActionFn<In, Out, Fn>>,
                "Invalid action<In, Out, Fn>: Fn must be callable as one of: "
                "Fn(input) -> Out, "
                "Fn(input) -> Result<Out, Error>, "
                "Fn(input, const AnyContext&) -> Out, "
                "Fn(input, const AnyContext&) -> Result<Out, Error>, "
                "or, when In is void: "
                "Fn() -> Out, "
                "Fn() -> Result<Out, Error>, "
                "Fn(const AnyContext&) -> Out, "
                "Fn(const AnyContext&) -> Result<Out, Error>.");
};

template <class In, class Out, auto Fn>
  requires(
      !std::same_as<std::remove_cvref_t<In>, void> &&
      (
          requires(std::remove_cvref_t<In> in) {
            { Fn(std::move(in)) } -> std::same_as<std::remove_cvref_t<Out>>;
          } ||
          requires(std::remove_cvref_t<In> in) {
            {
              Fn(std::move(in))
            } -> std::same_as<Result<std::remove_cvref_t<Out>, Error>>;
          } ||
          requires(std::remove_cvref_t<In> in, const detail::AnyContext& ctx) {
            { Fn(std::move(in), ctx) }
                -> std::same_as<std::remove_cvref_t<Out>>;
          } ||
          requires(std::remove_cvref_t<In> in, const detail::AnyContext& ctx) {
            {
              Fn(std::move(in), ctx)
            } -> std::same_as<Result<std::remove_cvref_t<Out>, Error>>;
          }
      ))
struct ActionFn<In, Out, Fn> {
  using input_type = std::remove_cvref_t<In>;
  using output_type = std::remove_cvref_t<Out>;

  template <class Context>
    requires(
        requires(input_type in, const Context& ctx) {
          { Fn(std::move(in), ctx) } -> std::same_as<output_type>;
        } ||
        requires(input_type in, const Context& ctx) {
          { Fn(std::move(in), ctx) } -> std::same_as<Result<output_type, Error>>;
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
        } ||
            requires(input_type in) {
              { Fn(std::move(in)) } -> std::same_as<Result<output_type, Error>>;
            },
        "Action function must be callable as Fn(input) and return output_type "
        "or Result<output_type, Error>.");

    return std::invoke(Fn, std::move(input));
  }
};

template <class Out, auto Fn>
  requires((
      requires() {
        { Fn() } -> std::same_as<std::remove_cvref_t<Out>>;
      } ||
      requires() {
        { Fn() } -> std::same_as<Result<std::remove_cvref_t<Out>, Error>>;
      } ||
      requires(const detail::AnyContext& ctx) {
        { Fn(ctx) } -> std::same_as<std::remove_cvref_t<Out>>;
      } ||
      requires(const detail::AnyContext& ctx) {
        { Fn(ctx) } -> std::same_as<Result<std::remove_cvref_t<Out>, Error>>;
      }))
struct ActionFn<void, Out, Fn> {
  using input_type = void;
  using output_type = std::remove_cvref_t<Out>;

  template <class Context>
    requires(
        requires(const Context& ctx) {
          { Fn(ctx) } -> std::same_as<output_type>;
        } ||
        requires(const Context& ctx) {
          { Fn(ctx) } -> std::same_as<Result<output_type, Error>>;
        })
  [[nodiscard]]
  constexpr auto operator()(const Context& ctx) const
      -> std::invoke_result_t<decltype(Fn), const Context&> {
    return std::invoke(Fn, ctx);
  }

  [[nodiscard]]
  constexpr auto operator()() const {
    if constexpr (
        requires {
          { Fn() } -> std::same_as<output_type>;
        } ||
        requires {
          { Fn() } -> std::same_as<Result<output_type, Error>>;
        }) {
      return std::invoke(Fn);
    } else {
      static_assert(
          detail::always_false<decltype(Fn)>,
          "Action function must be callable as Fn() and return output_type or "
          "Result<output_type, Error>.");
    }
  }
};

template <class In, class Out, auto Fn>
struct MutableActionFn;

template <class In, class Out, auto Fn>
  requires(
      !std::same_as<std::remove_cvref_t<In>, void> &&
      (
          requires(std::remove_cvref_t<In> in) {
            { Fn(std::move(in)) } -> std::same_as<std::remove_cvref_t<Out>>;
          } ||
          requires(std::remove_cvref_t<In> in) {
            {
              Fn(std::move(in))
            } -> std::same_as<Result<std::remove_cvref_t<Out>, Error>>;
          } ||
          requires(std::remove_cvref_t<In> in, detail::AnyContext& ctx) {
            { Fn(std::move(in), ctx) }
                -> std::same_as<std::remove_cvref_t<Out>>;
          } ||
          requires(std::remove_cvref_t<In> in, detail::AnyContext& ctx) {
            {
              Fn(std::move(in), ctx)
            } -> std::same_as<Result<std::remove_cvref_t<Out>, Error>>;
          }
      ))
struct MutableActionFn<In, Out, Fn> {
  using input_type = std::remove_cvref_t<In>;
  using output_type = std::remove_cvref_t<Out>;

  template <class Context>
    requires(
        requires(input_type in, Context& ctx) {
          { Fn(std::move(in), ctx) } -> std::same_as<output_type>;
        } ||
        requires(input_type in, Context& ctx) {
          { Fn(std::move(in), ctx) } -> std::same_as<Result<output_type, Error>>;
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
        } ||
            requires(input_type in) {
              { Fn(std::move(in)) } -> std::same_as<Result<output_type, Error>>;
            },
        "Action function must be callable as Fn(input) and return output_type "
        "or Result<output_type, Error>.");

    return std::invoke(Fn, std::move(input));
  }
};

template <class Out, auto Fn>
  requires((
      requires() {
        { Fn() } -> std::same_as<std::remove_cvref_t<Out>>;
      } ||
      requires() {
        { Fn() } -> std::same_as<Result<std::remove_cvref_t<Out>, Error>>;
      } ||
      requires(const detail::AnyContext& ctx) {
        { Fn(ctx) } -> std::same_as<std::remove_cvref_t<Out>>;
      } ||
      requires(const detail::AnyContext& ctx) {
        { Fn(ctx) } -> std::same_as<Result<std::remove_cvref_t<Out>, Error>>;
      }))
struct MutableActionFn<void, Out, Fn> {
  using input_type = void;
  using output_type = std::remove_cvref_t<Out>;

  template <class Context>
    requires(
        requires(Context& ctx) {
          { Fn(ctx) } -> std::same_as<output_type>;
        } ||
        requires(Context& ctx) {
          { Fn(ctx) } -> std::same_as<Result<output_type, Error>>;
        })
  [[nodiscard]]
  constexpr auto operator()(Context& ctx) const
      -> std::invoke_result_t<decltype(Fn), Context&> {
    return std::invoke(Fn, ctx);
  }

  [[nodiscard]]
  constexpr auto operator()() const {
    if constexpr (
        requires {
          { Fn() } -> std::same_as<output_type>;
        } ||
        requires {
          { Fn() } -> std::same_as<Result<output_type, Error>>;
        }) {
      return std::invoke(Fn);
    } else {
      static_assert(
          detail::always_false<decltype(Fn)>,
          "Action function must be callable as Fn() and return output_type or "
          "Result<output_type, Error>.");
    }
  }
};

struct NoopDecoder {
  template <class>
  using storage_type = std::monostate;

  constexpr auto operator()(auto&) const -> Result<void, Error> { return {}; }

  constexpr auto operator()(const auto&) const -> Result<void, Error> {
    return {};
  }
};

struct NoopEncoder {
  template <class T, class Context>
  constexpr auto operator()(T&&, Context&) const -> Result<void, Error> {
    return {};
  }

  template <class T, class Context>
  constexpr auto operator()(T&&, const Context&) const -> Result<void, Error> {
    return {};
  }

  template <class T>
  constexpr auto operator()(T&&) const -> Result<void, Error> {
    return {};
  }
};

}  // namespace detail

template <class In, class Out, auto Fn>
inline constexpr detail::ActionFn<In, Out, Fn> action{};

template <class In, class Out, auto Fn>
inline constexpr detail::MutableActionFn<In, Out, Fn> mutable_action{};

inline constexpr auto noop_decoder = step<detail::NoopDecoder>;

inline constexpr auto noop_encoder = step<detail::NoopEncoder>;

};  // namespace ser20
