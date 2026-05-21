#include <gtest/gtest.h>

#include <cstddef>
#include <string_view>
#include <variant>
#include <vector>

#include "hosq/json/simple_reader.hh"

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

static auto read_all(std::string_view input) -> ReadResult {
  hosq::json::SimpleReader r;
  hosq::json::SimpleReader::state_type s;

  r.reset(s, input);
  TokenSeq seq;
  for (;;) {
    auto res = r.next_token(s);
    if (!res) return res.error();

    const auto& tok = r.current_token(s);
    auto ctx = static_cast<JTC>(tok.context);
    seq.push_back({tok.type, ctx});

    switch (tok.type) {
      case TT::ObjectBegin:
      case TT::ArrayBegin:
      case TT::Scalar:
      case TT::ObjectEnd:
      case TT::ArrayEnd:
      case TT::End:
        return seq;
    }
  }
}

// ── Assertion helpers ─────────────────────────────────────────────────────────

static void expect_seq(std::string_view input, TokenSeq expected) {
  expected.push_back(END);
  auto result = read_all(input);
  ASSERT_TRUE(std::holds_alternative<TokenSeq>(result))
      << "Expected success, got error code "
      << static_cast<int>(std::get<hosq::Error>(result).code) << " at offset "
      << std::get<hosq::Error>(result).offset;
  EXPECT_EQ(std::get<TokenSeq>(result), expected);
}

static void expect_error(std::string_view input, EC code) {
  auto result = read_all(input);
  ASSERT_TRUE(std::holds_alternative<hosq::Error>(result))
      << "Expected error but parsing succeeded";
  EXPECT_EQ(std::get<hosq::Error>(result).code, code);
}

static void expect_error_at(std::string_view input, EC code,
                            std::size_t offset) {
  auto result = read_all(input);
  ASSERT_TRUE(std::holds_alternative<hosq::Error>(result))
      << "Expected error but parsing succeeded";
  const auto& err = std::get<hosq::Error>(result);
  EXPECT_EQ(err.code, code);
  EXPECT_EQ(err.offset, offset);
}

}  // namespace

// ── Top-level scalars ─────────────────────────────────────────────────────────

TEST(ReaderValid, TopLevelString) { expect_seq(R"("hello")", {SBs}); }

TEST(ReaderValid, TopLevelEmptyString) { expect_seq(R"("")", {SBs}); }

TEST(ReaderValid, TopLevelNumber) { expect_seq("42", {SBl}); }

TEST(ReaderValid, TopLevelNegativeNumber) { expect_seq("-7", {SBl}); }

TEST(ReaderValid, TopLevelFloat) { expect_seq("3.14", {SBl}); }

TEST(ReaderValid, TopLevelTrue) { expect_seq("true", {SBl}); }

TEST(ReaderValid, TopLevelFalse) { expect_seq("false", {SBl}); }

TEST(ReaderValid, TopLevelNull) { expect_seq("null", {SBl}); }

// ── Empty containers ──────────────────────────────────────────────────────────

TEST(ReaderValid, EmptyArray) { expect_seq("[]", {AB, AE}); }

TEST(ReaderValid, EmptyArraySpaced) { expect_seq("[   ]", {AB, AE}); }

TEST(ReaderValid, EmptyArrayTabNewline) { expect_seq("[\t\n]", {AB, AE}); }

TEST(ReaderValid, EmptyObject) { expect_seq("{}", {OB, OE}); }

TEST(ReaderValid, EmptyObjectSpaced) { expect_seq("{  }", {OB, OE}); }

// ── Simple arrays ─────────────────────────────────────────────────────────────

TEST(ReaderValid, ArraySingleString) {
  expect_seq(R"(["hello"])", {AB, SBs, AE});
}

TEST(ReaderValid, ArrayOfStrings) {
  expect_seq(R"(["a","b","c"])", {AB, SBs, SBs, SBs, AE});
}

TEST(ReaderValid, ArrayOfLiterals) {
  expect_seq("[1,2,3]", {AB, SBl, SBl, SBl, AE});
}

TEST(ReaderValid, ArrayMixedTypes) {
  expect_seq(R"(["x",1,true,null])", {AB, SBs, SBl, SBl, SBl, AE});
}

// ── Simple objects ────────────────────────────────────────────────────────────

TEST(ReaderValid, SimpleObject) {
  expect_seq(R"({"k":"v"})", {OB, SBs, SBs, OE});
}

TEST(ReaderValid, ObjectLiteralValue) {
  expect_seq(R"({"n":42})", {OB, SBs, SBl, OE});
}

TEST(ReaderValid, ObjectBoolValue) {
  expect_seq(R"({"ok":true})", {OB, SBs, SBl, OE});
}

TEST(ReaderValid, ObjectNullValue) {
  expect_seq(R"({"x":null})", {OB, SBs, SBl, OE});
}

TEST(ReaderValid, ObjectMultipleKeys) {
  // 3 key-value pairs → 6 SBs
  expect_seq(R"({"a":"1","b":"2","c":"3"})",
             {OB, SBs, SBs, SBs, SBs, SBs, SBs, OE});
}

// ── Empty containers as values ────────────────────────────────────────────────

TEST(ReaderValid, ObjectEmptyArrayValue) {
  expect_seq(R"({"foobar":[]})", {OB, SBs, AB, AE, OE});
}

TEST(ReaderValid, ObjectSpacedEmptyArrayValue) {
  expect_seq(R"({"foobar":[  ]})", {OB, SBs, AB, AE, OE});
}

TEST(ReaderValid, ObjectEmptyObjectValue) {
  expect_seq(R"({"foo":{}})", {OB, SBs, OB, OE, OE});
}

TEST(ReaderValid, ArrayEmptyObject) { expect_seq("[{}]", {AB, OB, OE, AE}); }

TEST(ReaderValid, ArrayEmptyArray) { expect_seq("[[]]", {AB, AB, AE, AE}); }

TEST(ReaderValid, ArraySpacedEmptyArrays) {
  expect_seq("[  [  ]  ]", {AB, AB, AE, AE});
}

TEST(ReaderValid, ObjectEmptyArrayAndEmptyObjectValues) {
  // {"a":[],"b":{}}
  expect_seq(R"({"a":[],"b":{}})", {OB, SBs, AB, AE, SBs, OB, OE, OE});
}

// ── Nested structures (≤ 5 levels) ────────────────────────────────────────────

TEST(ReaderValid, NestedObject5Levels) {
  // {"a":{"b":{"c":{"d":{"e":"val"}}}}}
  expect_seq(
      R"({"a":{"b":{"c":{"d":{"e":"val"}}}}})",
      {OB, SBs, OB, SBs, OB, SBs, OB, SBs, OB, SBs, SBs, OE, OE, OE, OE, OE});
}

TEST(ReaderValid, NestedArray5Levels) {
  expect_seq("[[[[[\"v\"]]]]]", {AB, AB, AB, AB, AB, SBs, AE, AE, AE, AE, AE});
}

TEST(ReaderValid, MixedNesting) {
  // {"a":[{"b":[[],{}]}]}
  expect_seq(R"({"a":[{"b":[[],{}]}]})",
             {OB, SBs, AB, OB, SBs, AB, AB, AE, OB, OE, AE, OE, AE, OE});
}

// ── Large / complex JSON ──────────────────────────────────────────────────────

TEST(ReaderValid, LargeComplexJson) {
  // {"name":"Alice","scores":[1,2,3],"meta":{"active":true,"tags":[],"info":{"x":42,"y":null}}}
  expect_seq(
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

TEST(ReaderValid, LeadingWhitespace) { expect_seq("   42", {SBl}); }

TEST(ReaderValid, TrailingWhitespace) { expect_seq("42   ", {SBl}); }

TEST(ReaderValid, WhitespaceAroundObject) { expect_seq("  { }  ", {OB, OE}); }

// ── Error: empty / whitespace-only input ─────────────────────────────────────

TEST(ReaderError, EmptyInput) { expect_error_at("", EC::unexpected_eof, 0); }

TEST(ReaderError, WhitespaceOnly) {
  expect_error_at("   ", EC::unexpected_eof, 3);
}

// ── Error: double-delimiter patterns (no spaces) ──────────────────────────────

TEST(ReaderError, DoubleComma) {
  // `,` at top level (ctx_size_=0) → unexpected_token at 0
  expect_error_at(",,", EC::unexpected_token, 0);
}

TEST(ReaderError, DoubleColon) {
  // `:` at top level → unexpected_token at 0
  expect_error_at("::", EC::unexpected_token, 0);
}

TEST(ReaderError, DoubleOpenBrace) {
  // Second `{` seen while expect_=key_or_end → unexpected_token at 1
  expect_error_at("{{", EC::unexpected_token, 1);
}

// ── Error: double-delimiter patterns (with spaces) ────────────────────────────

TEST(ReaderError, DoubleCommaSpaced) {
  expect_error_at(", ,", EC::unexpected_token, 0);
}

TEST(ReaderError, DoubleColonSpaced) {
  expect_error_at(": :", EC::unexpected_token, 0);
}

TEST(ReaderError, DoubleOpenBraceSpaced) {
  // Second `{` at offset 3 while expect_=key_or_end
  expect_error_at("{  {", EC::unexpected_token, 3);
}

// ── Error: trailing data ──────────────────────────────────────────────────────

TEST(ReaderError, TrailingDataAfterObject) {
  // `a` at offset 2 with expect_=comma_delimiter → unexpected_token
  expect_error_at("{}a", EC::unexpected_token, 2);
}

TEST(ReaderError, TrailingDataAfterObjectSpaced) {
  // `a` at offset 5 in "{   }a"
  expect_error_at("{   }a", EC::unexpected_token, 5);
}

TEST(ReaderError, TrailingCommaAtTopLevel) {
  // `,` at top level (ctx_size_=0) after closing `}`
  expect_error_at("{},", EC::unexpected_token, 2);
}

// ── Error: bracket mismatches ─────────────────────────────────────────────────

TEST(ReaderError, CloseBraceInArray) {
  // {"a":[} — `}` inside array context
  expect_error_at(R"({"a":[})", EC::unexpected_token, 6);
}

TEST(ReaderError, CloseBracketInObject) {
  // {"a":{] — `]` inside object context
  expect_error_at(R"({"a":{])", EC::unexpected_token, 6);
}

TEST(ReaderError, StandaloneCloseBrace) {
  expect_error_at("}", EC::unexpected_token, 0);
}

TEST(ReaderError, StandaloneCloseBracket) {
  expect_error_at("]", EC::unexpected_token, 0);
}

TEST(ReaderError, ObjectEndInArray) {
  expect_error_at("[}", EC::unexpected_token, 1);
}

TEST(ReaderError, ArrayEndInObject) {
  expect_error_at("{]", EC::unexpected_token, 1);
}

// ── Error: wrong separator characters ────────────────────────────────────────

TEST(ReaderError, ColonAsArraySeparator) {
  // ["a":"b"] — `:` after array value with expect_=comma_delimiter
  // After "a" at offset 1, advance(3) → pos_=4; `:` at 4 → unexpected_token
  expect_error_at(R"(["a":"b"])", EC::unexpected_token, 4);
}

TEST(ReaderError, CommaAsObjectSeparator) {
  // {"k","v"} — `,` where `:` expected (expect_=colon_delimiter)
  // After "k" key at offset 1, advance(3) → pos_=4; `,` at 4 → unexpected_token
  expect_error_at(R"({"k","v"})", EC::unexpected_token, 4);
}

TEST(ReaderError, MissingColonInObject) {
  // {"k" "v"} — `"` seen with expect_=colon_delimiter → unexpected_token at 5
  expect_error_at(R"({"k" "v"})", EC::unexpected_token, 5);
}

// ── Error: scalar with embedded whitespace ────────────────────────────────────

TEST(ReaderError, ScalarWithSpaceInObject) {
  // {"value":638 472} — after advancing past "638" (3 bytes from offset 9),
  // whitespace is skipped, `4` at offset 13 seen with expect_=comma_delimiter
  expect_error_at(R"({"value":638 472})", EC::unexpected_token, 13);
}

// ── Error: double colon inside object ────────────────────────────────────────

TEST(ReaderError, DoubleColonInObject) {
  // {"k"::"v"} — second `:` at offset 5 with expect_=value → unexpected_token
  expect_error_at(R"({"k"::"v"})", EC::unexpected_token, 5);
}

// ── Error: key without value ──────────────────────────────────────────────────

TEST(ReaderError, ObjectKeyWithoutValue) {
  // {"v"} — after key "v", expect_=colon_delimiter; `}` at 4 is not colon →
  // unexpected_token
  expect_error_at(R"({"v"})", EC::unexpected_token, 4);
}

// ── Error: comma before first array element ───────────────────────────────────

TEST(ReaderError, LeadingCommaInArray) {
  // [,] — `,` at offset 1 with expect_=value_or_end → unexpected_token
  expect_error_at("[,]", EC::unexpected_token, 1);
}

TEST(ReaderError, ColonInArrayBeforeValue) {
  // [:] — `:` at offset 1 with ctx=array (not object) → unexpected_token
  expect_error_at("[:]", EC::unexpected_token, 1);
}

// ── Error: unclosed containers ────────────────────────────────────────────────

TEST(ReaderError, UnclosedObject) {
  expect_error(R"({"k":"v")", EC::unexpected_eof);
}

TEST(ReaderError, UnclosedArray) { expect_error("[1,2", EC::unexpected_eof); }

TEST(ReaderError, UnclosedNestedObject) {
  expect_error(R"({"a":{"b":1})", EC::unexpected_eof);
}
