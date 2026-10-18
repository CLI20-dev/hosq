#pragma once

#include "hosq/record_policy.hh"
#include "hosq/try.hh"

namespace hosq::types {

/// Attach a constant object key to an existing value Elem without changing its
/// storage, decoder or encoder. An empty key is a valid named field. Optional
/// InputAliases / Default policies apply when used inside a typed Record.
template <StringLiteral K, hosq::detail::ElemT E, class... Policies>
using Field = Entry<Key<K>, E, Policies...>;

/// The existing dispatch marker, with its existing storage and encode behavior.
using hosq::Try;

}  // namespace hosq::types
