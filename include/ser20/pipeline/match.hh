#pragma once

#include <string_view>
#include <type_traits>

#include "ser20/error.hh"
#include "ser20/pipeline_util.hh"
#include "ser20/result.hh"
#include "ser20/string_literal.hh"

namespace ser20::match {

namespace detail {

template <auto V>
using literal_value_type = std::remove_cvref_t<decltype(V)>;

template <auto V>
inline constexpr auto literal_fn =
    [](literal_value_type<V> input) -> Result<literal_value_type<V>, Error> {
  if (input == V) {
    return input;
  }
  return unexpected(Error{
      .code = ErrorCode::candidate_rejected,
  });
};

// For StringLiteral NTTP: accepts string_view, compares content, stores
// monostate.
template <StringLiteral Str>
struct StringLiteralMatchImpl {
  template <class Input>
    requires std::same_as<std::remove_cvref_t<Input>, std::string_view>
  using storage_type = std::monostate;

  auto operator()(std::string_view input) const -> Result<void, Error> {
    if (input == Str.view()) return {};
    return unexpected(Error{.code = ErrorCode::candidate_rejected});
  }
};

struct AnyImpl {
  template <class T>
  constexpr auto operator()(T&& value) const -> std::remove_cvref_t<T> {
    return std::forward<T>(value);
  }
};

}  // namespace detail

namespace detail {

template <auto V>
struct LiteralStep {
  static constexpr auto value =
      ser20::action<literal_value_type<V>, literal_value_type<V>, literal_fn<V>>;
};

template <StringLiteral Str>
struct LiteralStep<Str> {
  static constexpr auto value = ser20::step<StringLiteralMatchImpl<Str>>;
};

}  // namespace detail

template <auto V>
inline constexpr auto literal = detail::LiteralStep<V>::value;

inline constexpr auto any = ser20::Pipeline<detail::AnyImpl{}>{};

}  // namespace ser20::match
