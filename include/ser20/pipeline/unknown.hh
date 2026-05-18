#pragma once

#include "ser20/pipeline.hh"

namespace ser20::unknown {

namespace detail {

struct IgnoreImpl {
  template <class Input>
  using storage_type = std::monostate;

  template <class T>
  auto operator()(T) const -> void {};
};

};  // namespace detail

static constexpr auto ignore = step<detail::IgnoreImpl>;

};  // namespace ser20::unknown
