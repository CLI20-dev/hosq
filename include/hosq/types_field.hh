#pragma once

#include <optional>

#include "hosq/entry.hh"
#include "hosq/try.hh"

namespace hosq::types::presence {

/// A present value and a missing member are separate concerns. These policies
/// apply to Record/Object fields; the Elem still determines accepted values.
struct Required {
  static constexpr bool allows_missing = false;
  template <class E>
  static auto omit(const E&) -> bool {
    return false;
  }
};

struct Optional {
  static constexpr bool allows_missing = true;
  template <class E>
  static auto omit(const E& element) -> bool {
    return !element.value().has_value();
  }
};

}  // namespace hosq::types::presence

namespace hosq::detail {

/// Keep field policy independent of the candidate-dispatch Try marker.
template <class Entry, class Presence>
struct PolicyField : Entry {
  using presence_policy = Presence;
};

template <class E, class P>
struct IsEntry<PolicyField<E, P>> : IsEntry<E> {};
template <class E, class P>
struct IsConstKeyEntry<PolicyField<E, P>> : IsConstKeyEntry<E> {};

template <class E, class P>
struct IsOptionalField<PolicyField<E, P>>
    : std::bool_constant<P::allows_missing> {};
// Adapt optional fields to the legacy unordered aggregate traversal without
// making presence fields candidate-dispatch Try types.
template <class E, class P>
struct TryBase<PolicyField<E, P>> {
  using type = PolicyField<E, P>;
};

// Required fields retain the exact Entry identity of the low-level API.
template <class Entry, class Presence>
struct ApplyFieldPresence {
  using type = PolicyField<Entry, Presence>;
};
template <class Entry>
struct ApplyFieldPresence<Entry, types::presence::Required> {
  using type = Entry;
};

template <class T>
struct IsPresenceOptionalStorage : std::false_type {};
template <class T>
struct IsPresenceOptionalStorage<std::optional<T>> : std::true_type {};

/// Compatibility for existing Try<Entry> schemas is confined to this adapter.
/// Try without optional storage permits absence but encodes its default value.
template <class Field>
struct RecordFieldPresence : types::presence::Required {};
template <class E, class P>
struct RecordFieldPresence<PolicyField<E, P>> : P {
  static auto omit(const PolicyField<E, P>& field) -> bool {
    return P::omit(field.value());
  }
};
template <class E>
struct RecordFieldPresence<Try<E>> {
  static constexpr bool allows_missing = true;
  static auto omit(const Try<E>& field) -> bool {
    if constexpr (IsPresenceOptionalStorage<
                      typename E::value_type::storage_type>::value)
      return !field.value().value().has_value();
    else
      return false;
  }
};

}  // namespace hosq::detail

namespace hosq::types {

/// Attach a constant key and member-presence policy to an existing value Elem.
/// Optional presence requires observable optional storage; Optional<K,E> below
/// supplies that storage while keeping null acceptance in E's value contract.
template <StringLiteral K, hosq::detail::ElemT E,
          class Presence = presence::Required>
using Field =
    typename hosq::detail::ApplyFieldPresence<Entry<Key<K>, E>, Presence>::type;

/// Legacy candidate-dispatch marker; Field presence does not depend on it.
using hosq::Try;

}  // namespace hosq::types
