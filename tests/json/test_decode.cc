#include <gtest/gtest.h>

#include <string_view>

#include "hosq/hosq.hh"
#include "hosq/json/simple_reader.hh"

using namespace std::string_view_literals;
using namespace hosq;

namespace {

using Reader = json::SimpleReader;

template <StringLiteral Name>
using Key =
    Elem<scan::scalar | decode::string_view | match::literal<Name>,
         noop_encoder>;

using StringValue =
    Elem<scan::scalar | decode::string_view | store::assign, noop_encoder>;

using StringOnceValue =
    Elem<scan::scalar | decode::string_view | store::once, noop_encoder>;

template <class T>
using NumberValue =
    Elem<scan::scalar | decode::number<T> | store::assign, noop_encoder>;

template <class T>
using NumberOnceValue =
    Elem<scan::scalar | decode::number<T> | store::once, noop_encoder>;

template <class T>
using NumberFirstWinsValue =
    Elem<scan::scalar | decode::number<T> | store::first_wins, noop_encoder>;

struct Person {
  Try<Entry<Key<"name">, StringValue>> name;
  Try<Entry<Key<"id">, NumberValue<int>>> id;
};

struct PersonWithAge {
  Try<Entry<Key<"name">, StringValue>> name;
  Try<Entry<Key<"age">, NumberValue<int>>> age;
};

struct OnlyId {
  Try<Entry<Key<"id">, NumberValue<int>>> id;
};

struct OnlyX {
  Try<Entry<Key<"x">, NumberValue<int>>> x;
};

struct OnlyV {
  Try<Entry<Key<"v">, NumberValue<int>>> v;
};

struct OnlyMsg {
  Try<Entry<Key<"msg">, StringValue>> msg;
};

struct OnlyRatio {
  Try<Entry<Key<"ratio">, NumberValue<double>>> ratio;
};

struct OnceId {
  Try<Entry<Key<"id">, NumberOnceValue<int>>> id;
};

struct FirstWinsId {
  Try<Entry<Key<"id">, NumberFirstWinsValue<int>>> id;
};

}  // namespace

TEST(DecodeJson, StringAndIntFields) {
  auto json = Person{};

  const auto result =
      decode_into<Reader>(json, R"({"name":"John","id":123})"sv);

  ASSERT_TRUE(result.is_ok());
  EXPECT_EQ(json.name.value().value(), "John"sv);
  EXPECT_EQ(json.id.value().value(), 123);
}

TEST(DecodeJson, FieldsInReverseOrder) {
  auto json = PersonWithAge{};

  const auto result =
      decode_into<Reader>(json, R"({"age":30,"name":"Alice"})"sv);

  ASSERT_TRUE(result.is_ok());
  EXPECT_EQ(json.name.value().value(), "Alice"sv);
  EXPECT_EQ(json.age.value().value(), 30);
}

TEST(DecodeJson, RejectsUnknownFields) {
  auto json = OnlyId{};

  const auto result =
      decode_into<Reader>(json, R"({"name":"ignored","id":42})"sv);

  ASSERT_FALSE(result.is_ok());
  EXPECT_EQ(result.code, ErrorCode::unknown_member);
}

TEST(DecodeJson, WrongTopLevelTypeErrors) {
  auto json = OnlyV{};

  const auto result = decode_into<Reader>(json, "[1,2,3]"sv);

  ASSERT_FALSE(result.is_ok());
  EXPECT_EQ(result.code, ErrorCode::type_mismatch);
}

TEST(DecodeJson, SingleStringField) {
  auto json = OnlyMsg{};

  const auto result =
      decode_into<Reader>(json, R"({"msg":"hello world"})"sv);

  ASSERT_TRUE(result.is_ok());
  EXPECT_EQ(json.msg.value().value(), "hello world"sv);
}

TEST(DecodeJson, FloatField) {
  auto json = OnlyRatio{};

  const auto result = decode_into<Reader>(json, R"({"ratio":3.14})"sv);

  ASSERT_TRUE(result.is_ok());
  EXPECT_DOUBLE_EQ(json.ratio.value().value(), 3.14);
}

TEST(DecodeJson, OnceRejectsDuplicateMembers) {
  auto json = OnceId{};

  const auto result = decode_into<Reader>(json, R"({"id":1,"id":2})"sv);

  ASSERT_FALSE(result.is_ok());
  EXPECT_EQ(result.code, ErrorCode::duplicate_member);
  ASSERT_TRUE(json.id.value().value().has_value());
  EXPECT_EQ(*json.id.value().value(), 1);
}

TEST(DecodeJson, FirstWinsKeepsFirstDuplicateMember) {
  auto json = FirstWinsId{};

  const auto result = decode_into<Reader>(json, R"({"id":1,"id":2})"sv);

  ASSERT_TRUE(result.is_ok());
  ASSERT_TRUE(json.id.value().value().has_value());
  EXPECT_EQ(*json.id.value().value(), 1);
}
