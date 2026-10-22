#include <gtest/gtest.h>

#include <string>
#include <type_traits>
#include <utility>

#include "hosq/bson/reader.hh"
#include "hosq/bson/writer.hh"
#include "hosq/hosq.hh"
#include "hosq/json/compact_writer.hh"
#include "hosq/json/fast_reader.hh"
#include "hosq/json/simple_reader.hh"
#include "hosq/msgpack/reader.hh"
#include "hosq/msgpack/writer.hh"
#include "hosq/toml/reader.hh"
#include "hosq/toml/writer.hh"

namespace {
using namespace hosq;
namespace v = hosq::types::value;
template <class S>
using Object = Elem<scan::object | decode::to<S> | store::assign, encode::value>;
template <class S>
using Array = Elem<scan::array | decode::to<S> | store::assign, encode::value>;
struct User {
  Entry<Key<"id">, v::Int64> id;
};
struct Group {
  Entry<Key<"user">, User> user;
};
struct Root {
  Entry<Key<"group">, Group> group;
};
struct Coordinates {
  v::Int64 x;
  v::Int64 y;
};
using UserField = Entry<Key<"user">, User>;
static_assert(std::same_as<UserField::value_type, Object<User>>);
static_assert(std::same_as<Entry<v::String, User>::key_type, v::String>);
static_assert(std::same_as<Entry<User, v::Int64>::value_type, v::Int64>);
static_assert(std::same_as<Entry<User, User>::key_type, Object<User>>);
static_assert(std::same_as<Entry<User, User>::value_type, Object<User>>);
static_assert(
    std::same_as<Entry<Key<"xy">, Coordinates>::value_type, Array<Coordinates>>);
static_assert(std::same_as<decltype(*std::declval<UserField&>()), User&>);
static_assert(
    std::same_as<decltype(*std::declval<const UserField&>()), const User&>);

template <class W, class T>
auto wire(const T& value) -> std::string {
  std::string output;
  W writer(output);
  EXPECT_TRUE(encode_into(value, writer)) << writer.error().detail;
  return output;
}
template <class R, class T>
auto read(T& value, std::string_view input) -> Error {
  return decode_document_into<R>(value, input);
}
template <class W, class R>
void check_nested_roundtrip(const Root& source) {
  Root decoded;
  ASSERT_TRUE(read<R>(decoded, wire<W>(source)).is_ok());
  EXPECT_EQ(*decoded.group->user->id, 42);
  const auto json = wire<json::CompactWriter<>>(decoded);
  EXPECT_EQ(json, R"({"group":{"user":{"id":42}}})");
  Root converted;
  ASSERT_TRUE(read<json::SimpleReader>(converted, json).is_ok());
  EXPECT_EQ(wire<W>(converted), wire<W>(source));
}
TEST(EntrySchema, ObjectStorageAndConstSameAndCrossFormatEncoding) {
  Root source;
  *source.group->user->id = 42;
  check_nested_roundtrip<json::CompactWriter<>, json::SimpleReader>(source);
  check_nested_roundtrip<bson::Writer<>, bson::Reader<>>(source);
  check_nested_roundtrip<msgpack::Writer<>, msgpack::Reader<>>(source);
  check_nested_roundtrip<toml::Writer<>, toml::Reader<>>(source);
  UserField direct;
  Entry<Key<"user">, Object<User>> explicit_pipeline;
  EXPECT_EQ(&*direct, &*direct);
  EXPECT_EQ(direct.operator->(), &*direct);
  EXPECT_EQ(&*std::as_const(direct), std::as_const(direct).operator->());
  *direct->id = 7;
  *explicit_pipeline->id = 7;
  struct Direct {
    UserField user;
  } a{direct};
  struct Explicit {
    Entry<Key<"user">, Object<User>> user;
  } b{explicit_pipeline};
  EXPECT_EQ(wire<json::CompactWriter<>>(a), wire<json::CompactWriter<>>(b));
}
TEST(EntrySchema, TupleShapedAggregateUsesArrayAdapter) {
  struct Document {
    Entry<Key<"xy">, Coordinates> xy;
  } document;
  ASSERT_TRUE(read<json::SimpleReader>(document, R"({"xy":[3,4]})").is_ok());
  EXPECT_EQ(document.xy->x.value(), 3);
  EXPECT_EQ(document.xy->y.value(), 4);
  const auto encoded = wire<msgpack::Writer<>>(std::as_const(document));
  decltype(document) round;
  ASSERT_TRUE(read<msgpack::Reader<>>(round, encoded).is_ok());
  EXPECT_EQ(wire<json::CompactWriter<>>(round), R"({"xy":[3,4]})");
  EXPECT_EQ(read<json::SimpleReader>(document, R"({"xy":{}})").code,
            ErrorCode::type_mismatch);
}
TEST(EntrySchema, MessagePackAggregateKeysAndValues) {
  struct Both {
    Entry<User, User> item;
  } both;
  const std::string input("\x81\x81\xa2id\x01\x81\xa2id\x02", 11);
  ASSERT_TRUE(read<msgpack::Reader<>>(both, input).is_ok());
  EXPECT_EQ(*both.item.key().id, 1);
  EXPECT_EQ(*both.item->id, 2);
  EXPECT_EQ(wire<msgpack::Writer<>>(std::as_const(both)), input);
  struct KeyOnly {
    Entry<User, v::Int64> item;
  } key_only;
  const std::string key_input("\x81\x81\xa2id\x01\x03", 7);
  ASSERT_TRUE(read<msgpack::Reader<>>(key_only, key_input).is_ok());
  EXPECT_EQ(*key_only.item.key().id, 1);
  EXPECT_EQ(*key_only.item, 3);
  EXPECT_EQ(wire<msgpack::Writer<>>(key_only), key_input);
  auto rejects_object_key = [&]<class W>() {
    std::string output;
    W writer(output);
    EXPECT_FALSE(encode_into(key_only, writer));
    EXPECT_FALSE(writer.error().is_ok());
  };
  rejects_object_key.template operator()<json::CompactWriter<>>();
  rejects_object_key.template operator()<bson::Writer<>>();
  rejects_object_key.template operator()<toml::Writer<>>();
}
TEST(EntrySchema, PreservesExplicitPipelineObjectPolicies) {
  struct Direct {
    Entry<Key<"user">, User> user;
  };
  struct Explicit {
    Entry<Key<"user">, Object<User>> user;
  };
  for (auto input :
       {R"({"user":{"id":7}})", R"({"user":{}})", R"({"user":{"other":7}})",
        R"({"user":{"id":7,"id":8}})", R"({"user":{"id":7,"other":8}})"}) {
    Direct direct;
    Explicit explicit_pipeline;
    *direct.user->id = 91;
    *explicit_pipeline.user->id = 91;
    const auto actual = read<json::SimpleReader>(direct, input);
    const auto expected = read<json::SimpleReader>(explicit_pipeline, input);
    EXPECT_EQ(actual.code, expected.code) << input;
    EXPECT_EQ(actual.offset, expected.offset) << input;
    EXPECT_EQ(actual.detail, expected.detail) << input;
    EXPECT_EQ(*direct.user->id, *explicit_pipeline.user->id) << input;
  }
}
TEST(EntrySchema, NestedShapeAndValidationFailuresPropagate) {
  struct Document {
    UserField user;
  } document;
  for (auto input :
       {R"({"user":[]})", R"({"user":1})", R"({"user":{"id":"x"}})"})
    EXPECT_EQ(read<json::SimpleReader>(document, input).code,
              ErrorCode::type_mismatch);
  using One = Elem<scan::scalar | decode::integer<int> | validate::match<1> |
                       store::assign,
                   encode::value>;
  struct Checked {
    Entry<Key<"n">, One> n;
  };
  struct CheckedRoot {
    Entry<Key<"checked">, Checked> checked;
  } checked;
  EXPECT_EQ(read<json::SimpleReader>(checked, R"({"checked":{"n":2}})").code,
            ErrorCode::invalid_choice);
  ASSERT_TRUE(
      read<json::SimpleReader>(checked, R"({"checked":{"n":1}})").is_ok());
}
template <class R>
void check_prefailed() {
  struct Document {
    UserField user;
  } document;
  *document.user->id = 91;
  R reader;
  typename R::state_type state;
  reader.reset(state, R"({"user":{"id":7}})");
  ASSERT_TRUE(reader.next_token(state));
  const Error original{ErrorCode::validation_failed, 17, "already failed"};
  reader.report_error(state, original);
  const auto position = state.pos;
  const auto token = reader.current_token(state);
  const auto error = Decoder<R>::decode_nested(document, reader, state);
  EXPECT_EQ(error.code, original.code);
  EXPECT_EQ(error.offset, original.offset);
  EXPECT_EQ(error.detail, original.detail);
  EXPECT_EQ(state.pos, position);
  EXPECT_EQ(reader.current_token(state).type, token.type);
  EXPECT_EQ(reader.current_token(state).offset, token.offset);
  EXPECT_EQ(*document.user->id, 91);
}
TEST(EntrySchema, PrefailedNestedDecodePreservesStorageAndReader) {
  check_prefailed<json::SimpleReader>();
  check_prefailed<json::FastReader<>>();
}
template <class R>
void check_try() {
  using AKey = Elem<scan::scalar | decode::string_view |
                        match::literal<StringLiteral{"a"}> | store::assign,
                    encode::value>;
  using BKey = Elem<scan::scalar | decode::string_view |
                        match::literal<StringLiteral{"b"}> | store::assign,
                    encode::value>;
  struct Document {
    Try<Entry<Key<"fixed">, User>> fixed;
    Try<Entry<AKey, User>> a;
    Try<Entry<BKey, User>> b;
  } document;
  ASSERT_TRUE(
      read<R>(document, R"({"b":{"id":3},"fixed":{"id":1},"a":{"id":2}})")
          .is_ok());
  EXPECT_EQ(*document.fixed->id, 1);
  EXPECT_EQ(document.a.key(), "a");
  EXPECT_EQ(*document.a->id, 2);
  EXPECT_EQ(document.b.key(), "b");
  EXPECT_EQ(*document.b->id, 3);
  EXPECT_EQ(read<R>(document, R"({"fixed":[]})").code, ErrorCode::type_mismatch);
}
TEST(EntrySchema, TryConstantAndMatchingDynamicKeyDecodeNestedValues) {
  check_try<json::SimpleReader>();
  check_try<json::FastReader<>>();
}
template <class R>
void check_matching_values() {
  using DynamicKey =
      Elem<scan::scalar | decode::string_view | store::assign, encode::value>;
  using OneUser =
      Elem<scan::object | decode::to<User> |
               match::predicate<[](const User& user) { return *user.id == 1; }> |
               store::assign,
           encode::value>;
  struct Document {
    Try<Entry<DynamicKey, OneUser>> one;
    Try<Entry<DynamicKey, User>> fallback;
  } document;
  ASSERT_TRUE(
      read<R>(document, R"({"other":{"id":2},"first":{"id":1}})").is_ok());
  EXPECT_EQ(document.one.key(), "first");
  EXPECT_EQ(*document.one->id, 1);
  EXPECT_EQ(document.fallback.key(), "other");
  EXPECT_EQ(*document.fallback->id, 2);
}
TEST(EntrySchema, DynamicMatchingValueFallsBackToDirectSchema) {
  check_matching_values<json::SimpleReader>();
  check_matching_values<json::FastReader<>>();
}
template <class R>
void check_nested_try() {
  using One = Elem<scan::scalar | decode::integer<int> | match::literal<1> |
                       store::assign,
                   encode::value>;
  using Two = Elem<scan::scalar | decode::integer<int> | match::literal<2> |
                       store::assign,
                   encode::value>;
  struct Choices {
    Try<One> one;
    Try<Two> two;
  };
  struct Document {
    Entry<Key<"choices">, Choices> choices;
  } document;
  ASSERT_TRUE(read<R>(document, R"({"choices":[2,1]})").is_ok());
  EXPECT_EQ(document.choices->one.value(), 1);
  EXPECT_EQ(document.choices->two.value(), 2);
  EXPECT_EQ(wire<json::CompactWriter<>>(std::as_const(document)),
            R"({"choices":[1,2]})");
}
TEST(EntrySchema, NestedTrySchemaRetainsMatchingDispatch) {
  check_nested_try<json::SimpleReader>();
  check_nested_try<json::FastReader<>>();
}
}  // namespace
