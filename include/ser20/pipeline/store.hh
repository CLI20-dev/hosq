#pragma once

#include "ser20/pipeline.hh"

namespace ser20::store {

namespace detail {

struct IgnoreImpl {
  template <class Input>
  static constexpr bool accepts_input = true;

  template <class Input>
  using storage_type = std::monostate;

  template <class Input>
  using after_type = void;

  template <class Context, class T>
  auto operator()(Context, Result<T, Error>) const -> Result<void, Error> {
    return Result<void, Error>{};
  };
};

};  // namespace detail

static constexpr auto ignore = step<detail::IgnoreImpl>;

};  // namespace ser20::store
