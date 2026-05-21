#include "hosq/pipeline/decode.hh"

#include <gtest/gtest.h>

#include <string_view>

#include "hosq/decoder.hh"
#include "hosq/entry.hh"
#include "hosq/json/simple_reader.hh"
#include "hosq/pipeline/match.hh"
#include "hosq/pipeline/scan.hh"
#include "hosq/pipeline/store.hh"
#include "hosq/pipeline_util.hh"
#include "hosq/string_literal.hh"
#include "hosq/try.hh"

using namespace std::string_view_literals;

namespace {

using Reader = hosq::json::SimpleReader;

template <hosq::StringLiteral Name>
using Key = hosq::Elem<hosq::scan::string | hosq::match::literal<Name>,
                       hosq::noop_encoder>;

using StringValue =
    hosq::Elem<hosq::scan::string | hosq::decode::string_view |
                   hosq::store::assign,
               hosq::noop_encoder>;

template <class T>
using NumberValue =
    hosq::Elem<hosq::scan::literal | hosq::decode::number<T> |
                   hosq::store::assign,
               hosq::noop_encoder>;

struct Person {
  hosq::Try<hosq::Entry<Key<"name">, StringValue>> name;
  hosq::Try<hosq::Entry<Key<"id">, NumberValue<int>>> id;
};

struct PersonWithAge {
  hosq::Try<hosq::Entry<Key<"name">, StringValue>> name;
  hosq::Try<hosq::Entry<Key<"age">, NumberValue<int>>> age;
};

struct OnlyId {
  hosq::Try<hosq::Entry<Key<"id">, NumberValue<int>>> id;
};

struct OnlyX {
  hosq::Try<hosq::Entry<Key<"x">, NumberValue<int>>> x;
};

struct OnlyV {
  hosq::Try<hosq::Entry<Key<"v">, NumberValue<int>>> v;
};

struct OnlyMsg {
  hosq::Try<hosq::Entry<Key<"msg">, StringValue>> msg;
};

struct OnlyRatio {
  hosq::Try<hosq::Entry<Key<"ratio">, NumberValue<double>>> ratio;
};

}  // namespace

TEST(DecodeJson, StringAndIntFields) {
  auto json = Person{};

  const auto result = hosq::decode_into<Reader>(json, R"({"name":"John","id":123})"sv);

  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(json.name.value().value(), "John"sv);
  EXPECT_EQ(json.id.value().value(), 123);
}

TEST(DecodeJson, FieldsInReverseOrder) {
  auto json = PersonWithAge{};

  const auto result =
      hosq::decode_into<Reader>(json, R"({"age":30,"name":"Alice"})"sv);

  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(json.name.value().value(), "Alice"sv);
  EXPECT_EQ(json.age.value().value(), 30);
}

TEST(DecodeJson, RejectsUnknownFields) {
  auto json = OnlyId{};

  const auto result =
      hosq::decode_into<Reader>(json, R"({"name":"ignored","id":42})"sv);

  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code, hosq::ErrorCode::unknown_member);
}

TEST(DecodeJson, WrongTopLevelTypeErrors) {
  auto json = OnlyV{};

  const auto result = hosq::decode_into<Reader>(json, "[1,2,3]"sv);

  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code, hosq::ErrorCode::type_mismatch);
}

TEST(DecodeJson, SingleStringField) {
  auto json = OnlyMsg{};

  const auto result =
      hosq::decode_into<Reader>(json, R"({"msg":"hello world"})"sv);

  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(json.msg.value().value(), "hello world"sv);
}

TEST(DecodeJson, FloatField) {
  auto json = OnlyRatio{};

  const auto result = hosq::decode_into<Reader>(json, R"({"ratio":3.14})"sv);

  ASSERT_TRUE(result.has_value());
  EXPECT_DOUBLE_EQ(json.ratio.value().value(), 3.14);
}
