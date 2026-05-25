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
  // using terminal_type = typename std::tuple_element_t<
  //     2, typename std::remove_cvref_t<T>::decoder_info>::type;
  // static_assert(std::same_as<terminal_type, bool>,
  //               "Try<T>: Elem decoder terminal type must be bool.");
  Try() = default;
};

template <detail::EntryT T>
struct Try<T> : public T {
  using base_type = T;
  using T::T;
  // using key_type = typename std::remove_cvref_t<T>::key_type;
  // using terminal_type = typename std::tuple_element_t<
  //     2, typename std::remove_cvref_t<key_type>::decoder_info>::type;
  // static_assert(std::same_as<terminal_type, bool>,
  //               "Try<T>: Entry key Elem decoder terminal type must be bool.");
  Try() = default;
};

};  // namespace hosq
