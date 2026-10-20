#include <gtest/gtest.h>

#include <string>

#include "hosq/bson/reader.hh"
#include "hosq/hosq.hh"
#include "hosq/json/fast_reader.hh"
#include "hosq/json/simple_reader.hh"
#include "hosq/msgpack/reader.hh"
#include "hosq/toml/reader.hh"

namespace {
using namespace hosq;

TEST(TypedDocument, TopLevelRejectsTrailingValuesAcrossFormats) {
  types::value::Int<> integer;
  EXPECT_FALSE(
      decode_document_into<json::SimpleReader>(integer, "1 true").is_ok());
  EXPECT_FALSE(
      decode_document_into<json::FastReader<>>(integer, "1 true").is_ok());
  EXPECT_FALSE(decode_document_into<msgpack::Reader<>>(
                   integer, std::string("\x01\x02", 2))
                   .is_ok());
  struct Document {
    types::Int<"v"> v;
  } object;
  const std::string bson("\x0c\0\0\0\x10v\0\x01\0\0\0\0", 12);
  ASSERT_TRUE(decode_document_into<bson::Reader<>>(object, bson).is_ok());
  EXPECT_FALSE(
      decode_document_into<bson::Reader<>>(object, bson + bson).is_ok());
  EXPECT_FALSE(decode_document_into<toml::Reader<>>(object, "[]").is_ok());
}

TEST(TypedDocument, CompleteScalarAndNestedContainersConsumeOneDocument) {
  types::value::Int<> integer;
  EXPECT_TRUE(
      decode_document_into<json::SimpleReader>(integer, "1 \n\t").is_ok());
  EXPECT_TRUE(
      decode_document_into<json::FastReader<>>(integer, "1 \n\t").is_ok());
  EXPECT_TRUE(
      decode_document_into<msgpack::Reader<>>(integer, std::string("\x01", 1))
          .is_ok());
  types::value::Array<types::value::Array<types::value::Int<>>> arrays;
  ASSERT_TRUE(
      decode_document_into<json::SimpleReader>(arrays, "[[1],[2,3]]").is_ok());
  EXPECT_EQ(arrays.value().size(), 2u);
  EXPECT_FALSE(
      decode_document_into<json::SimpleReader>(arrays, "[[1]] []").is_ok());
}

TEST(TypedDocument, DecodeNestedLeavesFollowingArrayElementAvailable) {
  json::SimpleReader reader;
  json::SimpleReader::state_type state;
  reader.reset(state, "[1,2]");
  ASSERT_TRUE(reader.next_token(state));
  ASSERT_TRUE(reader.next_token(state));
  types::value::Int<> first;
  ASSERT_TRUE(
      Decoder<json::SimpleReader>::decode_nested(first, reader, state).is_ok());
  EXPECT_EQ(first.value(), 1);
  ASSERT_TRUE(reader.next_token(state));
  types::value::Int<> second;
  ASSERT_TRUE(
      Decoder<json::SimpleReader>::decode_nested(second, reader, state).is_ok());
  EXPECT_EQ(second.value(), 2);
  ASSERT_TRUE(reader.next_token(state));
  EXPECT_EQ(reader.current_token(state).type, TokenType::ArrayEnd);
}
}  // namespace
