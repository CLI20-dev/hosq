#include "ser20/pipeline/encode.hh"

#include <gtest/gtest.h>

#include <string_view>

#include "common.hh"

using namespace std::string_view_literals;

TEST(EncodeNumber, Integer) {
  using P = decltype(ser20::encode::number<int>);
  auto r = pinvoke<P>(42);
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(*r, "42");
}

TEST(EncodeNumber, NegativeInteger) {
  using P = decltype(ser20::encode::number<int>);
  auto r = pinvoke<P>(-7);
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(*r, "-7");
}

TEST(EncodeNumber, Float) {
  using P = decltype(ser20::encode::number<double>);
  auto r = pinvoke<P>(3.14);
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(*r, "3.14");
}

TEST(EncodeBoolean, True) {
  using P = decltype(ser20::encode::boolean);
  auto r = pinvoke<P>(true);
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(*r, "true"sv);
}

TEST(EncodeBoolean, False) {
  using P = decltype(ser20::encode::boolean);
  auto r = pinvoke<P>(false);
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(*r, "false"sv);
}

TEST(EncodeStringView, PassesThrough) {
  using P = decltype(ser20::encode::string_view);
  auto r = pinvoke<P>("hello"sv);
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(*r, "hello"sv);
}

TEST(EncodeIdentity, PassesThrough) {
  using P = decltype(ser20::encode::identity);
  auto r = pinvoke<P>(7);
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(*r, 7);
}
