#pragma once

#include "ser20/pipeline.hh"

namespace ser20::unknown {

namespace detail {

struct IdentityImpl {
  template <class Input>
  static constexpr bool accepts_input = true;

  template <class Input>
  using after_type = Input;

  template <class Context, class Input>
  auto operator()(Context, Result<Input, Error> input) const
      -> Result<Input, Error> {
    return input;
  };
};

};  // namespace detail

static constexpr auto identity = step<detail::IdentityImpl>;

};  // namespace ser20::unknown
