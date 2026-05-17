#include <gtest/gtest.h>

#include <string>
#include <string_view>
#include <vector>

#include "ser20/parse_context.hh"
#include "ser20/scanner.hh"
#ifdef __ARM_NEON
#include "ser20/scanner_neon.hh"
#include "ser20/scanner_neon2.hh"
#endif

namespace {

// ── Typed test infrastructure ─────────────────────────────────────────────────

template <typename S>
class ScannerTest : public ::testing::Test {};

#ifdef __ARM_NEON
using ScannerTypes = ::testing::Types<ser20::ScannerSimple, ser20::ScannerNeon>;
#else
using ScannerTypes = ::testing::Types<ser20::Scanner>;
#endif
TYPED_TEST_SUITE(ScannerTest, ScannerTypes);

// Collect all candidate positions emitted by the scanner.
template <typename S>
static auto collect(std::string_view input) -> std::vector<size_t> {
  S scanner(input);
  std::vector<size_t> out;
  while (true) {
    const size_t pos = scanner.next();
    if (pos >= input.size()) break;
    out.push_back(pos);
  }
  return out;
}

// ── Basic structural candidates ───────────────────────────────────────────────

TYPED_TEST(ScannerTest, EmptyInput) {
  TypeParam s("");
  EXPECT_GE(s.next(), size_t{0});  // returns input.size() == 0
  EXPECT_TRUE(s.eof());
}

TYPED_TEST(ScannerTest, WhitespaceOnly) {
  TypeParam s("   \t\n\r");
  const size_t pos = s.next();
  EXPECT_GE(pos, 6u);  // no structural chars
}

TYPED_TEST(ScannerTest, EmptyObject) {
  auto pos = collect<TypeParam>("{}");
  ASSERT_EQ(pos.size(), 2u);
  EXPECT_EQ(pos[0], 0u);
  EXPECT_EQ(pos[1], 1u);
  TypeParam s("{}");
  EXPECT_EQ(s.input()[pos[0]], '{');
  EXPECT_EQ(s.input()[pos[1]], '}');
}

TYPED_TEST(ScannerTest, EmptyArray) {
  auto pos = collect<TypeParam>("[]");
  ASSERT_EQ(pos.size(), 2u);
  EXPECT_EQ(pos[0], 0u);
  EXPECT_EQ(pos[1], 1u);
  TypeParam s("[]");
  EXPECT_EQ(s.input()[pos[0]], '[');
  EXPECT_EQ(s.input()[pos[1]], ']');
}

TYPED_TEST(ScannerTest, ColonAndComma) {
  // "{:,}" → positions 0(':' no, '{' yes), wait: {=0 :=1 ,=2 }=3
  auto pos = collect<TypeParam>("{:,}");
  ASSERT_EQ(pos.size(), 4u);
  std::string_view sv = "{:,}";
  EXPECT_EQ(sv[pos[1]], ':');
  EXPECT_EQ(sv[pos[2]], ',');
}

TYPED_TEST(ScannerTest, WhitespaceSkipped) {
  auto pos = collect<TypeParam>("  {  }  ");
  ASSERT_EQ(pos.size(), 2u);
  EXPECT_EQ(pos[0], 2u);
  EXPECT_EQ(pos[1], 5u);
}

// ── Quote candidates ──────────────────────────────────────────────────────────

TYPED_TEST(ScannerTest, SimpleString) {
  // {"a":"b"} — structural positions: { " " : " " }
  auto pos = collect<TypeParam>(R"({"a":"b"})");
  ASSERT_EQ(pos.size(), 7u);
  std::string_view sv = R"({"a":"b"})";
  EXPECT_EQ(sv[pos[0]], '{');
  EXPECT_EQ(sv[pos[1]], '"');
  EXPECT_EQ(sv[pos[2]], '"');
  EXPECT_EQ(sv[pos[3]], ':');
  EXPECT_EQ(sv[pos[4]], '"');
  EXPECT_EQ(sv[pos[5]], '"');
  EXPECT_EQ(sv[pos[6]], '}');
}

TYPED_TEST(ScannerTest, QuotePositions) {
  auto pos = collect<TypeParam>(R"("a")");
  ASSERT_EQ(pos.size(), 2u);
  EXPECT_EQ(pos[0], 0u);
  EXPECT_EQ(pos[1], 2u);
}

// Scanner emits every '"' as a candidate — escape tracking is the parser's job.
TYPED_TEST(ScannerTest, EscapedQuoteIsEmitted) {
  // "ab\"cd" → '"' at 0, 4, 7 all emitted
  auto pos = collect<TypeParam>(R"("ab\"cd")");
  ASSERT_EQ(pos.size(), 3u);
  EXPECT_EQ(pos[0], 0u);
  EXPECT_EQ(pos[1], 4u);
  EXPECT_EQ(pos[2], 7u);
}

TYPED_TEST(ScannerTest, DoubleBackslashThenQuote) {
  // "\\" → open at 0, close at 3
  auto pos = collect<TypeParam>(R"("\\")");
  ASSERT_EQ(pos.size(), 2u);
  EXPECT_EQ(pos[0], 0u);
  EXPECT_EQ(pos[1], 3u);
}

TYPED_TEST(ScannerTest, TripleBackslashThenQuote) {
  // "\\\"x" → '"' at 0, 4, 6
  auto pos = collect<TypeParam>(R"("\\\"x")");
  ASSERT_EQ(pos.size(), 3u);
  EXPECT_EQ(pos[0], 0u);
  EXPECT_EQ(pos[1], 4u);
  EXPECT_EQ(pos[2], 6u);
}

// ── Full JSON examples ────────────────────────────────────────────────────────

TYPED_TEST(ScannerTest, FlatObject) {
  // {"key":123,"b":true} → { " " : , " " : }
  auto pos = collect<TypeParam>(R"({"key":123,"b":true})");
  ASSERT_EQ(pos.size(), 9u);
  std::string_view sv = R"({"key":123,"b":true})";
  EXPECT_EQ(sv[pos[0]], '{');
  EXPECT_EQ(sv[pos[3]], ':');
  EXPECT_EQ(sv[pos[4]], ',');
  EXPECT_EQ(sv[pos[8]], '}');
}

TYPED_TEST(ScannerTest, NestedObject) {
  // {"a":{"b":1}} → { " " : { " " : } }
  auto pos = collect<TypeParam>(R"({"a":{"b":1}})");
  ASSERT_EQ(pos.size(), 10u);
  std::string_view sv = R"({"a":{"b":1}})";
  EXPECT_EQ(sv[pos[4]], '{');
  EXPECT_EQ(sv[pos[8]], '}');
  EXPECT_EQ(sv[pos[9]], '}');
}

TYPED_TEST(ScannerTest, PrettyJSON) {
  const std::string_view input = R"({
    "x": 1,
    "y": 2
})";
  auto pos = collect<TypeParam>(input);
  ASSERT_EQ(pos.size(), 9u);
  EXPECT_EQ(input[pos[0]], '{');
  EXPECT_EQ(input[pos[8]], '}');
}

// ── Nested structure candidates ───────────────────────────────────────────────

TYPED_TEST(ScannerTest, DeepNestingObject) {
  // {"a":{"b":{"c":1}}} → { " " : { " " : { " " : } } }
  auto pos = collect<TypeParam>(R"({"a":{"b":{"c":1}}})");
  ASSERT_EQ(pos.size(), 15u);
  std::string_view sv = R"({"a":{"b":{"c":1}}})";
  EXPECT_EQ(sv[pos[0]], '{');
  EXPECT_EQ(sv[pos[4]], '{');
  EXPECT_EQ(sv[pos[8]], '{');
  EXPECT_EQ(sv[pos[12]], '}');
  EXPECT_EQ(sv[pos[13]], '}');
  EXPECT_EQ(sv[pos[14]], '}');
}

TYPED_TEST(ScannerTest, NestedArrays) {
  // [[1,2],[3,4]] → [ [ , ] , [ , ] ]
  auto pos = collect<TypeParam>("[[1,2],[3,4]]");
  ASSERT_EQ(pos.size(), 9u);
  std::string_view sv = "[[1,2],[3,4]]";
  EXPECT_EQ(sv[pos[0]], '[');
  EXPECT_EQ(sv[pos[1]], '[');
  EXPECT_EQ(sv[pos[3]], ']');
  EXPECT_EQ(sv[pos[8]], ']');
}

TYPED_TEST(ScannerTest, ArrayOfObjects) {
  // [{"x":1},{"x":2}]
  auto pos = collect<TypeParam>(R"([{"x":1},{"x":2}])");
  ASSERT_EQ(pos.size(), 13u);
  std::string_view sv = R"([{"x":1},{"x":2}])";
  EXPECT_EQ(sv[pos[0]], '[');
  EXPECT_EQ(sv[pos[1]], '{');
  EXPECT_EQ(sv[pos[5]], '}');
  EXPECT_EQ(sv[pos[6]], ',');
  EXPECT_EQ(sv[pos[7]], '{');
  EXPECT_EQ(sv[pos[11]], '}');
  EXPECT_EQ(sv[pos[12]], ']');
}

// ── json_skip_string / json_skip_block (parse_context.hh) ────────────────────
// These are parser-level functions that operate on the ParserScanner type.
// They count backslashes to correctly identify escaped quotes, ensuring the
// scanner's raw candidates (which include all '"') are interpreted properly.

TEST(ParseContextTest, SkipStringReturnsUnescapedClose) {
  // "say \"hi\"" — escaped quotes at pos 6 and 10; real close at pos 11.
  // Without escape tracking, json_skip_string would stop at pos 6 (wrong).
  ser20::ParserScanner s(R"("say \"hi\"")");
  (void)s.next();  // consume opening '"' at pos 0
  const size_t close = ser20::json_skip_string(s);
  EXPECT_EQ(close, 11u);
}

TEST(ParseContextTest, SkipStringHtmlStyleMultipleEscapedQuotes) {
  // "<a href=\"url\" rel=\"nofollow\">text</a>" — four escaped quotes inside.
  // json_skip_string must skip all of them and return the real closing '"'.
  const std::string_view sv = R"("<a href=\"url\" rel=\"nofollow\">text</a>")";
  ser20::ParserScanner s(sv);
  (void)s.next();  // consume opening '"' at pos 0
  const size_t close = ser20::json_skip_string(s);
  EXPECT_EQ(close, sv.size() - 1);
}

TEST(ParseContextTest, SkipBlockNestedBrace) {
  // After consuming outer '{' at pos 0, skip to matching '}' at pos 12.
  // json_skip_block returns position just after the closing brace.
  ser20::ParserScanner s(R"({"a":{"b":1}})");
  (void)s.next();  // consume '{' at pos 0
  const size_t end = ser20::json_skip_block(s, '}');
  EXPECT_EQ(end, 13u);
}

TEST(ParseContextTest, SkipBlockNestedBracket) {
  // [1,[2,3],4] — after consuming '[', skip to matching ']' at pos 10.
  ser20::ParserScanner s("[1,[2,3],4]");
  (void)s.next();  // consume '[' at pos 0
  const size_t end = ser20::json_skip_block(s, ']');
  EXPECT_EQ(end, 11u);
}

TEST(ParseContextTest, SkipBlockSkipsStringWithBraces) {
  // {"k":"{v}"} — the '{' and '}' inside the string must not affect depth.
  ser20::ParserScanner s(R"({"k":"{v}"})");
  (void)s.next();  // consume '{' at pos 0
  const size_t end = ser20::json_skip_block(s, '}');
  EXPECT_EQ(end, 11u);
}

TEST(ParseContextTest, SkipBlockSkipsStringWithEscapedQuote) {
  // {"k":"a\"b"} — escaped quote inside string must not close the string early.
  ser20::ParserScanner s(R"({"k":"a\"b"})");
  (void)s.next();  // consume '{' at pos 0
  const size_t end = ser20::json_skip_block(s, '}');
  EXPECT_EQ(end, 12u);
}

TEST(ParseContextTest, SkipBlockSkipsStringWithEscapedQuoteAndStructural) {
  // {"k":"val {\"nested\":\"x\"}"} — string contains { } AND escaped quotes.
  // json_skip_block must call json_skip_string (which handles escape), otherwise
  // it would count the '}' inside the string as the closing brace.
  const std::string_view sv = R"({"k":"val {\"nested\":\"x\"}"})";
  ser20::ParserScanner s(sv);
  (void)s.next();  // consume '{' at pos 0
  const size_t end = ser20::json_skip_block(s, '}');
  EXPECT_EQ(end, sv.size());
}

// ── Block-boundary tests ──────────────────────────────────────────────────────

TYPED_TEST(ScannerTest, BlockBoundaryUnescapedQuote) {
  // '\' at pos 63 is not tracked by the scanner; '"' at pos 64 IS emitted.
  std::string input = "[\"";
  input += std::string(61, 'a');  // pos 2-62
  input += '\\';                  // pos 63
  input += '"';                   // pos 64: emitted
  input += '"';                   // pos 65: emitted
  input += ']';

  auto pos = collect<TypeParam>(input);
  // [ " " " ]
  ASSERT_EQ(pos.size(), 5u);
  EXPECT_EQ(input[pos[0]], '[');
  EXPECT_EQ(input[pos[1]], '"');
  EXPECT_EQ(pos[1], 1u);
  EXPECT_EQ(input[pos[2]], '"');
  EXPECT_EQ(pos[2], 64u);
  EXPECT_EQ(input[pos[3]], '"');
  EXPECT_EQ(pos[3], 65u);
  EXPECT_EQ(input[pos[4]], ']');
}

TYPED_TEST(ScannerTest, BlockBoundaryDoubleBackslash) {
  // '\\' at pos 62-63, '"' at pos 64 is emitted.
  std::string input = "[\"";
  input += std::string(60, 'a');
  input += '\\';  // pos 62
  input += '\\';  // pos 63
  input += '"';   // pos 64: emitted
  input += ']';

  auto pos = collect<TypeParam>(input);
  ASSERT_EQ(pos.size(), 4u);
  EXPECT_EQ(input[pos[2]], '"');
  EXPECT_EQ(pos[2], 64u);
}

TYPED_TEST(ScannerTest, BlockBoundaryTripleBackslash) {
  // '\\' + '\' at pos 61-63, '"' at 64 and '"' at 65 both emitted.
  std::string input = "[\"";
  input += std::string(59, 'a');
  input += '\\';  // pos 61
  input += '\\';  // pos 62
  input += '\\';  // pos 63
  input += '"';   // pos 64: emitted
  input += '"';   // pos 65: emitted
  input += ']';

  auto pos = collect<TypeParam>(input);
  ASSERT_EQ(pos.size(), 5u);
  EXPECT_EQ(input[pos[2]], '"');
  EXPECT_EQ(pos[2], 64u);
  EXPECT_EQ(input[pos[3]], '"');
  EXPECT_EQ(pos[3], 65u);
}

// ── eof() and idempotent end ──────────────────────────────────────────────────

TYPED_TEST(ScannerTest, EofAfterDrain) {
  TypeParam s("{}");
  while (s.next() < 2u) {}
  EXPECT_TRUE(s.eof());
}

TYPED_TEST(ScannerTest, MultipleEndCallsAreIdempotent) {
  TypeParam s("");
  for (int i = 0; i < 5; ++i) {
    EXPECT_GE(s.next(), 0u);  // always returns input.size() == 0
  }
}


}  // namespace
