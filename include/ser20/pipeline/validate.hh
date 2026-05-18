#pragma once

#include "ser20/pipeline.hh"

namespace ser20::unknown {

namespace detail {

struct IdentityImpl {
  template <class Input>
  auto operator()(Input input) const -> Input {
    return input;
  };
};

};  // namespace detail

static constexpr auto identity = step<detail::IdentityImpl>;

};  // namespace ser20::unknown
