#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "hosq/json/compact_writer.hh"

using namespace std::string_view_literals;

namespace {

using TT = hosq::TokenType;
using JTC = hosq::JsonTokenContext;
using EC = hosq::ErrorCode;
using CW = hosq::json::CompactWriter<>;

// ── Write helpers ─────────────────────────────────────────────────────────────

static auto wr_scalar(CW& w, std::string_view content, JTC ctx) -> bool {
  auto buf = w.write({.length = content.size(),
                      .type = TT::Scalar,
                      .context = static_cast<uint8_t>(ctx)});
  if (w.error().has_error()) return false;
  std::copy(content.begin(), content.end(), buf.data());
  return w.commit(content.size());
}

static auto wr_str(CW& w, std::string_view s) -> bool {
  return wr_scalar(w, s, JTC::string);
}
static auto wr_lit(CW& w, std::string_view s) -> bool {
  return wr_scalar(w, s, JTC::literal);
}

static auto wr_ob(CW& w) -> bool {
  (void)w.write({.length = 0, .type = TT::ObjectBegin, .context = 0});
  return !w.error().has_error();
}
static auto wr_oe(CW& w) -> bool {
  (void)w.write({.length = 0, .type = TT::ObjectEnd, .context = 0});
  return !w.error().has_error();
}
static auto wr_ab(CW& w) -> bool {
  (void)w.write({.length = 0, .type = TT::ArrayBegin, .context = 0});
  return !w.error().has_error();
}
static auto wr_ae(CW& w) -> bool {
  (void)w.write({.length = 0, .type = TT::ArrayEnd, .context = 0});
  return !w.error().has_error();
}
static auto wr_end(CW& w) -> bool {
  (void)w.write({.length = 0, .type = TT::End, .context = 0});
  return !w.error().has_error();
}

}  // namespace

// ── Valid outputs ─────────────────────────────────────────────────────────────

TEST(CompactWriterValid, SingleString) {
  std::string out;
  CW w{out};
  ASSERT_TRUE(wr_str(w, "hello"));
  ASSERT_TRUE(wr_end(w));
  EXPECT_EQ(out, R"("hello")");
}

TEST(CompactWriterValid, EmptyString) {
  std::string out;
  CW w{out};
  ASSERT_TRUE(wr_str(w, ""));
  ASSERT_TRUE(wr_end(w));
  EXPECT_EQ(out, R"("")");
}

TEST(CompactWriterValid, SingleLiteral) {
  std::string out;
  CW w{out};
  ASSERT_TRUE(wr_lit(w, "42"));
  ASSERT_TRUE(wr_end(w));
  EXPECT_EQ(out, "42");
}

TEST(CompactWriterValid, LiteralTrue) {
  std::string out;
  CW w{out};
  ASSERT_TRUE(wr_lit(w, "true"));
  ASSERT_TRUE(wr_end(w));
  EXPECT_EQ(out, "true");
}

TEST(CompactWriterValid, LiteralNull) {
  std::string out;
  CW w{out};
  ASSERT_TRUE(wr_lit(w, "null"));
  ASSERT_TRUE(wr_end(w));
  EXPECT_EQ(out, "null");
}

TEST(CompactWriterValid, EmptyArray) {
  std::string out;
  CW w{out};
  ASSERT_TRUE(wr_ab(w));
  ASSERT_TRUE(wr_ae(w));
  ASSERT_TRUE(wr_end(w));
  EXPECT_EQ(out, "[]");
}

TEST(CompactWriterValid, EmptyObject) {
  std::string out;
  CW w{out};
  ASSERT_TRUE(wr_ob(w));
  ASSERT_TRUE(wr_oe(w));
  ASSERT_TRUE(wr_end(w));
  EXPECT_EQ(out, "{}");
}

TEST(CompactWriterValid, ArrayOfStrings) {
  std::string out;
  CW w{out};
  ASSERT_TRUE(wr_ab(w));
  ASSERT_TRUE(wr_str(w, "a"));
  ASSERT_TRUE(wr_str(w, "b"));
  ASSERT_TRUE(wr_str(w, "c"));
  ASSERT_TRUE(wr_ae(w));
  ASSERT_TRUE(wr_end(w));
  EXPECT_EQ(out, R"(["a","b","c"])");
}

TEST(CompactWriterValid, ArrayOfLiterals) {
  std::string out;
  CW w{out};
  ASSERT_TRUE(wr_ab(w));
  ASSERT_TRUE(wr_lit(w, "1"));
  ASSERT_TRUE(wr_lit(w, "2"));
  ASSERT_TRUE(wr_lit(w, "3"));
  ASSERT_TRUE(wr_ae(w));
  ASSERT_TRUE(wr_end(w));
  EXPECT_EQ(out, "[1,2,3]");
}

TEST(CompactWriterValid, SimpleObject) {
  std::string out;
  CW w{out};
  ASSERT_TRUE(wr_ob(w));
  ASSERT_TRUE(wr_str(w, "key"));
  ASSERT_TRUE(wr_str(w, "val"));
  ASSERT_TRUE(wr_oe(w));
  ASSERT_TRUE(wr_end(w));
  EXPECT_EQ(out, R"({"key":"val"})");
}

TEST(CompactWriterValid, ObjectLiteralValue) {
  std::string out;
  CW w{out};
  ASSERT_TRUE(wr_ob(w));
  ASSERT_TRUE(wr_str(w, "n"));
  ASSERT_TRUE(wr_lit(w, "42"));
  ASSERT_TRUE(wr_oe(w));
  ASSERT_TRUE(wr_end(w));
  EXPECT_EQ(out, R"({"n":42})");
}

TEST(CompactWriterValid, ObjectMultipleKeys) {
  std::string out;
  CW w{out};
  ASSERT_TRUE(wr_ob(w));
  ASSERT_TRUE(wr_str(w, "a"));
  ASSERT_TRUE(wr_str(w, "1"));
  ASSERT_TRUE(wr_str(w, "b"));
  ASSERT_TRUE(wr_str(w, "2"));
  ASSERT_TRUE(wr_oe(w));
  ASSERT_TRUE(wr_end(w));
  EXPECT_EQ(out, R"({"a":"1","b":"2"})");
}

TEST(CompactWriterValid, ObjectWithEmptyArrayValue) {
  std::string out;
  CW w{out};
  ASSERT_TRUE(wr_ob(w));
  ASSERT_TRUE(wr_str(w, "foobar"));
  ASSERT_TRUE(wr_ab(w));
  ASSERT_TRUE(wr_ae(w));
  ASSERT_TRUE(wr_oe(w));
  ASSERT_TRUE(wr_end(w));
  EXPECT_EQ(out, R"({"foobar":[]})");
}

TEST(CompactWriterValid, ObjectWithEmptyObjectValue) {
  std::string out;
  CW w{out};
  ASSERT_TRUE(wr_ob(w));
  ASSERT_TRUE(wr_str(w, "foo"));
  ASSERT_TRUE(wr_ob(w));
  ASSERT_TRUE(wr_oe(w));
  ASSERT_TRUE(wr_oe(w));
  ASSERT_TRUE(wr_end(w));
  EXPECT_EQ(out, R"({"foo":{}})");
}

TEST(CompactWriterValid, NestedObject5Levels) {
  // {"a":{"b":{"c":{"d":{"e":"val"}}}}}
  std::string out;
  CW w{out};
  ASSERT_TRUE(wr_ob(w));
  ASSERT_TRUE(wr_str(w, "a"));
  ASSERT_TRUE(wr_ob(w));
  ASSERT_TRUE(wr_str(w, "b"));
  ASSERT_TRUE(wr_ob(w));
  ASSERT_TRUE(wr_str(w, "c"));
  ASSERT_TRUE(wr_ob(w));
  ASSERT_TRUE(wr_str(w, "d"));
  ASSERT_TRUE(wr_ob(w));
  ASSERT_TRUE(wr_str(w, "e"));
  ASSERT_TRUE(wr_str(w, "val"));
  ASSERT_TRUE(wr_oe(w));
  ASSERT_TRUE(wr_oe(w));
  ASSERT_TRUE(wr_oe(w));
  ASSERT_TRUE(wr_oe(w));
  ASSERT_TRUE(wr_oe(w));
  ASSERT_TRUE(wr_end(w));
  EXPECT_EQ(out, R"({"a":{"b":{"c":{"d":{"e":"val"}}}}})");
}

TEST(CompactWriterValid, LargeComplexJson) {
  // {"name":"Alice","scores":[1,2,3],"meta":{"active":true,"tags":[],"info":{"x":42,"y":null}}}
  std::string out;
  CW w{out};
  ASSERT_TRUE(wr_ob(w));
  ASSERT_TRUE(wr_str(w, "name"));
  ASSERT_TRUE(wr_str(w, "Alice"));
  ASSERT_TRUE(wr_str(w, "scores"));
  ASSERT_TRUE(wr_ab(w));
  ASSERT_TRUE(wr_lit(w, "1"));
  ASSERT_TRUE(wr_lit(w, "2"));
  ASSERT_TRUE(wr_lit(w, "3"));
  ASSERT_TRUE(wr_ae(w));
  ASSERT_TRUE(wr_str(w, "meta"));
  ASSERT_TRUE(wr_ob(w));
  ASSERT_TRUE(wr_str(w, "active"));
  ASSERT_TRUE(wr_lit(w, "true"));
  ASSERT_TRUE(wr_str(w, "tags"));
  ASSERT_TRUE(wr_ab(w));
  ASSERT_TRUE(wr_ae(w));
  ASSERT_TRUE(wr_str(w, "info"));
  ASSERT_TRUE(wr_ob(w));
  ASSERT_TRUE(wr_str(w, "x"));
  ASSERT_TRUE(wr_lit(w, "42"));
  ASSERT_TRUE(wr_str(w, "y"));
  ASSERT_TRUE(wr_lit(w, "null"));
  ASSERT_TRUE(wr_oe(w));
  ASSERT_TRUE(wr_oe(w));
  ASSERT_TRUE(wr_oe(w));
  ASSERT_TRUE(wr_end(w));
  EXPECT_EQ(
      out,
      R"({"name":"Alice","scores":[1,2,3],"meta":{"active":true,"tags":[],"info":{"x":42,"y":null}}})");
}

TEST(CompactWriterValid, CommitLessThanCapacity) {
  // Request 5 bytes but commit only 3 — output is "hel" (truncated)
  std::string out;
  CW w{out};
  auto buf = w.write({.length = 5,
                      .type = TT::Scalar,
                      .context = static_cast<uint8_t>(JTC::string)});
  ASSERT_FALSE(w.error().has_error());
  const std::string_view src = "hello";
  std::copy(src.begin(), src.end(), buf.data());
  ASSERT_TRUE(w.commit(3));  // commit only 3 of 5
  ASSERT_TRUE(wr_end(w));
  EXPECT_EQ(out, R"("hel")");
}

TEST(CompactWriterValid, NoSpacesInOutput) {
  // Verify compact format produces no whitespace
  std::string out;
  CW w{out};
  ASSERT_TRUE(wr_ob(w));
  ASSERT_TRUE(wr_str(w, "k"));
  ASSERT_TRUE(wr_ab(w));
  ASSERT_TRUE(wr_str(w, "v"));
  ASSERT_TRUE(wr_ae(w));
  ASSERT_TRUE(wr_oe(w));
  ASSERT_TRUE(wr_end(w));
  EXPECT_EQ(out.find(' '), std::string::npos)
      << "output must not contain spaces";
  EXPECT_EQ(out.find('\n'), std::string::npos)
      << "output must not contain newlines";
}

// ── Error: document / root state ─────────────────────────────────────────────

TEST(CompactWriterError, EndWithNoRootWritten) {
  std::string out;
  CW w{out};
  ASSERT_FALSE(wr_end(w));
  EXPECT_EQ(w.error().code, EC::invalid_state);
  EXPECT_EQ(w.error().kind(), hosq::ErrorKind::state);
}

TEST(CompactWriterError, WriteAfterEnd) {
  std::string out;
  CW w{out};
  ASSERT_TRUE(wr_lit(w, "1"));
  ASSERT_TRUE(wr_end(w));
  ASSERT_FALSE(wr_lit(w, "2"));
  EXPECT_EQ(w.error().code, EC::invalid_state);
}

TEST(CompactWriterError, TwoRootValues) {
  std::string out;
  CW w{out};
  ASSERT_TRUE(wr_lit(w, "1"));
  ASSERT_FALSE(wr_lit(w, "2"));
  EXPECT_EQ(w.error().code, EC::invalid_state);
}

TEST(CompactWriterError, EndWithOpenObject) {
  std::string out;
  CW w{out};
  ASSERT_TRUE(wr_ob(w));
  ASSERT_FALSE(wr_end(w));
  EXPECT_EQ(w.error().code, EC::invalid_state);
}

TEST(CompactWriterError, EndWithOpenArray) {
  std::string out;
  CW w{out};
  ASSERT_TRUE(wr_ab(w));
  ASSERT_FALSE(wr_end(w));
  EXPECT_EQ(w.error().code, EC::invalid_state);
}

// ── Error: commit ─────────────────────────────────────────────────────────────

TEST(CompactWriterError, CommitWithoutPending) {
  std::string out;
  CW w{out};
  ASSERT_FALSE(w.commit(0));
  EXPECT_EQ(w.error().code, EC::invalid_state);
}

TEST(CompactWriterError, CommitAfterEnd) {
  std::string out;
  CW w{out};
  ASSERT_TRUE(wr_lit(w, "1"));
  ASSERT_TRUE(wr_end(w));
  ASSERT_FALSE(w.commit(0));
  EXPECT_EQ(w.error().code, EC::invalid_state);
}

TEST(CompactWriterError, CommitOverCapacity) {
  std::string out;
  CW w{out};
  (void)w.write({.length = 3,
                 .type = TT::Scalar,
                 .context = static_cast<uint8_t>(JTC::literal)});
  ASSERT_FALSE(w.error().has_error());
  ASSERT_FALSE(w.commit(4));  // 4 > 3
  EXPECT_EQ(w.error().code, EC::invalid_token);
}

// ── Error: write while pending ────────────────────────────────────────────────

TEST(CompactWriterError, WriteWhilePending) {
  std::string out;
  CW w{out};
  (void)w.write({.length = 3,
                 .type = TT::Scalar,
                 .context = static_cast<uint8_t>(JTC::literal)});
  ASSERT_FALSE(w.error().has_error());
  // second write without commit
  ASSERT_FALSE(wr_lit(w, "42"));
  EXPECT_EQ(w.error().code, EC::invalid_state);
}

TEST(CompactWriterError, ArrayEndWhilePending) {
  std::string out;
  CW w{out};
  ASSERT_TRUE(wr_ab(w));
  // ScalarBegin but not yet committed
  (void)w.write({.length = 3,
                 .type = TT::Scalar,
                 .context = static_cast<uint8_t>(JTC::literal)});
  ASSERT_FALSE(w.error().has_error());
  ASSERT_FALSE(wr_ae(w));
  EXPECT_EQ(w.error().code, EC::invalid_state);
}

TEST(CompactWriterError, ObjectEndWhilePending) {
  std::string out;
  CW w{out};
  ASSERT_TRUE(wr_ob(w));
  (void)w.write({.length = 1,
                 .type = TT::Scalar,
                 .context = static_cast<uint8_t>(JTC::string)});
  ASSERT_FALSE(w.error().has_error());
  ASSERT_FALSE(wr_oe(w));  // pending active → error
  EXPECT_EQ(w.error().code, EC::invalid_state);
}

// ── Error: mismatched container close ─────────────────────────────────────────

TEST(CompactWriterError, ObjectEndWithoutOpen) {
  std::string out;
  CW w{out};
  ASSERT_FALSE(wr_oe(w));
  EXPECT_EQ(w.error().code, EC::invalid_state);
}

TEST(CompactWriterError, ArrayEndWithoutOpen) {
  std::string out;
  CW w{out};
  ASSERT_FALSE(wr_ae(w));
  EXPECT_EQ(w.error().code, EC::invalid_state);
}

TEST(CompactWriterError, ObjectEndInArray) {
  std::string out;
  CW w{out};
  ASSERT_TRUE(wr_ab(w));
  ASSERT_FALSE(wr_oe(w));
  EXPECT_EQ(w.error().code, EC::invalid_state);
}

TEST(CompactWriterError, ArrayEndInObject) {
  std::string out;
  CW w{out};
  ASSERT_TRUE(wr_ob(w));
  // Must close an empty object: needs key first, or close directly
  // (expecting_key=true) Actually with empty object, expecting_key=true, so
  // ArrayEnd should mismatch on kind
  ASSERT_FALSE(wr_ae(w));
  EXPECT_EQ(w.error().code, EC::invalid_state);
}

// ── Error: key/value ordering in objects ─────────────────────────────────────

TEST(CompactWriterError, LiteralAsObjectKey) {
  // Literals cannot be object keys — only strings
  std::string out;
  CW w{out};
  ASSERT_TRUE(wr_ob(w));
  ASSERT_FALSE(wr_lit(w, "42"));  // literal where key expected
  EXPECT_EQ(w.error().code, EC::invalid_token);
}

TEST(CompactWriterError, ArrayBeginWhenKeyExpected) {
  // ObjectBegin/ArrayBegin as value where a key is expected
  std::string out;
  CW w{out};
  ASSERT_TRUE(wr_ob(w));
  ASSERT_FALSE(wr_ab(w));  // array value where key expected
  EXPECT_EQ(w.error().code, EC::invalid_state);
}

TEST(CompactWriterError, ObjectBeginWhenKeyExpected) {
  std::string out;
  CW w{out};
  ASSERT_TRUE(wr_ob(w));
  ASSERT_FALSE(wr_ob(w));  // nested object where key expected
  EXPECT_EQ(w.error().code, EC::invalid_state);
}

TEST(CompactWriterError, ObjectEndAfterKeyNoValue) {
  // Key committed, no value yet → expecting_key=false → ObjectEnd rejected
  std::string out;
  CW w{out};
  ASSERT_TRUE(wr_ob(w));
  ASSERT_TRUE(wr_str(w, "k"));  // key committed
  ASSERT_FALSE(wr_oe(w));       // value not written
  EXPECT_EQ(w.error().code, EC::invalid_state);
}

// ── Error: bad WriteRequest parameters ───────────────────────────────────────

TEST(CompactWriterError, ScalarBeginUnresolvedLength) {
  std::string out;
  CW w{out};
  (void)w.write({.length = hosq::unresolved_length,
                 .type = TT::Scalar,
                 .context = static_cast<uint8_t>(JTC::string)});
  ASSERT_TRUE(w.error().has_error());
  EXPECT_EQ(w.error().code, EC::invalid_token);
}

TEST(CompactWriterError, ScalarBeginContextNone) {
  std::string out;
  CW w{out};
  (void)w.write({.length = 3,
                 .type = TT::Scalar,
                 .context = static_cast<uint8_t>(JTC::none)});
  ASSERT_TRUE(w.error().has_error());
  EXPECT_EQ(w.error().code, EC::invalid_token);
}

TEST(CompactWriterError, ObjectBeginNonZeroContext) {
  std::string out;
  CW w{out};
  (void)w.write({.length = 0, .type = TT::ObjectBegin, .context = 1});
  ASSERT_TRUE(w.error().has_error());
  EXPECT_EQ(w.error().code, EC::invalid_token);
}

TEST(CompactWriterError, ObjectEndNonZeroLength) {
  std::string out;
  CW w{out};
  ASSERT_TRUE(wr_ob(w));
  (void)w.write({.length = 1, .type = TT::ObjectEnd, .context = 0});
  ASSERT_TRUE(w.error().has_error());
  EXPECT_EQ(w.error().code, EC::invalid_token);
}

TEST(CompactWriterError, ArrayEndNonZeroContext) {
  std::string out;
  CW w{out};
  ASSERT_TRUE(wr_ab(w));
  (void)w.write({.length = 0, .type = TT::ArrayEnd, .context = 1});
  ASSERT_TRUE(w.error().has_error());
  EXPECT_EQ(w.error().code, EC::invalid_token);
}

TEST(CompactWriterError, EndNonZeroLength) {
  std::string out;
  CW w{out};
  ASSERT_TRUE(wr_lit(w, "1"));
  (void)w.write({.length = 1, .type = TT::End, .context = 0});
  ASSERT_TRUE(w.error().has_error());
  EXPECT_EQ(w.error().code, EC::invalid_token);
}

TEST(CompactWriterError, EndNonZeroContext) {
  std::string out;
  CW w{out};
  ASSERT_TRUE(wr_lit(w, "1"));
  (void)w.write({.length = 0, .type = TT::End, .context = 1});
  ASSERT_TRUE(w.error().has_error());
  EXPECT_EQ(w.error().code, EC::invalid_token);
}

// ── Error: depth limit ────────────────────────────────────────────────────────

TEST(CompactWriterError, DepthLimitExceeded) {
  std::string out;
  hosq::json::CompactWriter<2> w{out};

  (void)w.write({.length = 0, .type = TT::ArrayBegin, .context = 0});
  ASSERT_FALSE(w.error().has_error());
  (void)w.write({.length = 0, .type = TT::ArrayBegin, .context = 0});
  ASSERT_FALSE(w.error().has_error());
  (void)w.write({.length = 0, .type = TT::ArrayBegin, .context = 0});
  ASSERT_TRUE(w.error().has_error());
  EXPECT_EQ(w.error().code, EC::depth_limit_exceeded);
  EXPECT_EQ(w.error().kind(), hosq::ErrorKind::limit);
}
