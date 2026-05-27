#include <gtest/gtest.h>

#include <cstddef>
#include <string_view>
#include <variant>
#include <vector>

#include "reader_test_types.hh"

using namespace std::string_view_literals;

namespace {

using TT = hosq::TokenType;
using JTC = hosq::JsonTokenContext;
using EC = hosq::ErrorCode;

struct TokInfo {
  hosq::TokenType type;
  hosq::JsonTokenContext context{JTC::none};

  auto operator==(const TokInfo&) const -> bool = default;
};

// ── Shorthand token constants ─────────────────────────────────────────────────

constexpr TokInfo OB = {.type = TT::ObjectBegin, .context = JTC::none};
constexpr TokInfo OE = {.type = TT::ObjectEnd, .context = JTC::none};
constexpr TokInfo AB = {.type = TT::ArrayBegin, .context = JTC::none};
constexpr TokInfo AE = {.type = TT::ArrayEnd, .context = JTC::none};
constexpr TokInfo SBs = {.type = TT::Scalar, .context = JTC::string};
constexpr TokInfo SBl = {.type = TT::Scalar, .context = JTC::literal};
constexpr TokInfo END = {.type = TT::End, .context = JTC::none};

// ── read_all helper ───────────────────────────────────────────────────────────

using TokenSeq = std::vector<TokInfo>;
using ReadResult = std::variant<TokenSeq, hosq::Error>;

template <class Reader>
static auto read_all(std::string_view input) -> ReadResult {
  Reader r;
  typename Reader::state_type s;

  r.reset(s, input);
  TokenSeq seq;
  for (;;) {
    if (!r.next_token(s)) return r.error(s);

    const auto& tok = r.current_token(s);
    const auto ctx = static_cast<JTC>(tok.context);
    seq.push_back({tok.type, ctx});

    switch (tok.type) {
      case TT::End:
        return seq;
      case TT::ObjectBegin:
      case TT::ArrayBegin:
      case TT::Scalar:
      case TT::ObjectEnd:
      case TT::ArrayEnd:
        break;
      case TT::Error:
        return r.error(s);
    }
  }
}

// ── Assertion helpers ─────────────────────────────────────────────────────────

template <class Reader>
static void expect_seq(std::string_view input, TokenSeq expected) {
  expected.push_back(END);
  auto result = read_all<Reader>(input);
  ASSERT_TRUE(std::holds_alternative<TokenSeq>(result))
      << "Expected success, got error code "
      << static_cast<int>(std::get<hosq::Error>(result).code) << " at offset "
      << std::get<hosq::Error>(result).offset;
  EXPECT_EQ(std::get<TokenSeq>(result), expected);
}

template <class Reader>
static void expect_error(std::string_view input, EC code) {
  auto result = read_all<Reader>(input);
  ASSERT_TRUE(std::holds_alternative<hosq::Error>(result))
      << "Expected error but parsing succeeded";
  EXPECT_EQ(std::get<hosq::Error>(result).code, code);
}

template <class Reader>
static void expect_error_at(std::string_view input, EC code,
                            std::size_t offset) {
  auto result = read_all<Reader>(input);
  ASSERT_TRUE(std::holds_alternative<hosq::Error>(result))
      << "Expected error but parsing succeeded";
  const auto& err = std::get<hosq::Error>(result);
  EXPECT_EQ(err.code, code);
  EXPECT_EQ(err.offset, offset);
}

template <class Reader>
static void expect_error_detail(std::string_view input, EC code,
                                std::string_view detail) {
  auto result = read_all<Reader>(input);
  ASSERT_TRUE(std::holds_alternative<hosq::Error>(result))
      << "Expected error but parsing succeeded";
  const auto& err = std::get<hosq::Error>(result);
  EXPECT_EQ(err.code, code);
  EXPECT_EQ(err.detail, detail);
}

}  // namespace

template <class Reader>
class ReaderJsonTest : public hosq::test::RuntimeReaderTest<Reader> {};

TYPED_TEST_SUITE(ReaderJsonTest, hosq::test::RuntimeReaderTypes);

// ── Top-level scalars ─────────────────────────────────────────────────────────

TYPED_TEST(ReaderJsonTest, TopLevelString) {
  expect_seq<TypeParam>(R"("hello")", {SBs});
}

TYPED_TEST(ReaderJsonTest, TopLevelEmptyString) {
  expect_seq<TypeParam>(R"("")", {SBs});
}

TYPED_TEST(ReaderJsonTest, TopLevelNumber) {
  expect_seq<TypeParam>("42", {SBl});
}

TYPED_TEST(ReaderJsonTest, TopLevelNegativeNumber) {
  expect_seq<TypeParam>("-7", {SBl});
}

TYPED_TEST(ReaderJsonTest, TopLevelFloat) {
  expect_seq<TypeParam>("3.14", {SBl});
}

TYPED_TEST(ReaderJsonTest, TopLevelTrue) {
  expect_seq<TypeParam>("true", {SBl});
}

TYPED_TEST(ReaderJsonTest, TopLevelFalse) {
  expect_seq<TypeParam>("false", {SBl});
}

TYPED_TEST(ReaderJsonTest, TopLevelNull) {
  expect_seq<TypeParam>("null", {SBl});
}

// ── Empty containers ──────────────────────────────────────────────────────────

TYPED_TEST(ReaderJsonTest, EmptyArray) { expect_seq<TypeParam>("[]", {AB, AE}); }

TYPED_TEST(ReaderJsonTest, EmptyArraySpaced) {
  expect_seq<TypeParam>("[   ]", {AB, AE});
}

TYPED_TEST(ReaderJsonTest, EmptyArrayTabNewline) {
  expect_seq<TypeParam>("[\t\n]", {AB, AE});
}

TYPED_TEST(ReaderJsonTest, EmptyObject) {
  expect_seq<TypeParam>("{}", {OB, OE});
}

TYPED_TEST(ReaderJsonTest, EmptyObjectSpaced) {
  expect_seq<TypeParam>("{  }", {OB, OE});
}

// ── Simple arrays ─────────────────────────────────────────────────────────────

TYPED_TEST(ReaderJsonTest, ArraySingleString) {
  expect_seq<TypeParam>(R"(["hello"])", {AB, SBs, AE});
}

TYPED_TEST(ReaderJsonTest, ArrayOfStrings) {
  expect_seq<TypeParam>(R"(["a","b","c"])", {AB, SBs, SBs, SBs, AE});
}

TYPED_TEST(ReaderJsonTest, ArrayOfLiterals) {
  expect_seq<TypeParam>("[1,2,3]", {AB, SBl, SBl, SBl, AE});
}

TYPED_TEST(ReaderJsonTest, ArrayMixedTypes) {
  expect_seq<TypeParam>(R"(["x",1,true,null])", {AB, SBs, SBl, SBl, SBl, AE});
}

// ── Simple objects ────────────────────────────────────────────────────────────

TYPED_TEST(ReaderJsonTest, SimpleObject) {
  expect_seq<TypeParam>(R"({"k":"v"})", {OB, SBs, SBs, OE});
}

TYPED_TEST(ReaderJsonTest, ObjectLiteralValue) {
  expect_seq<TypeParam>(R"({"n":42})", {OB, SBs, SBl, OE});
}

TYPED_TEST(ReaderJsonTest, ObjectBoolValue) {
  expect_seq<TypeParam>(R"({"ok":true})", {OB, SBs, SBl, OE});
}

TYPED_TEST(ReaderJsonTest, ObjectNullValue) {
  expect_seq<TypeParam>(R"({"x":null})", {OB, SBs, SBl, OE});
}

TYPED_TEST(ReaderJsonTest, ObjectMultipleKeys) {
  // 3 key-value pairs → 6 SBs
  expect_seq<TypeParam>(R"({"a":"1","b":"2","c":"3"})",
                        {OB, SBs, SBs, SBs, SBs, SBs, SBs, OE});
}

// ── Empty containers as values ────────────────────────────────────────────────

TYPED_TEST(ReaderJsonTest, ObjectEmptyArrayValue) {
  expect_seq<TypeParam>(R"({"foobar":[]})", {OB, SBs, AB, AE, OE});
}

TYPED_TEST(ReaderJsonTest, ObjectSpacedEmptyArrayValue) {
  expect_seq<TypeParam>(R"({"foobar":[  ]})", {OB, SBs, AB, AE, OE});
}

TYPED_TEST(ReaderJsonTest, ObjectEmptyObjectValue) {
  expect_seq<TypeParam>(R"({"foo":{}})", {OB, SBs, OB, OE, OE});
}

TYPED_TEST(ReaderJsonTest, ArrayEmptyObject) {
  expect_seq<TypeParam>("[{}]", {AB, OB, OE, AE});
}

TYPED_TEST(ReaderJsonTest, ArrayEmptyArray) {
  expect_seq<TypeParam>("[[]]", {AB, AB, AE, AE});
}

TYPED_TEST(ReaderJsonTest, ArraySpacedEmptyArrays) {
  expect_seq<TypeParam>("[  [  ]  ]", {AB, AB, AE, AE});
}

TYPED_TEST(ReaderJsonTest, ObjectEmptyArrayAndEmptyObjectValues) {
  // {"a":[],"b":{}}
  expect_seq<TypeParam>(R"({"a":[],"b":{}})",
                        {OB, SBs, AB, AE, SBs, OB, OE, OE});
}

// ── Nested structures (≤ 5 levels) ────────────────────────────────────────────

TYPED_TEST(ReaderJsonTest, NestedObject5Levels) {
  // {"a":{"b":{"c":{"d":{"e":"val"}}}}}
  expect_seq<TypeParam>(
      R"({"a":{"b":{"c":{"d":{"e":"val"}}}}})",
      {OB, SBs, OB, SBs, OB, SBs, OB, SBs, OB, SBs, SBs, OE, OE, OE, OE, OE});
}

TYPED_TEST(ReaderJsonTest, NestedArray5Levels) {
  expect_seq<TypeParam>("[[[[[\"v\"]]]]]",
                        {AB, AB, AB, AB, AB, SBs, AE, AE, AE, AE, AE});
}

TYPED_TEST(ReaderJsonTest, MixedNesting) {
  // {"a":[{"b":[[],{}]}]}
  expect_seq<TypeParam>(
      R"({"a":[{"b":[[],{}]}]})",
      {OB, SBs, AB, OB, SBs, AB, AB, AE, OB, OE, AE, OE, AE, OE});
}

// ── Large / complex JSON ──────────────────────────────────────────────────────

TYPED_TEST(ReaderJsonTest, LargeComplexJson) {
  // {"name":"Alice","scores":[1,2,3],"meta":{"active":true,"tags":[],"info":{"x":42,"y":null}}}
  expect_seq<TypeParam>(
      R"({"name":"Alice","scores":[1,2,3],"meta":{"active":true,"tags":[],"info":{"x":42,"y":null}}})",
      {
          OB,  SBs, SBs,                // "name":"Alice"
          SBs, AB,  SBl, SBl, SBl, AE,  // "scores":[1,2,3]
          SBs, OB,                      // "meta":{
          SBs, SBl,                     //   "active":true
          SBs, AB,  AE,                 //   "tags":[]
          SBs, OB,                      //   "info":{
          SBs, SBl,                     //     "x":42
          SBs, SBl,                     //     "y":null
          OE,                           //   }
          OE,                           // }
          OE,
      });
}

// ── Whitespace handling ───────────────────────────────────────────────────────

TYPED_TEST(ReaderJsonTest, LeadingWhitespace) {
  expect_seq<TypeParam>("   42", {SBl});
}

TYPED_TEST(ReaderJsonTest, TrailingWhitespace) {
  expect_seq<TypeParam>("42   ", {SBl});
}

TYPED_TEST(ReaderJsonTest, WhitespaceAroundObject) {
  expect_seq<TypeParam>("  { }  ", {OB, OE});
}

// ── Error: empty / whitespace-only input ─────────────────────────────────────

TYPED_TEST(ReaderJsonTest, EmptyInput) {
  expect_error_at<TypeParam>("", EC::unexpected_eof, 0);
  expect_error_detail<TypeParam>("", EC::unexpected_eof, "expected JSON value");
}

TYPED_TEST(ReaderJsonTest, WhitespaceOnly) {
  expect_error_at<TypeParam>("   ", EC::unexpected_eof, 3);
}

// ── Error: double-delimiter patterns (no spaces) ──────────────────────────────

TYPED_TEST(ReaderJsonTest, DoubleComma) {
  // `,` at top level (ctx_size_=0) → unexpected_token at 0
  expect_error_at<TypeParam>(",,", EC::unexpected_token, 0);
}

TYPED_TEST(ReaderJsonTest, DoubleColon) {
  // `:` at top level → unexpected_token at 0
  expect_error_at<TypeParam>("::", EC::unexpected_token, 0);
}

TYPED_TEST(ReaderJsonTest, DoubleOpenBrace) {
  // Second `{` seen while expect_=key_or_end → unexpected_token at 1
  expect_error_at<TypeParam>("{{", EC::unexpected_token, 1);
}

// ── Error: double-delimiter patterns (with spaces) ────────────────────────────

TYPED_TEST(ReaderJsonTest, DoubleCommaSpaced) {
  expect_error_at<TypeParam>(", ,", EC::unexpected_token, 0);
}

TYPED_TEST(ReaderJsonTest, DoubleColonSpaced) {
  expect_error_at<TypeParam>(": :", EC::unexpected_token, 0);
}

TYPED_TEST(ReaderJsonTest, DoubleOpenBraceSpaced) {
  // Second `{` at offset 3 while expect_=key_or_end
  expect_error_at<TypeParam>("{  {", EC::unexpected_token, 3);
}

// ── Error: trailing data ──────────────────────────────────────────────────────

TYPED_TEST(ReaderJsonTest, TrailingDataAfterObject) {
  // Top-level trailing bytes are reported after the root value completes.
  expect_error_at<TypeParam>("{}a", EC::trailing_data, 2);
  expect_error_detail<TypeParam>("{}a", EC::trailing_data,
                                 "non-whitespace data after root value");
}

TYPED_TEST(ReaderJsonTest, TrailingDataAfterObjectSpaced) {
  // `a` at offset 5 in "{   }a"
  expect_error_at<TypeParam>("{   }a", EC::trailing_data, 5);
}

TYPED_TEST(ReaderJsonTest, TrailingCommaAtTopLevel) {
  // Any bytes after the completed root value are trailing data.
  expect_error_at<TypeParam>("{},", EC::trailing_data, 2);
}

// ── Error: bracket mismatches ─────────────────────────────────────────────────

TYPED_TEST(ReaderJsonTest, CloseBraceInArray) {
  // {"a":[} — `}` inside array context
  expect_error_at<TypeParam>(R"({"a":[})", EC::unexpected_token, 6);
}

TYPED_TEST(ReaderJsonTest, CloseBracketInObject) {
  // {"a":{] — `]` inside object context
  expect_error_at<TypeParam>(R"({"a":{])", EC::unexpected_token, 6);
}

TYPED_TEST(ReaderJsonTest, StandaloneCloseBrace) {
  expect_error_at<TypeParam>("}", EC::unexpected_token, 0);
}

TYPED_TEST(ReaderJsonTest, StandaloneCloseBracket) {
  expect_error_at<TypeParam>("]", EC::unexpected_token, 0);
}

TYPED_TEST(ReaderJsonTest, ObjectEndInArray) {
  expect_error_at<TypeParam>("[}", EC::unexpected_token, 1);
}

TYPED_TEST(ReaderJsonTest, ArrayEndInObject) {
  expect_error_at<TypeParam>("{]", EC::unexpected_token, 1);
}

// ── Error: wrong separator characters ────────────────────────────────────────

TYPED_TEST(ReaderJsonTest, ColonAsArraySeparator) {
  // ["a":"b"] — `:` after array value with expect_=comma_delimiter
  // After "a" at offset 1, advance(3) → pos_=4; `:` at 4 → unexpected_token
  expect_error_at<TypeParam>(R"(["a":"b"])", EC::unexpected_token, 4);
}

TYPED_TEST(ReaderJsonTest, CommaAsObjectSeparator) {
  // {"k","v"} — `,` where `:` expected (expect_=colon_delimiter)
  // After "k" key at offset 1, advance(3) → pos_=4; `,` at 4 → unexpected_token
  expect_error_at<TypeParam>(R"({"k","v"})", EC::unexpected_token, 4);
}

TYPED_TEST(ReaderJsonTest, MissingColonInObject) {
  // {"k" "v"} — `"` seen with expect_=colon_delimiter → unexpected_token at 5
  expect_error_at<TypeParam>(R"({"k" "v"})", EC::unexpected_token, 5);
}

// ── Error: scalar with embedded whitespace ────────────────────────────────────

TYPED_TEST(ReaderJsonTest, ScalarWithSpaceInObject) {
  // {"value":638 472} — after advancing past "638" (3 bytes from offset 9),
  // whitespace is skipped, `4` at offset 13 seen with expect_=comma_delimiter
  expect_error_at<TypeParam>(R"({"value":638 472})", EC::unexpected_token, 13);
}

// ── Error: double colon inside object ────────────────────────────────────────

TYPED_TEST(ReaderJsonTest, DoubleColonInObject) {
  // {"k"::"v"} — second `:` at offset 5 with expect_=value → unexpected_token
  expect_error_at<TypeParam>(R"({"k"::"v"})", EC::unexpected_token, 5);
}

// ── Error: key without value ──────────────────────────────────────────────────

TYPED_TEST(ReaderJsonTest, ObjectKeyWithoutValue) {
  // {"v"} — after key "v", expect_=colon_delimiter; `}` at 4 is not colon →
  // unexpected_token
  expect_error_at<TypeParam>(R"({"v"})", EC::unexpected_token, 4);
}

// ── Error: comma before first array element ───────────────────────────────────

TYPED_TEST(ReaderJsonTest, LeadingCommaInArray) {
  // [,] — `,` at offset 1 with expect_=value_or_end → unexpected_token
  expect_error_at<TypeParam>("[,]", EC::unexpected_token, 1);
}

TYPED_TEST(ReaderJsonTest, ColonInArrayBeforeValue) {
  // [:] — `:` at offset 1 with ctx=array (not object) → unexpected_token
  expect_error_at<TypeParam>("[:]", EC::unexpected_token, 1);
}

// ── Error: unclosed containers ────────────────────────────────────────────────

TYPED_TEST(ReaderJsonTest, UnclosedObject) {
  expect_error<TypeParam>(R"({"k":"v")", EC::unexpected_eof);
}

TYPED_TEST(ReaderJsonTest, UnclosedArray) {
  expect_error<TypeParam>("[1,2", EC::unexpected_eof);
}

TYPED_TEST(ReaderJsonTest, UnclosedNestedObject) {
  expect_error<TypeParam>(R"({"a":{"b":1})", EC::unexpected_eof);
}

TEST(FastReaderError, DepthLimitExceededObject) {
  auto result = read_all<hosq::json::FastReader<2>>(R"({"a":{"b":{"c":1}}})");
  ASSERT_TRUE(std::holds_alternative<hosq::Error>(result));
  const auto& err = std::get<hosq::Error>(result);
  EXPECT_EQ(err.code, EC::depth_limit_exceeded);
  EXPECT_EQ(err.kind(), hosq::ErrorKind::limit);
  EXPECT_EQ(err.detail, "fast reader container depth limit"sv);
}

TEST(FastReaderError, DepthLimitExceededArray) {
  auto result = read_all<hosq::json::FastReader<2>>("[[[1]]]");
  ASSERT_TRUE(std::holds_alternative<hosq::Error>(result));
  const auto& err = std::get<hosq::Error>(result);
  EXPECT_EQ(err.code, EC::depth_limit_exceeded);
  EXPECT_EQ(err.detail, "fast reader container depth limit"sv);
}
