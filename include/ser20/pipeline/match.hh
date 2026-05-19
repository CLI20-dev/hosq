#pragma once

#include <type_traits>

#include "ser20/error.hh"
#include "ser20/pipeline_util.hh"
#include "ser20/result.hh"

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

}  // namespace detail

template <auto V>
inline constexpr auto literal =
    ser20::action<detail::literal_value_type<V>, detail::literal_value_type<V>,
                  detail::literal_fn<V>>;

}  // namespace ser20::match
