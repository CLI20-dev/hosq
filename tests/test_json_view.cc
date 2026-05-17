#include <gtest/gtest.h>

#include <span>
#include <string_view>
#include <vector>

#include "ser20/json_view.hh"
#include "ser20/parser.hh"
#include "ser20/pipeline.hh"

// ── helpers ───────────────────────────────────────────────────────────────────

// Schema used in the tests
namespace tv {
struct Point {
  ser20::ElemImpl<"x", ser20::scan::scalar | ser20::decode::integer<int> |
                            ser20::pack::once>
      x;
  ser20::ElemImpl<"y", ser20::scan::scalar | ser20::decode::integer<int> |
                            ser20::pack::once>
      y;
  ser20::ElemImpl<"", ser20::decode::member<ser20::pack::ignore> |
                          ser20::pack::ignore>
      unknown;
};

struct Wrapper {
  ser20::ElemImpl<"point",
                  ser20::scan::object_tokens | ser20::pack::once>
      point;
  ser20::ElemImpl<"", ser20::decode::member<ser20::pack::ignore> |
                          ser20::pack::ignore>
      unknown;
};

struct WrapperN {
  ser20::ElemImpl<"point",
                  ser20::scan::object_tokens_n<64> | ser20::pack::once>
      point;
  ser20::ElemImpl<"", ser20::decode::member<ser20::pack::ignore> |
                          ser20::pack::ignore>
      unknown;
};

struct Numbers {
  ser20::ElemImpl<"values",
                  ser20::scan::array_tokens | ser20::pack::once>
      values;
  ser20::ElemImpl<"", ser20::decode::member<ser20::pack::ignore> |
                          ser20::pack::ignore>
      unknown;
};

}  // namespace tv

// ── collect_structural ────────────────────────────────────────────────────────

class CollectStructuralTest : public ::testing::Test {};

TEST_F(CollectStructuralTest, EmptyObject) {
  // {} → positions: [0, 1] ('{' and '}')
  const std::string_view sv = "{}";
  ser20::ParserScanner sc(sv);
  (void)sc.next();  // consume '{'
  std::vector<size_t> pos;
  ser20::collect_structural(sc, sv, 0, [&](size_t r) { pos.push_back(r); });
  ASSERT_EQ(pos.size(), 2u);
  EXPECT_EQ(sv[pos[0]], '{');
  EXPECT_EQ(sv[pos[1]], '}');
}

TEST_F(CollectStructuralTest, SimpleObject) {
  // {"a":1,"b":2}
  const std::string_view sv = R"({"a":1,"b":2})";
  ser20::ParserScanner sc(sv);
  (void)sc.next();  // consume '{'
  std::vector<size_t> pos;
  ser20::collect_structural(sc, sv, 0, [&](size_t r) { pos.push_back(r); });
  // Verify first is '{' and last is '}'
  EXPECT_EQ(sv[pos.front()], '{');
  EXPECT_EQ(sv[pos.back()], '}');
  // All stored positions must refer to structural chars
  for (size_t r : pos) {
    const char c = sv[r];
    EXPECT_TRUE(c == '{' || c == '}' || c == '[' || c == ']' ||
                c == '"' || c == ':' || c == ',')
        << "unexpected char '" << c << "' at offset " << r;
  }
}

TEST_F(CollectStructuralTest, StringInteriorFiltered) {
  // Structural chars inside string values must NOT appear in positions.
  // e.g.  {"k":"val {\"}\","x":1}
  //                ^^ ^ these are inside a string — must not be collected
  const std::string_view sv = R"({"k":"val {\"}\","x":1})";
  ser20::ParserScanner sc(sv);
  (void)sc.next();
  std::vector<size_t> pos;
  ser20::collect_structural(sc, sv, 0, [&](size_t r) { pos.push_back(r); });

  // No position should point inside the string value "val {\"}\",
  // i.e. between the opening and closing real quotes of that value.
  // The opening '"' of "val..." is at sv.find('"', 4) = 4+1 = 5.
  // The chars at positions 10 ('{') and 12 ('}') are inside the string.
  for (size_t r : pos) {
    EXPECT_NE(sv[r], '{') << "found '{' inside string at offset " << r;
    // We expect no raw '}' that is inside the string content
    if (sv[r] == '}') {
      // Only the final closing '}' at the end is valid
      EXPECT_EQ(r, sv.size() - 1) << "spurious '}' at offset " << r;
    }
  }
}

TEST_F(CollectStructuralTest, NestedObject) {
  const std::string_view sv = R"({"a":{"b":1}})";
  ser20::ParserScanner sc(sv);
  (void)sc.next();
  std::vector<size_t> pos;
  ser20::collect_structural(sc, sv, 0, [&](size_t r) { pos.push_back(r); });
  EXPECT_EQ(sv[pos.front()], '{');
  EXPECT_EQ(sv[pos.back()], '}');
  // Both '{' positions should appear
  size_t open_count = 0, close_count = 0;
  for (size_t r : pos) {
    if (sv[r] == '{') ++open_count;
    if (sv[r] == '}') ++close_count;
  }
  EXPECT_EQ(open_count, 2u);
  EXPECT_EQ(close_count, 2u);
}

TEST_F(CollectStructuralTest, EmptyArray) {
  const std::string_view sv = "[]";
  ser20::ParserScanner sc(sv);
  (void)sc.next();
  std::vector<size_t> pos;
  ser20::collect_structural(sc, sv, 0, [&](size_t r) { pos.push_back(r); });
  ASSERT_EQ(pos.size(), 2u);
  EXPECT_EQ(sv[pos[0]], '[');
  EXPECT_EQ(sv[pos[1]], ']');
}

TEST_F(CollectStructuralTest, ArrayWithEscapedStringElements) {
  // Ensure escaped quotes inside array strings don't confuse the collector.
  const std::string_view sv = R"(["say \"hi\"","done"])";
  ser20::ParserScanner sc(sv);
  (void)sc.next();
  std::vector<size_t> pos;
  ser20::collect_structural(sc, sv, 0, [&](size_t r) { pos.push_back(r); });
  EXPECT_EQ(sv[pos.front()], '[');
  EXPECT_EQ(sv[pos.back()], ']');
}

TEST_F(CollectStructuralTest, PositionsRelativeToFirstPos) {
  // Embed the target in a larger string; first_pos is non-zero.
  const std::string_view input = R"(PREFIX{"k":42}SUFFIX)";
  const size_t first_pos = 6;  // position of '{'
  ser20::ParserScanner sc(input);
  // Advance scanner to the '{' at first_pos
  while (true) {
    size_t p = sc.next();
    if (p == first_pos) break;
  }
  std::vector<size_t> rel_pos;
  ser20::collect_structural(sc, input, first_pos,
                             [&](size_t r) { rel_pos.push_back(r); });
  // All positions should be relative to first_pos
  for (size_t r : rel_pos) {
    EXPECT_LT(r, input.size() - first_pos)
        << "relative position " << r << " exceeds slice size";
    const char c = input[first_pos + r];
    EXPECT_TRUE(c == '{' || c == '}' || c == '"' || c == ':' || c == ',')
        << "unexpected char '" << c << "'";
  }
  EXPECT_EQ(input[first_pos + rel_pos.front()], '{');
  EXPECT_EQ(input[first_pos + rel_pos.back()], '}');
}

// ── TokenVector ───────────────────────────────────────────────────────────────

class TokenVectorTest : public ::testing::Test {};

TEST_F(TokenVectorTest, DefaultConstructed) {
  ser20::TokenVector tv;
  EXPECT_TRUE(tv.slice().empty());
  EXPECT_TRUE(tv.positions().empty());
}

TEST_F(TokenVectorTest, SliceAndPositions) {
  std::string_view sv = R"({"x":1})";
  std::vector<size_t> pos = {0, 1, 2, 3, 5, 6};
  ser20::TokenVector tv(sv, pos);
  EXPECT_EQ(tv.slice(), sv);
  EXPECT_EQ(tv.positions().size(), pos.size());
}

TEST_F(TokenVectorTest, AsParsesSelf) {
  const std::string_view sv = R"({"x":3,"y":7})";
  std::vector<size_t> dummy_pos;
  ser20::ParserScanner sc(sv);
  (void)sc.next();
  ser20::collect_structural(sc, sv, 0, [&](size_t r) { dummy_pos.push_back(r); });
  ser20::TokenVector tv(sv, std::move(dummy_pos));

  auto pt = tv.as<tv::Point>();
  EXPECT_EQ(static_cast<int>(pt.x), 3);
  EXPECT_EQ(static_cast<int>(pt.y), 7);
}

// ── TokenView<N> ─────────────────────────────────────────────────────────────

class TokenViewTest : public ::testing::Test {};

TEST_F(TokenViewTest, DefaultConstructed) {
  ser20::TokenView<16> tv;
  EXPECT_EQ(tv.size(), 0u);
  EXPECT_FALSE(tv.overflow());
}

TEST_F(TokenViewTest, PushWithinCapacity) {
  ser20::TokenView<4> tv;
  EXPECT_TRUE(tv.push(0));
  EXPECT_TRUE(tv.push(1));
  EXPECT_TRUE(tv.push(2));
  EXPECT_TRUE(tv.push(3));
  EXPECT_EQ(tv.size(), 4u);
  EXPECT_FALSE(tv.overflow());
}

TEST_F(TokenViewTest, OverflowWhenFull) {
  ser20::TokenView<2> tv;
  tv.push(0);
  tv.push(1);
  tv.push(2);  // overflow
  EXPECT_TRUE(tv.overflow());
}

TEST_F(TokenViewTest, AsParsesSelf) {
  const std::string_view sv = R"({"x":5,"y":9})";
  ser20::TokenView<32> tv;
  tv.set_slice(sv);
  ser20::ParserScanner sc(sv);
  (void)sc.next();
  ser20::collect_structural(sc, sv, 0, [&](size_t r) { tv.push(r); });
  ASSERT_FALSE(tv.overflow());

  auto pt = tv.as<tv::Point>();
  EXPECT_EQ(static_cast<int>(pt.x), 5);
  EXPECT_EQ(static_cast<int>(pt.y), 9);
}

// ── scan::object_tokens pipeline primitive ────────────────────────────────────

class ScanObjectTokensTest : public ::testing::Test {};

TEST_F(ScanObjectTokensTest, CapturesNestedObject) {
  const std::string_view sv = R"({"point":{"x":10,"y":20}})";
  auto doc = ser20::parse<tv::Wrapper>(sv);
  const ser20::TokenVector& tv_ref = doc.point;

  // Slice should contain the nested object
  EXPECT_EQ(tv_ref.slice(), R"({"x":10,"y":20})");

  // Re-parse via as<>
  auto pt = tv_ref.as<tv::Point>();
  EXPECT_EQ(static_cast<int>(pt.x), 10);
  EXPECT_EQ(static_cast<int>(pt.y), 20);
}

TEST_F(ScanObjectTokensTest, PositionsDoNotIncludeStringInterior) {
  // The value strings contain structural chars that must be filtered out.
  const std::string_view sv = R"({"point":{"x":1,"y":2,"label":"a{b}c"}})";
  auto doc = ser20::parse<tv::Wrapper>(sv);
  const ser20::TokenVector& tv_ref = doc.point;

  // Verify: no position inside the string "a{b}c" (positions inside the
  // slice "a{b}c" are characters '{' and '}').  After filtering, only the
  // structural tokens of the object itself should appear.
  const std::string_view sl = tv_ref.slice();
  for (size_t r : tv_ref.positions()) {
    if (sl[r] == '{' || sl[r] == '}') {
      // Must be the outermost '{' at 0 or '}' at the very end
      EXPECT_TRUE(r == 0 || r == sl.size() - 1)
          << "spurious structural char '" << sl[r] << "' at slice offset " << r;
    }
  }
}

TEST_F(ScanObjectTokensTest, FixedN) {
  const std::string_view sv = R"({"point":{"x":3,"y":4}})";
  auto doc = ser20::parse<tv::WrapperN>(sv);
  const ser20::TokenView<64>& tv_ref = doc.point;

  EXPECT_FALSE(tv_ref.overflow());
  EXPECT_EQ(tv_ref.slice(), R"({"x":3,"y":4})");

  auto pt = tv_ref.as<tv::Point>();
  EXPECT_EQ(static_cast<int>(pt.x), 3);
  EXPECT_EQ(static_cast<int>(pt.y), 4);
}

// ── scan::array_tokens pipeline primitive ─────────────────────────────────────

class ScanArrayTokensTest : public ::testing::Test {};

TEST_F(ScanArrayTokensTest, CapturesArray) {
  const std::string_view sv = R"({"values":[1,2,3]})";
  auto doc = ser20::parse<tv::Numbers>(sv);
  const ser20::TokenVector& tv_ref = doc.values;

  EXPECT_EQ(tv_ref.slice(), "[1,2,3]");
  EXPECT_FALSE(tv_ref.positions().empty());
  EXPECT_EQ(tv_ref.slice()[tv_ref.positions().front()], '[');
  EXPECT_EQ(tv_ref.slice()[tv_ref.positions().back()], ']');
}

TEST_F(ScanArrayTokensTest, StringInteriorFilteredInArray) {
  const std::string_view sv = R"({"values":["a{b}","c[d]"]})";
  auto doc = ser20::parse<tv::Numbers>(sv);
  const ser20::TokenVector& tv_ref = doc.values;

  // No position in the stored slice should point to a '{', '}', '[', or ']'
  // that is inside a string element.
  const std::string_view sl = tv_ref.slice();
  for (size_t r : tv_ref.positions()) {
    if (sl[r] == '[' || sl[r] == ']') {
      EXPECT_TRUE(r == 0 || r == sl.size() - 1)
          << "spurious bracket '" << sl[r] << "' at slice offset " << r;
    }
  }
}
