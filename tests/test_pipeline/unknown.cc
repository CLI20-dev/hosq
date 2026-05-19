#include <gtest/gtest.h>

#include "ser20/pipeline/unknown.hh"
#include "common.hh"

TEST(UnknownIdentity, PassesThrough) {
  using P = decltype(ser20::unknown::identity);
  auto r = pinvoke<P>(42);
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(*r, 42);
}

