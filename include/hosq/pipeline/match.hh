#pragma once

#include <string_view>
#include <type_traits>

#include "hosq/match_result.hh"
#include "hosq/pipeline.hh"
#include "hosq/string_literal.hh"

namespace hosq::match {
namespace detail {

template <auto V>
struct LiteralMatchImpl {
  using value_type = std::remove_cvref_t<decltype(V)>;

  auto operator()(const value_type& input) const {
    return MatchResult{input == V, input};
  }
};

template <StringLiteral Str>
struct LiteralMatchImpl<Str> {
  auto operator()(std::string_view input) const {
    return MatchResult{input == Str.view(), input};
  }
};

}  // namespace detail

template <auto V>
inline constexpr auto literal = hosq::step<detail::LiteralMatchImpl<V>>;

}  // namespace hosq::match
