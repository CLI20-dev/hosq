#pragma once

#include "hosq/detail/type_traits.hh"

namespace hosq {

template <class T>
struct Try {
  static_assert(detail::always_false<T>,
                "Try<T>: T must be either an Elem or an Entry.");
};

template <detail::ElemT T>
struct Try<T> : public T {
  using base_type = T;
  using T::T;
  Try() = default;
};

template <detail::EntryT T>
struct Try<T> : public T {
  using base_type = T;
  using T::T;
  Try() = default;
};

};  // namespace hosq
