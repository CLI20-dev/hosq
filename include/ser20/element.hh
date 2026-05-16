#pragma once

#include "ser20/string_literal.hh"

namespace ser20 {

namespace detail {

template <StringLiteral Name>
struct ElemImpl {};

};  // namespace detail

};  // namespace ser20
