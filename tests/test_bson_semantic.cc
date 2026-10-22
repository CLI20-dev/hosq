#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>

#include "hosq/bson/reader.hh"
#include "hosq/bson/writer.hh"
#include "hosq/hosq.hh"

namespace {
using namespace hosq;
using TT = TokenType;
using EC = ErrorCode;

auto little(std::uint64_t bits, unsigned width = 4) -> std::string {
  std::string result;
  for (unsigned i = 0; i < width; ++i)
    result += static_cast<char>(bits >> (8 * i));
  return result;
}
auto member(unsigned marker, std::string payload, std::string name = "v")
    -> std::string {
  return static_cast<char>(marker) + name + '\0' + payload;
}
auto document(std::string body) -> std::string {
  return little(body.size() + 5) + body + '\0';
}
template <class W>
auto token(W& writer, TT type) -> bool {
  (void)writer.write({0, type, 0});
  return writer.error().is_ok();
}
template <class T>
auto write_value(const T& value, std::string& output) -> Error {
  bson::Writer writer(output);
  if (token(writer, TT::ObjectBegin) && writer.write_string("v") &&
      writer.write_semantic(value) && token(writer, TT::ObjectEnd))
    (void)token(writer, TT::End);
  return writer.error();
}
template <class T>
auto read_value(std::string_view input, T& output) -> Error {
  bson::Reader reader;
  bson::Reader<>::state_type state;
  reader.reset(state, input);
  if (reader.next_token(state) && reader.next_token(state) &&
      reader.next_token(state) && reader.read_semantic(state, output)) {
    while (reader.next_token(state) && state.token.type != TT::End) {
    }
  }
  return reader.error(state);
}
template <class T>
auto payload(const T& value) -> std::string {
  return {reinterpret_cast<const char*>(value.bytes.data()), value.bytes.size()};
}
template <class T>
struct Single {
  Entry<Key<"v">, T> v;
};
template <class Alias, class Value>
void typed_roundtrip(const Value& original, unsigned marker,
                     const std::string& bytes) {
  Single<Alias> input;
  *input.v = original;
  std::string encoded;
  bson::Writer writer(encoded);
  ASSERT_TRUE(encode_into(input, writer)) << writer.error().detail;
  EXPECT_EQ(encoded, document(member(marker, bytes)));
  Single<Alias> output;
  const auto error = decode_into<bson::Reader<>>(output, encoded);
  ASSERT_TRUE(error.is_ok()) << error.detail;
  EXPECT_EQ(*output.v, original);
}

TEST(BsonSemantic, InstantSignedMillisecondsAndInt64Extremes) {
  for (const auto& [millis, expected] :
       std::array<std::pair<std::int64_t, InstantValue>, 7>{{
           {0, {0, 0}},
           {1, {0, 1000000}},
           {-1, {-1, 999000000}},
           {-1001, {-2, 999000000}},
           {1234567, {1234, 567000000}},
           {std::numeric_limits<std::int64_t>::min(),
            {-9223372036854776LL, 192000000}},
           {std::numeric_limits<std::int64_t>::max(),
            {9223372036854775LL, 807000000}},
       }}) {
    const auto input =
        document(member(9, little(static_cast<std::uint64_t>(millis), 8)));
    InstantValue decoded;
    ASSERT_TRUE(read_value(input, decoded).is_ok());
    EXPECT_EQ(decoded, expected);
    std::string encoded;
    ASSERT_TRUE(write_value(expected, encoded).is_ok());
    EXPECT_EQ(encoded, input);
  }
}

TEST(BsonSemantic, InstantRejectsPrecisionLossAndOverflowBeforePayload) {
  for (const auto& instant : std::array<InstantValue, 9>{{
           {0, 1},
           {0, 1000},
           {-1, 999999999},
           {0, 1000000000},
           {-9223372036854776LL, 191000000},
           {9223372036854775LL, 808000000},
           {std::numeric_limits<std::int64_t>::min(), 0},
           {std::numeric_limits<std::int64_t>::max(), 0},
           {-1, std::numeric_limits<std::uint32_t>::max()},
       }}) {
    std::string encoded;
    EXPECT_EQ(write_value(instant, encoded).code, EC::out_of_range);
    EXPECT_EQ(encoded, little(0));
  }
}

TEST(BsonSemantic, OpaqueDecimalAndObjectIdBytesRemainVerbatim) {
  for (unsigned pattern : {0u, 255u, 17u}) {
    Decimal128Value decimal;
    ObjectIdValue object_id;
    for (std::size_t i = 0; i < decimal.bytes.size(); ++i)
      decimal.bytes[i] = static_cast<std::byte>((pattern + i * pattern) & 255);
    for (std::size_t i = 0; i < object_id.bytes.size(); ++i)
      object_id.bytes[i] = static_cast<std::byte>((pattern + i * pattern) & 255);
    std::string encoded;
    ASSERT_TRUE(write_value(decimal, encoded).is_ok());
    EXPECT_EQ(encoded, document(member(0x13, payload(decimal))));
    Decimal128Value decoded_decimal;
    ASSERT_TRUE(read_value(encoded, decoded_decimal).is_ok());
    EXPECT_EQ(decoded_decimal, decimal);
    encoded.clear();
    ASSERT_TRUE(write_value(object_id, encoded).is_ok());
    EXPECT_EQ(encoded, document(member(7, payload(object_id))));
    ObjectIdValue decoded_object_id;
    ASSERT_TRUE(read_value(encoded, decoded_object_id).is_ok());
    EXPECT_EQ(decoded_object_id, object_id);
  }
  // Every decimal128 bit pattern remains opaque, including all-one bits.
  Decimal128Value special;
  special.bytes.fill(std::byte{255});
  std::string encoded;
  ASSERT_TRUE(write_value(special, encoded).is_ok());
  EXPECT_EQ(encoded, document(member(0x13, std::string(16, '\xff'))));
  Decimal128Value decoded;
  ASSERT_TRUE(read_value(encoded, decoded).is_ok());
  EXPECT_EQ(decoded, special);
}

TEST(BsonSemantic, TimestampWireOrderAndUnsignedBoundaries) {
  for (auto value : std::array<BsonTimestampValue, 4>{{
           {0, 0},
           {0x12345678, 0x90abcdef},
           {std::numeric_limits<std::uint32_t>::max(), 0},
           {std::numeric_limits<std::uint32_t>::max(),
            std::numeric_limits<std::uint32_t>::max()},
       }}) {
    std::string encoded;
    ASSERT_TRUE(write_value(value, encoded).is_ok());
    EXPECT_EQ(encoded, document(member(17, little(value.increment) +
                                               little(value.seconds))));
    BsonTimestampValue decoded;
    ASSERT_TRUE(read_value(encoded, decoded).is_ok());
    EXPECT_EQ(decoded, value);
  }
}

TEST(BsonSemantic, WrongMarkersAndScalarCategoriesAreRejected) {
  const std::array<std::string, 8> inputs{{
      document(member(9, little(0, 8))),
      document(member(17, little(0, 8))),
      document(member(19, std::string(16, 0))),
      document(member(7, std::string(12, 0))),
      document(member(18, little(0, 8))),
      document(member(1, little(0, 8))),
      document(member(5, little(16) + '\0' + std::string(16, 0))),
      document(member(2, little(25) + std::string(24, 'a') + '\0')),
  }};
  for (std::size_t i = 0; i < inputs.size(); ++i) {
    InstantValue instant;
    BsonTimestampValue timestamp;
    Decimal128Value decimal;
    ObjectIdValue object_id;
    if (i != 0)
      EXPECT_EQ(read_value(inputs[i], instant).code, EC::type_mismatch);
    if (i != 1)
      EXPECT_EQ(read_value(inputs[i], timestamp).code, EC::type_mismatch);
    if (i != 2)
      EXPECT_EQ(read_value(inputs[i], decimal).code, EC::type_mismatch);
    if (i != 3)
      EXPECT_EQ(read_value(inputs[i], object_id).code, EC::type_mismatch);
  }
}

TEST(BsonSemantic, TruncatedPayloadsAreRejected) {
  for (auto [marker, width] : std::array<std::pair<unsigned, unsigned>, 4>{
           {{9, 8}, {17, 8}, {19, 16}, {7, 12}}}) {
    for (unsigned size = 0; size < width; ++size) {
      const auto input = document(member(marker, std::string(size, 0)));
      InstantValue instant;
      BsonTimestampValue timestamp;
      Decimal128Value decimal;
      ObjectIdValue object_id;
      EXPECT_EQ(read_value(input, instant).code, EC::unexpected_eof);
      EXPECT_EQ(read_value(input, timestamp).code, EC::unexpected_eof);
      EXPECT_EQ(read_value(input, decimal).code, EC::unexpected_eof);
      EXPECT_EQ(read_value(input, object_id).code, EC::unexpected_eof);
    }
  }
}

TEST(BsonSemantic, ReadChecksLengthAndBoundsBeforeAccess) {
  bson::Reader reader;
  bson::Reader<>::state_type state;
  reader.reset(state, {});
  state.token = {0, 7, TT::Scalar,
                 static_cast<std::uint8_t>(detail::NativeContext::extension)};
  state.scalar_marker = 9;
  InstantValue value{42, 0};
  EXPECT_FALSE(reader.read_semantic(state, value));
  EXPECT_EQ(reader.error(state).code, EC::invalid_length);
  EXPECT_EQ(value, InstantValue{});
  reader.reset(state, {});
  state.token = {std::numeric_limits<std::size_t>::max(), 8, TT::Scalar,
                 static_cast<std::uint8_t>(detail::NativeContext::extension)};
  state.scalar_marker = 9;
  EXPECT_FALSE(reader.read_semantic(state, value));
  EXPECT_EQ(reader.error(state).code, EC::unexpected_eof);
}

TEST(BsonSemantic, MarkerIdentityDoesNotLeakToKeysAndSurvivesRollback) {
  const auto input = document(member(9, little(1, 8), "first") +
                              member(9, little(2, 8), "next"));
  bson::Reader reader;
  bson::Reader<>::state_type state;
  reader.reset(state, input);
  ASSERT_TRUE(reader.next_token(state));
  ASSERT_TRUE(reader.next_token(state));
  EXPECT_EQ(state.scalar_marker, 0);
  ASSERT_TRUE(reader.next_token(state));
  EXPECT_EQ(state.scalar_marker, 9);
  const auto saved = state.checkpoint();
  ASSERT_TRUE(reader.next_token(state));
  EXPECT_EQ(state.scalar_marker, 0);
  state.rollback(saved);
  InstantValue instant;
  ASSERT_TRUE(reader.read_semantic(state, instant));
  EXPECT_EQ(instant, (InstantValue{0, 1000000}));
}

TEST(BsonSemantic, WriterAppliesRootKeyOutputAndStateConstraints) {
  const BsonTimestampValue value{1, 2};
  std::string root;
  bson::Writer root_writer(root);
  EXPECT_FALSE(root_writer.write_semantic(value));
  EXPECT_EQ(root_writer.error().code, EC::unsupported_type);
  std::string key;
  bson::Writer key_writer(key);
  ASSERT_TRUE(token(key_writer, TT::ObjectBegin));
  EXPECT_FALSE(key_writer.write_semantic(value));
  EXPECT_EQ(key_writer.error().code, EC::type_mismatch);
  std::string limited;
  bson::Writer<64, 10> limited_writer(limited);
  ASSERT_TRUE(token(limited_writer, TT::ObjectBegin));
  ASSERT_TRUE(limited_writer.write_string("v"));
  EXPECT_FALSE(limited_writer.write_semantic(value));
  EXPECT_EQ(limited_writer.error().code, EC::output_too_large);
  EXPECT_LE(limited.size(), 10u);
  std::string finished;
  bson::Writer finished_writer(finished);
  ASSERT_TRUE(token(finished_writer, TT::ObjectBegin));
  ASSERT_TRUE(token(finished_writer, TT::ObjectEnd));
  ASSERT_TRUE(token(finished_writer, TT::End));
  EXPECT_FALSE(finished_writer.write_semantic(value));
  EXPECT_EQ(finished_writer.error().code, EC::invalid_state);
}

TEST(BsonSemantic, WriterArrayIndexingAndDepthConstraints) {
  std::string encoded;
  bson::Writer writer(encoded);
  ASSERT_TRUE(token(writer, TT::ObjectBegin));
  ASSERT_TRUE(writer.write_string("v"));
  ASSERT_TRUE(token(writer, TT::ArrayBegin));
  ASSERT_TRUE(writer.write_semantic(InstantValue{-1, 999000000}));
  ASSERT_TRUE(writer.write_semantic(BsonTimestampValue{5, 7}));
  ASSERT_TRUE(token(writer, TT::ArrayEnd));
  ASSERT_TRUE(token(writer, TT::ObjectEnd));
  ASSERT_TRUE(token(writer, TT::End));
  EXPECT_EQ(
      encoded,
      document(member(4, document(member(9, little(~std::uint64_t{0}, 8), "0") +
                                  member(17, little(7) + little(5), "1")))));
  std::string shallow;
  bson::Writer<1> shallow_writer(shallow);
  ASSERT_TRUE(token(shallow_writer, TT::ObjectBegin));
  ASSERT_TRUE(shallow_writer.write_string("v"));
  EXPECT_FALSE(token(shallow_writer, TT::ArrayBegin));
  EXPECT_EQ(shallow_writer.error().code, EC::depth_limit_exceeded);
}

TEST(BsonSemantic, PublicAliasesUseSemanticCodecs) {
  typed_roundtrip<types::value::Instant>(InstantValue{-1, 999000000}, 9,
                                         little(~std::uint64_t{0}, 8));
  Decimal128Value decimal;
  decimal.bytes.fill(std::byte{255});
  typed_roundtrip<types::value::Decimal128>(decimal, 19, payload(decimal));
  ObjectIdValue object_id;
  for (std::size_t i = 0; i < object_id.bytes.size(); ++i)
    object_id.bytes[i] = static_cast<std::byte>(i * 23);
  typed_roundtrip<types::value::ObjectId>(object_id, 7, payload(object_id));
  typed_roundtrip<types::value::BsonTimestamp>(
      BsonTimestampValue{0x12345678, 0x90abcdef}, 17,
      little(0x90abcdef) + little(0x12345678));
}
}  // namespace
