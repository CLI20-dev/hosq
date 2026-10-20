#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <utility>

#include "hosq/detail/temporal.hh"

namespace {
using namespace hosq;
using namespace hosq::detail;

TEST(TemporalValues, ValidatesCalendarAndStorageInvariants) {
  for (auto value :
       {LocalDateValue{0, 2, 29}, {2000, 2, 29}, {2024, 2, 29}, {9999, 12, 31}})
    EXPECT_TRUE(valid_date(value));
  for (auto value : {LocalDateValue{-1, 1, 1},
                     {10000, 1, 1},
                     {1900, 2, 29},
                     {2023, 2, 29},
                     {2024, 4, 31},
                     {2024, 0, 1},
                     {2024, 13, 1},
                     {2024, 1, 0}})
    EXPECT_FALSE(valid_date(value));
  EXPECT_TRUE(valid_time({23, 59, 60, 999'999'999}));
  for (auto value : {LocalTimeValue{24, 0, 0, 0},
                     {0, 60, 0, 0},
                     {0, 0, 61, 0},
                     {0, 0, 0, 1'000'000'000}})
    EXPECT_FALSE(valid_time(value));
  EXPECT_TRUE(valid_instant({std::numeric_limits<std::int64_t>::min(), 0}));
  EXPECT_TRUE(
      valid_instant({std::numeric_limits<std::int64_t>::max(), 999'999'999}));
  EXPECT_FALSE(valid_instant({0, 1'000'000'000}));
  EXPECT_FALSE(valid_local_datetime({{2023, 2, 29}, {}}));
  EXPECT_FALSE(valid_local_datetime({{}, {24, 0, 0, 0}}));
  EXPECT_TRUE(valid_offset_datetime({{}, -1439, false}));
  EXPECT_TRUE(valid_offset_datetime({{}, 1439, false}));
  EXPECT_TRUE(valid_offset_datetime({{}, 0, true}));
  EXPECT_FALSE(valid_offset_datetime({{}, 1440, false}));
  EXPECT_FALSE(valid_offset_datetime({{}, -1440, false}));
  EXPECT_FALSE(valid_offset_datetime({{}, 1, true}));
}

TEST(TemporalValues, MillisecondsUseFloorSecondsAndRoundTripSignedBoundaries) {
  EXPECT_EQ(instant_from_milliseconds(-1), (InstantValue{-1, 999'000'000}));
  EXPECT_EQ(instant_from_milliseconds(-1001), (InstantValue{-2, 999'000'000}));
  EXPECT_EQ(instant_from_milliseconds(1001), (InstantValue{1, 1'000'000}));
  constexpr auto minimum = std::numeric_limits<std::int64_t>::min();
  constexpr auto maximum = std::numeric_limits<std::int64_t>::max();
  for (std::int64_t source :
       {minimum, minimum + 1, minimum + 999, std::int64_t{-1001},
        std::int64_t{-1000}, std::int64_t{-1}, std::int64_t{0}, std::int64_t{1},
        std::int64_t{1000}, maximum - 999, maximum - 1, maximum}) {
    std::int64_t result = 42;
    ASSERT_TRUE(
        instant_to_milliseconds(instant_from_milliseconds(source), result));
    EXPECT_EQ(result, source);
  }
  EXPECT_EQ(instant_from_milliseconds(minimum),
            (InstantValue{-9'223'372'036'854'776, 192'000'000}));
  EXPECT_EQ(instant_from_milliseconds(maximum),
            (InstantValue{9'223'372'036'854'775, 807'000'000}));
}

TEST(TemporalValues, MillisecondsRejectOverflowAndPrecisionLossWithoutMutation) {
  for (auto value : {InstantValue{-9'223'372'036'854'776, 191'000'000},
                     {-9'223'372'036'854'777, 999'000'000},
                     {9'223'372'036'854'775, 808'000'000},
                     {9'223'372'036'854'776, 0},
                     {std::numeric_limits<std::int64_t>::min(), 0},
                     {std::numeric_limits<std::int64_t>::max(), 0},
                     {0, 1},
                     {-1, 999'999'999},
                     {0, 1'000'000'000}}) {
    std::int64_t output = 42;
    EXPECT_FALSE(instant_to_milliseconds(value, output));
    EXPECT_EQ(output, 42);
  }
}

TEST(TemporalValues, ParsesAndCanonicalizesEveryTemporalCategory) {
  LocalDateValue date;
  ASSERT_TRUE(parse_local_date("0000-02-29", date));
  EXPECT_EQ(date, (LocalDateValue{0, 2, 29}));
  EXPECT_EQ(format_local_date(date), "0000-02-29");
  LocalTimeValue time;
  ASSERT_TRUE(parse_local_time("23:59:60.000000001", time));
  EXPECT_EQ(time, (LocalTimeValue{23, 59, 60, 1}));
  EXPECT_EQ(format_local_time(time), "23:59:60.000000001");
  for (auto input : {"12:34:56.120000000", "12:34:56.1200000000000000"}) {
    ASSERT_TRUE(parse_local_time(input, time));
    EXPECT_EQ(format_local_time(time), "12:34:56.12");
  }
  ASSERT_TRUE(parse_local_time("00:00:00.000000000000", time));
  EXPECT_EQ(format_local_time(time), "00:00:00");
  LocalDateTimeValue local;
  for (auto input : {"2024-02-29T12:34:56.1", "2024-02-29t12:34:56.1",
                     "2024-02-29 12:34:56.1"}) {
    ASSERT_TRUE(parse_local_datetime(input, local));
    EXPECT_EQ(format_local_datetime(local), "2024-02-29T12:34:56.1");
  }
  OffsetDateTimeValue offset;
  for (auto input : {"2024-02-29T12:34:56Z", "2024-02-29t12:34:56z",
                     "2024-02-29 12:34:56+00:00"}) {
    ASSERT_TRUE(parse_offset_datetime(input, offset));
    EXPECT_EQ(format_offset_datetime(offset), "2024-02-29T12:34:56Z");
  }
  for (auto input :
       {"2024-02-29T12:34:56.12+23:59", "2024-02-29T12:34:56.12-23:59",
        "2024-02-29T12:34:56.12-00:00"}) {
    ASSERT_TRUE(parse_offset_datetime(input, offset));
    EXPECT_EQ(format_offset_datetime(offset), input);
  }
}

TEST(TemporalValues, RejectsMalformedAndOutOfRangeTextWithoutMutation) {
  LocalDateValue date{2024, 2, 29};
  for (auto input : {"", "2024-2-29", "024-02-29", "10000-01-01", "-001-01-01",
                     "2023-02-29", "1900-02-29", "2024-04-31", "2024-00-01",
                     "2024-13-01", "2024-01-00", "2024-01-32", "2024/01/01",
                     "2024-01-01 ", " 2024-01-01", "202x-01-01"}) {
    EXPECT_FALSE(parse_local_date(input, date)) << input;
    EXPECT_EQ(date, (LocalDateValue{2024, 2, 29}));
  }
  LocalTimeValue time{12, 34, 56, 789};
  for (auto input :
       {"", "12:34", "1:34:56", "12:3:56", "12:34:5", "24:00:00", "00:60:00",
        "00:00:61", "12-34-56", "12:34:56.", "12:34:56.1234567891",
        "12:34:56.12345678901", "12:34:56.1x", "12:34:56Z", "12:34:56+01:00",
        "12:34:56 ", " 12:34:56"}) {
    EXPECT_FALSE(parse_local_time(input, time)) << input;
    EXPECT_EQ(time, (LocalTimeValue{12, 34, 56, 789}));
  }
  LocalDateTimeValue local{{2024, 2, 29}, time};
  const auto original_local = local;
  for (auto input : {"2024-02-29", "2024-02-29X12:34:56", "2023-02-29T12:34:56",
                     "2024-02-29T24:00:00", "2024-02-29T12:34:56Z",
                     "2024-02-29T12:34:56.0000000001"}) {
    EXPECT_FALSE(parse_local_datetime(input, local)) << input;
    EXPECT_EQ(local, original_local);
  }
  OffsetDateTimeValue offset{original_local, -60, false};
  const auto original_offset = offset;
  for (auto input :
       {"2024-02-29T12:34:56", "2024-02-29T12:34:56+24:00",
        "2024-02-29T12:34:56+00:60", "2024-02-29T12:34:56-24:00",
        "2024-02-29T12:34:56+01", "2024-02-29T12:34:56+0100",
        "2024-02-29T12:34:56+1:00", "2024-02-29T12:34:56Z ",
        "2024-02-29T12:34:56Zz", "2024-02-29T12:34:56.0000000001Z"}) {
    EXPECT_FALSE(parse_offset_datetime(input, offset)) << input;
    EXPECT_EQ(offset, original_offset);
  }
}

TEST(TemporalValues, ClassificationSeparatesShapeFromStoragePrecision) {
  EXPECT_EQ(classify_temporal("0000-02-29"), TemporalKind::date);
  EXPECT_EQ(classify_temporal("23:59:60.12345678901"), TemporalKind::time);
  EXPECT_EQ(classify_temporal("2024-02-29t12:34:56.12345678901"),
            TemporalKind::local_datetime);
  EXPECT_EQ(classify_temporal("2024-02-29 12:34:56.12345678901-00:00"),
            TemporalKind::offset_datetime);
  for (auto input : {"2023-02-29", "24:00:00", "12:34:56.",
                     "2024-02-29T12:34:56+24:00", "\"2024-02-29\"", "42"})
    EXPECT_EQ(classify_temporal(input), TemporalKind::invalid) << input;
}

TEST(TemporalValues, OffsetConversionCrossesEpochAndCalendarBoundariesExactly) {
  for (auto [text, expected] :
       {std::pair{"1970-01-01T00:00:00Z", InstantValue{0, 0}},
        {"1969-12-31T23:59:59.999999999Z", {-1, 999'999'999}},
        {"1970-01-01T09:00:00.1+09:00", {0, 100'000'000}},
        {"1969-12-31T19:00:00-05:00", {0, 0}},
        {"2000-01-01T00:00:00Z", {946684800, 0}},
        {"0000-01-01T00:00:00Z", {-62167219200, 0}},
        {"9999-12-31T23:59:59.999999999Z", {253402300799, 999'999'999}}}) {
    OffsetDateTimeValue offset;
    ASSERT_TRUE(parse_offset_datetime(text, offset));
    InstantValue result;
    ASSERT_TRUE(offset_to_instant(offset, result));
    EXPECT_EQ(result, expected) << text;
    OffsetDateTimeValue utc;
    ASSERT_TRUE(instant_to_offset(result, utc));
    EXPECT_EQ(utc.offset_minutes, 0);
    EXPECT_FALSE(utc.negative_zero_offset);
    InstantValue roundtrip;
    ASSERT_TRUE(offset_to_instant(utc, roundtrip));
    EXPECT_EQ(roundtrip, expected);
  }
  OffsetDateTimeValue offset;
  ASSERT_TRUE(parse_offset_datetime("0000-01-01T00:00:00+23:59", offset));
  InstantValue instant;
  ASSERT_TRUE(offset_to_instant(offset, instant));
  EXPECT_EQ(instant.seconds, -62167305540);
  EXPECT_FALSE(instant_to_offset(instant, offset));
  ASSERT_TRUE(parse_offset_datetime("9999-12-31T23:59:59-23:59", offset));
  ASSERT_TRUE(offset_to_instant(offset, instant));
  EXPECT_EQ(instant.seconds, 253402387139);
  EXPECT_FALSE(instant_to_offset(instant, offset));
}

TEST(TemporalValues, ConversionRejectsUnknownOffsetLeapSecondsAndInvalidValues) {
  InstantValue result{42, 123};
  for (auto text : {"2024-01-01T00:00:00-00:00", "2016-12-31T23:59:60Z"}) {
    OffsetDateTimeValue offset;
    ASSERT_TRUE(parse_offset_datetime(text, offset));
    EXPECT_FALSE(offset_to_instant(offset, result));
    EXPECT_EQ(result, (InstantValue{42, 123}));
    EXPECT_EQ(format_offset_datetime(offset), text);
  }
  EXPECT_FALSE(offset_to_instant({{}, 1440, false}, result));
  EXPECT_FALSE(offset_to_instant({{}, 1, true}, result));
  EXPECT_EQ(result, (InstantValue{42, 123}));
  OffsetDateTimeValue offset{{{2024, 1, 1}, {}}, 60, false};
  const auto original = offset;
  for (auto instant : {InstantValue{std::numeric_limits<std::int64_t>::min(), 0},
                       {std::numeric_limits<std::int64_t>::max(), 0},
                       {-62167219201, 999'999'999},
                       {253402300800, 0},
                       {0, 1'000'000'000}}) {
    EXPECT_FALSE(instant_to_offset(instant, offset));
    EXPECT_EQ(offset, original);
  }
}

TEST(TemporalValues, EverySupportedDateMatchesIndependentChronoCalendar) {
  // Compare all 3.65 million calendar days, including year zero and century
  // leap boundaries, with the standard calendar implementation.
  using namespace std::chrono;
  const auto first = sys_days{year{0} / January / 1};
  const auto end = sys_days{year{9999} / December / 31} + days{1};
  for (auto day = first; day < end; day += days{1}) {
    const year_month_day ymd{day};
    const LocalDateValue date{static_cast<int>(ymd.year()),
                              static_cast<std::uint8_t>(unsigned(ymd.month())),
                              static_cast<std::uint8_t>(unsigned(ymd.day()))};
    const InstantValue expected{
        duration_cast<seconds>(day.time_since_epoch()).count(), 999'999'999};
    InstantValue actual;
    ASSERT_TRUE(
        offset_to_instant({{date, {0, 0, 0, 999'999'999}}, 0, false}, actual));
    ASSERT_EQ(actual, expected);
    OffsetDateTimeValue roundtrip;
    ASSERT_TRUE(instant_to_offset(expected, roundtrip));
    ASSERT_EQ(roundtrip.local.date, date);
    ASSERT_EQ(roundtrip.local.time, (LocalTimeValue{0, 0, 0, 999'999'999}));
  }
}
}  // namespace
