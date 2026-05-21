#pragma once

#include "hosq/pipeline.hh"

namespace hosq::unknown {

namespace detail {

struct IgnoreImpl {
  template <class Input>
  using storage_type = std::monostate;

  template <class T>
  auto operator()(T) const -> void {}
};

struct IdentityImpl {
  template <class T>
  auto operator()(T value) const -> T {
    return value;
  }
};

}  // namespace detail

/// Discards any unknown value; storage_type is monostate.
inline constexpr auto ignore = step<detail::IgnoreImpl>;

/// Passes any value through unchanged.
inline constexpr auto identity = step<detail::IdentityImpl>;

}  // namespace hosq::unknown
