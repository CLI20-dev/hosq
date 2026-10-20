#pragma once

#include "hosq/pipeline/typed_containers.hh"
#include "hosq/types_field.hh"

namespace hosq::types::value {

/// A strict array of Elem values, stored as vector<E::storage_type>.
template <hosq::detail::ElemT E>
using Array = Elem<decode::array<E> | store::assign,
                   step<encode::detail::TypedArrayImpl<E>>>;

/// A strict object/map stored as MappingValue key/value pairs. Both directions
/// reject semantically equal duplicate keys; keys and values always remain
/// paired. The concrete codec still controls representable key kinds and root
/// shapes.
template <hosq::detail::ElemT KeyElem, hosq::detail::ElemT ValueElem>
using Mapping = Elem<decode::mapping<KeyElem, ValueElem> | store::assign,
                     step<encode::detail::TypedMappingImpl<KeyElem, ValueElem>>>;

/// An unordered, closed object schema. Schema is an aggregate or std::tuple of
/// constant-key fields. Field presence controls missing members independently
/// of the value codec. Unknown and duplicate members are errors. Legacy
/// Try<Entry> schemas retain their original missing/default behavior.
template <class Schema>
using Record = Elem<decode::record<Schema> | store::assign,
                    step<encode::detail::TypedRecordImpl<Schema>>>;

/// Object and Record name the same fixed schema wrapper. Dynamic objects use
/// Mapping<String, ValueElem> (or another explicitly selected key Elem).
template <class Schema>
using Object = Record<Schema>;

/// Presence storage, not a nullable conversion: a present value must decode as
/// E (including Null only when E accepts it). Empty optionals mean missing and
/// fail encoding except in optional-presence members of Record/Object (and
/// legacy Try<Entry>).
template <hosq::detail::ElemT E>
using Optional = Elem<decode::optional<E> | store::assign,
                      step<encode::detail::TypedOptionalImpl<E>>>;

}  // namespace hosq::types::value

namespace hosq::types {

template <StringLiteral K, hosq::detail::ElemT E>
using Array = Field<K, value::Array<E>>;

template <StringLiteral K, hosq::detail::ElemT KeyElem,
          hosq::detail::ElemT ValueElem>
using Mapping = Field<K, value::Mapping<KeyElem, ValueElem>>;

template <StringLiteral K, class Schema>
using Record = Field<K, value::Record<Schema>>;

template <StringLiteral K, class Schema>
using Object = Record<K, Schema>;

/// A field that may be absent. In value::Record/Object an empty optional is
/// omitted on encode; a present value must match E, with no implicit null.
template <StringLiteral K, hosq::detail::ElemT E>
using Optional = Field<K, value::Optional<E>, presence::Optional>;

}  // namespace hosq::types
