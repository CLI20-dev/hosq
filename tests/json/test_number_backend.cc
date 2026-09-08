#include <gtest/gtest.h>
#include <hosq/detail/number_parse.hh>
#include <charconv>
#include <cmath>
#include <random>
#include <string>
#include <vector>

namespace {
template<class T, bool Json = false> void compare(const std::string& text) {
  T expected = T(42), actual = T(42);
  const auto a = std::from_chars(text.data(), text.data()+text.size(), expected);
  const auto b = hosq::detail::number_from_chars<T, Json>(text.data(), text.data()+text.size(), actual);
  ASSERT_EQ(a.ec, b.ec) << text;
  ASSERT_EQ(a.ptr-text.data(), b.ptr-text.data()) << text;
  if (std::isnan(expected)) EXPECT_TRUE(std::isnan(actual)) << text;
  else {
    EXPECT_EQ(expected, actual) << text;
    EXPECT_EQ(std::signbit(expected), std::signbit(actual)) << text;
  }
}
TEST(NumberBackend, FromCharsContract) {
  for (const auto* text : {"", "-", "+1", " 1", "0", "-0", "-0.0", ".5",
      "1.", "1e", "1e+", "1.25junk", "nan", "nan(payload)", "-nan",
      "inf", "-infinity", "1e9999", "-1e9999", "1e-9999", "-1e-9999",
      "2.2250738585072014e-308", "4.9406564584124654e-324",
      "1.7976931348623157e308", "1.7976931348623159e308",
      "3.4028234663852886e38", "1.1754943508222875e-38", "1.401298464324817e-45"}) {
    compare<float>(text); compare<double>(text);
    compare<float, true>(text); compare<double, true>(text);
  }
  std::mt19937_64 rng(20260906);
  for (int i=0; i<10000; ++i) {
    auto text = std::string(i%2 ? "-" : "") + std::to_string(rng()) + "." +
        std::to_string(rng()) + "e" + std::to_string(int(rng()%800)-400);
    compare<float>(text); compare<double>(text);
    compare<float, true>(text); compare<double, true>(text);
  }
}

TEST(NumberBackend, JsonPrefixLeavesTheFollowingInput) {
  for (const std::string suffix : {",\"next\":123}", "]", "}", "\r\n ", "\"", ""}) {
    const std::string text = "-2.5e-12" + suffix;
    double value = 42;
    const auto result = hosq::detail::number_from_chars<double, true>(
        text.data(), text.data() + text.size(), value);
    ASSERT_EQ(result.ec, std::errc{});
    EXPECT_EQ(result.ptr, text.data() + 8);
    EXPECT_EQ(value, -2.5e-12);
  }
}

template<class T> void compare_integer(std::string_view text) {
  T expected = 42, actual = 42;
  const auto a = std::from_chars(text.data(), text.data() + text.size(), expected);
  const auto b = hosq::detail::number_from_chars(
      text.data(), text.data() + text.size(), actual);
  ASSERT_EQ(a.ec, b.ec) << text;
  ASSERT_EQ(a.ptr, b.ptr) << text;
  EXPECT_EQ(expected, actual) << text;
}

TEST(NumberBackend, IntegerPrefixAndOverflowContract) {
  auto check = [](std::string_view text) {
    compare_integer<int8_t>(text); compare_integer<uint8_t>(text);
    compare_integer<int16_t>(text); compare_integer<uint16_t>(text);
    compare_integer<int32_t>(text); compare_integer<uint32_t>(text);
    compare_integer<int64_t>(text); compare_integer<uint64_t>(text);
  };
  for (const std::string text : {"", "-", "+1", "0", "-0", "000123,4]",
       "127", "128", "255", "256", "-128", "-129", "32767", "32768",
       "65535", "65536", "2147483647", "2147483648", "4294967295",
       "4294967296", "9223372036854775807", "9223372036854775808",
       "-9223372036854775808", "-9223372036854775809", "18446744073709551615",
       "18446744073709551616", "99999999999999999999", "42.0,1]", "42e3,1]"}) {
    for (std::size_t n = 0; n <= text.size(); ++n) check(std::string_view(text).substr(0, n));
  }
  std::mt19937_64 rng(20260908);
  for (int i = 0; i < 10000; ++i) {
    auto text = std::string(i % 2 ? "-" : "") + std::to_string(rng()) + ",0]";
    if (i % 3 == 0) text.insert(i % 2 ? 1 : 0, rng() % 40, '0');
    check(text);
  }
}
}
