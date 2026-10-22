#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <string>
#include <string_view>

#include "hosq/hosq.hh"
#include "hosq/toml/reader.hh"
#include "hosq/toml/writer.hh"

namespace {
using namespace hosq;

template <class Value>
struct TemporalDocument {
  Entry<Key<"value">, Value> value;
};

template <class Value>
auto decode_temporal(TemporalDocument<Value>& document, std::string_view text)
    -> Error {
  return decode_document_into<toml::Reader<>>(
      document, std::string("value = ") + std::string(text));
}

template <class Value>
void expect_round_trip(std::string_view text, std::string_view canonical) {
  TemporalDocument<Value> document;
  ASSERT_TRUE(decode_temporal(document, text).is_ok()) << text;
  std::string output;
  toml::Writer writer(output);
  ASSERT_TRUE(encode_into(document, writer)) << writer.error().detail;
  EXPECT_EQ(output, std::string("\"value\" = ") + std::string(canonical) + '\n');
  TemporalDocument<Value> decoded;
  ASSERT_TRUE(decode_document_into<toml::Reader<>>(decoded, output).is_ok());
  EXPECT_EQ(*decoded.value, *document.value);
}

TEST(TomlSemantic, FourNativeDateTimeCategoriesRoundTrip) {
  expect_round_trip<types::value::LocalDate>("2000-02-29", "2000-02-29");
  expect_round_trip<types::value::LocalTime>("07:32:00.123456789",
                                             "07:32:00.123456789");
  expect_round_trip<types::value::LocalDateTime>("1979-05-27 07:32:00.123400",
                                                 "1979-05-27T07:32:00.1234");
  expect_round_trip<types::value::OffsetDateTime>("1979-05-27t07:32:00z",
                                                  "1979-05-27T07:32:00Z");
}

TEST(TomlSemantic, OffsetAndUnknownZeroArePreserved) {
  for (const std::string_view offset :
       {"+05:45", "-07:00", "+23:59", "-23:59", "-00:00"}) {
    const std::string text = "1979-05-27T07:32:00" + std::string(offset);
    expect_round_trip<types::value::OffsetDateTime>(text, text);
  }
  TemporalDocument<types::value::OffsetDateTime> document;
  ASSERT_TRUE(decode_temporal(document, "1979-05-27T07:32:00-00:00").is_ok());
  EXPECT_EQ(document.value->offset_minutes, 0);
  EXPECT_TRUE(document.value->negative_zero_offset);
  ASSERT_TRUE(decode_temporal(document, "1979-05-27T07:32:00+05:45").is_ok());
  EXPECT_EQ(document.value->offset_minutes, 345);
  EXPECT_FALSE(document.value->negative_zero_offset);
}

TEST(TomlSemantic, LeapSecondIsRetainedWithoutCoercingToInstant) {
  expect_round_trip<types::value::LocalTime>("23:59:60.5", "23:59:60.5");
  expect_round_trip<types::value::LocalDateTime>("2016-12-31T23:59:60",
                                                 "2016-12-31T23:59:60");
  expect_round_trip<types::value::OffsetDateTime>("2016-12-31T23:59:60Z",
                                                  "2016-12-31T23:59:60Z");
  TemporalDocument<types::value::Instant> instant;
  EXPECT_EQ(decode_temporal(instant, "2016-12-31T23:59:60Z").code,
            ErrorCode::conversion_error);
  EXPECT_EQ(decode_temporal(instant, "2016-12-31T23:59:59-00:00").code,
            ErrorCode::conversion_error);
}

TEST(TomlSemantic, NativeCategoriesDoNotImplicitlyConvert) {
  const std::string_view forms[] = {
      "1979-05-27", "07:32:00", "1979-05-27T07:32:00", "1979-05-27T07:32:00Z"};
  for (unsigned i = 0; i < 4; ++i) {
    TemporalDocument<types::value::LocalDate> date;
    TemporalDocument<types::value::LocalTime> time;
    TemporalDocument<types::value::LocalDateTime> local;
    TemporalDocument<types::value::OffsetDateTime> offset;
    EXPECT_EQ(decode_temporal(date, forms[i]).is_ok(), i == 0);
    EXPECT_EQ(decode_temporal(time, forms[i]).is_ok(), i == 1);
    EXPECT_EQ(decode_temporal(local, forms[i]).is_ok(), i == 2);
    EXPECT_EQ(decode_temporal(offset, forms[i]).is_ok(), i == 3);
    TemporalDocument<types::value::Instant> instant;
    EXPECT_EQ(decode_temporal(instant, forms[i]).is_ok(), i == 3);
    TemporalDocument<types::value::Text> text;
    EXPECT_EQ(decode_temporal(text, forms[i]).code, ErrorCode::type_mismatch);
  }
}

TEST(TomlSemantic, QuotedDateTimesRemainText) {
  TemporalDocument<types::value::LocalDate> date;
  TemporalDocument<types::value::LocalTime> time;
  TemporalDocument<types::value::LocalDateTime> local;
  TemporalDocument<types::value::OffsetDateTime> offset;
  TemporalDocument<types::value::Instant> instant;
  EXPECT_EQ(decode_temporal(date, "\"1979-05-27\"").code,
            ErrorCode::type_mismatch);
  EXPECT_EQ(decode_temporal(time, "'07:32:00'").code, ErrorCode::type_mismatch);
  EXPECT_EQ(decode_temporal(local, "\"1979-05-27T07:32:00\"").code,
            ErrorCode::type_mismatch);
  EXPECT_EQ(decode_temporal(offset, "\"1979-05-27T07:32:00Z\"").code,
            ErrorCode::type_mismatch);
  EXPECT_EQ(decode_temporal(instant, "\"1979-05-27T07:32:00Z\"").code,
            ErrorCode::type_mismatch);
  TemporalDocument<types::value::Text> text;
  ASSERT_TRUE(decode_temporal(text, "\"1979-05-27T07:32:00Z\"").is_ok());
  EXPECT_EQ(*text.value, "1979-05-27T07:32:00Z");
}

TEST(TomlSemantic, NanosecondPrecisionNeverSilentlyTruncates) {
  expect_round_trip<types::value::LocalTime>("00:00:00.000000001000",
                                             "00:00:00.000000001");
  expect_round_trip<types::value::LocalTime>("00:00:00.999999999",
                                             "00:00:00.999999999");
  expect_round_trip<types::value::LocalDateTime>(
      "1970-01-01T00:00:00.123456789000", "1970-01-01T00:00:00.123456789");
  expect_round_trip<types::value::OffsetDateTime>(
      "1970-01-01T00:00:00.123456789000Z", "1970-01-01T00:00:00.123456789Z");
  TemporalDocument<types::value::LocalTime> time;
  TemporalDocument<types::value::LocalDateTime> local;
  TemporalDocument<types::value::OffsetDateTime> offset;
  TemporalDocument<types::value::Instant> instant;
  EXPECT_EQ(decode_temporal(time, "00:00:00.1234567891").code,
            ErrorCode::conversion_error);
  EXPECT_EQ(decode_temporal(local, "1970-01-01T00:00:00.1234567891").code,
            ErrorCode::conversion_error);
  EXPECT_EQ(decode_temporal(offset, "1970-01-01T00:00:00.1234567891Z").code,
            ErrorCode::conversion_error);
  EXPECT_EQ(decode_temporal(instant, "1970-01-01T00:00:00.1234567891Z").code,
            ErrorCode::conversion_error);
}

TEST(TomlSemantic, InvalidDatesAndBrokenInputAreRejected) {
  for (const std::string_view text : {"1900-02-29",
                                      "2001-02-29",
                                      "2000-02-30",
                                      "2000-00-01",
                                      "2000-13-01",
                                      "2000-01-00",
                                      "2000-01-32",
                                      "-001-01-01",
                                      "10000-01-01",
                                      "24:00:00",
                                      "12:60:00",
                                      "12:00:61",
                                      "1:00:00",
                                      "12:00",
                                      "12:00:00.",
                                      "2000-01-01T00:00:00+24:00",
                                      "2000-01-01T00:00:00+00:60",
                                      "2000-01-01T00:00:00+0000",
                                      "2000-01-01TT00:00:00Z",
                                      "2000-01-01T00:00:00Zjunk"}) {
    TemporalDocument<types::value::OffsetDateTime> document;
    EXPECT_FALSE(decode_temporal(document, text).is_ok()) << text;
  }
  expect_round_trip<types::value::LocalDate>("0000-02-29", "0000-02-29");
  expect_round_trip<types::value::LocalDate>("9999-12-31", "9999-12-31");
}

TEST(TomlSemantic, InstantRequiresExplicitOffsetAndWritesUtc) {
  TemporalDocument<types::value::Instant> document;
  ASSERT_TRUE(
      decode_temporal(document, "1970-01-01T01:30:00.000000001+01:30").is_ok());
  EXPECT_EQ(*document.value, (InstantValue{0, 1}));
  expect_round_trip<types::value::Instant>("1970-01-01T01:30:00.000000001+01:30",
                                           "1970-01-01T00:00:00.000000001Z");
  expect_round_trip<types::value::Instant>("1969-12-31T23:59:59.999999999Z",
                                           "1969-12-31T23:59:59.999999999Z");
  expect_round_trip<types::value::Instant>("0000-01-01T00:00:00Z",
                                           "0000-01-01T00:00:00Z");
  expect_round_trip<types::value::Instant>("9999-12-31T23:59:59Z",
                                           "9999-12-31T23:59:59Z");
}

template <class Value>
void expect_invalid_write(const Value& value) {
  std::string output;
  toml::Writer writer(output);
  writer.write({0, TokenType::ObjectBegin, 0});
  ASSERT_TRUE(writer.write_string("value"));
  const auto prefix = output;
  EXPECT_FALSE(writer.write_semantic(value));
  EXPECT_EQ(writer.error().code, ErrorCode::out_of_range);
  EXPECT_EQ(output, prefix);
}

TEST(TomlSemantic, WriterValidatesTemporalFieldsBeforeAppending) {
  expect_invalid_write(LocalDateValue{2001, 2, 29});
  expect_invalid_write(LocalDateValue{-1, 1, 1});
  expect_invalid_write(LocalDateValue{10000, 1, 1});
  expect_invalid_write(LocalTimeValue{24, 0, 0, 0});
  expect_invalid_write(LocalTimeValue{0, 60, 0, 0});
  expect_invalid_write(LocalTimeValue{0, 0, 61, 0});
  expect_invalid_write(LocalTimeValue{0, 0, 0, 1'000'000'000});
  expect_invalid_write(LocalDateTimeValue{{2000, 1, 1}, {0, 0, 61, 0}});
  expect_invalid_write(OffsetDateTimeValue{{}, 1440, false});
  expect_invalid_write(OffsetDateTimeValue{{}, -1440, false});
  expect_invalid_write(OffsetDateTimeValue{{}, 1, true});
  expect_invalid_write(InstantValue{0, 1'000'000'000});
  expect_invalid_write(
      InstantValue{std::numeric_limits<std::int64_t>::min(), 0});
  expect_invalid_write(
      InstantValue{std::numeric_limits<std::int64_t>::max(), 0});
  expect_invalid_write(InstantValue{-62167219201LL, 0});
  expect_invalid_write(InstantValue{253402300800LL, 0});
}

TEST(TomlSemantic, TemporalValuesRespectRootKeyAndOutputLimits) {
  std::string root;
  toml::Writer root_writer(root);
  EXPECT_FALSE(root_writer.write_semantic(LocalDateValue{}));
  EXPECT_EQ(root_writer.error().code, ErrorCode::unsupported_type);

  std::string key;
  toml::Writer key_writer(key);
  key_writer.write({0, TokenType::ObjectBegin, 0});
  EXPECT_FALSE(key_writer.write_semantic(LocalDateValue{}));
  EXPECT_EQ(key_writer.error().code, ErrorCode::type_mismatch);

  TemporalDocument<types::value::LocalDate> document;
  std::string limited;
  toml::Writer<64, 15> limited_writer(limited);
  EXPECT_FALSE(encode_into(document, limited_writer));
  EXPECT_EQ(limited_writer.error().code, ErrorCode::output_too_large);
  EXPECT_LE(limited.size(), 15u);
}

TEST(TomlSemantic, TemporalArraysAndInlineTablesKeepWriterStructure) {
  struct Document {
    Entry<Key<"dates">, types::value::Array<types::value::LocalDate>> dates;
    Entry<Key<"nested">,
          types::value::Record<TemporalDocument<types::value::LocalTime>>>
        nested;
  } document;
  ASSERT_TRUE(
      decode_document_into<toml::Reader<>>(document,
                                           "dates = [2000-01-01, 2001-01-01]\n"
                                           "nested = { value = 12:34:56.5 }\n")
          .is_ok());
  std::string output;
  toml::Writer writer(output);
  ASSERT_TRUE(encode_into(document, writer)) << writer.error().detail;
  EXPECT_EQ(output,
            "\"dates\" = [2000-01-01, 2001-01-01]\n"
            "\"nested\" = {\"value\" = 12:34:56.5}\n");
}

TEST(TomlSemantic, LegacyTraversalKeepsArbitraryDatetimeFractions) {
  toml::Reader reader;
  toml::Reader<>::state_type state;
  const std::string input = "value = 00:00:00.123456789123456789\n";
  reader.reset(state, input);
  ASSERT_TRUE(reader.error(state).is_ok());
  ASSERT_TRUE(reader.next_token(state));
  ASSERT_TRUE(reader.next_token(state));
  ASSERT_TRUE(reader.next_token(state));
  const auto token = reader.current_token(state);
  EXPECT_EQ(input.substr(token.offset, token.length),
            "00:00:00.123456789123456789");
  ASSERT_TRUE(reader.next_token(state));
  EXPECT_EQ(reader.current_token(state).type, TokenType::ObjectEnd);
  ASSERT_TRUE(reader.next_token(state));
  EXPECT_EQ(reader.current_token(state).type, TokenType::End);
}

TEST(TomlSemantic, ForgedTemporalTokenSpansFailWithoutThrowing) {
  toml::Reader reader;
  toml::Reader<>::state_type state;
  LocalDateValue date;
  const std::string input = "value = 2000-01-01";
  for (unsigned attempt = 0; attempt < 3; ++attempt) {
    reader.reset(state, input);
    ASSERT_TRUE(reader.next_token(state));
    ASSERT_TRUE(reader.next_token(state));
    ASSERT_TRUE(reader.next_token(state));
    if (attempt == 0)
      state.token.offset = input.size() + 1;
    else if (attempt == 1)
      state.token.length = input.size();
    else
      state.current = state.nodes.size();
    EXPECT_NO_THROW(EXPECT_FALSE(reader.read_semantic(state, date)));
    EXPECT_EQ(reader.error(state).code, ErrorCode::invalid_state);
  }
}
}  // namespace
