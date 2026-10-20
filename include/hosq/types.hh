#pragma once

#include <cstdint>

#include "hosq/element.hh"
#include "hosq/pipeline/encode.hh"
#include "hosq/pipeline/store.hh"
#include "hosq/pipeline/string.hh"
#include "hosq/pipeline/typed_decode.hh"
#include "hosq/pipeline/validate.hh"
#include "hosq/pipeline_util.hh"
#include "hosq/types_field.hh"

namespace hosq::types::value {
template <std::signed_integral T = std::int64_t>
using Int =
    Elem<scan::scalar | decode::integer<T> | store::assign, encode::value>;
template <std::unsigned_integral T = std::uint64_t>
  requires(!std::same_as<T, bool>)
using Uint = Elem<scan::scalar | decode::unsigned_integer<T> | store::assign,
                  encode::value>;
template <std::floating_point T = double>
using Float =
    Elem<scan::scalar | decode::floating<T> | store::assign, encode::value>;
using Int8 = Int<std::int8_t>;
using Int16 = Int<std::int16_t>;
using Int32 = Int<std::int32_t>;
using Int64 = Int<std::int64_t>;
using Uint8 = Uint<std::uint8_t>;
using Uint16 = Uint<std::uint16_t>;
using Uint32 = Uint<std::uint32_t>;
using Uint64 = Uint<std::uint64_t>;
using Float32 = Float<float>;
using Float64 = Float<double>;
using Bool =
    Elem<scan::scalar | decode::typed_boolean | store::assign, encode::value>;
using Null = Elem<scan::scalar | decode::null | store::assign, encode::value>;
using String = Elem<scan::scalar | decode::string_source |
                        construct<std::string> | decode::json_unescape |
                        decode::toml_unescape | validate::utf8 | store::assign,
                    encode::value>;
using Bytes = Elem<scan::scalar | decode::bytes | store::assign, encode::value>;
}  // namespace hosq::types::value

namespace hosq::types {
template <StringLiteral K, std::signed_integral T = std::int64_t>
using Int = Field<K, value::Int<T>>;
template <StringLiteral K, std::unsigned_integral T = std::uint64_t>
  requires(!std::same_as<T, bool>)
using Uint = Field<K, value::Uint<T>>;
template <StringLiteral K, std::floating_point T = double>
using Float = Field<K, value::Float<T>>;
template <StringLiteral K>
using Int8 = Field<K, value::Int8>;
template <StringLiteral K>
using Int16 = Field<K, value::Int16>;
template <StringLiteral K>
using Int32 = Field<K, value::Int32>;
template <StringLiteral K>
using Int64 = Field<K, value::Int64>;
template <StringLiteral K>
using Uint8 = Field<K, value::Uint8>;
template <StringLiteral K>
using Uint16 = Field<K, value::Uint16>;
template <StringLiteral K>
using Uint32 = Field<K, value::Uint32>;
template <StringLiteral K>
using Uint64 = Field<K, value::Uint64>;
template <StringLiteral K>
using Float32 = Field<K, value::Float32>;
template <StringLiteral K>
using Float64 = Field<K, value::Float64>;
template <StringLiteral K>
using Bool = Field<K, value::Bool>;
template <StringLiteral K>
using Null = Field<K, value::Null>;
template <StringLiteral K>
using String = Field<K, value::String>;
template <StringLiteral K>
using Bytes = Field<K, value::Bytes>;
}  // namespace hosq::types

#include "hosq/types_containers.hh"
#include "hosq/types_extended.hh"
