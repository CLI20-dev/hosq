#include <gtest/gtest.h>

#include "hosq/json/detail/validated_value.hh"
#include "hosq/json/fast_reader.hh"
#include "hosq/json/simple_reader.hh"

namespace {
using namespace hosq;
template <class R>
bool valid(std::string_view text) {
  R reader;
  typename R::state_type state;
  reader.reset(state, text);
  if (!reader.next_token(state)) return false;
  return json::detail::validate_value<false>(reader, state,
                                             [](const auto&, auto) {}) &&
         reader.next_token(state) &&
         reader.current_token(state).type == TokenType::End;
}

template <class R>
void grammar() {
  for (auto text : {"null", "true", "false", "-0", "1.25e+2", "[]", "{}",
                    "{\"a\":[true,null,{\"b\":\"\\uD834\\uDD1E\"}]}",
                    "{\"\":0,\"a\":1}", "\"雪\""})
    EXPECT_TRUE(valid<R>(text)) << text;
  for (auto text : {"",
                    "nonsense",
                    "truefalse",
                    "01",
                    "-",
                    "1.",
                    "1e",
                    "+1",
                    "NaN",
                    "[1,]",
                    "{\"a\":}",
                    "[}",
                    "[1 2]",
                    "{a:1}",
                    "\"\\x\"",
                    "\"\\uD800\"",
                    "\"\\uDC00\"",
                    "\"\\uZZZZ\"",
                    "{\"a\":1,\"a\":2}",
                    "{\"x\":1,\"\\u0078\":2}",
                    "{\"nested\":{\"x\":1,\"x\":2}}",
                    "{} []"})
    EXPECT_FALSE(valid<R>(text)) << text;
  EXPECT_FALSE(valid<R>(std::string("\"a\nb\"")));
  EXPECT_FALSE(valid<R>(std::string("\"\xc0\xaf\"")));
  EXPECT_TRUE(valid<R>(std::string(1000, '9')));
  EXPECT_TRUE(valid<R>("1e9999999999999999999999999999999999"));
  EXPECT_TRUE(valid<R>("{\"a\":{\"x\":1},\"b\":{\"x\":2}}"));
}
TEST(DomValidation, FastGrammar) { grammar<json::FastReader<>>(); }
TEST(DomValidation, SimpleGrammar) { grammar<json::SimpleReader>(); }

template <class R>
void embedded() {
  constexpr std::string_view text = "[ {\"a\":[\"x\",-0]}, 42 ]";
  R reader;
  typename R::state_type state;
  reader.reset(state, text);
  ASSERT_TRUE(reader.next_token(state));
  ASSERT_TRUE(reader.next_token(state));
  std::vector<std::pair<ErrorPath, json::detail::ValueRange>> seen;
  ASSERT_TRUE(json::detail::validate_value<true>(
      reader, state, [&](const ErrorPath& path, auto range) {
        seen.emplace_back(path, range);
      }));
  ASSERT_EQ(seen.size(), 4u);
  EXPECT_EQ(text.substr(seen.back().second.offset, seen.back().second.length),
            "{\"a\":[\"x\",-0]}");
  EXPECT_TRUE(seen.back().first.empty());
  ASSERT_EQ(seen[0].first.size(), 2u);
  EXPECT_EQ(seen[0].first[0], PathSegment::field("a"));
  EXPECT_EQ(seen[0].first[1], PathSegment::element(0));
  EXPECT_EQ(text.substr(seen[0].second.offset, seen[0].second.length), "\"x\"");
  EXPECT_EQ(reader.current_token(state).type, TokenType::ObjectEnd);
  ASSERT_TRUE(reader.next_token(state));
  ASSERT_TRUE(json::detail::validate_value<false>(
      reader, state, [](const auto& path, auto) { EXPECT_TRUE(path.empty()); }));
  EXPECT_EQ(reader.current_token(state).type, TokenType::Scalar);
  ASSERT_TRUE(reader.next_token(state));
  EXPECT_EQ(reader.current_token(state).type, TokenType::ArrayEnd);
}
TEST(DomValidation, FastEmbedded) { embedded<json::FastReader<>>(); }
TEST(DomValidation, SimpleEmbedded) { embedded<json::SimpleReader>(); }
template <class R>
void limits_and_errors() {
  R reader;
  typename R::state_type state;
  const auto visit = [](const auto&, auto) {};
  reader.reset(state, "[[0]]");
  ASSERT_TRUE(reader.next_token(state));
  EXPECT_FALSE(json::detail::validate_value<false>(reader, state, visit, 1));
  EXPECT_EQ(::hosq::detail::reader_error(reader, state).code,
            ErrorCode::depth_limit_exceeded);
  for (auto [text, code] : {std::pair{"tru", ErrorCode::invalid_token},
                            std::pair{"01", ErrorCode::invalid_number}}) {
    reader.reset(state, text);
    ASSERT_TRUE(reader.next_token(state));
    EXPECT_FALSE(json::detail::validate_value<false>(reader, state, visit));
    EXPECT_EQ(::hosq::detail::reader_error(reader, state).code, code);
  }
  reader.reset(state, "null");
  ASSERT_TRUE(reader.next_token(state));
  ::hosq::detail::report_reader_error(
      reader, state, {ErrorCode::validation_failed, 0, "prior failure"});
  bool visited = false;
  EXPECT_FALSE(json::detail::validate_value<true>(
      reader, state, [&](const auto&, auto) { visited = true; }));
  EXPECT_FALSE(visited);
  EXPECT_EQ(::hosq::detail::reader_error(reader, state).code,
            ErrorCode::validation_failed);
}
TEST(DomValidation, FastLimitsAndErrors) {
  limits_and_errors<json::FastReader<>>();
}
TEST(DomValidation, SimpleLimitsAndErrors) {
  limits_and_errors<json::SimpleReader>();
}

}  // namespace
