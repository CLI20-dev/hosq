#pragma once

#include <string_view>
#include <type_traits>

#include "hosq/pipeline.hh"
#include "hosq/string_literal.hh"

namespace hosq::match {

template <class T>
struct MatchResult {
  using value_type = T;

  MatchResult(bool matched, T value)
      : matched_(matched), value_(std::move(value)) {}
  [[nodiscard]] auto matched() const -> bool { return matched_; }
  [[nodiscard]] auto value() const -> const T& { return value_; }
  [[nodiscard]] auto value() -> T& { return value_; }

  /* implicit */ operator bool() const { return matched_; }

 private:
  bool matched_;
  T value_;
};

namespace detail {

// For StringLiteral NTTP: accepts string_view, compares content, stores
// monostate.
template <auto V>
struct LiteralMatchImpl {
  template <class Input>
    requires std::same_as<std::remove_cvref_t<Input>, std::string_view>

  auto operator()(std::remove_cvref_t<decltype(V)>& input) const {
    // return input == Str.view();
    return MatchResult{input == V, V};
  }
};

// For StringLiteral NTTP: accepts string_view, compares content, stores
// monostate.
template <StringLiteral Str>
struct LiteralMatchImpl<Str> {
  template <class Input>
    requires std::same_as<std::remove_cvref_t<Input>, std::string_view>

  auto operator()(std::string_view input) const {
    // return input == Str.view();
    return MatchResult{input == Str.view(), Str.view()};
  }
};

}  // namespace detail

template <auto V>
inline constexpr auto literal = hosq::step<detail::LiteralMatchImpl<V>>;

}  // namespace hosq::match
