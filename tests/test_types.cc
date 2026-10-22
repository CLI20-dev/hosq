#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include "hosq/bson/reader.hh"
#include "hosq/bson/writer.hh"
#include "hosq/hosq.hh"
#include "hosq/json/compact_writer.hh"
#include "hosq/json/fast_reader.hh"
#include "hosq/json/simple_reader.hh"
#include "hosq/msgpack/reader.hh"
#include "hosq/msgpack/writer.hh"
#include "hosq/toml/reader.hh"
#include "hosq/toml/writer.hh"
#include "hosq/types.hh"

namespace {
using namespace hosq;
using EC = ErrorCode;

std::string scalar_octets(std::initializer_list<unsigned> values) {
  std::string result;
  for (auto value : values) result += static_cast<char>(value);
  return result;
}

template <class Scalar, class Reader>
Error scalar_error(std::string_view source) {
  Scalar scalar;
  return decode_into<Reader>(scalar, source);
}

template <class Scalar>
void check_json_category(std::string_view source, EC expected) {
  EXPECT_EQ((scalar_error<Scalar, json::SimpleReader>(source)).code, expected)
      << source;
  EXPECT_EQ((scalar_error<Scalar, json::FastReader<>>(source)).code, expected)
      << source;
}

static_assert(std::same_as<types::value::Int<>::storage_type, std::int64_t>);
static_assert(std::same_as<types::value::Uint<>::storage_type, std::uint64_t>);
static_assert(std::same_as<types::value::Float<>::storage_type, double>);
static_assert(std::same_as<types::value::Bool::storage_type, bool>);
static_assert(std::same_as<types::value::String::storage_type, std::string>);
static_assert(std::same_as<types::value::Bytes::storage_type, BytesValue>);
static_assert(std::same_as<types::value::Int8::storage_type, std::int8_t>);
static_assert(std::same_as<types::value::Int16::storage_type, std::int16_t>);
static_assert(std::same_as<types::value::Int32::storage_type, std::int32_t>);
static_assert(std::same_as<types::value::Int64::storage_type, std::int64_t>);
static_assert(std::same_as<types::value::Uint8::storage_type, std::uint8_t>);
static_assert(std::same_as<types::value::Uint16::storage_type, std::uint16_t>);
static_assert(std::same_as<types::value::Uint32::storage_type, std::uint32_t>);
static_assert(std::same_as<types::value::Uint64::storage_type, std::uint64_t>);
static_assert(std::same_as<types::value::Float32::storage_type, float>);
static_assert(std::same_as<types::value::Float64::storage_type, double>);

TEST(ScalarTypes, JsonNumbersKeepStrictCategories) {
  for (auto input : {"1", "-1", "0"}) {
    check_json_category<types::value::Int<>>(input, EC::ok);
    check_json_category<types::value::Float<>>(input, EC::type_mismatch);
  }
  for (auto input : {"1.0", "1e0", "-0.0"}) {
    check_json_category<types::value::Int<>>(input, EC::type_mismatch);
    check_json_category<types::value::Uint<>>(input, EC::type_mismatch);
    check_json_category<types::value::Float<>>(input, EC::ok);
  }
  for (auto input : {R"("42")", R"("1.0")", "true", "null"}) {
    check_json_category<types::value::Int<>>(input, EC::type_mismatch);
    check_json_category<types::value::Uint<>>(input, EC::type_mismatch);
    check_json_category<types::value::Float<>>(input, EC::type_mismatch);
  }
}

TEST(ScalarTypes, LegacyNumberStillAcceptsIntegralNativeFloat) {
  using Legacy =
      Elem<scan::scalar | decode::number<int> | store::assign, encode::value>;
  auto integral_float = scalar_octets({0xcb, 0x40, 0x45, 0, 0, 0, 0, 0, 0});
  Legacy old_value;
  ASSERT_TRUE(decode_into<msgpack::Reader<>>(old_value, integral_float).is_ok());
  EXPECT_EQ(old_value.value(), 42);
  EXPECT_EQ(
      (scalar_error<types::value::Int<>, msgpack::Reader<>>(integral_float))
          .code,
      EC::type_mismatch);
  EXPECT_EQ((scalar_error<types::value::Float<>, msgpack::Reader<>>(
                 scalar_octets({42})))
                .code,
            EC::type_mismatch);
}

TEST(ScalarTypes, StrictNumbersRejectMalformedJsonGrammar) {
  for (auto source : {"01", "-01", "+1", "00", "-"}) {
    EXPECT_FALSE(
        (scalar_error<types::value::Int<>, json::SimpleReader>(source)).is_ok());
    EXPECT_FALSE(
        (scalar_error<types::value::Int<>, json::FastReader<>>(source)).is_ok());
  }
  for (auto source : {"01.5", "1.e2", "-.5", "1.", ".5", "1e", "1e+"}) {
    EXPECT_FALSE(
        (scalar_error<types::value::Float<>, json::SimpleReader>(source))
            .is_ok());
    EXPECT_FALSE(
        (scalar_error<types::value::Float<>, json::FastReader<>>(source))
            .is_ok());
  }
}

TEST(ScalarTypes, JsonIntegerBoundsAndUnsignedSign) {
  for (auto [input, expected] : {std::pair{"-128", EC::ok},
                                 {"127", EC::ok},
                                 {"-129", EC::out_of_range},
                                 {"128", EC::out_of_range}})
    check_json_category<types::value::Int8>(input, expected);
  for (auto [input, expected] : {std::pair{"0", EC::ok},
                                 {"255", EC::ok},
                                 {"-1", EC::out_of_range},
                                 {"256", EC::out_of_range}})
    check_json_category<types::value::Uint8>(input, expected);
  check_json_category<types::value::Int<>>("-9223372036854775808", EC::ok);
  check_json_category<types::value::Int<>>("9223372036854775807", EC::ok);
  check_json_category<types::value::Int<>>("9223372036854775808",
                                           EC::out_of_range);
  check_json_category<types::value::Uint<>>("18446744073709551615", EC::ok);
  check_json_category<types::value::Uint<>>("18446744073709551616",
                                            EC::out_of_range);
}

TEST(ScalarTypes, NativeIntegerBoundsDoNotPassThroughDouble) {
  auto largest = scalar_octets({0xcf, 255, 255, 255, 255, 255, 255, 255, 255});
  types::value::Uint<> value;
  ASSERT_TRUE(decode_into<msgpack::Reader<>>(value, largest).is_ok());
  EXPECT_EQ(value.value(), std::numeric_limits<std::uint64_t>::max());
  EXPECT_EQ((scalar_error<types::value::Int<>, msgpack::Reader<>>(largest)).code,
            EC::out_of_range);
  EXPECT_EQ((scalar_error<types::value::Uint<>, msgpack::Reader<>>(
                 scalar_octets({255})))
                .code,
            EC::out_of_range);
  types::value::Int<> smallest;
  ASSERT_TRUE(decode_into<msgpack::Reader<>>(
                  smallest, scalar_octets({0xd3, 128, 0, 0, 0, 0, 0, 0, 0}))
                  .is_ok());
  EXPECT_EQ(smallest.value(), std::numeric_limits<std::int64_t>::min());
}

TEST(ScalarTypes, FloatNarrowingRequiresExactRepresentation) {
  check_json_category<types::value::Float<float>>("1.5", EC::ok);
  check_json_category<types::value::Float<float>>("0.1", EC::out_of_range);
  check_json_category<types::value::Float<float>>("1e100", EC::out_of_range);
  check_json_category<types::value::Float<float>>("1e-100", EC::out_of_range);
  types::value::Float<float> negative_zero;
  ASSERT_TRUE(decode_into<json::SimpleReader>(negative_zero, "-0.0").is_ok());
  EXPECT_TRUE(std::signbit(negative_zero.value()));
  auto double_point_one =
      scalar_octets({0xcb, 0x3f, 0xb9, 0x99, 0x99, 0x99, 0x99, 0x99, 0x9a});
  EXPECT_EQ((scalar_error<types::value::Float<float>, msgpack::Reader<>>(
                 double_point_one))
                .code,
            EC::out_of_range);
  types::value::Float<> promoted;
  ASSERT_TRUE(decode_into<msgpack::Reader<>>(
                  promoted, scalar_octets({0xca, 0x3f, 0xc0, 0, 0}))
                  .is_ok());
  EXPECT_EQ(promoted.value(), 1.5);
}

TEST(ScalarTypes, BoolNullAndTextDoNotCoerceQuotedLiterals) {
  check_json_category<types::value::Bool>("true", EC::ok);
  check_json_category<types::value::Bool>("false", EC::ok);
  check_json_category<types::value::Null>("null", EC::ok);
  for (auto input : {R"("true")", R"("false")", "1", "null"})
    check_json_category<types::value::Bool>(input, EC::type_mismatch);
  for (auto input : {R"("null")", "false", "0"})
    check_json_category<types::value::Null>(input, EC::type_mismatch);
  for (auto input : {"true", "null", "42", "1.5"})
    check_json_category<types::value::String>(input, EC::type_mismatch);
}

TEST(ScalarTypes, NativeNonFiniteFloatsAndSignedZeroKeepMeaning) {
  for (double expected : {0.0, -0.0, std::numeric_limits<double>::infinity(),
                          -std::numeric_limits<double>::infinity(),
                          std::numeric_limits<double>::quiet_NaN()}) {
    types::value::Float<> source;
    source.value() = expected;
    std::string encoded;
    msgpack::Writer writer(encoded);
    ASSERT_TRUE(encode_into(source, writer));
    types::value::Float<float> result;
    ASSERT_TRUE(decode_into<msgpack::Reader<>>(result, encoded).is_ok());
    if (std::isnan(expected)) {
      EXPECT_TRUE(std::isnan(result.value()));
    } else {
      EXPECT_EQ(result.value(), expected);
      EXPECT_EQ(std::signbit(result.value()), std::signbit(expected));
    }
  }
}

TEST(ScalarTypes, Float32JsonRoundTripRetainsItsExactValue) {
  for (float expected : {0.1f, -0.1f, std::numeric_limits<float>::max(),
                         std::numeric_limits<float>::denorm_min(), -0.0f}) {
    types::value::Float32 source;
    source.value() = expected;
    std::string encoded;
    json::CompactWriter writer(encoded);
    ASSERT_TRUE(encode_into(source, writer));
    auto check = [&]<class Reader>() {
      types::value::Float32 result;
      ASSERT_TRUE(decode_into<Reader>(result, encoded).is_ok()) << encoded;
      EXPECT_EQ(result.value(), expected);
      EXPECT_EQ(std::signbit(result.value()), std::signbit(expected));
    };
    check.template operator()<json::SimpleReader>();
    check.template operator()<json::FastReader<>>();
  }
}

TEST(ScalarTypes, TomlNumericalCategoriesStayStrict) {
  struct SignedDocument {
    Try<Entry<Key<"v">, types::value::Int<>>> v;
  };
  struct UnsignedDocument {
    Try<Entry<Key<"v">, types::value::Uint<>>> v;
  };
  struct FloatDocument {
    Try<Entry<Key<"v">, types::value::Float<>>> v;
  };
  for (auto input : {"v = 1.0", "v = 1e0", "v = inf", "v = nan"}) {
    SignedDocument integer;
    UnsignedDocument unsigned_integer;
    FloatDocument floating;
    EXPECT_EQ(decode_into<toml::Reader<>>(integer, input).code,
              EC::type_mismatch);
    EXPECT_EQ(decode_into<toml::Reader<>>(unsigned_integer, input).code,
              EC::type_mismatch);
    EXPECT_TRUE(decode_into<toml::Reader<>>(floating, input).is_ok());
  }
  FloatDocument floating;
  EXPECT_EQ(decode_into<toml::Reader<>>(floating, "v = 1").code,
            EC::type_mismatch);
  UnsignedDocument unsigned_integer;
  EXPECT_EQ(decode_into<toml::Reader<>>(unsigned_integer, "v = -1").code,
            EC::out_of_range);
}

TEST(ScalarTypes, TextUnescapesUnicodeSurrogatesAndEmbeddedNul) {
  const std::string source = R"("A\n\t\"\\\u0000\u65e5\uD83D\uDE00")";
  const std::string expected = std::string("A\n\t\"\\\0", 6) + "日😀";
  auto check = [&]<class Reader>() {
    types::value::String value;
    ASSERT_TRUE(decode_into<Reader>(value, source).is_ok());
    EXPECT_EQ(value.value(), expected);
    std::string output;
    json::CompactWriter writer(output);
    ASSERT_TRUE(encode_into(value, writer));
    types::value::String again;
    ASSERT_TRUE(decode_into<Reader>(again, output).is_ok());
    EXPECT_EQ(again.value(), expected);
  };
  check.template operator()<json::SimpleReader>();
  check.template operator()<json::FastReader<>>();
}

TEST(ScalarTypes, TextDoesNotUnescapeTwice) {
  for (auto source : {R"("\\n")", R"("\\u0061")", R"("\\\"")"}) {
    types::value::String value;
    ASSERT_TRUE(decode_into<json::SimpleReader>(value, source).is_ok());
    std::string output;
    json::CompactWriter writer(output);
    ASSERT_TRUE(encode_into(value, writer));
    EXPECT_EQ(output, source);
  }
}

TEST(ScalarTypes, TextRejectsBrokenSurrogatesAndInvalidUtf8) {
  for (auto source : {R"("\uD800")", R"("\uDC00")", R"("\uD800\u0041")",
                      R"("\uD800\uD800")"}) {
    EXPECT_FALSE((scalar_error<types::value::String, json::SimpleReader>(source))
                     .is_ok());
    EXPECT_FALSE((scalar_error<types::value::String, json::FastReader<>>(source))
                     .is_ok());
  }
  auto invalid = std::string("\"") + scalar_octets({0xc0, 0xaf}) + '"';
  EXPECT_FALSE(
      (scalar_error<types::value::String, json::SimpleReader>(invalid)).is_ok());
  EXPECT_FALSE(
      (scalar_error<types::value::String, json::FastReader<>>(invalid)).is_ok());
}

TEST(ScalarTypes, TextOwnsDataAfterInputHasGoneAway) {
  types::value::String value;
  {
    std::string source = R"("owned\ntext")";
    ASSERT_TRUE(decode_into<json::SimpleReader>(value, source).is_ok());
    source.assign(source.size(), 'x');
  }
  EXPECT_EQ(value.value(), "owned\ntext");
}

TEST(ScalarTypes, TextRejectsBinaryAndNativeExtensions) {
  auto binary = scalar_octets({0xc4, 2, 0, 255});
  auto extension = scalar_octets({0xd4, 255, 42});
  EXPECT_EQ((scalar_error<types::value::String, msgpack::Reader<>>(binary)).code,
            EC::type_mismatch);
  EXPECT_FALSE((scalar_error<types::value::String, msgpack::Reader<>>(extension))
                   .is_ok());
  EXPECT_FALSE(
      (scalar_error<types::value::Bytes, msgpack::Reader<>>(extension)).is_ok());
  EXPECT_EQ((scalar_error<types::value::Bytes, msgpack::Reader<>>(
                 scalar_octets({0xa1}) + "a"))
                .code,
            EC::type_mismatch);
}

TEST(ScalarTypes, BytesOwnPayloadAfterInputIsDestroyed) {
  types::value::Bytes value;
  {
    std::string source = scalar_octets({0xc4, 3, 0, 255, 42});
    ASSERT_TRUE(decode_into<msgpack::Reader<>>(value, source).is_ok());
    source.assign(source.size(), 'x');
  }
  EXPECT_EQ(
      value.value().value,
      (std::vector<std::byte>{std::byte{0}, std::byte{255}, std::byte{42}}));
  EXPECT_EQ(value.value().subtype, 0);
}

struct TypedScalarRecord {
  Try<Entry<Key<"integer">, types::value::Int<>>> integer;
  Try<Entry<Key<"real">, types::value::Float<>>> real;
  Try<Entry<Key<"text">, types::value::String>> text;
  Try<Entry<Key<"flag">, types::value::Bool>> flag;
};

template <class Writer, class Reader>
void typed_scalar_roundtrip(const TypedScalarRecord& source) {
  std::string output;
  Writer writer(output);
  ASSERT_TRUE(encode_into(source, writer)) << writer.error().detail;
  TypedScalarRecord result;
  const auto status = decode_into<Reader>(result, output);
  ASSERT_TRUE(status.is_ok()) << to_string(status.code) << ": " << status.detail;
  EXPECT_EQ(*result.integer, *source.integer);
  EXPECT_EQ(*result.real, *source.real);
  EXPECT_EQ(*result.text, *source.text);
  EXPECT_EQ(*result.flag, *source.flag);
}

TEST(ScalarTypes, SameSchemaRoundTripsAllFourFormats) {
  TypedScalarRecord value;
  *value.integer = -9007199254740993LL;
  *value.real = 1.5;
  *value.text = std::string("日本語\n\\\"\0z", 14);
  *value.flag = true;
  typed_scalar_roundtrip<json::CompactWriter<>, json::SimpleReader>(value);
  typed_scalar_roundtrip<msgpack::Writer<>, msgpack::Reader<>>(value);
  typed_scalar_roundtrip<bson::Writer<>, bson::Reader<>>(value);
  typed_scalar_roundtrip<toml::Writer<>, toml::Reader<>>(value);
}

TEST(ScalarTypes, JsonTextCanTravelThroughEveryOtherFormat) {
  const std::string json_input =
      R"({"integer":42,"real":2.5,"text":"a\n\uD83D\uDE00\\u0061","flag":false})";
  TypedScalarRecord value;
  ASSERT_TRUE(decode_into<json::SimpleReader>(value, json_input).is_ok());
  EXPECT_EQ(*value.text, "a\n😀\\u0061");
  typed_scalar_roundtrip<msgpack::Writer<>, msgpack::Reader<>>(value);
  typed_scalar_roundtrip<bson::Writer<>, bson::Reader<>>(value);
  typed_scalar_roundtrip<toml::Writer<>, toml::Reader<>>(value);
}
}  // namespace
