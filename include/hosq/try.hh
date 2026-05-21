#pragma once

#include "hosq/element.hh"
#include "hosq/entry.hh"
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
  using terminal_type = typename std::tuple_element_t<
      2, typename std::remove_cvref_t<T>::decoder_info>::type;
  static_assert(std::same_as<terminal_type, bool>,
                "Try<T>: Elem decoder terminal type must be "
                "bool/Result<bool, Error>.");
  Try() = default;
};

template <detail::EntryT T>
struct Try<T> : public T {
  using base_type = T;
  using T::T;
  using key_type = typename std::remove_cvref_t<T>::key_type;
  using terminal_type = typename std::tuple_element_t<
      2, typename std::remove_cvref_t<key_type>::decoder_info>::type;
  static_assert(std::same_as<terminal_type, bool>,
                "Try<T>: Entry key Elem decoder terminal type must be "
                "bool/Result<bool, Error>.");
  Try() = default;
};

namespace detail {

template <class T>
struct IsTry : std::false_type {};

template <class T>
struct IsTry<::hosq::Try<T>> : std::true_type {};

template <class T>
concept TryT = IsTry<std::remove_cvref_t<T>>::value;

template <class Tuple>
struct TryElemTuple : std::false_type {};

template <ElemT... Ts>
struct TryElemTuple<std::tuple<Ts...>>
    : std::bool_constant<(sizeof...(Ts) > 0) && (TryT<Ts> && ...)> {};

template <class T>
concept TryElemTupleT = ElemTuple<std::remove_cvref_t<T>>::value;

template <class T>
concept AsTryElemTupleT = requires(T t) {
  { hosq::detail::as_tuple(t) } -> ElemTupleT;
};

template <class Tuple>
struct TryEntryTuple : std::false_type {};

template <EntryT... Ts>
struct TryEntryTuple<std::tuple<Ts...>>
    : std::bool_constant<(sizeof...(Ts) > 0) && (TryT<Ts> && ...)> {};

template <class T>
concept TryEntryTupleT = EntryTuple<std::remove_cvref_t<T>>::value;

template <class T>
concept AsTryEntryTupleT = requires(T t) {
  { hosq::detail::as_tuple(t) } -> EntryTupleT;
};

}  // namespace detail

};  // namespace hosq
