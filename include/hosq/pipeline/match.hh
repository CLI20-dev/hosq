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
#if defined(__clang__) || defined(__GNUC__)
#if defined(__aarch64__) || defined(__arm64__)
    __asm__("// String Match");
#else
    __asm__("# String Match");
#endif
#endif
    return MatchResult{input == Str.view(), input};
  }
};

}  // namespace detail

template <auto V>
inline constexpr auto literal = hosq::step<detail::LiteralMatchImpl<V>>;

}  // namespace hosq::match
