#pragma once

#include <cstddef>
#include <string_view>
#include <tuple>
#include <type_traits>

#include "hosq/element.hh"
#include "hosq/entry.hh"
#include "hosq/pipeline/typed_containers.hh"
#include "hosq/string_literal.hh"
#include "hosq/try.hh"

namespace hosq {
namespace detail {

template <class E>
constexpr auto key_is(std::string_view want) -> bool {
  constexpr auto key = record_key<record_entry_t<E>>;
  return std::string_view{key.data(), key.size()} == want;
}

template <StringLiteral K, class Tuple>
constexpr auto field_index() -> std::size_t {
  return []<std::size_t... I>(std::index_sequence<I...>) {
    constexpr std::string_view want{K.data(), K.size()};
    constexpr bool match[] = {key_is<std::tuple_element_t<I, Tuple>>(want)...};
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
  if constexpr (ElemT<T>) {
    return *v;
  } else {
    return (v);
  }
}

}  // namespace detail

/// Look up a field of a Record schema (an aggregate or std::tuple of
/// constant-key entries) by its key. Accepts the schema itself or the
/// `value::Record<Schema>` element holding it, and returns the entry, so
/// `*field<"city">(address)` is the stored value. A missing or duplicate key
/// is a compile-time error.
template <StringLiteral K, class T>
[[nodiscard]] constexpr decltype(auto) field(T& record) {
  auto&& schema = detail::field_schema(record);
  auto fields = detail::record_tuple(schema);
  using Tuple = decltype(fields);
  constexpr std::size_t index = detail::field_index<K, Tuple>();
  static_assert(index < std::tuple_size_v<Tuple>,
                "hosq::field: no field with this key in the schema");
  return std::get<index>(fields);
}

}  // namespace hosq
