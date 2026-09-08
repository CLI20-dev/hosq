#pragma once

#include <type_traits>
#include <utility>
#include "hosq/detail/compiler.hh"

namespace hosq::ext {

// Optional reader-specific entry input. An unsupported input uses normal decoding.
template <class Reader> struct EntryInput {};

// A reader-specific input can expose its native representation to an accepting
// step, and its ordinary representation to every other step. No sequence of
// pipeline steps is inspected or replaced.
template <class Input>
struct InputView {
  template <class Step, class Context>
  using type = Input;

  template <class Step, class Context>
  static constexpr auto get(Input& input, Context&) -> Input& { return input; }
};

template <>
struct InputView<void> {
  template <class Step, class Context>
  using type = void;
};

}  // namespace hosq::ext

namespace hosq::detail {

template <class Input, class Step, class Context>
using step_input_t = typename ext::InputView<std::remove_cvref_t<Input>>
    ::template type<Step, Context>;

template <class Ext, class Default, class = void>
struct ExtensionInput { using type = Default; };

template <class Ext, class Default>
struct ExtensionInput<Ext, Default, std::void_t<typename Ext::input_type>> {
  using type = typename Ext::input_type;
};

template <class Ext, class Default>
using extension_input_t = typename ExtensionInput<Ext, Default>::type;

// A terminal scan keeps its public ordinary View and resolved-state contract.
template <class Context, class F>
HOSQ_ALWAYS_INLINE constexpr decltype(auto) invoke_terminal(Context& ctx, F&& fn) {
  using Result = std::invoke_result_t<F>;
  using Ordinary = step_input_t<Result, void, Context>;
  if constexpr (std::is_same_v<std::remove_cvref_t<Result>, Ordinary>) {
    return std::forward<F>(fn)();
  } else {
    auto input = std::forward<F>(fn)();
    return ext::InputView<std::remove_cvref_t<Result>>::template get<void>(input, ctx);
  }
}

}  // namespace hosq::detail
