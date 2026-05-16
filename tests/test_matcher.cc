#include <gtest/gtest.h>

#include <string>
#include <string_view>
#include <tuple>

#include "ser20/matcher.hh"
#include "ser20/string_literal.hh"

namespace {

using ser20::Matcher;

// ── Field helper ──────────────────────────────────────────────────────────────

template <ser20::StringLiteral N>
struct Field {
  static constexpr std::string_view name = {N.data(), N.size()};
  int value{0};
};

// ── Basic find ────────────────────────────────────────────────────────────────

TEST(MatcherTest, FindFirst) {
  auto fields = std::make_tuple(Field<"a">{}, Field<"b">{}, Field<"c">{});
  bool hit = Matcher::find("a", fields, [](auto& f) { f.value = 1; });
  EXPECT_TRUE(hit);
  EXPECT_EQ(std::get<0>(fields).value, 1);
  EXPECT_EQ(std::get<1>(fields).value, 0);
  EXPECT_EQ(std::get<2>(fields).value, 0);
}

TEST(MatcherTest, FindMid) {
  auto fields = std::make_tuple(Field<"a">{}, Field<"b">{}, Field<"c">{});
  bool hit = Matcher::find("b", fields, [](auto& f) { f.value = 2; });
  EXPECT_TRUE(hit);
  EXPECT_EQ(std::get<0>(fields).value, 0);
  EXPECT_EQ(std::get<1>(fields).value, 2);
  EXPECT_EQ(std::get<2>(fields).value, 0);
}

TEST(MatcherTest, FindLast) {
  auto fields = std::make_tuple(Field<"a">{}, Field<"b">{}, Field<"c">{});
  bool hit = Matcher::find("c", fields, [](auto& f) { f.value = 3; });
  EXPECT_TRUE(hit);
  EXPECT_EQ(std::get<0>(fields).value, 0);
  EXPECT_EQ(std::get<1>(fields).value, 0);
  EXPECT_EQ(std::get<2>(fields).value, 3);
}

TEST(MatcherTest, Miss) {
  auto fields = std::make_tuple(Field<"a">{}, Field<"b">{}, Field<"c">{});
  bool hit = Matcher::find("z", fields, [](auto& f) { f.value = 99; });
  EXPECT_FALSE(hit);
  EXPECT_EQ(std::get<0>(fields).value, 0);
  EXPECT_EQ(std::get<1>(fields).value, 0);
  EXPECT_EQ(std::get<2>(fields).value, 0);
}

// ── Empty tuple ───────────────────────────────────────────────────────────────

TEST(MatcherTest, EmptyTuple) {
  auto fields = std::make_tuple();
  bool hit = Matcher::find("x", fields, [](auto&) {});
  EXPECT_FALSE(hit);
}

// ── Single element ────────────────────────────────────────────────────────────

TEST(MatcherTest, SingleElementHit) {
  auto fields = std::make_tuple(Field<"only">{});
  bool hit = Matcher::find("only", fields, [](auto& f) { f.value = 7; });
  EXPECT_TRUE(hit);
  EXPECT_EQ(std::get<0>(fields).value, 7);
}

TEST(MatcherTest, SingleElementMiss) {
  auto fields = std::make_tuple(Field<"only">{});
  bool hit = Matcher::find("other", fields, [](auto& f) { f.value = 7; });
  EXPECT_FALSE(hit);
  EXPECT_EQ(std::get<0>(fields).value, 0);
}

// ── Callback invoked exactly once ────────────────────────────────────────────

TEST(MatcherTest, CallbackOnce) {
  auto fields = std::make_tuple(Field<"x">{}, Field<"x2">{});
  int count = 0;
  Matcher::find("x", fields, [&](auto&) { ++count; });
  EXPECT_EQ(count, 1);
}

// ── Returns false without invoking callback on miss ───────────────────────────

TEST(MatcherTest, MissCallbackNotInvoked) {
  auto fields = std::make_tuple(Field<"a">{}, Field<"b">{});
  bool invoked = false;
  bool hit = Matcher::find("c", fields, [&](auto&) { invoked = true; });
  EXPECT_FALSE(hit);
  EXPECT_FALSE(invoked);
}

// ── Long names ────────────────────────────────────────────────────────────────

TEST(MatcherTest, LongNames) {
  auto fields = std::make_tuple(
      Field<"very_long_field_name_that_goes_on_for_quite_a_while_aaa">{},
      Field<"very_long_field_name_that_goes_on_for_quite_a_while_bbb">{});

  bool hit =
      Matcher::find("very_long_field_name_that_goes_on_for_quite_a_while_bbb",
                    fields, [](auto& f) { f.value = 42; });

  EXPECT_TRUE(hit);
  EXPECT_EQ(std::get<0>(fields).value, 0);
  EXPECT_EQ(std::get<1>(fields).value, 42);
}

// ── Prefix that differs only at the end ──────────────────────────────────────

TEST(MatcherTest, SimilarPrefixDistinguishedAtEnd) {
  auto fields = std::make_tuple(Field<"field_a">{}, Field<"field_b">{},
                                Field<"field_c">{});
  bool hit = Matcher::find("field_b", fields, [](auto& f) { f.value = 1; });
  EXPECT_TRUE(hit);
  EXPECT_EQ(std::get<1>(fields).value, 1);

  bool miss = Matcher::find("field_", fields, [](auto& f) { f.value = 99; });
  EXPECT_FALSE(miss);
}

// ── Callback can mutate the field ────────────────────────────────────────────

TEST(MatcherTest, CallbackMutates) {
  auto fields = std::make_tuple(Field<"counter">{});
  for (int i = 0; i < 5; ++i) {
    Matcher::find("counter", fields, [](auto& f) { ++f.value; });
  }
  EXPECT_EQ(std::get<0>(fields).value, 5);
}

}  // namespace
