#include "ser20/pipeline/validate.hh"

#include <gtest/gtest.h>

#include <string_view>

#include "common.hh"

using namespace std::string_view_literals;

TEST(ValidateMatch, Equal) {
  using P = decltype(ser20::validate::match<42>);
  auto r = pinvoke<P>(42);
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(*r, 42);
}

TEST(ValidateMatch, NotEqual) {
  using P = decltype(ser20::validate::match<42>);
  auto r = pinvoke<P>(99);
  ASSERT_FALSE(r.has_value());
  EXPECT_EQ(r.error().code, ser20::ErrorCode::invalid_choice);
}

TEST(ValidateOneOf, Matches) {
  using P = decltype(ser20::validate::one_of<1, 2, 3>);
  EXPECT_TRUE(pinvoke<P>(1).has_value());
  EXPECT_TRUE(pinvoke<P>(2).has_value());
  EXPECT_TRUE(pinvoke<P>(3).has_value());
}

TEST(ValidateOneOf, NoMatch) {
  using P = decltype(ser20::validate::one_of<1, 2, 3>);
  auto r = pinvoke<P>(4);
  ASSERT_FALSE(r.has_value());
  EXPECT_EQ(r.error().code, ser20::ErrorCode::invalid_choice);
}

TEST(ValidateRange, InRange) {
  using P = decltype(ser20::validate::range<0, 10>);
  EXPECT_TRUE(pinvoke<P>(0).has_value());
  EXPECT_TRUE(pinvoke<P>(5).has_value());
  EXPECT_TRUE(pinvoke<P>(10).has_value());
}

TEST(ValidateRange, OutOfRange) {
  using P = decltype(ser20::validate::range<0, 10>);
  auto lo = pinvoke<P>(-1);
  ASSERT_FALSE(lo.has_value());
  EXPECT_EQ(lo.error().code, ser20::ErrorCode::out_of_range);

  auto hi = pinvoke<P>(11);
  ASSERT_FALSE(hi.has_value());
  EXPECT_EQ(hi.error().code, ser20::ErrorCode::out_of_range);
}

TEST(ValidateMin, AboveOrAt) {
  using P = decltype(ser20::validate::min<5>);
  EXPECT_TRUE(pinvoke<P>(5).has_value());
  EXPECT_TRUE(pinvoke<P>(100).has_value());
}

TEST(ValidateMin, Below) {
  using P = decltype(ser20::validate::min<5>);
  auto r = pinvoke<P>(4);
  ASSERT_FALSE(r.has_value());
  EXPECT_EQ(r.error().code, ser20::ErrorCode::out_of_range);
}

TEST(ValidateMax, BelowOrAt) {
  using P = decltype(ser20::validate::max<5>);
  EXPECT_TRUE(pinvoke<P>(5).has_value());
  EXPECT_TRUE(pinvoke<P>(-100).has_value());
}

TEST(ValidateMax, Above) {
  using P = decltype(ser20::validate::max<5>);
  auto r = pinvoke<P>(6);
  ASSERT_FALSE(r.has_value());
  EXPECT_EQ(r.error().code, ser20::ErrorCode::out_of_range);
}

TEST(ValidateNonEmpty, NonEmpty) {
  using P = decltype(ser20::validate::non_empty);
  auto r = pinvoke<P>("hello"sv);
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(*r, "hello"sv);
}

TEST(ValidateNonEmpty, Empty) {
  using P = decltype(ser20::validate::non_empty);
  auto r = pinvoke<P>(""sv);
  ASSERT_FALSE(r.has_value());
  EXPECT_EQ(r.error().code, ser20::ErrorCode::validation_failed);
}

TEST(ValidateSize, Exact) {
  using P = decltype(ser20::validate::size<3>);
  EXPECT_TRUE(pinvoke<P>("abc"sv).has_value());
}

TEST(ValidateSize, Mismatch) {
  using P = decltype(ser20::validate::size<3>);
  auto r = pinvoke<P>("ab"sv);
  ASSERT_FALSE(r.has_value());
  EXPECT_EQ(r.error().code, ser20::ErrorCode::size_mismatch);
}

TEST(ValidateSizeRange, InRange) {
  using P = decltype(ser20::validate::size_range<2, 5>);
  EXPECT_TRUE(pinvoke<P>("ab"sv).has_value());
  EXPECT_TRUE(pinvoke<P>("abcde"sv).has_value());
}

TEST(ValidateSizeRange, OutOfRange) {
  using P = decltype(ser20::validate::size_range<2, 5>);
  EXPECT_FALSE(pinvoke<P>("a"sv).has_value());
  EXPECT_FALSE(pinvoke<P>("abcdef"sv).has_value());
}

TEST(ValidatePredicate, Passes) {
  constexpr auto is_even = [](int v) -> bool { return v % 2 == 0; };
  using P = decltype(ser20::validate::predicate<is_even>);
  EXPECT_TRUE(pinvoke<P>(4).has_value());
}

TEST(ValidatePredicate, Fails) {
  constexpr auto is_even = [](int v) -> bool { return v % 2 == 0; };
  using P = decltype(ser20::validate::predicate<is_even>);
  auto r = pinvoke<P>(3);
  ASSERT_FALSE(r.has_value());
  EXPECT_EQ(r.error().code, ser20::ErrorCode::validation_failed);
}

TEST(ValidateCustom, BoolReturn) {
  constexpr auto positive = [](int v) -> bool { return v > 0; };
  using P = decltype(ser20::validate::custom<positive>);
  EXPECT_TRUE(pinvoke<P>(1).has_value());
  EXPECT_FALSE(pinvoke<P>(-1).has_value());
}

TEST(ValidateCustom, ResultReturn) {
  constexpr auto check = [](int v) -> ser20::Result<int, ser20::Error> {
    if (v == 0) {
      return ser20::unexpected(
          ser20::Error{.code = ser20::ErrorCode::invalid_choice});
    }
    return v;
  };
  using P = decltype(ser20::validate::custom<check>);
  EXPECT_TRUE(pinvoke<P>(1).has_value());
  auto r = pinvoke<P>(0);
  ASSERT_FALSE(r.has_value());
  EXPECT_EQ(r.error().code, ser20::ErrorCode::invalid_choice);
}

TEST(ValidateIdentity, PassesThrough) {
  using P = decltype(ser20::validate::identity);
  auto r = pinvoke<P>(7);
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(*r, 7);
}
