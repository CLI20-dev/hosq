#pragma once

#include <ser20/context.hh>
#include <ser20/pipeline.hh>

namespace ser20 {

namespace detail {

template <class In, class Out, auto Fn>
  requires(
      requires(std::remove_cvref_t<In> in) {
        { Fn(std::move(in)) } -> std::same_as<std::remove_cvref_t<Out>>;
      } ||
      requires(std::remove_cvref_t<In> in) {
        {
          Fn(std::move(in))
        } -> std::same_as<Result<std::remove_cvref_t<Out>, Error>>;
      } ||
      requires(std::remove_cvref_t<In> in, const detail::AnyContext& ctx) {
        { Fn(std::move(in), ctx) } -> std::same_as<std::remove_cvref_t<Out>>;
      } ||
      requires(std::remove_cvref_t<In> in, const detail::AnyContext& ctx) {
        {
          Fn(std::move(in), ctx)
        } -> std::same_as<Result<std::remove_cvref_t<Out>, Error>>;
      })
struct ActionFn {
  using input_type = std::remove_cvref_t<In>;
  using output_type = std::remove_cvref_t<Out>;

  template <class Input, class Context>
    requires std::same_as<std::remove_cvref_t<Input>, input_type> && (
         requires(std::remove_cvref_t<In> in, const Context& ctx) {
            { Fn(std::move(in), ctx) } -> std::same_as<output_type>;
          } ||
          requires(std::remove_cvref_t<In> in, const Context& ctx) {
            { Fn(std::move(in), ctx) } -> std::same_as<Result<output_type, Error>>;
          })
  [[nodiscard]]
  constexpr auto operator()(input_type input, const Context& ctx) const
      -> std::invoke_result_t<decltype(Fn), input_type, const Context&> {
    return std::invoke(Fn, std::move(input), ctx);
  }

  template <class Input>
    requires std::same_as<std::remove_cvref_t<Input>, input_type> && (
        requires(std::remove_cvref_t<In> in) {
          { Fn(std::move(in)) } -> std::same_as<output_type>;
        } ||
        requires(std::remove_cvref_t<In> in) {
          { Fn(std::move(in)) } -> std::same_as<Result<output_type, Error>>;
        })
  [[nodiscard]]
  constexpr auto operator()(input_type input) const
      -> std::invoke_result_t<decltype(Fn), input_type> {
    return std::invoke(Fn, std::move(input));
  }
};

template <class Out, auto Fn>
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
    static_assert(
        requires {
          { Fn() } -> std::same_as<output_type>;
        } ||
            requires {
              { Fn() } -> std::same_as<Result<output_type, Error>>;
            },
        "Action function must be callable as Fn() and return output_type or "
        "Result<output_type, Error>.");

    return std::invoke(Fn);
  }
};

}  // namespace detail

template <class In, class Out, auto Fn>
inline constexpr detail::ActionFn<In, Out, Fn> action{};

// template <class In, class Storage, auto Fn>
// struct TerminalActionFn {
//   using input_type = std::remove_cvref_t<In>;
//   using storage_type_ = std::remove_cvref_t<Storage>;
//
//   template <class Input>
//   static constexpr bool accepts_input =
//       std::same_as<std::remove_cvref_t<Input>, input_type>;
//
//   template <class Input>
//   using storage_type = storage_type_;
//
//   template <class Context>
//     requires requires(Result<input_type, Error> input, const Context& ctx) {
//       Fn(std::move(input), ctx);
//     }
//   [[nodiscard]]
//   constexpr auto operator()(const Context& ctx,
//                             Result<input_type, Error> input) const
//       -> decltype(Fn(ctx, std::move(input))) {
//     return Fn(ctx, std::move(input));
//   }
// };
//
// template <class In, class Storage, auto Fn>
// inline constexpr TerminalActionFn<In, Storage, Fn> terminal_action{};
//
// template <class In, class Out, auto Fn>
// struct MutableActionFn {
//   using input_type = std::remove_cvref_t<In>;
//   using output_type = std::remove_cvref_t<Out>;
//
//   template <class Input>
//   static constexpr bool accepts_input =
//       std::same_as<std::remove_cvref_t<Input>, input_type>;
//
//   template <class Input>
//   using after_type = output_type;
//
//   template <class Context>
//     requires requires(Context& ctx, Result<input_type, Error> input) {
//       Fn(ctx, std::move(input));
//     }
//   [[nodiscard]]
//   constexpr auto operator()(Context& ctx, Result<input_type, Error> input)
//   const
//       -> decltype(Fn(ctx, std::move(input))) {
//     return Fn(ctx, std::move(input));
//   }
// };
//
// template <class In, class Out, auto Fn>
// inline constexpr MutableActionFn<In, Out, Fn> mutable_action{};
//
// template <class In, class Storage, auto Fn>
// struct MutableTerminalActionFn {
//   using input_type = std::remove_cvref_t<In>;
//   using storage_type_ = std::remove_cvref_t<Storage>;
//
//   template <class Input>
//   static constexpr bool accepts_input =
//       std::same_as<std::remove_cvref_t<Input>, input_type>;
//
//   template <class Input>
//   using storage_type = storage_type_;
//
//   template <class Context>
//     requires requires(Context& ctx, Result<input_type, Error> input) {
//       Fn(ctx, std::move(input));
//     }
//   [[nodiscard]]
//   constexpr auto operator()(Context& ctx, Result<input_type, Error> input)
//   const
//       -> decltype(Fn(ctx, std::move(input))) {
//     return Fn(ctx, std::move(input));
//   }
// };
//
// template <class In, class Storage, auto Fn>
// inline constexpr MutableTerminalActionFn<In, Storage, Fn>
//     mutable_terminal_action{};
//

};  // namespace ser20
