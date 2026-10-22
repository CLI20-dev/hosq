#pragma once

#include <cstddef>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

#include "hosq/detail/as_tuple.hh"
#include "hosq/detail/type_traits.hh"
#include "hosq/element.hh"
#include "hosq/entry.hh"
#include "hosq/string_literal.hh"
#include "hosq/try.hh"

namespace hosq {
namespace detail {

template <class E>
constexpr auto key_is(std::string_view want) -> bool {
  using U = std::remove_cvref_t<E>;
  if constexpr (requires { U::key_literal; }) {
    return U::key() == want;
  } else {
    return false;
  }
}

template <StringLiteral K, class Tuple>
constexpr auto field_index() -> std::size_t {
  return []<std::size_t... I>(std::index_sequence<I...>) {
    constexpr std::string_view want{K.data(), K.size()};
    constexpr bool match[] = {key_is<std::tuple_element_t<I, Tuple>>(want)...,
                              false};
    std::size_t found = sizeof...(I);
    std::size_t hits = 0;
    for (std::size_t i = 0; i < sizeof...(I); ++i) {
      if (match[i]) {
        found = i;
        ++hits;
      }
    }
    return hits == 1 ? found : sizeof...(I);
  }(std::make_index_sequence<std::tuple_size_v<Tuple>>{});
}

template <class T>
decltype(auto) field_schema(T& v) {
  if constexpr (ElemT<T> || EntryT<T> || TryT<T>) {
    return (*v);
  } else {
    return (v);
  }
}

template <class T>
auto field_tuple(T& schema) {
  if constexpr (requires { std::tuple_size<std::remove_cv_t<T>>::value; }) {
    return std::apply([](auto&... f) { return std::tie(f...); }, schema);
  } else {
    return as_tuple(schema);
  }
}

}  // namespace detail

/// Look up a field of a record schema (an aggregate or std::tuple of
/// constant-key entries) by its key. Accepts the schema itself or an Elem or
/// Entry holding it, and returns the field's entry, so `*field<"city">(addr)`
/// is the stored value. A missing or duplicate key is a compile-time error.
template <StringLiteral K, class T>
[[nodiscard]] constexpr decltype(auto) field(T& record) {
  auto&& schema = detail::field_schema(record);
  auto fields = detail::field_tuple(schema);
  using Tuple = decltype(fields);
  constexpr std::size_t index = detail::field_index<K, Tuple>();
  static_assert(index < std::tuple_size_v<Tuple>,
                "hosq::field: key must match exactly one field in the schema");
  return std::get<index>(fields);
}

}  // namespace hosq
