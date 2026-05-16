#include <gtest/gtest.h>

#include <string_view>
#include <variant>
#include <vector>

#include "ser20/parser.hh"

namespace {

// ── Simple flat object ────────────────────────────────────────────────────────

struct Point {
  ser20::ElemImpl<"x", ser20::scan::scalar | ser20::decode::integer<int> |
                           ser20::pack::once>
      x;
  ser20::ElemImpl<"y", ser20::scan::scalar | ser20::decode::integer<int> |
                           ser20::pack::once>
      y;
};

TEST(ParserTest, FlatObject) {
  auto p = ser20::parse<Point>(R"({"x": 3, "y": -7})");
  EXPECT_EQ(static_cast<int>(p.x), 3);
  EXPECT_EQ(static_cast<int>(p.y), -7);
}

TEST(ParserTest, FlatObjectReverseOrder) {
  auto p = ser20::parse<Point>(R"({"y": 100, "x": 42})");
  EXPECT_EQ(static_cast<int>(p.x), 42);
  EXPECT_EQ(static_cast<int>(p.y), 100);
}

// ── String field ──────────────────────────────────────────────────────────────

struct Named {
  ser20::ElemImpl<"name", ser20::scan::string | ser20::decode::string_view |
                              ser20::pack::once>
      name;
  ser20::ElemImpl<"count", ser20::scan::scalar | ser20::decode::integer<int> |
                               ser20::pack::once>
      count;
};

TEST(ParserTest, StringField) {
  auto n = ser20::parse<Named>(R"({"name": "hello", "count": 5})");
  EXPECT_EQ(static_cast<std::string_view>(n.name), "hello");
  EXPECT_EQ(static_cast<int>(n.count), 5);
}

TEST(ParserTest, StringFieldWithBraces) {
  auto n = ser20::parse<Named>(R"({"name": "val{ue}", "count": 3})");
  EXPECT_EQ(static_cast<std::string_view>(n.name), "val{ue}");
  EXPECT_EQ(static_cast<int>(n.count), 3);
}

TEST(ParserTest, StringFieldWithBrackets) {
  auto n = ser20::parse<Named>(R"({"name": "a[b,c]", "count": 4})");
  EXPECT_EQ(static_cast<std::string_view>(n.name), "a[b,c]");
  EXPECT_EQ(static_cast<int>(n.count), 4);
}

TEST(ParserTest, StringFieldWithAllStructural) {
  auto n = ser20::parse<Named>(R"({"name": "{[key:val,x]}", "count": 9})");
  EXPECT_EQ(static_cast<std::string_view>(n.name), "{[key:val,x]}");
  EXPECT_EQ(static_cast<int>(n.count), 9);
}

TEST(ParserTest, StringFieldWithEscapedQuote) {
  auto n = ser20::parse<Named>(R"({"name": "say \"hi\"", "count": 2})");
  EXPECT_EQ(static_cast<std::string_view>(n.name), R"(say \"hi\")");
  EXPECT_EQ(static_cast<int>(n.count), 2);
}

// ── Unknown field: ignore ─────────────────────────────────────────────────────

struct WithIgnore {
  ser20::ElemImpl<"val", ser20::scan::scalar | ser20::decode::integer<int> |
                             ser20::pack::once>
      val;
  ser20::ElemImpl<"", ser20::decode::member<ser20::pack::ignore> |
                          ser20::pack::ignore>
      unknown;
};

TEST(ParserTest, IgnoreUnknownFields) {
  auto w = ser20::parse<WithIgnore>(R"({"extra": 99, "val": 7, "other": "x"})");
  EXPECT_EQ(static_cast<int>(w.val), 7);
}

TEST(ParserTest, IgnoreUnknownStringWithBraces) {
  auto w = ser20::parse<WithIgnore>(R"({"extra": "{a:1}", "val": 7})");
  EXPECT_EQ(static_cast<int>(w.val), 7);
}

TEST(ParserTest, IgnoreUnknownStringWithBrackets) {
  auto w = ser20::parse<WithIgnore>(R"({"val": 5, "other": "[1,2,3]"})");
  EXPECT_EQ(static_cast<int>(w.val), 5);
}

TEST(ParserTest, IgnoreUnknownEscapedQuoteString) {
  // Unknown string field contains an escaped quote. Without skip_string(),
  // json_skip_value stops at the first '\"' (treating it as the close quote),
  // leaving the scanner mid-string and corrupting all subsequent field parses.
  auto w = ser20::parse<WithIgnore>(R"({"extra": "say \"hi\"", "val": 42})");
  EXPECT_EQ(static_cast<int>(w.val), 42);
}

TEST(ParserTest, IgnoreUnknownHtmlString) {
  // Simulates twitter.json's "source" field: HTML string with multiple \"
  // pairs. Without skip_string(), the parser stops at the first \" and
  // misreads everything after it, breaking integer fields like "val".
  auto w = ser20::parse<WithIgnore>(
      R"({"source": "<a href=\"http://x.com\" rel=\"nofollow\">Link</a>", "val": 7})");
  EXPECT_EQ(static_cast<int>(w.val), 7);
}

TEST(ParserTest, IgnoreUnknownObjectContainingEscapedQuoteString) {
  // Unknown nested object whose string values contain escaped quotes.
  // consume_until_matching must call skip_string() so it doesn't mistake a
  // '\"' inside a string for the real close quote of that string, which would
  // then leave an unmatched '}' that terminates the outer object early.
  auto w = ser20::parse<WithIgnore>(
      R"({"meta": {"href": "<a href=\"url\">text</a>"}, "val": 9})");
  EXPECT_EQ(static_cast<int>(w.val), 9);
}

// ── Nested object (parse_to) ──────────────────────────────────────────────────

struct Inner {
  ser20::ElemImpl<"a", ser20::scan::scalar | ser20::decode::integer<int> |
                           ser20::pack::once>
      a;
  ser20::ElemImpl<"b", ser20::scan::scalar | ser20::decode::integer<int> |
                           ser20::pack::once>
      b;
};

struct Outer {
  ser20::ElemImpl<"inner", ser20::decode::parse_to<Inner> | ser20::pack::once>
      inner;
  ser20::ElemImpl<"top", ser20::scan::scalar | ser20::decode::integer<int> |
                             ser20::pack::once>
      top;
};

TEST(ParserTest, NestedObject) {
  auto o = ser20::parse<Outer>(R"({"inner": {"a": 1, "b": 2}, "top": 99})");
  EXPECT_EQ(static_cast<int>(o.top), 99);
  const Inner& inner = o.inner;
  EXPECT_EQ(static_cast<int>(inner.a), 1);
  EXPECT_EQ(static_cast<int>(inner.b), 2);
}

// ── Array of scalars ──────────────────────────────────────────────────────────

struct IntList {
  ser20::ElemImpl<"vals", ser20::decode::vector<ser20::scan::scalar |
                                                ser20::decode::integer<int> |
                                                ser20::pack::assign> |
                              ser20::pack::once>
      vals;
};

TEST(ParserTest, ArrayOfScalars) {
  auto r = ser20::parse<IntList>(R"({"vals": [1, 2, 3]})");
  const std::vector<int>& v = r.vals;
  ASSERT_EQ(v.size(), 3u);
  EXPECT_EQ(v.at(0), 1);
  EXPECT_EQ(v.at(1), 2);
  EXPECT_EQ(v.at(2), 3);
}

TEST(ParserTest, ArraySingleElement) {
  auto r = ser20::parse<IntList>(R"({"vals": [42]})");
  const std::vector<int>& v = r.vals;
  ASSERT_EQ(v.size(), 1u);
  EXPECT_EQ(v.at(0), 42);
}

TEST(ParserTest, ArrayEmpty) {
  auto r = ser20::parse<IntList>(R"({"vals": []})");
  const std::vector<int>& v = r.vals;
  EXPECT_TRUE(v.empty());
}

// ── Array of objects ──────────────────────────────────────────────────────────

struct PointList {
  ser20::ElemImpl<"pts", ser20::decode::vector<ser20::decode::parse_to<Point> |
                                               ser20::pack::assign> |
                             ser20::pack::once>
      pts;
};

TEST(ParserTest, ArrayOfObjects) {
  auto r = ser20::parse<PointList>(
      R"({"pts": [{"x": 1, "y": 2}, {"x": 3, "y": 4}]})");
  const std::vector<Point>& v = r.pts;
  ASSERT_EQ(v.size(), 2u);
  EXPECT_EQ(static_cast<int>(v.at(0).x), 1);
  EXPECT_EQ(static_cast<int>(v.at(0).y), 2);
  EXPECT_EQ(static_cast<int>(v.at(1).x), 3);
  EXPECT_EQ(static_cast<int>(v.at(1).y), 4);
}

// ── global unknown handler (reject) ──────────────────────────────────────────

struct StrictDoc {
  ser20::ElemImpl<"val", ser20::scan::scalar | ser20::decode::integer<int> |
                             ser20::pack::once>
      val;
  ser20::ElemImpl<"", ser20::unknown::reject_with_suggestion> unknown;
};

TEST(ParserTest, KnownKeyParsesCorrectly) {
  auto r = ser20::parse<StrictDoc>(R"({"val": 55})");
  EXPECT_EQ(static_cast<int>(r.val), 55);
}

TEST(ParserTest, UnknownRejectPropagatesError) {
  auto r = ser20::parse_result<StrictDoc>(R"({"extra": 55})");
  ASSERT_TRUE(r.has_error());
  EXPECT_EQ(r.error().code, ser20::ErrorCode::unknown_option);
  EXPECT_EQ(r.error().subject, "extra");
}

TEST(ParserTest, NestedParseErrorPropagates) {
  auto r = ser20::parse_result<Outer>(
      R"({"inner": {"a": false, "b": 2}, "top": 99})");
  ASSERT_TRUE(r.has_error());
  EXPECT_EQ(r.error().code, ser20::ErrorCode::invalid_value);
  EXPECT_EQ(r.error().subject, "false");
}

}  // namespace
