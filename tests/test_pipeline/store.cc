#include <gtest/gtest.h>

#include <string_view>
#include <vector>

#include "ser20/pipeline/decode.hh"
#include "ser20/pipeline/store.hh"
#include "common.hh"

using namespace std::string_view_literals;

TEST(StoreAssign, WritesToElem) {
  ElemCtx<int> ctx;
  using P = decltype(ser20::store::assign);
  auto r = pinvoke_with<P>(42, ctx);
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(ctx.storage, 42);
}

TEST(StoreOverwrite, AllowsRepeatedWrites) {
  ElemCtx<int> ctx;
  using P = decltype(ser20::store::overwrite);
  ASSERT_TRUE(pinvoke_with<P>(1, ctx).has_value());
  ASSERT_TRUE(pinvoke_with<P>(2, ctx).has_value());
  EXPECT_EQ(ctx.storage, 2);
}

TEST(StorePush, AppendsToVector) {
  ElemCtx<std::vector<int>> ctx;
  using P = decltype(ser20::store::push);
  ASSERT_TRUE(pinvoke_with<P>(1, ctx).has_value());
  ASSERT_TRUE(pinvoke_with<P>(2, ctx).has_value());
  ASSERT_EQ(ctx.storage.size(), 2u);
  EXPECT_EQ(ctx.storage[0], 1);
  EXPECT_EQ(ctx.storage[1], 2);
}

TEST(StoreEmplace, AppendsToVector) {
  ElemCtx<std::vector<int>> ctx;
  using P = decltype(ser20::store::emplace);
  ASSERT_TRUE(pinvoke_with<P>(10, ctx).has_value());
  ASSERT_TRUE(pinvoke_with<P>(20, ctx).has_value());
  ASSERT_EQ(ctx.storage.size(), 2u);
  EXPECT_EQ(ctx.storage[0], 10);
  EXPECT_EQ(ctx.storage[1], 20);
}

TEST(StoreIgnore, DiscardsValue) {
  using P = decltype(ser20::decode::number<int> | ser20::store::ignore);
  auto r = pinvoke<P>("123"sv);
  EXPECT_TRUE(r.has_value());
}

