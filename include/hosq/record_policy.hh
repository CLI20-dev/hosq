#pragma once

#include <array>
#include <concepts>
#include <cstddef>
#include <string_view>
#include <tuple>
#include <type_traits>

#include "hosq/entry.hh"

namespace hosq::types {

/// Additional input names for a Record field; output always uses its key.
template <StringLiteral... Names>
struct InputAliases {
  static constexpr auto names =
      std::array<std::string_view, sizeof...(Names)>{Names.view()...};
};

/// A zero-argument factory for missing Record members only. The returned value
/// initializes E::storage_type; present input still runs E's normal decoder.
template <auto Factory>
struct Default {
  static auto make() { return Factory(); }
};

/// Unknown input members are rejected unless ignore is explicitly selected.
/// Ignore uses decode::skip's structural traversal; discarded payloads do not
/// receive semantic / complete scalar grammar validation.
enum class UnknownMembers { reject, ignore };

}  // namespace hosq::types

namespace hosq::detail {

template <class T>
struct IsInputAliases : std::false_type {};
template <StringLiteral... Names>
struct IsInputAliases<types::InputAliases<Names...>> : std::true_type {};

template <class T>
struct IsRecordDefault : std::false_type {};
template <auto Factory>
struct IsRecordDefault<types::Default<Factory>> : std::true_type {};

template <StringLiteral K, ElemT E, class... Policies>
  requires(sizeof...(Policies) > 0)
struct IsEntry<Entry<Key<K>, E, Policies...>> : std::true_type {};
template <StringLiteral K, ElemT E, class... Policies>
  requires(sizeof...(Policies) > 0)
struct IsConstKeyEntry<Entry<Key<K>, E, Policies...>> : std::true_type {};

}  // namespace hosq::detail

namespace hosq {

// Retain the exact policy-free Entry type and inherited field interface.
// A declared default constructor prevents aggregate decomposition of the field.
template <StringLiteral K, detail::ElemT E, class... Policies>
  requires(sizeof...(Policies) > 0)
struct Entry<Key<K>, E, Policies...> : Entry<Key<K>, E> {
  static_assert(((detail::IsInputAliases<Policies>::value ||
                  detail::IsRecordDefault<Policies>::value) &&
                 ...),
                "Field policy must be InputAliases or Default");
  static_assert((std::size_t{0} + ... +
                 detail::IsInputAliases<Policies>::value) <= 1,
                "Field may have only one InputAliases policy");
  static_assert((std::size_t{0} + ... +
                 detail::IsRecordDefault<Policies>::value) <= 1,
                "Field may have only one Default policy");
  using input_policies = std::tuple<Policies...>;
  Entry() = default;
};

}  // namespace hosq
