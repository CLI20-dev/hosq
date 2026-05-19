#include <gtest/gtest.h>

#include <cstdint>
#include <string_view>

#include "ser20/pipeline/decode.hh"
#include "common.hh"

using namespace std::string_view_literals;

TEST(DecodeBoolean, True) {
  using P = decltype(ser20::decode::boolean);
  auto r = pinvoke<P>("true"sv);
  ASSERT_TRUE(r.has_value());
  EXPECT_TRUE(r.value());
}

TEST(DecodeBoolean, False) {
  using P = decltype(ser20::decode::boolean);
  auto r = pinvoke<P>("false"sv);
  ASSERT_TRUE(r.has_value());
  EXPECT_FALSE(r.value());
}

TEST(DecodeBoolean, InvalidReturnsTypeMismatch) {
  using P = decltype(ser20::decode::boolean);
  auto r = pinvoke<P>("yes"sv);
  ASSERT_FALSE(r.has_value());
  EXPECT_EQ(r.error().code, ser20::ErrorCode::type_mismatch);
}

TEST(DecodeStringView, PassesThrough) {
  using P = decltype(ser20::decode::string_view);
  auto r = pinvoke<P>("hello"sv);
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(*r, "hello"sv);
}

TEST(DecodeStringView, Empty) {
  using P = decltype(ser20::decode::string_view);
  auto r = pinvoke<P>(""sv);
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(*r, ""sv);
}

TEST(DecodeNumber, Integer) {
  using P = decltype(ser20::decode::number<int>);
  auto r = pinvoke<P>("42"sv);
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(*r, 42);
}

TEST(DecodeNumber, NegativeInteger) {
  using P = decltype(ser20::decode::number<int>);
  auto r = pinvoke<P>("-7"sv);
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(*r, -7);
}

TEST(DecodeNumber, Float) {
  using P = decltype(ser20::decode::number<double>);
  auto r = pinvoke<P>("3.14"sv);
  ASSERT_TRUE(r.has_value());
  EXPECT_DOUBLE_EQ(*r, 3.14);
}

TEST(DecodeNumber, OutOfRange) {
  using P = decltype(ser20::decode::number<int8_t>);
  auto r = pinvoke<P>("999"sv);
  ASSERT_FALSE(r.has_value());
  EXPECT_EQ(r.error().code, ser20::ErrorCode::out_of_range);
}

TEST(DecodeNumber, InvalidString) {
  using P = decltype(ser20::decode::number<int>);
  auto r = pinvoke<P>("abc"sv);
  ASSERT_FALSE(r.has_value());
  EXPECT_EQ(r.error().code, ser20::ErrorCode::invalid_number);
}

TEST(DecodeNumber, TrailingChars) {
  using P = decltype(ser20::decode::number<int>);
  auto r = pinvoke<P>("42abc"sv);
  ASSERT_FALSE(r.has_value());
  EXPECT_EQ(r.error().code, ser20::ErrorCode::invalid_number);
}

TEST(DecodeIdentity, Int) {
  using P = decltype(ser20::decode::identity);
  auto r = pinvoke<P>(99);
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(*r, 99);
}

TEST(DecodeIdentity, StringView) {
  using P = decltype(ser20::decode::identity);
  auto r = pinvoke<P>("foo"sv);
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(*r, "foo"sv);
}

TEST(DecodeCustom, PlainReturn) {
  constexpr auto to_len = [](std::string_view sv) -> int {
    return static_cast<int>(sv.size());
  };
  using P = decltype(ser20::decode::custom<to_len>);
  auto r = pinvoke<P>("hello"sv);
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(*r, 5);
}

TEST(DecodeCustom, ResultReturn) {
  constexpr auto checked = [](int v) -> ser20::Result<int, ser20::Error> {
    if (v < 0) {
      return ser20::unexpected(
          ser20::Error{.code = ser20::ErrorCode::out_of_range});
    }
    return v * 2;
  };
  using P = decltype(ser20::decode::custom<checked>);
  auto ok = pinvoke<P>(3);
  ASSERT_TRUE(ok.has_value());
  EXPECT_EQ(*ok, 6);

  auto err = pinvoke<P>(-1);
  ASSERT_FALSE(err.has_value());
  EXPECT_EQ(err.error().code, ser20::ErrorCode::out_of_range);
}

