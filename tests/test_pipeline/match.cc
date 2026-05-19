#include "ser20/pipeline/match.hh"

#include <gtest/gtest.h>

#include <string_view>

#include "common.hh"

using namespace std::string_view_literals;

TEST(MatchAny, PassesThrough) {
  using P = decltype(ser20::match::any);
  auto r = pinvoke<P>("anything"sv);
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(*r, "anything"sv);
}
