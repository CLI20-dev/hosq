#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include "hosq/bson/reader.hh"
#include "hosq/bson/writer.hh"
#include "hosq/hosq.hh"
#include "hosq/json/compact_writer.hh"
#include "hosq/json/simple_reader.hh"
#include "hosq/msgpack/reader.hh"
#include "hosq/msgpack/writer.hh"
#include "hosq/toml/reader.hh"
#include "hosq/toml/writer.hh"
#include "hosq/types.hh"

namespace {
using namespace hosq;

auto octets(std::initializer_list<unsigned> bytes) -> std::string {
  std::string output;
  for (unsigned byte : bytes) output += static_cast<char>(byte);
  return output;
}
auto little(std::uint64_t value, unsigned size = 4) -> std::string {
  std::string output;
  for (unsigned i = 0; i < size; ++i)
    output += static_cast<char>(value >> (8 * i));
  return output;
}
auto document(unsigned marker, const std::string& payload) -> std::string {
  const auto body = static_cast<char>(marker) + std::string("b\0", 2) + payload;
  return little(body.size() + 5) + body + '\0';
}
template <class Value>
struct Document {
  Entry<Key<"b">, Value> b;
};

TEST(TypeCodec, BsonBinarySubtypesRoundTripAndOwnPayload) {
  for (unsigned subtype : {0u, 2u, 4u, 128u}) {
    const std::string payload("\0\xffz", 3);
    const auto binary = little(payload.size() + (subtype == 2 ? 4 : 0)) +
                        static_cast<char>(subtype) +
                        (subtype == 2 ? little(payload.size()) : std::string{}) +
                        payload;
    std::string input = document(5, binary);
    const std::string expected = input;
    Document<types::value::Bytes> decoded;
    ASSERT_TRUE(decode_into<bson::Reader<>>(decoded, input).is_ok());
    const auto& bytes = decoded.b.value().value();
    EXPECT_EQ(bytes.subtype, subtype);
    EXPECT_EQ(bytes.value, (std::vector<std::byte>{std::byte{0}, std::byte{255},
                                                   std::byte{'z'}}));
    input.assign(input.size(), 'x');
    std::string output;
    bson::Writer writer(output);
    ASSERT_TRUE(encode_into(decoded, writer)) << writer.error().detail;
    EXPECT_EQ(output, expected);
  }
}

TEST(TypeCodec, BytesDoNotLoseBsonSubtypeWhenEncodingMessagePack) {
  types::value::Bytes bytes;
  bytes.value().value = {std::byte{0}, std::byte{255}};
  bytes.value().subtype = 4;
  std::string output;
  msgpack::Writer writer(output);
  EXPECT_FALSE(encode_into(bytes, writer));
  EXPECT_EQ(writer.error().code, ErrorCode::unsupported_type);
  EXPECT_TRUE(output.empty());

  bytes.value().subtype = 0;
  std::string generic;
  msgpack::Writer generic_writer(generic);
  ASSERT_TRUE(encode_into(bytes, generic_writer));
  EXPECT_EQ(generic, octets({0xc4, 2, 0, 255}));
  types::value::Bytes decoded;
  ASSERT_TRUE(decode_into<msgpack::Reader<>>(decoded, generic).is_ok());
  EXPECT_EQ(decoded.value().value, bytes.value().value);
  EXPECT_EQ(decoded.value().subtype, 0);
}

TEST(TypeCodec, TextFormatsRejectBytes) {
  types::value::Bytes bytes;
  std::string json;
  json::CompactWriter json_writer(json);
  EXPECT_FALSE(encode_into(bytes, json_writer));
  EXPECT_TRUE(json_writer.error().has_error());
  Document<types::value::Bytes> value;
  std::string toml;
  toml::Writer toml_writer(toml);
  EXPECT_FALSE(encode_into(value, toml_writer));
  EXPECT_EQ(toml_writer.error().code, ErrorCode::unsupported_type);
}

TEST(TypeCodec, MessagePackSemanticTextRejectsInvalidUtf8) {
  for (const std::string& input :
       {octets({0xa2, 0xc0, 0xaf}), octets({0xa1, 0x80}),
        octets({0xa3, 0xed, 0xa0, 0x80}), octets({0xa1, 0xc3})}) {
    types::value::Text text;
    types::value::RawText raw;
    const auto text_error = decode_into<msgpack::Reader<>>(text, input);
    const auto raw_error = decode_into<msgpack::Reader<>>(raw, input);
    EXPECT_EQ(text_error.code, ErrorCode::invalid_utf8);
    EXPECT_EQ(raw_error.code, ErrorCode::invalid_utf8);
    EXPECT_EQ(text_error.offset, 1);
    EXPECT_EQ(raw_error.offset, 1);
    // The original borrowed string pipeline retains its established behavior.
    Elem<scan::scalar | decode::string_view | store::assign, encode::value>
        legacy;
    ASSERT_TRUE(decode_into<msgpack::Reader<>>(legacy, input).is_ok());
    EXPECT_EQ(legacy.value(), std::string_view(input).substr(1));
  }
  const auto valid = octets({0xa2, 0xc3, 0xa9});
  types::value::Text text;
  types::value::RawText raw;
  ASSERT_TRUE(decode_into<msgpack::Reader<>>(text, valid).is_ok());
  ASSERT_TRUE(decode_into<msgpack::Reader<>>(raw, valid).is_ok());
  EXPECT_EQ(text.value(), "é");
  EXPECT_EQ(raw.value().value, "é");

  types::value::Bytes bytes;
  ASSERT_TRUE(
      decode_into<msgpack::Reader<>>(bytes, octets({0xc4, 2, 0xc0, 0xaf}))
          .is_ok());
  EXPECT_EQ(bytes.value().value,
            (std::vector<std::byte>{std::byte{0xc0}, std::byte{0xaf}}));
}

TEST(TypeCodec, OpaqueBsonValuesAreNotBytesOrText) {
  for (const auto& [marker, payload] :
       std::vector<std::pair<unsigned, std::string>>{
           {7, std::string(12, 'x')},
           {9, little(0, 8)},
           {0x13, std::string(16, '\0')},
           {0x0d, little(2) + "x" + '\0'}}) {
    const auto input = document(marker, payload);
    Document<types::value::Bytes> bytes;
    Document<types::value::Text> text;
    Document<types::value::RawText> raw;
    EXPECT_FALSE(decode_into<bson::Reader<>>(bytes, input).is_ok());
    EXPECT_FALSE(decode_into<bson::Reader<>>(text, input).is_ok());
    EXPECT_FALSE(decode_into<bson::Reader<>>(raw, input).is_ok());
  }
}

TEST(TypeCodec, MessagePackExtensionsAndTomlDatetimeAreNotCommonScalars) {
  const auto extension = octets({0xd4, 1, 42});
  types::value::Bytes bytes;
  types::value::Text text;
  types::value::RawText raw;
  EXPECT_FALSE(decode_into<msgpack::Reader<>>(bytes, extension).is_ok());
  EXPECT_FALSE(decode_into<msgpack::Reader<>>(text, extension).is_ok());
  EXPECT_FALSE(decode_into<msgpack::Reader<>>(raw, extension).is_ok());
  Document<types::value::Bytes> date_bytes;
  Document<types::value::Text> date_text;
  Document<types::value::RawText> date_raw;
  EXPECT_FALSE(decode_into<toml::Reader<>>(date_bytes, "b=1979-05-27").is_ok());
  EXPECT_FALSE(decode_into<toml::Reader<>>(date_text, "b=1979-05-27").is_ok());
  EXPECT_FALSE(decode_into<toml::Reader<>>(date_raw, "b=1979-05-27").is_ok());
}

TEST(TypeCodec, TomlRawTextPreservesLexicalFormAndResolvesAcrossFormats) {
  for (const std::string& literal :
       {std::string("'a\\nb'"), std::string("\"a\\nb\""),
        std::string("'''\na\nb'''"), std::string("\"\"\"\na\\\n  b\"\"\"")}) {
    const auto input = "b = " + literal;
    Document<types::value::RawText> raw;
    Document<types::value::Text> semantic;
    ASSERT_TRUE(decode_into<toml::Reader<>>(raw, input).is_ok());
    ASSERT_TRUE(decode_into<toml::Reader<>>(semantic, input).is_ok());
    EXPECT_EQ(raw.b.value().value().value, literal);
    EXPECT_EQ(raw.b.value().value().format, Format::toml);
    EXPECT_EQ(raw.b.value().value().value.data(), input.data() + 4);
    std::string output;
    json::CompactWriter writer(output);
    ASSERT_TRUE(encode_into(raw, writer)) << writer.error().detail;
    Document<types::value::Text> decoded;
    ASSERT_TRUE(decode_into<json::SimpleReader>(decoded, output).is_ok());
    EXPECT_EQ(decoded.b.value().value(), semantic.b.value().value());
  }
}

TEST(TypeCodec, JsonRawTextDoesNotDoubleEscapeInBinaryFormats) {
  const std::string input = R"("a\n\u65e5\\b")";
  types::value::RawText raw;
  ASSERT_TRUE(decode_into<json::SimpleReader>(raw, input).is_ok());
  EXPECT_EQ(raw.value().value, R"(a\n\u65e5\\b)");
  std::string output;
  msgpack::Writer writer(output);
  ASSERT_TRUE(encode_into(raw, writer)) << writer.error().detail;
  types::value::Text text;
  ASSERT_TRUE(decode_into<msgpack::Reader<>>(text, output).is_ok());
  EXPECT_EQ(text.value(), "a\n日\\b");
}

TEST(TypeCodec, NativeScalarEncodingRespectsNumericAndRootLimits) {
  Document<types::value::Uint64> document;
  document.b.value().value() = std::numeric_limits<std::uint64_t>::max();
  auto oversized = [&]<class W>() {
    std::string output;
    W writer(output);
    EXPECT_FALSE(encode_into(document, writer));
    EXPECT_EQ(writer.error().code, ErrorCode::out_of_range);
  };
  oversized.template operator()<bson::Writer<>>();
  oversized.template operator()<toml::Writer<>>();
  auto scalar_root = [&]<class W>() {
    types::value::Int64 integer;
    std::string output;
    W writer(output);
    EXPECT_FALSE(encode_into(integer, writer));
    EXPECT_EQ(writer.error().code, ErrorCode::unsupported_type);
  };
  scalar_root.template operator()<bson::Writer<>>();
  scalar_root.template operator()<toml::Writer<>>();
  for (double value : {std::numeric_limits<double>::infinity(),
                       std::numeric_limits<double>::quiet_NaN()}) {
    types::value::Float64 number;
    number.value() = value;
    std::string output;
    json::CompactWriter writer(output);
    EXPECT_FALSE(encode_into(number, writer));
    EXPECT_TRUE(writer.error().has_error());
  }
}

TEST(TypeCodec, JsonFloat32RoundTripPreservesBinaryValue) {
  for (float value : {0.1f, std::numeric_limits<float>::denorm_min(),
                      std::numeric_limits<float>::max(), -0.0f}) {
    types::value::Float32 original;
    original.value() = value;
    std::string output;
    json::CompactWriter writer(output);
    ASSERT_TRUE(encode_into(original, writer));
    types::value::Float32 decoded;
    ASSERT_TRUE(decode_into<json::SimpleReader>(decoded, output).is_ok())
        << output;
    EXPECT_EQ(decoded.value(), value);
    EXPECT_EQ(std::signbit(decoded.value()), std::signbit(value));
  }
  types::value::Float<long double> exact;
  exact.value() = 1.25L;
  std::string output;
  json::CompactWriter writer(output);
  ASSERT_TRUE(encode_into(exact, writer));
  types::value::Float<long double> decoded;
  ASSERT_TRUE(decode_into<json::SimpleReader>(decoded, output).is_ok());
  EXPECT_EQ(decoded.value(), exact.value());
  if constexpr (std::numeric_limits<long double>::digits >
                std::numeric_limits<double>::digits) {
    types::value::Float<long double> wider;
    wider.value() = std::nextafter(1.0L, 2.0L);
    std::string rejected;
    json::CompactWriter reject_writer(rejected);
    EXPECT_FALSE(encode_into(wider, reject_writer));
    EXPECT_TRUE(reject_writer.error().has_error());
  }
}

TEST(TypeCodec, WiderFloatingRangeIsRejectedBeforeBinary64Conversion) {
  if constexpr (std::numeric_limits<long double>::max_exponent >
                    std::numeric_limits<double>::max_exponent ||
                std::numeric_limits<long double>::digits >
                    std::numeric_limits<double>::digits) {
    for (long double value : {std::numeric_limits<long double>::max(),
                              -std::numeric_limits<long double>::max()}) {
      types::value::Float<long double> number;
      number.value() = value;
      std::string output;
      json::CompactWriter writer(output);
      EXPECT_FALSE(encode_into(number, writer));
      EXPECT_TRUE(writer.error().has_error());
      auto check_native = [&]<class W>() {
        std::string encoded;
        W native_writer(encoded);
        EXPECT_FALSE(native_writer.write_number(value));
        EXPECT_EQ(native_writer.error().code, ErrorCode::out_of_range);
      };
      check_native.template operator()<msgpack::Writer<>>();
      check_native.template operator()<bson::Writer<>>();
      check_native.template operator()<toml::Writer<>>();
    }
  } else {
    GTEST_SKIP() << "long double has the binary64 range and precision";
  }
}

TEST(TypeCodec, NativeDocumentKeysMustBeText) {
  struct InvalidKey {
    Entry<types::value::Int64, types::value::Int64> entry;
  };
  const InvalidKey value;
  auto check = [&]<class W>() {
    std::string output;
    W writer(output);
    EXPECT_FALSE(encode_into(value, writer));
    EXPECT_EQ(writer.error().code, ErrorCode::type_mismatch);
  };
  check.template operator()<bson::Writer<>>();
  check.template operator()<toml::Writer<>>();
}

TEST(TypeCodec, TomlRawKeysBorrowTheirOriginalQuotedSource) {
  const std::string input = R"("b\u0061r" = 'x')";
  toml::Reader<> reader;
  toml::Reader<>::state_type state;
  reader.reset(state, input);
  ASSERT_TRUE(reader.next_token(state));
  ASSERT_TRUE(reader.next_token(state));
  RawTextValue key;
  ASSERT_TRUE(reader.read_raw_text(state, key));
  EXPECT_EQ(key.value, R"("b\u0061r")");
  EXPECT_EQ(key.value.data(), input.data());
  EXPECT_EQ(key.format, Format::toml);
  std::string semantic;
  ASSERT_TRUE(detail::resolve_raw_text(key, semantic));
  EXPECT_EQ(semantic, "bar");
}

TEST(TypeCodec, InvalidRawTextCannotInjectStructuredValues) {
  for (const RawTextValue value : {RawTextValue{R"(\q)", Format::json},
                                   RawTextValue{"'x'\nb=1", Format::toml},
                                   RawTextValue{"123", Format::toml}}) {
    types::value::RawText raw;
    raw.value() = value;
    std::string output;
    msgpack::Writer writer(output);
    EXPECT_FALSE(encode_into(raw, writer));
    EXPECT_EQ(writer.error().code, ErrorCode::invalid_escape);
    EXPECT_TRUE(output.empty());
  }
}
}  // namespace
