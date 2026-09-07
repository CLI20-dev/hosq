#include <gtest/gtest.h>
#include <hosq/detail/number_parse.hh>
#include <charconv>
#include <cmath>
#include <random>
#include <string>
#include <vector>

namespace {
template<class T> void compare(const std::string& text) {
  T expected = T(42), actual = T(42);
  const auto a = std::from_chars(text.data(), text.data()+text.size(), expected);
  const auto b = hosq::detail::number_from_chars(text.data(), text.data()+text.size(), actual);
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
  }
  std::mt19937_64 rng(20260906);
  for (int i=0; i<10000; ++i) {
    auto text = std::string(i%2 ? "-" : "") + std::to_string(rng()) + "." +
        std::to_string(rng()) + "e" + std::to_string(int(rng()%800)-400);
    compare<float>(text); compare<double>(text);
  }
}
}
