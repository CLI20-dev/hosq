#include <gtest/gtest.h>

#include <string>
#include <string_view>
#include <vector>

#include "ser20/scanner.hh"
#ifdef __ARM_NEON
#include "ser20/scanner_neon.hh"
#include "ser20/scanner_neon2.hh"
#endif

namespace {

using ser20::ScannerToken;
using ser20::TokenKind;

// ── Typed test infrastructure ─────────────────────────────────────────────────

template <typename S>
class ScannerTest : public ::testing::Test {};

#ifdef __ARM_NEON
using ScannerTypes = ::testing::Types<ser20::Scanner, ser20::ScannerNeon>;
#else
using ScannerTypes = ::testing::Types<ser20::Scanner>;
#endif
TYPED_TEST_SUITE(ScannerTest, ScannerTypes);

template <typename S>
static auto tokenize(std::string_view input) -> std::vector<ScannerToken> {
  S scanner(input);
  std::vector<ScannerToken> out;
  while (true) {
    auto tok = scanner.consume_next_token();
    if (tok.is_end()) break;
    out.push_back(tok);
  }
  return out;
}

// ── Basic structural tokens ───────────────────────────────────────────────────

TYPED_TEST(ScannerTest, EmptyInput) {
  TypeParam s("");
  EXPECT_TRUE(s.consume_next_token().is_end());
  EXPECT_TRUE(s.eof());
}

TYPED_TEST(ScannerTest, WhitespaceOnly) {
  TypeParam s("   \t\n\r");
  EXPECT_TRUE(s.consume_next_token().is_end());
}

TYPED_TEST(ScannerTest, EmptyObject) {
  auto toks = tokenize<TypeParam>("{}");
  ASSERT_EQ(toks.size(), 2u);
  EXPECT_EQ(toks[0].kind, TokenKind::LBrace);
  EXPECT_EQ(toks[0].pos, 0u);
  EXPECT_EQ(toks[1].kind, TokenKind::RBrace);
  EXPECT_EQ(toks[1].pos, 1u);
}

TYPED_TEST(ScannerTest, EmptyArray) {
  auto toks = tokenize<TypeParam>("[]");
  ASSERT_EQ(toks.size(), 2u);
  EXPECT_EQ(toks[0].kind, TokenKind::LBracket);
  EXPECT_EQ(toks[0].pos, 0u);
  EXPECT_EQ(toks[1].kind, TokenKind::RBracket);
  EXPECT_EQ(toks[1].pos, 1u);
}

TYPED_TEST(ScannerTest, ColonAndComma) {
  auto toks = tokenize<TypeParam>("{:,}");
  ASSERT_EQ(toks.size(), 4u);
  EXPECT_EQ(toks[1].kind, TokenKind::Colon);
  EXPECT_EQ(toks[2].kind, TokenKind::Comma);
}

TYPED_TEST(ScannerTest, WhitespaceSkipped) {
  auto toks = tokenize<TypeParam>("  {  }  ");
  ASSERT_EQ(toks.size(), 2u);
  EXPECT_EQ(toks[0].pos, 2u);
  EXPECT_EQ(toks[1].pos, 5u);
}

// ── Quote tokens ──────────────────────────────────────────────────────────────

TYPED_TEST(ScannerTest, SimpleString) {
  // {"a":"b"} → LBrace Quote Quote Colon Quote Quote RBrace
  auto toks = tokenize<TypeParam>(R"({"a":"b"})");
  ASSERT_EQ(toks.size(), 7u);
  EXPECT_EQ(toks[0].kind, TokenKind::LBrace);
  EXPECT_EQ(toks[1].kind, TokenKind::Quote);
  EXPECT_EQ(toks[2].kind, TokenKind::Quote);
  EXPECT_EQ(toks[3].kind, TokenKind::Colon);
  EXPECT_EQ(toks[4].kind, TokenKind::Quote);
  EXPECT_EQ(toks[5].kind, TokenKind::Quote);
  EXPECT_EQ(toks[6].kind, TokenKind::RBrace);
}

TYPED_TEST(ScannerTest, QuotePositions) {
  auto toks = tokenize<TypeParam>(R"("a")");
  ASSERT_EQ(toks.size(), 2u);
  EXPECT_EQ(toks[0].pos, 0u);
  EXPECT_EQ(toks[1].pos, 2u);
}

// Escape sequences are not tracked: every '"' is emitted as a Quote token.
TYPED_TEST(ScannerTest, EscapedQuoteIsEmitted) {
  // "ab\"cd" → Quote(0) Quote(4) Quote(7)
  auto toks = tokenize<TypeParam>(R"("ab\"cd")");
  ASSERT_EQ(toks.size(), 3u);
  EXPECT_EQ(toks[0].pos, 0u);
  EXPECT_EQ(toks[1].pos, 4u);
  EXPECT_EQ(toks[2].pos, 7u);
}

TYPED_TEST(ScannerTest, DoubleBackslashThenQuote) {
  // "\\" → open at 0, close at 3 (backslashes are not structural)
  auto toks = tokenize<TypeParam>(R"("\\")");
  ASSERT_EQ(toks.size(), 2u);
  EXPECT_EQ(toks[0].pos, 0u);
  EXPECT_EQ(toks[1].pos, 3u);
}

TYPED_TEST(ScannerTest, TripleBackslashThenQuote) {
  // "\\\"x" → Quote(0) Quote(4) Quote(6)
  auto toks = tokenize<TypeParam>(R"("\\\"x")");
  ASSERT_EQ(toks.size(), 3u);
  EXPECT_EQ(toks[0].pos, 0u);
  EXPECT_EQ(toks[1].pos, 4u);
  EXPECT_EQ(toks[2].pos, 6u);
}

// ── Full JSON examples ────────────────────────────────────────────────────────

TYPED_TEST(ScannerTest, FlatObject) {
  // {"key":123,"b":true} → LBrace Q Q Colon Comma Q Q Colon RBrace
  auto toks = tokenize<TypeParam>(R"({"key":123,"b":true})");
  ASSERT_EQ(toks.size(), 9u);
  EXPECT_EQ(toks[0].kind, TokenKind::LBrace);
  EXPECT_EQ(toks[3].kind, TokenKind::Colon);
  EXPECT_EQ(toks[4].kind, TokenKind::Comma);
  EXPECT_EQ(toks[8].kind, TokenKind::RBrace);
}

TYPED_TEST(ScannerTest, NestedObject) {
  // {"a":{"b":1}} → LBrace Q Q Colon LBrace Q Q Colon RBrace RBrace
  auto toks = tokenize<TypeParam>(R"({"a":{"b":1}})");
  ASSERT_EQ(toks.size(), 10u);
  EXPECT_EQ(toks[4].kind, TokenKind::LBrace);
  EXPECT_EQ(toks[8].kind, TokenKind::RBrace);
  EXPECT_EQ(toks[9].kind, TokenKind::RBrace);
}

TYPED_TEST(ScannerTest, PrettyJSON) {
  const std::string_view input = R"({
    "x": 1,
    "y": 2
})";
  // LBrace Q Q Colon Comma Q Q Colon RBrace
  auto toks = tokenize<TypeParam>(input);
  ASSERT_EQ(toks.size(), 9u);
  EXPECT_EQ(toks[0].kind, TokenKind::LBrace);
  EXPECT_EQ(toks[8].kind, TokenKind::RBrace);
}

// ── Nested structure tokens ───────────────────────────────────────────────────

TYPED_TEST(ScannerTest, DeepNestingObject) {
  // {"a":{"b":{"c":1}}}
  // LBrace Q Q Colon LBrace Q Q Colon LBrace Q Q Colon RBrace RBrace RBrace
  auto toks = tokenize<TypeParam>(R"({"a":{"b":{"c":1}}})");
  ASSERT_EQ(toks.size(), 15u);
  EXPECT_EQ(toks[0].kind, TokenKind::LBrace);
  EXPECT_EQ(toks[4].kind, TokenKind::LBrace);
  EXPECT_EQ(toks[8].kind, TokenKind::LBrace);
  EXPECT_EQ(toks[12].kind, TokenKind::RBrace);
  EXPECT_EQ(toks[13].kind, TokenKind::RBrace);
  EXPECT_EQ(toks[14].kind, TokenKind::RBrace);
}

TYPED_TEST(ScannerTest, NestedArrays) {
  // [[1,2],[3,4]] → LBracket LBracket Comma RBracket Comma LBracket Comma RBracket RBracket
  auto toks = tokenize<TypeParam>("[[1,2],[3,4]]");
  ASSERT_EQ(toks.size(), 9u);
  EXPECT_EQ(toks[0].kind, TokenKind::LBracket);
  EXPECT_EQ(toks[1].kind, TokenKind::LBracket);
  EXPECT_EQ(toks[3].kind, TokenKind::RBracket);
  EXPECT_EQ(toks[8].kind, TokenKind::RBracket);
}

TYPED_TEST(ScannerTest, ArrayOfObjects) {
  // [{"x":1},{"x":2}]
  auto toks = tokenize<TypeParam>(R"([{"x":1},{"x":2}])");
  ASSERT_EQ(toks.size(), 13u);
  EXPECT_EQ(toks[0].kind, TokenKind::LBracket);
  EXPECT_EQ(toks[1].kind, TokenKind::LBrace);
  EXPECT_EQ(toks[5].kind, TokenKind::RBrace);
  EXPECT_EQ(toks[6].kind, TokenKind::Comma);
  EXPECT_EQ(toks[7].kind, TokenKind::LBrace);
  EXPECT_EQ(toks[11].kind, TokenKind::RBrace);
  EXPECT_EQ(toks[12].kind, TokenKind::RBracket);
}

// ── skip_string ───────────────────────────────────────────────────────────────

TYPED_TEST(ScannerTest, SkipStringReturnsUnescapedClose) {
  // "say \"hi\"" has escaped quotes at pos 6 and 10; real close at pos 11.
  // Without correct escape tracking skip_string would stop at pos 6 (wrong).
  TypeParam s(R"("say \"hi\"")");
  auto open = s.consume_next_token();
  ASSERT_EQ(open.kind, TokenKind::Quote);
  ASSERT_EQ(open.pos, 0u);
  auto close = s.skip_string();
  EXPECT_EQ(close.kind, TokenKind::Quote);
  EXPECT_EQ(close.pos, 11u);
}

TYPED_TEST(ScannerTest, SkipStringHtmlStyleMultipleEscapedQuotes) {
  // "<a href=\"url\" rel=\"nofollow\">text</a>" — four escaped quotes inside.
  // skip_string must skip all of them and return the real closing quote.
  TypeParam s(R"("<a href=\"url\" rel=\"nofollow\">text</a>")");
  auto open = s.consume_next_token();
  ASSERT_EQ(open.kind, TokenKind::Quote);
  const std::string_view sv = R"("<a href=\"url\" rel=\"nofollow\">text</a>")";
  auto close = s.skip_string();
  EXPECT_EQ(close.kind, TokenKind::Quote);
  EXPECT_EQ(close.pos, sv.size() - 1);
}

// ── consume_until_matching with nesting ───────────────────────────────────────

TYPED_TEST(ScannerTest, ConsumeUntilMatchingNestedBrace) {
  // After consuming outer '{', skip to matching '}' at pos 12
  TypeParam s(R"({"a":{"b":1}})");
  auto open = s.consume_next_token();
  ASSERT_EQ(open.kind, TokenKind::LBrace);
  auto close = s.consume_until_matching(TokenKind::LBrace, TokenKind::RBrace);
  EXPECT_EQ(close.kind, TokenKind::RBrace);
  EXPECT_EQ(close.pos, 12u);
}

TYPED_TEST(ScannerTest, ConsumeUntilMatchingNestedBracket) {
  // [1,[2,3],4] — after consuming '[', skip to matching ']' at pos 10
  TypeParam s("[1,[2,3],4]");
  auto open = s.consume_next_token();
  ASSERT_EQ(open.kind, TokenKind::LBracket);
  auto close = s.consume_until_matching(TokenKind::LBracket, TokenKind::RBracket);
  EXPECT_EQ(close.kind, TokenKind::RBracket);
  EXPECT_EQ(close.pos, 10u);
}

TYPED_TEST(ScannerTest, ConsumeUntilMatchingSkipsStringWithBraces) {
  // {"k":"{v}"} — the '{' and '}' inside the string must not affect depth
  TypeParam s(R"({"k":"{v}"})");
  auto open = s.consume_next_token();
  ASSERT_EQ(open.kind, TokenKind::LBrace);
  auto close = s.consume_until_matching(TokenKind::LBrace, TokenKind::RBrace);
  EXPECT_EQ(close.kind, TokenKind::RBrace);
  EXPECT_EQ(close.pos, 10u);
}

TYPED_TEST(ScannerTest, ConsumeUntilMatchingSkipsStringWithEscapedQuote) {
  // {"k":"a\"b"} — escaped quote inside string must not close the string early
  TypeParam s(R"({"k":"a\"b"})");
  auto open = s.consume_next_token();
  ASSERT_EQ(open.kind, TokenKind::LBrace);
  auto close = s.consume_until_matching(TokenKind::LBrace, TokenKind::RBrace);
  EXPECT_EQ(close.kind, TokenKind::RBrace);
  EXPECT_EQ(close.pos, 11u);
}

TYPED_TEST(ScannerTest, ConsumeUntilMatchingSkipsStringWithEscapedQuoteAndStructural) {
  // {"k":"val {\"nested\":\"x\"}"} — string contains { } AND escaped quotes.
  // consume_until_matching must use skip_string (which handles escape), otherwise
  // it would count the '}' inside the string as the closing brace.
  TypeParam s(R"({"k":"val {\"nested\":\"x\"}"})");
  auto open = s.consume_next_token();
  ASSERT_EQ(open.kind, TokenKind::LBrace);
  auto close = s.consume_until_matching(TokenKind::LBrace, TokenKind::RBrace);
  EXPECT_EQ(close.kind, TokenKind::RBrace);
  const std::string_view sv = R"({"k":"val {\"nested\":\"x\"}"})";
  EXPECT_EQ(close.pos, sv.size() - 1);
}

// ── Block-boundary tests ──────────────────────────────────────────────────────

TYPED_TEST(ScannerTest, BlockBoundaryUnescapedQuote) {
  // '\' at pos 63 is not tracked; '"' at pos 64 IS emitted as a Quote token
  std::string input = "[\"";
  input += std::string(61, 'a');  // pos 2-62
  input += '\\';                  // pos 63
  input += '"';                   // pos 64: emitted (no escape tracking)
  input += '"';                   // pos 65: emitted
  input += ']';

  auto toks = tokenize<TypeParam>(input);
  // LBracket Quote(1) Quote(64) Quote(65) RBracket
  ASSERT_EQ(toks.size(), 5u);
  EXPECT_EQ(toks[0].kind, TokenKind::LBracket);
  EXPECT_EQ(toks[1].kind, TokenKind::Quote);
  EXPECT_EQ(toks[1].pos, 1u);
  EXPECT_EQ(toks[2].kind, TokenKind::Quote);
  EXPECT_EQ(toks[2].pos, 64u);
  EXPECT_EQ(toks[3].kind, TokenKind::Quote);
  EXPECT_EQ(toks[3].pos, 65u);
  EXPECT_EQ(toks[4].kind, TokenKind::RBracket);
}

TYPED_TEST(ScannerTest, BlockBoundaryDoubleBackslash) {
  // '\\' at pos 62-63, '"' at pos 64 is emitted
  std::string input = "[\"";
  input += std::string(60, 'a');
  input += '\\';  // pos 62
  input += '\\';  // pos 63
  input += '"';   // pos 64: emitted
  input += ']';

  auto toks = tokenize<TypeParam>(input);
  ASSERT_EQ(toks.size(), 4u);
  EXPECT_EQ(toks[2].kind, TokenKind::Quote);
  EXPECT_EQ(toks[2].pos, 64u);
}

TYPED_TEST(ScannerTest, BlockBoundaryTripleBackslash) {
  // '\\' + '\' at pos 61-63, '"' at 64 and '"' at 65 both emitted
  std::string input = "[\"";
  input += std::string(59, 'a');
  input += '\\';  // pos 61
  input += '\\';  // pos 62
  input += '\\';  // pos 63
  input += '"';   // pos 64: emitted
  input += '"';   // pos 65: emitted
  input += ']';

  auto toks = tokenize<TypeParam>(input);
  ASSERT_EQ(toks.size(), 5u);
  EXPECT_EQ(toks[2].kind, TokenKind::Quote);
  EXPECT_EQ(toks[2].pos, 64u);
  EXPECT_EQ(toks[3].kind, TokenKind::Quote);
  EXPECT_EQ(toks[3].pos, 65u);
}

// ── reset() and eof() ─────────────────────────────────────────────────────────

TYPED_TEST(ScannerTest, ResetRestartsScan) {
  TypeParam s("{}");
  (void)s.consume_next_token();
  (void)s.consume_next_token();
  EXPECT_TRUE(s.consume_next_token().is_end());

  s.reset();
  auto t = s.consume_next_token();
  EXPECT_EQ(t.kind, TokenKind::LBrace);
  EXPECT_EQ(t.pos, 0u);
}

TYPED_TEST(ScannerTest, EofAfterDrain) {
  TypeParam s("{}");
  while (!s.consume_next_token().is_end()) {
  }
  EXPECT_TRUE(s.eof());
}

TYPED_TEST(ScannerTest, MultipleEndCallsAreIdempotent) {
  TypeParam s("");
  for (int i = 0; i < 5; ++i) {
    EXPECT_TRUE(s.consume_next_token().is_end());
  }
}

}  // namespace
