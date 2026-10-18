#pragma once

#include "hosq/entry.hh"
#include "hosq/try.hh"

namespace hosq::types {

/// Attach a constant object key to an existing value Elem without changing its
/// storage, decoder or encoder. An empty key is a valid named field.
template <StringLiteral K, hosq::detail::ElemT E>
using Field = Entry<Key<K>, E>;

/// The existing dispatch marker, with its existing storage and encode behavior.
using hosq::Try;

}  // namespace hosq::types
