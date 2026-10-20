#include <gtest/gtest.h>

#include <tuple>
#include <type_traits>

#include "hosq/bson/reader.hh"
#include "hosq/bson/writer.hh"
#include "hosq/hosq.hh"
#include "hosq/json/compact_writer.hh"
#include "hosq/json/simple_reader.hh"
#include "hosq/msgpack/reader.hh"
#include "hosq/msgpack/writer.hh"
#include "hosq/toml/reader.hh"
#include "hosq/toml/writer.hh"

namespace {
using namespace hosq;
template <class Field>
struct Document {
  Entry<Key<"v">, Field> v;
};

template <class Field, class W>
void unsupported_encoding() {
  Document<Field> value;
  std::string output;
  W writer(output);
  EXPECT_FALSE(encode_into(value, writer));
  EXPECT_TRUE(writer.error().has_error());
}

template <class Field>
void json_rejects_semantic_type() {
  Field value;
  EXPECT_EQ(decode_document_into<json::SimpleReader>(value, "null").code,
            ErrorCode::unsupported_type);
  EXPECT_EQ(
      decode_document_into<json::SimpleReader>(value, "\"1970-01-01\"").code,
      ErrorCode::unsupported_type);
  unsupported_encoding<Field, json::CompactWriter<>>();
}

TEST(SemanticFormats, JsonDoesNotInventStringsOrExtendedJson) {
  []<class... Field>(std::tuple<Field...>*) {
    (json_rejects_semantic_type<Field>(), ...);
  }(static_cast<std::tuple<types::value::Instant, types::value::OffsetDateTime,
                           types::value::LocalDateTime, types::value::LocalDate,
                           types::value::LocalTime, types::value::Decimal128,
                           types::value::ObjectId, types::value::BsonTimestamp,
                           types::value::Extension>*>(nullptr));
}

TEST(SemanticFormats, UnsupportedBinaryAndTomlTypesFailExplicitly) {
  []<class... Field>(std::tuple<Field...>*) {
    (unsupported_encoding<Field, bson::Writer<>>(), ...);
    (unsupported_encoding<Field, msgpack::Writer<>>(), ...);
  }(static_cast<
      std::tuple<types::value::OffsetDateTime, types::value::LocalDateTime,
                 types::value::LocalDate, types::value::LocalTime>*>(nullptr));
  []<class... Field>(std::tuple<Field...>*) {
    (unsupported_encoding<Field, msgpack::Writer<>>(), ...);
    (unsupported_encoding<Field, toml::Writer<>>(), ...);
  }(static_cast<std::tuple<types::value::Decimal128, types::value::ObjectId,
                           types::value::BsonTimestamp>*>(nullptr));
  unsupported_encoding<types::value::Extension, bson::Writer<>>();
  unsupported_encoding<types::value::Extension, toml::Writer<>>();
  Document<types::value::Decimal128> decimal;
  EXPECT_EQ(decode_document_into<toml::Reader<>>(decimal, "v = 1.0").code,
            ErrorCode::unsupported_type);
  types::value::ObjectId object_id;
  EXPECT_EQ(
      decode_document_into<msgpack::Reader<>>(object_id, std::string("\xc0", 1))
          .code,
      ErrorCode::unsupported_type);
}

template <class W, class R>
auto instant_roundtrip(const InstantValue& value) -> InstantValue {
  Document<types::value::Instant> source;
  source.v.value().value() = value;
  std::string output;
  W writer(output);
  EXPECT_TRUE(encode_into(source, writer)) << writer.error().detail;
  Document<types::value::Instant> decoded;
  EXPECT_TRUE(decode_document_into<R>(decoded, output).is_ok());
  return decoded.v.value().value();
}

TEST(SemanticFormats, InstantCrossesBsonMessagePackAndTomlWithoutLoss) {
  for (InstantValue value : {InstantValue{-1, 999000000}, InstantValue{0, 0},
                             InstantValue{1700000000, 123000000}}) {
    const auto bson_value =
        instant_roundtrip<bson::Writer<>, bson::Reader<>>(value);
    const auto msgpack_value =
        instant_roundtrip<msgpack::Writer<>, msgpack::Reader<>>(bson_value);
    const auto toml_value =
        instant_roundtrip<toml::Writer<>, toml::Reader<>>(msgpack_value);
    EXPECT_EQ(toml_value, value);
  }
}

TEST(SemanticFormats, ExplicitOffsetTargetConversionNeverUsesLocalTimezone) {
  Document<types::value::Instant> instant;
  ASSERT_TRUE(decode_document_into<toml::Reader<>>(instant,
                                                   "v=1970-01-01T01:00:00+01:00")
                  .is_ok());
  EXPECT_EQ(instant.v.value().value(), (InstantValue{0, 0}));
  EXPECT_FALSE(
      decode_document_into<toml::Reader<>>(instant, "v=1970-01-01T01:00:00")
          .is_ok());
  Document<types::value::OffsetDateTime> offset;
  ASSERT_TRUE(
      decode_document_into<toml::Reader<>>(offset, "v=1970-01-01T01:00:00+01:00")
          .is_ok());
  EXPECT_EQ(offset.v.value().value().offset_minutes, 60);
}

static_assert(std::same_as<types::value::Instant::storage_type, InstantValue>);
static_assert(
    std::same_as<types::value::Decimal128::storage_type, Decimal128Value>);
static_assert(!std::same_as<InstantValue, BsonTimestampValue>);
}  // namespace
