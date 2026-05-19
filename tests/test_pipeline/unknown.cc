#include "ser20/pipeline/unknown.hh"

#include <gtest/gtest.h>

#include "common.hh"

TEST(UnknownIdentity, PassesThrough) {
  using P = decltype(ser20::unknown::identity);
  auto r = pinvoke<P>(42);
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(*r, 42);
}
