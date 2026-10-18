#include <gtest/gtest.h>

#include <string>
#include <tuple>

#include "hosq/bson/reader.hh"
#include "hosq/bson/writer.hh"
#include "hosq/hosq.hh"
#include "hosq/json/compact_writer.hh"
#include "hosq/json/fast_reader.hh"
#include "hosq/json/simple_reader.hh"
#include "hosq/msgpack/reader.hh"
#include "hosq/msgpack/writer.hh"

namespace {
using namespace hosq;
namespace t = hosq::types;
namespace v = hosq::types::value;

template <class W, class T>
auto wire(const T& value) -> std::string {
  std::string output;
  W writer(output);
  EXPECT_TRUE(encode_into(value, writer)) << writer.error().detail;
  return output;
}

struct LegacyPresence {
  t::Try<t::Uint64<"id">> id;
  t::Optional<"nickname", v::Text> nickname;
  t::Try<t::Null<"nil">> nil;
};

template <class R, class W>
void check_legacy_presence() {
  Decoder<R> decoder;
  LegacyPresence value;
  v::Record<std::tuple<>> empty;
  const auto missing = wire<W>(empty);
  ASSERT_TRUE(decoder.decode_document(value, missing).is_ok());
  EXPECT_FALSE(value.nickname.value().value());

  // Missing optional storage cannot be silently encoded as null by a legacy
  // aggregate. Omission belongs to the typed Record encoder, not Try itself.
  std::string invalid_output;
  msgpack::Writer invalid_writer(invalid_output);
  EXPECT_FALSE(encode_into(value, invalid_writer));
  EXPECT_EQ(invalid_writer.error().code, ErrorCode::invalid_object_state);

  struct Present {
    t::Text<"nickname"> nickname;
  } present;
  present.nickname.value().value() = "a\nb";
  auto present_wire = wire<W>(present);
  ASSERT_TRUE(decoder.decode_document(value, present_wire).is_ok());
  ASSERT_TRUE(value.nickname.value().value());
  EXPECT_EQ(*value.nickname.value().value(), "a\nb");
  struct IdOnly {
    t::Uint64<"id"> id;
  } id_only;
  id_only.id.value().value() = 7;
  auto id_wire = wire<W>(id_only);
  ASSERT_TRUE(decoder.decode_document(value, id_wire).is_ok());
  ASSERT_TRUE(value.nickname.value().value());
  EXPECT_EQ(*value.nickname.value().value(), "a\nb");
  EXPECT_EQ(wire<json::CompactWriter<>>(value),
            R"({"id":7,"nickname":"a\nb","nil":null})");

  LegacyPresence roundtrip;
  auto encoded = wire<W>(value);
  ASSERT_TRUE(decoder.decode_document(roundtrip, encoded).is_ok());
  EXPECT_EQ(roundtrip.nickname.value().value(), value.nickname.value().value());
  EXPECT_EQ(roundtrip.nil.value().value(), nullptr);

  // The same declaration inside typed Record uses a new schema per decode.
  v::Record<LegacyPresence> record;
  ASSERT_TRUE(decoder.decode_document(record, present_wire).is_ok());
  ASSERT_TRUE(record.value().nickname.value().value());
  ASSERT_TRUE(decoder.decode_document(record, missing).is_ok());
  EXPECT_FALSE(record.value().nickname.value().value());
  EXPECT_EQ(wire<json::CompactWriter<>>(record), R"({"id":0,"nil":null})");

  struct NullNickname {
    t::Null<"nickname"> nickname;
  } null_nickname;
  auto null_wire = wire<W>(null_nickname);
  EXPECT_EQ(decoder.decode_document(value, null_wire).code,
            ErrorCode::type_mismatch);
  // A new decode clears the reader failure, but Try does not clear unrelated
  // saved fields. Empty text is present, unlike missing optional storage.
  present.nickname.value().value().clear();
  present_wire = wire<W>(present);
  ASSERT_TRUE(decoder.decode_document(value, present_wire).is_ok());
  ASSERT_TRUE(value.nickname.value().value());
  EXPECT_TRUE(value.nickname.value().value()->empty());
  EXPECT_EQ(value.id.value().value(), 7U);
  EXPECT_EQ(wire<json::CompactWriter<>>(value),
            R"({"id":7,"nickname":"","nil":null})");
}

TEST(TryPresence, LegacyMissingStorageAndExplicitNullRemainDifferent) {
  check_legacy_presence<json::SimpleReader, json::CompactWriter<>>();
  check_legacy_presence<json::FastReader<>, json::CompactWriter<>>();
  check_legacy_presence<msgpack::Reader<>, msgpack::Writer<>>();
  check_legacy_presence<bson::Reader<>, bson::Writer<>>();
}

struct Row {
  t::Uint8<"id"> id;
  t::Optional<"nickname", v::Text> nickname;
  t::Null<"nil"> nil;
};
struct NestedRows {
  t::Try<t::Array<"rows", v::Record<Row>>> rows;
};

TEST(TryPresence, NestedArrayEncodesAfterMissingAndRecoversAfterRangeFailure) {
  Decoder<json::FastReader<>> decoder;
  NestedRows value;
  const auto input =
      R"({"rows":[{"id":1,"nickname":"a\nb","nil":null},{"id":2,"nil":null}]})";
  ASSERT_TRUE(decoder.decode_document(value, input).is_ok());
  ASSERT_TRUE(decoder.decode_document(value, "{}").is_ok());
  ASSERT_EQ(value.rows.value().value().size(), 2U);

  const auto msgpack = wire<msgpack::Writer<>>(value);
  NestedRows from_msgpack;
  ASSERT_TRUE(
      decode_document_into<msgpack::Reader<>>(from_msgpack, msgpack).is_ok());
  const auto bson = wire<bson::Writer<>>(from_msgpack);
  NestedRows from_bson;
  ASSERT_TRUE(decode_document_into<bson::Reader<>>(from_bson, bson).is_ok());
  EXPECT_EQ(wire<json::CompactWriter<>>(from_bson), input);

  EXPECT_EQ(
      decoder
          .decode_document(
              value, R"({"rows":[{"id":3,"nil":null},{"id":256,"nil":null}]})")
          .code,
      ErrorCode::out_of_range);
  // Failed storage is intentionally not asserted: no transaction is promised.
  // A subsequent valid array must replace it and encode a complete document.
  ASSERT_TRUE(decoder.decode_document(value, R"({"rows":[]})").is_ok());
  EXPECT_TRUE(value.rows.value().value().empty());
  EXPECT_EQ(wire<json::CompactWriter<>>(value), R"({"rows":[]})");
}
}  // namespace
