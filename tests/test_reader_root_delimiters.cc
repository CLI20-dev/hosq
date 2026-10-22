#include <gtest/gtest.h>

#include <string_view>

#include "hosq/hosq.hh"
#include "hosq/json/dom.hh"
#include "hosq/json/simple_reader.hh"

namespace {
using namespace hosq;

TEST(SimpleReaderRootDelimiters, RootScalarLeavesSuffixForNextToken) {
  for (auto text : {"1]", "null}", "true,", "1 ]", "null\t}", "true\n,"}) {
    json::SimpleReader reader;
    json::SimpleReader::state_type state;
    reader.reset(state, text);
    ASSERT_TRUE(reader.next_token(state)) << text;
    EXPECT_EQ(reader.current_token(state).type, TokenType::Scalar);
    EXPECT_FALSE(reader.next_token(state)) << text;
    EXPECT_EQ(reader.error(state).code, ErrorCode::unexpected_token);
  }
}

TEST(SimpleReaderRootDelimiters, PrefixDecodingAndStrictDocuments) {
  for (auto text : {"1]", "1}", "1,", "1 ]", "1\t}", "1\n,"}) {
    types::value::Int<> value;
    EXPECT_TRUE(decode_into<json::SimpleReader>(value, text).is_ok()) << text;
    EXPECT_EQ(value.value(), 1);
    EXPECT_FALSE(decode_document_into<json::SimpleReader>(value, text).is_ok())
        << text;
  }
  for (auto text : {"null}", "null }"}) {
    types::value::Null value;
    EXPECT_TRUE(decode_into<json::SimpleReader>(value, text).is_ok()) << text;
    EXPECT_FALSE(decode_document_into<json::SimpleReader>(value, text).is_ok())
        << text;
  }
  for (auto text : {"true,", "true ,"}) {
    types::value::Bool value;
    EXPECT_TRUE(decode_into<json::SimpleReader>(value, text).is_ok()) << text;
    EXPECT_TRUE(value.value());
    EXPECT_FALSE(decode_document_into<json::SimpleReader>(value, text).is_ok())
        << text;
  }
}

TEST(SimpleReaderRootDelimiters, NestedAndCompleteDocumentsRemainValid) {
  types::value::Array<types::value::Int<>> array;
  EXPECT_TRUE(
      decode_document_into<json::SimpleReader>(array, "[1,2,3]").is_ok());
  EXPECT_EQ(array.value().size(), 3u);
  struct Record {
    types::Int<"x"> x;
  } record;
  EXPECT_TRUE(
      decode_document_into<json::SimpleReader>(record, "{\"x\":1}").is_ok());
  EXPECT_EQ(*record.x, 1);
  types::value::Int<> integer;
  EXPECT_TRUE(decode_document_into<json::SimpleReader>(integer, "1 \n").is_ok());
}

TEST(SimpleReaderRootDelimiters, DomAssignmentDiagnosticReplayIsSafe) {
  for (auto text : {"1]", "null}", "true,", "1 ]", "null\t}", "true\n,"}) {
    json::GenericDom<> eager;
    json::DeferredDom<> deferred;
    EXPECT_FALSE(eager.assign(text).is_ok()) << text;
    EXPECT_FALSE(deferred.assign(text).is_ok()) << text;
  }
}
}  // namespace
