#pragma once

#include "hosq/element.hh"
#include "hosq/pipeline/encode.hh"
#include "hosq/pipeline/extended_decode.hh"
#include "hosq/pipeline/scan.hh"
#include "hosq/pipeline/store.hh"
#include "hosq/types_field.hh"

namespace hosq::types::value {
using Instant =
    Elem<scan::scalar | decode::instant | store::assign, encode::value>;
using OffsetDateTime =
    Elem<scan::scalar | decode::offset_date_time | store::assign, encode::value>;
using LocalDateTime =
    Elem<scan::scalar | decode::local_date_time | store::assign, encode::value>;
using LocalDate =
    Elem<scan::scalar | decode::local_date | store::assign, encode::value>;
using LocalTime =
    Elem<scan::scalar | decode::local_time | store::assign, encode::value>;
using Decimal128 =
    Elem<scan::scalar | decode::decimal128 | store::assign, encode::value>;
using ObjectId =
    Elem<scan::scalar | decode::object_id | store::assign, encode::value>;
using BsonTimestamp =
    Elem<scan::scalar | decode::bson_timestamp | store::assign, encode::value>;
using Extension = Elem<scan::scalar | decode::msgpack_extension | store::assign,
                       encode::value>;
}  // namespace hosq::types::value

namespace hosq::types {
template <StringLiteral K>
using Instant = Field<K, value::Instant>;
template <StringLiteral K>
using OffsetDateTime = Field<K, value::OffsetDateTime>;
template <StringLiteral K>
using LocalDateTime = Field<K, value::LocalDateTime>;
template <StringLiteral K>
using LocalDate = Field<K, value::LocalDate>;
template <StringLiteral K>
using LocalTime = Field<K, value::LocalTime>;
template <StringLiteral K>
using Decimal128 = Field<K, value::Decimal128>;
template <StringLiteral K>
using ObjectId = Field<K, value::ObjectId>;
template <StringLiteral K>
using BsonTimestamp = Field<K, value::BsonTimestamp>;
template <StringLiteral K>
using Extension = Field<K, value::Extension>;
}  // namespace hosq::types
