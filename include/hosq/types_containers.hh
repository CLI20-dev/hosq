#pragma once

#include "hosq/pipeline/typed_containers.hh"
#include "hosq/types_field.hh"

namespace hosq::types::value {

/// A strict array of Elem values, stored as vector<E::storage_type>.
template <hosq::detail::ElemT E>
using Array = Elem<decode::array<E> | store::assign,
                   step<encode::detail::TypedArrayImpl<E>>>;

/// A strict object/map with paired vectors. Decoding and encoding reject
/// semantically equal duplicate keys; keys and values always remain paired.
/// The concrete codec still controls representable key kinds and root shapes.
template <hosq::detail::ElemT KeyElem, hosq::detail::ElemT ValueElem>
using Mapping = Elem<decode::mapping<KeyElem, ValueElem> | store::assign,
                     step<encode::detail::TypedMappingImpl<KeyElem, ValueElem>>>;

/// An unordered, closed object schema. Schema is an aggregate or std::tuple of
/// constant-key Entry<Key<...>, E> / Try<Entry<Key<...>, E>> fields. Plain Entry
/// fields are required; Try<Entry> may be missing and keeps default storage.
/// Unknown (unless explicitly ignored) and duplicate members are errors. Use
/// Optional<E> storage when absence must be observable; an empty optional Try
/// member is omitted on encode.
template <class Schema, UnknownMembers Unknown = UnknownMembers::reject>
using Record = Elem<decode::record<Schema, Unknown> | store::assign,
                    step<encode::detail::TypedRecordImpl<Schema>>>;

/// Object and Record name the same fixed schema wrapper. Dynamic objects use
/// Mapping<Text, ValueElem> (or another explicitly selected key Elem).
template <class Schema, UnknownMembers Unknown = UnknownMembers::reject>
using Object = Record<Schema, Unknown>;

/// Presence storage, not a nullable conversion: a present value must decode as
/// E (including Null only when E accepts it). Empty optionals mean missing and
/// fail encoding except in omitted Try<Entry> members of Record/Object.
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

template <StringLiteral K, class Schema,
          UnknownMembers Unknown = UnknownMembers::reject>
using Record = Field<K, value::Record<Schema, Unknown>>;

template <StringLiteral K, class Schema,
          UnknownMembers Unknown = UnknownMembers::reject>
using Object = Record<K, Schema, Unknown>;

/// A field that may be absent. In value::Record/Object an empty optional is
/// omitted on encode; a present value must match E, with no implicit null.
template <StringLiteral K, hosq::detail::ElemT E>
using Optional = Try<Field<K, value::Optional<E>>>;

}  // namespace hosq::types
