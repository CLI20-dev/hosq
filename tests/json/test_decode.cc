#include <gtest/gtest.h>

#include <string_view>

#include "hosq/hosq.hh"
#include "reader_test_types.hh"

using namespace std::string_view_literals;
using namespace hosq;

namespace {

template <StringLiteral Name>
using Key = Elem<scan::scalar | decode::string_view | match::literal<Name>,
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

template <class Reader>
class DecodeJsonTest : public test::RuntimeReaderTest<Reader> {};

TYPED_TEST_SUITE(DecodeJsonTest, test::RuntimeReaderTypes);

TYPED_TEST(DecodeJsonTest, StringAndIntFields) {
  auto json = Person{};

  const auto result =
      decode_into<TypeParam>(json, R"({"name":"John","id":123})"sv);

  ASSERT_TRUE(result.is_ok());
  EXPECT_EQ(*json.name, "John"sv);
  EXPECT_EQ(*json.id, 123);
}

TYPED_TEST(DecodeJsonTest, FieldsInReverseOrder) {
  auto json = PersonWithAge{};

  const auto result =
      decode_into<TypeParam>(json, R"({"age":30,"name":"Alice"})"sv);

  ASSERT_TRUE(result.is_ok());
  EXPECT_EQ(*json.name, "Alice"sv);
  EXPECT_EQ(*json.age, 30);
}

TYPED_TEST(DecodeJsonTest, RejectsUnknownFields) {
  auto json = OnlyId{};

  const auto result =
      decode_into<TypeParam>(json, R"({"name":"ignored","id":42})"sv);

  ASSERT_FALSE(result.is_ok());
  EXPECT_EQ(result.code, ErrorCode::unknown_member);
  EXPECT_EQ(result.kind(), ErrorKind::schema);
  EXPECT_EQ(result.detail, "no try entry candidate matched object member"sv);
}

TYPED_TEST(DecodeJsonTest, WrongTopLevelTypeErrors) {
  auto json = OnlyV{};

  const auto result = decode_into<TypeParam>(json, "[1,2,3]"sv);

  ASSERT_FALSE(result.is_ok());
  EXPECT_EQ(result.code, ErrorCode::type_mismatch);
  EXPECT_EQ(result.detail, "expected object while decoding try entry tuple"sv);
}

TYPED_TEST(DecodeJsonTest, SingleStringField) {
  auto json = OnlyMsg{};

  const auto result = decode_into<TypeParam>(json, R"({"msg":"hello world"})"sv);

  ASSERT_TRUE(result.is_ok());
  EXPECT_EQ(*json.msg, "hello world"sv);
}

TYPED_TEST(DecodeJsonTest, FloatField) {
  auto json = OnlyRatio{};

  const auto result = decode_into<TypeParam>(json, R"({"ratio":3.14})"sv);

  ASSERT_TRUE(result.is_ok());
  EXPECT_DOUBLE_EQ(*json.ratio, 3.14);
}

TYPED_TEST(DecodeJsonTest, OnceRejectsDuplicateMembers) {
  auto json = OnceId{};

  const auto result = decode_into<TypeParam>(json, R"({"id":1,"id":2})"sv);

  ASSERT_FALSE(result.is_ok());
  EXPECT_EQ(result.code, ErrorCode::duplicate_member);
  EXPECT_EQ(result.detail, "store::once already has a value"sv);
  ASSERT_TRUE(json.id->has_value());
  EXPECT_EQ(**json.id, 1);
}

TYPED_TEST(DecodeJsonTest, FirstWinsKeepsFirstDuplicateMember) {
  auto json = FirstWinsId{};

  const auto result = decode_into<TypeParam>(json, R"({"id":1,"id":2})"sv);

  ASSERT_TRUE(result.is_ok());
  ASSERT_TRUE(json.id->has_value());
  EXPECT_EQ(**json.id, 1);
}
