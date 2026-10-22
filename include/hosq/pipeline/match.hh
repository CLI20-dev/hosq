#pragma once

#include <concepts>
#include <string_view>
#include <type_traits>
#include <utility>

#include "hosq/match_result.hh"
#include "hosq/pipeline.hh"
#include "hosq/string_literal.hh"

namespace hosq::match {
namespace detail {

// A StringLiteral NTTP matches std::string_view input. It is branched on inside
// the primary template: a partial specialization on StringLiteral is not more
// specialized than `template <auto V>` and is rejected by Clang 22.
template <auto V>
struct LiteralMatchImpl {
  using value_type = std::remove_cvref_t<decltype(V)>;
  using input_type = std::conditional_t<is_string_literal_v<value_type>,
                                        std::string_view, value_type>;

  constexpr auto operator()(const input_type& input) const {
    if constexpr (is_string_literal_v<value_type>)
      return MatchResult<std::string_view>{input == V.view(), input};
    else
      return MatchResult<value_type>{input == V, input};
  }
};

template <auto Fn>
struct PredicateImpl {
 private:
  template <class Input, class Context>
  static consteval auto accepts_input() -> bool {
    if constexpr (requires(Input& input, Context& ctx) { Fn(input, ctx); })
      return requires(Input& input, Context& ctx) {
        { Fn(input, ctx) } -> std::same_as<bool>;
      };
    else
      return requires(Input& input) {
        { Fn(input) } -> std::same_as<bool>;
      };
  }
  template <class Context>
  static consteval auto accepts_context() -> bool {
    if constexpr (requires(Context& ctx) { Fn(ctx); })
      return requires(Context& ctx) {
        { Fn(ctx) } -> std::same_as<bool>;
      };
    else
      return requires {
        { Fn() } -> std::same_as<bool>;
      };
  }

 public:
  // Predicates observe the payload before ownership continues to the next step.
  // A predicate that consumes a move-only value cannot preserve that payload.
  template <class Input, class Context>
    requires(accepts_input<Input, Context>())
  constexpr auto operator()(Input input, Context& ctx) const
      -> MatchResult<std::remove_cvref_t<Input>> {
    const bool matched = [&] {
      if constexpr (requires { Fn(input, ctx); })
        return Fn(input, ctx);
      else
        return Fn(input);
    }();
    return {matched, std::move(input)};
  }
  template <class Context>
    requires(accepts_context<Context>())
  constexpr auto operator()(Context& ctx) const -> MatchResult<void> {
    if constexpr (requires { Fn(ctx); })
      return MatchResult<void>{Fn(ctx)};
    else
      return MatchResult<void>{Fn()};
  }
};

}  // namespace detail

template <auto V>
inline constexpr auto literal = hosq::step<detail::LiteralMatchImpl<V>>;

/// Convert an exact bool predicate to candidate control, retaining its input.
/// Context-aware signatures take priority over signatures without a context.
template <auto Fn>
inline constexpr auto predicate = hosq::step<detail::PredicateImpl<Fn>>;

}  // namespace hosq::match
