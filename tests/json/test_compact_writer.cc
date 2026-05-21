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

static auto wr_scalar(CW& w, std::string_view content, JTC ctx)
    -> hosq::Result<void, hosq::Error> {
  auto buf = w.write({.length = content.size(),
                      .type = TT::Scalar,
                      .context = static_cast<uint8_t>(ctx)});
  if (!buf) return hosq::unexpected(buf.error());
  std::copy(content.begin(), content.end(), buf->data());
  return w.commit(content.size());
}

static auto wr_str(CW& w, std::string_view s) {
  return wr_scalar(w, s, JTC::string);
}
static auto wr_lit(CW& w, std::string_view s) {
  return wr_scalar(w, s, JTC::literal);
}

static auto wr_ob(CW& w) {
  return w.write({.length = 0, .type = TT::ObjectBegin, .context = 0});
}
static auto wr_oe(CW& w) {
  return w.write({.length = 0, .type = TT::ObjectEnd, .context = 0});
}
static auto wr_ab(CW& w) {
  return w.write({.length = 0, .type = TT::ArrayBegin, .context = 0});
}
static auto wr_ae(CW& w) {
  return w.write({.length = 0, .type = TT::ArrayEnd, .context = 0});
}
static auto wr_end(CW& w) {
  return w.write({.length = 0, .type = TT::End, .context = 0});
}

}  // namespace

// ── Valid outputs ─────────────────────────────────────────────────────────────

TEST(CompactWriterValid, SingleString) {
  std::string out;
  CW w{out};
  ASSERT_TRUE(wr_str(w, "hello").has_value());
  ASSERT_TRUE(wr_end(w).has_value());
  EXPECT_EQ(out, R"("hello")");
}

TEST(CompactWriterValid, EmptyString) {
  std::string out;
  CW w{out};
  ASSERT_TRUE(wr_str(w, "").has_value());
  ASSERT_TRUE(wr_end(w).has_value());
  EXPECT_EQ(out, R"("")");
}

TEST(CompactWriterValid, SingleLiteral) {
  std::string out;
  CW w{out};
  ASSERT_TRUE(wr_lit(w, "42").has_value());
  ASSERT_TRUE(wr_end(w).has_value());
  EXPECT_EQ(out, "42");
}

TEST(CompactWriterValid, LiteralTrue) {
  std::string out;
  CW w{out};
  ASSERT_TRUE(wr_lit(w, "true").has_value());
  ASSERT_TRUE(wr_end(w).has_value());
  EXPECT_EQ(out, "true");
}

TEST(CompactWriterValid, LiteralNull) {
  std::string out;
  CW w{out};
  ASSERT_TRUE(wr_lit(w, "null").has_value());
  ASSERT_TRUE(wr_end(w).has_value());
  EXPECT_EQ(out, "null");
}

TEST(CompactWriterValid, EmptyArray) {
  std::string out;
  CW w{out};
  ASSERT_TRUE(wr_ab(w).has_value());
  ASSERT_TRUE(wr_ae(w).has_value());
  ASSERT_TRUE(wr_end(w).has_value());
  EXPECT_EQ(out, "[]");
}

TEST(CompactWriterValid, EmptyObject) {
  std::string out;
  CW w{out};
  ASSERT_TRUE(wr_ob(w).has_value());
  ASSERT_TRUE(wr_oe(w).has_value());
  ASSERT_TRUE(wr_end(w).has_value());
  EXPECT_EQ(out, "{}");
}

TEST(CompactWriterValid, ArrayOfStrings) {
  std::string out;
  CW w{out};
  ASSERT_TRUE(wr_ab(w).has_value());
  ASSERT_TRUE(wr_str(w, "a").has_value());
  ASSERT_TRUE(wr_str(w, "b").has_value());
  ASSERT_TRUE(wr_str(w, "c").has_value());
  ASSERT_TRUE(wr_ae(w).has_value());
  ASSERT_TRUE(wr_end(w).has_value());
  EXPECT_EQ(out, R"(["a","b","c"])");
}

TEST(CompactWriterValid, ArrayOfLiterals) {
  std::string out;
  CW w{out};
  ASSERT_TRUE(wr_ab(w).has_value());
  ASSERT_TRUE(wr_lit(w, "1").has_value());
  ASSERT_TRUE(wr_lit(w, "2").has_value());
  ASSERT_TRUE(wr_lit(w, "3").has_value());
  ASSERT_TRUE(wr_ae(w).has_value());
  ASSERT_TRUE(wr_end(w).has_value());
  EXPECT_EQ(out, "[1,2,3]");
}

TEST(CompactWriterValid, SimpleObject) {
  std::string out;
  CW w{out};
  ASSERT_TRUE(wr_ob(w).has_value());
  ASSERT_TRUE(wr_str(w, "key").has_value());
  ASSERT_TRUE(wr_str(w, "val").has_value());
  ASSERT_TRUE(wr_oe(w).has_value());
  ASSERT_TRUE(wr_end(w).has_value());
  EXPECT_EQ(out, R"({"key":"val"})");
}

TEST(CompactWriterValid, ObjectLiteralValue) {
  std::string out;
  CW w{out};
  ASSERT_TRUE(wr_ob(w).has_value());
  ASSERT_TRUE(wr_str(w, "n").has_value());
  ASSERT_TRUE(wr_lit(w, "42").has_value());
  ASSERT_TRUE(wr_oe(w).has_value());
  ASSERT_TRUE(wr_end(w).has_value());
  EXPECT_EQ(out, R"({"n":42})");
}

TEST(CompactWriterValid, ObjectMultipleKeys) {
  std::string out;
  CW w{out};
  ASSERT_TRUE(wr_ob(w).has_value());
  ASSERT_TRUE(wr_str(w, "a").has_value());
  ASSERT_TRUE(wr_str(w, "1").has_value());
  ASSERT_TRUE(wr_str(w, "b").has_value());
  ASSERT_TRUE(wr_str(w, "2").has_value());
  ASSERT_TRUE(wr_oe(w).has_value());
  ASSERT_TRUE(wr_end(w).has_value());
  EXPECT_EQ(out, R"({"a":"1","b":"2"})");
}

TEST(CompactWriterValid, ObjectWithEmptyArrayValue) {
  std::string out;
  CW w{out};
  ASSERT_TRUE(wr_ob(w).has_value());
  ASSERT_TRUE(wr_str(w, "foobar").has_value());
  ASSERT_TRUE(wr_ab(w).has_value());
  ASSERT_TRUE(wr_ae(w).has_value());
  ASSERT_TRUE(wr_oe(w).has_value());
  ASSERT_TRUE(wr_end(w).has_value());
  EXPECT_EQ(out, R"({"foobar":[]})");
}

TEST(CompactWriterValid, ObjectWithEmptyObjectValue) {
  std::string out;
  CW w{out};
  ASSERT_TRUE(wr_ob(w).has_value());
  ASSERT_TRUE(wr_str(w, "foo").has_value());
  ASSERT_TRUE(wr_ob(w).has_value());
  ASSERT_TRUE(wr_oe(w).has_value());
  ASSERT_TRUE(wr_oe(w).has_value());
  ASSERT_TRUE(wr_end(w).has_value());
  EXPECT_EQ(out, R"({"foo":{}})");
}

TEST(CompactWriterValid, NestedObject5Levels) {
  // {"a":{"b":{"c":{"d":{"e":"val"}}}}}
  std::string out;
  CW w{out};
  ASSERT_TRUE(wr_ob(w).has_value());
  ASSERT_TRUE(wr_str(w, "a").has_value());
  ASSERT_TRUE(wr_ob(w).has_value());
  ASSERT_TRUE(wr_str(w, "b").has_value());
  ASSERT_TRUE(wr_ob(w).has_value());
  ASSERT_TRUE(wr_str(w, "c").has_value());
  ASSERT_TRUE(wr_ob(w).has_value());
  ASSERT_TRUE(wr_str(w, "d").has_value());
  ASSERT_TRUE(wr_ob(w).has_value());
  ASSERT_TRUE(wr_str(w, "e").has_value());
  ASSERT_TRUE(wr_str(w, "val").has_value());
  ASSERT_TRUE(wr_oe(w).has_value());
  ASSERT_TRUE(wr_oe(w).has_value());
  ASSERT_TRUE(wr_oe(w).has_value());
  ASSERT_TRUE(wr_oe(w).has_value());
  ASSERT_TRUE(wr_oe(w).has_value());
  ASSERT_TRUE(wr_end(w).has_value());
  EXPECT_EQ(out, R"({"a":{"b":{"c":{"d":{"e":"val"}}}}})");
}

TEST(CompactWriterValid, LargeComplexJson) {
  // {"name":"Alice","scores":[1,2,3],"meta":{"active":true,"tags":[],"info":{"x":42,"y":null}}}
  std::string out;
  CW w{out};
  ASSERT_TRUE(wr_ob(w).has_value());
  ASSERT_TRUE(wr_str(w, "name").has_value());
  ASSERT_TRUE(wr_str(w, "Alice").has_value());
  ASSERT_TRUE(wr_str(w, "scores").has_value());
  ASSERT_TRUE(wr_ab(w).has_value());
  ASSERT_TRUE(wr_lit(w, "1").has_value());
  ASSERT_TRUE(wr_lit(w, "2").has_value());
  ASSERT_TRUE(wr_lit(w, "3").has_value());
  ASSERT_TRUE(wr_ae(w).has_value());
  ASSERT_TRUE(wr_str(w, "meta").has_value());
  ASSERT_TRUE(wr_ob(w).has_value());
  ASSERT_TRUE(wr_str(w, "active").has_value());
  ASSERT_TRUE(wr_lit(w, "true").has_value());
  ASSERT_TRUE(wr_str(w, "tags").has_value());
  ASSERT_TRUE(wr_ab(w).has_value());
  ASSERT_TRUE(wr_ae(w).has_value());
  ASSERT_TRUE(wr_str(w, "info").has_value());
  ASSERT_TRUE(wr_ob(w).has_value());
  ASSERT_TRUE(wr_str(w, "x").has_value());
  ASSERT_TRUE(wr_lit(w, "42").has_value());
  ASSERT_TRUE(wr_str(w, "y").has_value());
  ASSERT_TRUE(wr_lit(w, "null").has_value());
  ASSERT_TRUE(wr_oe(w).has_value());
  ASSERT_TRUE(wr_oe(w).has_value());
  ASSERT_TRUE(wr_oe(w).has_value());
  ASSERT_TRUE(wr_end(w).has_value());
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
  ASSERT_TRUE(buf.has_value());
  const std::string_view src = "hello";
  std::copy(src.begin(), src.end(), buf->data());
  ASSERT_TRUE(w.commit(3).has_value());  // commit only 3 of 5
  ASSERT_TRUE(wr_end(w).has_value());
  EXPECT_EQ(out, R"("hel")");
}

TEST(CompactWriterValid, NoSpacesInOutput) {
  // Verify compact format produces no whitespace
  std::string out;
  CW w{out};
  ASSERT_TRUE(wr_ob(w).has_value());
  ASSERT_TRUE(wr_str(w, "k").has_value());
  ASSERT_TRUE(wr_ab(w).has_value());
  ASSERT_TRUE(wr_str(w, "v").has_value());
  ASSERT_TRUE(wr_ae(w).has_value());
  ASSERT_TRUE(wr_oe(w).has_value());
  ASSERT_TRUE(wr_end(w).has_value());
  EXPECT_EQ(out.find(' '), std::string::npos)
      << "output must not contain spaces";
  EXPECT_EQ(out.find('\n'), std::string::npos)
      << "output must not contain newlines";
}

// ── Error: document / root state ─────────────────────────────────────────────

TEST(CompactWriterError, EndWithNoRootWritten) {
  std::string out;
  CW w{out};
  auto res = wr_end(w);
  ASSERT_FALSE(res.has_value());
  EXPECT_EQ(res.error().code, EC::unexpected_eof);
}

TEST(CompactWriterError, WriteAfterEnd) {
  std::string out;
  CW w{out};
  ASSERT_TRUE(wr_lit(w, "1").has_value());
  ASSERT_TRUE(wr_end(w).has_value());
  auto res = wr_lit(w, "2");
  ASSERT_FALSE(res.has_value());
  EXPECT_EQ(res.error().code, EC::unexpected_token);
}

TEST(CompactWriterError, TwoRootValues) {
  std::string out;
  CW w{out};
  ASSERT_TRUE(wr_lit(w, "1").has_value());
  auto res = wr_lit(w, "2");
  ASSERT_FALSE(res.has_value());
  EXPECT_EQ(res.error().code, EC::unexpected_token);
}

TEST(CompactWriterError, EndWithOpenObject) {
  std::string out;
  CW w{out};
  ASSERT_TRUE(wr_ob(w).has_value());
  auto res = wr_end(w);
  ASSERT_FALSE(res.has_value());
  EXPECT_EQ(res.error().code, EC::unexpected_eof);
}

TEST(CompactWriterError, EndWithOpenArray) {
  std::string out;
  CW w{out};
  ASSERT_TRUE(wr_ab(w).has_value());
  auto res = wr_end(w);
  ASSERT_FALSE(res.has_value());
  EXPECT_EQ(res.error().code, EC::unexpected_eof);
}

// ── Error: commit ─────────────────────────────────────────────────────────────

TEST(CompactWriterError, CommitWithoutPending) {
  std::string out;
  CW w{out};
  auto res = w.commit(0);
  ASSERT_FALSE(res.has_value());
  EXPECT_EQ(res.error().code, EC::unexpected_token);
}

TEST(CompactWriterError, CommitAfterEnd) {
  std::string out;
  CW w{out};
  ASSERT_TRUE(wr_lit(w, "1").has_value());
  ASSERT_TRUE(wr_end(w).has_value());
  auto res = w.commit(0);
  ASSERT_FALSE(res.has_value());
  EXPECT_EQ(res.error().code, EC::unexpected_token);
}

TEST(CompactWriterError, CommitOverCapacity) {
  std::string out;
  CW w{out};
  ASSERT_TRUE(w.write({.length = 3,
                       .type = TT::Scalar,
                       .context = static_cast<uint8_t>(JTC::literal)})
                  .has_value());
  auto res = w.commit(4);  // 4 > 3
  ASSERT_FALSE(res.has_value());
  EXPECT_EQ(res.error().code, EC::unexpected_token);
}

// ── Error: write while pending ────────────────────────────────────────────────

TEST(CompactWriterError, WriteWhilePending) {
  std::string out;
  CW w{out};
  ASSERT_TRUE(w.write({.length = 3,
                       .type = TT::Scalar,
                       .context = static_cast<uint8_t>(JTC::literal)})
                  .has_value());
  // second write without commit
  auto res = wr_lit(w, "42");
  ASSERT_FALSE(res.has_value());
  EXPECT_EQ(res.error().code, EC::unexpected_token);
}

TEST(CompactWriterError, ArrayEndWhilePending) {
  std::string out;
  CW w{out};
  ASSERT_TRUE(wr_ab(w).has_value());
  // ScalarBegin but not yet committed
  ASSERT_TRUE(w.write({.length = 3,
                       .type = TT::Scalar,
                       .context = static_cast<uint8_t>(JTC::literal)})
                  .has_value());
  auto res = wr_ae(w);
  ASSERT_FALSE(res.has_value());
  EXPECT_EQ(res.error().code, EC::unexpected_token);
}

TEST(CompactWriterError, ObjectEndWhilePending) {
  std::string out;
  CW w{out};
  ASSERT_TRUE(wr_ob(w).has_value());
  ASSERT_TRUE(w.write({.length = 1,
                       .type = TT::Scalar,
                       .context = static_cast<uint8_t>(JTC::string)})
                  .has_value());
  auto res = wr_oe(w);  // pending active → error
  ASSERT_FALSE(res.has_value());
  EXPECT_EQ(res.error().code, EC::unexpected_token);
}

// ── Error: mismatched container close ─────────────────────────────────────────

TEST(CompactWriterError, ObjectEndWithoutOpen) {
  std::string out;
  CW w{out};
  auto res = wr_oe(w);
  ASSERT_FALSE(res.has_value());
  EXPECT_EQ(res.error().code, EC::unexpected_token);
}

TEST(CompactWriterError, ArrayEndWithoutOpen) {
  std::string out;
  CW w{out};
  auto res = wr_ae(w);
  ASSERT_FALSE(res.has_value());
  EXPECT_EQ(res.error().code, EC::unexpected_token);
}

TEST(CompactWriterError, ObjectEndInArray) {
  std::string out;
  CW w{out};
  ASSERT_TRUE(wr_ab(w).has_value());
  auto res = wr_oe(w);
  ASSERT_FALSE(res.has_value());
  EXPECT_EQ(res.error().code, EC::unexpected_token);
}

TEST(CompactWriterError, ArrayEndInObject) {
  std::string out;
  CW w{out};
  ASSERT_TRUE(wr_ob(w).has_value());
  // Must close an empty object: needs key first, or close directly
  // (expecting_key=true) Actually with empty object, expecting_key=true, so
  // ArrayEnd should mismatch on kind
  auto res = wr_ae(w);
  ASSERT_FALSE(res.has_value());
  EXPECT_EQ(res.error().code, EC::unexpected_token);
}

// ── Error: key/value ordering in objects ─────────────────────────────────────

TEST(CompactWriterError, LiteralAsObjectKey) {
  // Literals cannot be object keys — only strings
  std::string out;
  CW w{out};
  ASSERT_TRUE(wr_ob(w).has_value());
  auto res = wr_lit(w, "42");  // literal where key expected
  ASSERT_FALSE(res.has_value());
  EXPECT_EQ(res.error().code, EC::unexpected_token);
}

TEST(CompactWriterError, ArrayBeginWhenKeyExpected) {
  // ObjectBegin/ArrayBegin as value where a key is expected
  std::string out;
  CW w{out};
  ASSERT_TRUE(wr_ob(w).has_value());
  auto res = wr_ab(w);  // array value where key expected
  ASSERT_FALSE(res.has_value());
  EXPECT_EQ(res.error().code, EC::unexpected_token);
}

TEST(CompactWriterError, ObjectBeginWhenKeyExpected) {
  std::string out;
  CW w{out};
  ASSERT_TRUE(wr_ob(w).has_value());
  auto res = wr_ob(w);  // nested object where key expected
  ASSERT_FALSE(res.has_value());
  EXPECT_EQ(res.error().code, EC::unexpected_token);
}

TEST(CompactWriterError, ObjectEndAfterKeyNoValue) {
  // Key committed, no value yet → expecting_key=false → ObjectEnd rejected
  std::string out;
  CW w{out};
  ASSERT_TRUE(wr_ob(w).has_value());
  ASSERT_TRUE(wr_str(w, "k").has_value());  // key committed
  auto res = wr_oe(w);                      // value not written
  ASSERT_FALSE(res.has_value());
  EXPECT_EQ(res.error().code, EC::unexpected_token);
}

// ── Error: bad WriteRequest parameters ───────────────────────────────────────

TEST(CompactWriterError, ScalarBeginUnresolvedLength) {
  std::string out;
  CW w{out};
  auto res = w.write({.length = hosq::unresolved_length,
                      .type = TT::Scalar,
                      .context = static_cast<uint8_t>(JTC::string)});
  ASSERT_FALSE(res.has_value());
  EXPECT_EQ(res.error().code, EC::unexpected_token);
}

TEST(CompactWriterError, ScalarBeginContextNone) {
  std::string out;
  CW w{out};
  auto res = w.write({.length = 3,
                      .type = TT::Scalar,
                      .context = static_cast<uint8_t>(JTC::none)});
  ASSERT_FALSE(res.has_value());
  EXPECT_EQ(res.error().code, EC::unexpected_token);
}

TEST(CompactWriterError, ObjectBeginNonZeroContext) {
  std::string out;
  CW w{out};
  auto res = w.write({.length = 0, .type = TT::ObjectBegin, .context = 1});
  ASSERT_FALSE(res.has_value());
  EXPECT_EQ(res.error().code, EC::unexpected_token);
}

TEST(CompactWriterError, ObjectEndNonZeroLength) {
  std::string out;
  CW w{out};
  ASSERT_TRUE(wr_ob(w).has_value());
  auto res = w.write({.length = 1, .type = TT::ObjectEnd, .context = 0});
  ASSERT_FALSE(res.has_value());
  EXPECT_EQ(res.error().code, EC::unexpected_token);
}

TEST(CompactWriterError, ArrayEndNonZeroContext) {
  std::string out;
  CW w{out};
  ASSERT_TRUE(wr_ab(w).has_value());
  auto res = w.write({.length = 0, .type = TT::ArrayEnd, .context = 1});
  ASSERT_FALSE(res.has_value());
  EXPECT_EQ(res.error().code, EC::unexpected_token);
}

TEST(CompactWriterError, EndNonZeroLength) {
  std::string out;
  CW w{out};
  ASSERT_TRUE(wr_lit(w, "1").has_value());
  auto res = w.write({.length = 1, .type = TT::End, .context = 0});
  ASSERT_FALSE(res.has_value());
  EXPECT_EQ(res.error().code, EC::unexpected_token);
}

TEST(CompactWriterError, EndNonZeroContext) {
  std::string out;
  CW w{out};
  ASSERT_TRUE(wr_lit(w, "1").has_value());
  auto res = w.write({.length = 0, .type = TT::End, .context = 1});
  ASSERT_FALSE(res.has_value());
  EXPECT_EQ(res.error().code, EC::unexpected_token);
}

// ── Error: depth limit ────────────────────────────────────────────────────────

TEST(CompactWriterError, DepthLimitExceeded) {
  std::string out;
  hosq::json::CompactWriter<2> w{out};

  ASSERT_TRUE(
      w.write({.length = 0, .type = TT::ArrayBegin, .context = 0}).has_value());
  ASSERT_TRUE(
      w.write({.length = 0, .type = TT::ArrayBegin, .context = 0}).has_value());
  auto res = w.write({.length = 0, .type = TT::ArrayBegin, .context = 0});
  ASSERT_FALSE(res.has_value());
  EXPECT_EQ(res.error().code, EC::depth_limit_exceeded);
}
