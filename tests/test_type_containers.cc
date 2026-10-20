#include <gtest/gtest.h>

#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "hosq/bson/reader.hh"
#include "hosq/bson/writer.hh"
#include "hosq/hosq.hh"
#include "hosq/json/compact_writer.hh"
#include "hosq/json/simple_reader.hh"
#include "hosq/msgpack/reader.hh"
#include "hosq/msgpack/writer.hh"
#include "hosq/toml/reader.hh"
#include "hosq/toml/writer.hh"
#include "hosq/types.hh"

namespace {
using namespace hosq;
namespace t = hosq::types::value;
using EC = ErrorCode;
template <class R, class E>
auto read(E& value, std::string_view input) -> Error {
  Decoder<R> decoder;
  return decoder.decode(value, input);
}
template <class W, class E>
auto write(const E& value, Error* error = nullptr) -> std::string {
  std::string output;
  W writer(output);
  const bool ok = encode_into(value, writer);
  if (error)
    *error = writer.error();
  else
    EXPECT_TRUE(ok) << writer.error().detail;
  return output;
}
struct Child {
  Entry<Key<"n">, t::Int<int>> n;
};
struct Schema {
  Entry<Key<"title">, t::Text> title;
  Entry<Key<"matrix">, t::Array<t::Array<t::Int<int>>>> matrix;
  Entry<Key<"child">, t::Object<Child>> child;
  Try<Entry<Key<"note">, t::Optional<t::Text>>> note;
};
using Record = t::Record<Schema>;
static_assert(detail::ElemT<t::Array<t::Int<int>>>);
static_assert(std::same_as<typename t::Array<t::Int<int>>::storage_type,
                           std::vector<int>>);
static_assert(
    std::same_as<typename t::Mapping<t::Text, t::Int<int>>::storage_type,
                 MappingValue<std::string, int>>);
static_assert(std::same_as<t::Object<Schema>, Record>);

TEST(TypeContainers, NestedArrayStrictShapeAndReset) {
  t::Array<t::Array<t::Int<int>>> value;
  ASSERT_TRUE(read<json::SimpleReader>(value, "[[1,2],[],[3]]").is_ok());
  EXPECT_EQ(value.value(), (std::vector<std::vector<int>>{{1, 2}, {}, {3}}));
  ASSERT_TRUE(read<json::SimpleReader>(value, "[]").is_ok());
  EXPECT_TRUE(value.value().empty());
  for (auto input : {"{}", "null", "1", "\"text\"", "[1]"})
    EXPECT_EQ(read<json::SimpleReader>(value, input).code, EC::type_mismatch);
  t::Mapping<t::Text, t::Int<int>> map;
  EXPECT_EQ(read<json::SimpleReader>(map, "[]").code, EC::type_mismatch);
}
TEST(TypeContainers, RecordUnorderedRequiredUnknownAndDuplicate) {
  Record value;
  ASSERT_TRUE(read<json::SimpleReader>(
                  value, R"({"child":{"n":3},"matrix":[[1,2]],"title":"t"})")
                  .is_ok());
  EXPECT_EQ(value.value().title.value().value(), "t");
  EXPECT_EQ(value.value().child.value().value().n.value().value(), 3);
  EXPECT_FALSE(value.value().note.value().value());
  EXPECT_EQ(read<json::SimpleReader>(value, R"({"title":"t","matrix":[]})").code,
            EC::missing_required);
  EXPECT_EQ(
      read<json::SimpleReader>(
          value, R"({"title":"t","matrix":[],"child":{"n":1},"unknown":2})")
          .code,
      EC::unknown_member);
  EXPECT_EQ(
      read<json::SimpleReader>(
          value, R"({"title":"t","matrix":[],"child":{"n":1},"title":"dup"})")
          .code,
      EC::duplicate_member);
  EXPECT_EQ(
      read<json::SimpleReader>(
          value,
          R"({"title":"t","matrix":[],"child":{"n":1},"t\u0069tle":"dup"})")
          .code,
      EC::duplicate_member);
  EXPECT_EQ(read<json::SimpleReader>(value, "[]").code, EC::type_mismatch);
}
TEST(TypeContainers, MissingNullAndEmptyOptionalAreDifferent) {
  struct S {
    Entry<Key<"null">, t::Null> null;
    Try<Entry<Key<"optional">, t::Optional<t::Text>>> optional;
  };
  t::Record<S> value;
  ASSERT_TRUE(read<json::SimpleReader>(value, R"({"null":null})").is_ok());
  EXPECT_FALSE(value.value().optional.value().value());
  EXPECT_EQ(write<json::CompactWriter<>>(value), R"({"null":null})");
  EXPECT_EQ(read<json::SimpleReader>(value, "{}").code, EC::missing_required);
  EXPECT_EQ(
      read<json::SimpleReader>(value, R"({"null":null,"optional":null})").code,
      EC::type_mismatch);
  ASSERT_TRUE(
      read<json::SimpleReader>(value, R"({"null":null,"optional":""})").is_ok());
  ASSERT_TRUE(value.value().optional.value().value());
  EXPECT_EQ(*value.value().optional.value().value(), "");
  EXPECT_EQ(write<json::CompactWriter<>>(value),
            R"({"null":null,"optional":""})");
  t::Optional<t::Text> absent;
  Error error;
  (void)write<msgpack::Writer<>>(absent, &error);
  EXPECT_EQ(error.code, EC::invalid_object_state);
  t::Optional<t::Null> null;
  ASSERT_TRUE(read<json::SimpleReader>(null, "null").is_ok());
  EXPECT_TRUE(null.value().has_value());
  EXPECT_EQ(write<json::CompactWriter<>>(null), "null");
  ASSERT_TRUE(read<json::SimpleReader>(value, R"({"null":null})").is_ok());
  EXPECT_FALSE(value.value().optional.value().value());
  struct RequiredOptional {
    Entry<Key<"x">, t::Optional<t::Text>> x;
  };
  t::Record<RequiredOptional> required;
  EXPECT_EQ(read<json::SimpleReader>(required, "{}").code, EC::missing_required);
  (void)write<msgpack::Writer<>>(required, &error);
  EXPECT_EQ(error.code, EC::invalid_object_state);
}
TEST(TypeContainers, MappingDuplicatesAndPairedMutations) {
  t::Mapping<t::Text, t::Int<int>> value;
  ASSERT_TRUE(read<json::SimpleReader>(value, R"({"x":1,"y":2})").is_ok());
  EXPECT_EQ(value.value().entries(),
            (std::vector<std::pair<std::string, int>>{{"x", 1}, {"y", 2}}));
  EXPECT_EQ(read<json::SimpleReader>(value, R"({"x":1,"\u0078":2})").code,
            EC::duplicate_member);
  t::Mapping<t::RawText, t::Int<int>> raw;
  EXPECT_EQ(read<json::SimpleReader>(raw, R"({"x":1,"\u0078":2})").code,
            EC::duplicate_member);
  value.value().entries() = {{"x", 1}, {"x", 2}};
  Error error;
  (void)write<msgpack::Writer<>>(value, &error);
  EXPECT_EQ(error.code, EC::duplicate_member);
  // The legacy JSON writer has no report_error and collapses semantic errors
  // to invalid_state through encode_error; failure still must be observable.
  (void)write<json::CompactWriter<>>(value, &error);
  EXPECT_FALSE(error.is_ok());
  value.value().entries().erase(value.value().begin());
  value.value().emplace_back("y", 3);
  std::swap(value.value().entries()[0], value.value().entries()[1]);
  EXPECT_EQ(write<json::CompactWriter<>>(value), R"({"y":3,"x":2})");
  decltype(value) decoded;
  ASSERT_TRUE(
      read<msgpack::Reader<>>(decoded, write<msgpack::Writer<>>(value)).is_ok());
  EXPECT_EQ(decoded.value().entries(), value.value().entries());
  raw.value().entries() = {{{"x", Format::json}, 1},
                           {{"\\u0078", Format::json}, 2}};
  (void)write<msgpack::Writer<>>(raw, &error);
  EXPECT_EQ(error.code, EC::duplicate_member);
  t::Mapping<t::Int<int>, t::Int<int>> numeric;
  EXPECT_EQ(read<msgpack::Reader<>>(numeric,
                                    std::string("\x82\x01\x02\xcc\x01\x03", 6))
                .code,
            EC::duplicate_member);
}
TEST(TypeContainers, MessagePackComplexKeysCompareSemanticValues) {
  t::Mapping<t::Array<t::Int<int>>, t::Text> arrays;
  auto input = std::string("\x82\x91\x01\xa1", 4) + "a" +
               std::string("\x91\xcc\x01\xa1", 4) + "b";
  EXPECT_EQ(read<msgpack::Reader<>>(arrays, input).code, EC::duplicate_member);
  t::Mapping<t::Mapping<t::Text, t::Int<int>>, t::Int<int>> objects;
  input = std::string("\x82\x82\xa1", 3) + "a" + std::string("\x01\xa1", 2) +
          "b" + std::string("\x02\x03\x82\xa1", 4) + "b" +
          std::string("\x02\xa1", 2) + "a" + std::string("\x01\x04", 2);
  EXPECT_EQ(read<msgpack::Reader<>>(objects, input).code, EC::duplicate_member);
  t::Mapping<t::Object<Child>, t::Int<int>> records;
  input = std::string("\x82\x81\xa1", 3) + "n" +
          std::string("\x01\x02\x81\xa1", 4) + "n" + std::string("\x01\x03", 2);
  EXPECT_EQ(read<msgpack::Reader<>>(records, input).code, EC::duplicate_member);
}
TEST(TypeContainers, MappingKeysEncodeWithSemanticEquality) {
  t::Mapping<t::Mapping<t::Text, t::Int<int>>, t::Int<int>> objects;
  MappingValue<std::string, int> key;
  key.entries() = {{"a", 1}, {"b", 2}};
  objects.value().emplace_back(key, 3);
  std::swap(key.entries()[0], key.entries()[1]);
  objects.value().emplace_back(key, 4);
  Error error;
  (void)write<msgpack::Writer<>>(objects, &error);
  EXPECT_EQ(error.code, EC::duplicate_member);

  objects.value().entries()[1].first.entries()[0].second = 5;
  const auto encoded = write<msgpack::Writer<>>(objects);
  decltype(objects) decoded;
  ASSERT_TRUE(read<msgpack::Reader<>>(decoded, encoded).is_ok());
  ASSERT_EQ(decoded.value().size(), 2u);
  EXPECT_EQ(decoded.value().entries()[0].first.entries(),
            objects.value().entries()[0].first.entries());
  EXPECT_EQ(decoded.value().entries()[1].first.entries(),
            objects.value().entries()[1].first.entries());
}
template <class W, class R>
void roundtrip(const Record& original) {
  const auto encoded = write<W>(original);
  Record decoded;
  const auto error = read<R>(decoded, encoded);
  ASSERT_TRUE(error.is_ok()) << error.detail;
  EXPECT_EQ(decoded.value().title.value().value(), "hello\nworld");
  EXPECT_EQ(decoded.value().matrix.value().value(),
            (std::vector<std::vector<int>>{{1, 2}, {}, {3}}));
  EXPECT_EQ(decoded.value().child.value().value().n.value().value(), 7);
  ASSERT_TRUE(decoded.value().note.value().value());
  EXPECT_EQ(*decoded.value().note.value().value(), "note");
  const auto json = write<json::CompactWriter<>>(decoded);
  Record converted;
  ASSERT_TRUE(read<json::SimpleReader>(converted, json).is_ok());
  EXPECT_EQ(converted.value().title.value().value(), "hello\nworld");
}
TEST(TypeContainers, SameAndCrossFormatNestedRoundTrips) {
  Record original;
  original.value().title.value().value() = "hello\nworld";
  original.value().matrix.value().value() = {{1, 2}, {}, {3}};
  original.value().child.value().value().n.value().value() = 7;
  original.value().note.value().value() = "note";
  roundtrip<json::CompactWriter<>, json::SimpleReader>(original);
  roundtrip<bson::Writer<>, bson::Reader<>>(original);
  roundtrip<msgpack::Writer<>, msgpack::Reader<>>(original);
  roundtrip<toml::Writer<>, toml::Reader<>>(original);
}
TEST(TypeContainers, TupleEmptySchemasAndEscapedTomlKeys) {
  using S = std::tuple<Entry<Key<"x">, t::Int<int>>,
                       Try<Entry<Key<"y">, t::Optional<t::Text>>>>;
  t::Record<S> tuple;
  ASSERT_TRUE(read<toml::Reader<>>(tuple, "\"\\u0078\"=42").is_ok());
  EXPECT_EQ(std::get<0>(tuple.value()).value().value(), 42);
  EXPECT_EQ(write<json::CompactWriter<>>(tuple), R"({"x":42})");
  struct Empty {};
  t::Object<Empty> empty;
  EXPECT_TRUE(read<json::SimpleReader>(empty, "{}").is_ok());
  EXPECT_EQ(read<json::SimpleReader>(empty, R"({"x":1})").code,
            EC::unknown_member);
  EXPECT_EQ(write<json::CompactWriter<>>(empty), "{}");
}
TEST(TypeContainers, CodecRootAndKeyRestrictionsApply) {
  t::Array<t::Int<int>> array;
  array.value() = {1, 2};
  Error error;
  (void)write<bson::Writer<>>(array, &error);
  EXPECT_FALSE(error.is_ok());
  (void)write<toml::Writer<>>(array, &error);
  EXPECT_FALSE(error.is_ok());
  t::Mapping<t::Int<int>, t::Text> map;
  map.value().entries() = {{1, "x"}};
  (void)write<json::CompactWriter<>>(map, &error);
  EXPECT_FALSE(error.is_ok());
  (void)write<bson::Writer<>>(map, &error);
  EXPECT_FALSE(error.is_ok());
  (void)write<toml::Writer<>>(map, &error);
  EXPECT_FALSE(error.is_ok());
  auto encoded = write<msgpack::Writer<>>(map);
  decltype(map) decoded;
  ASSERT_TRUE(read<msgpack::Reader<>>(decoded, encoded).is_ok());
  EXPECT_EQ(decoded.value().entries(), map.value().entries());
}
TEST(TypeContainers, BoolVectorProxiesEncodeAndCompare) {
  t::Array<t::Bool> array;
  array.value() = {true, false, true};
  EXPECT_EQ(write<json::CompactWriter<>>(array), "[true,false,true]");
  const auto encoded = write<msgpack::Writer<>>(array);
  decltype(array) decoded;
  ASSERT_TRUE(read<msgpack::Reader<>>(decoded, encoded).is_ok());
  EXPECT_EQ(decoded.value(), array.value());
  t::Mapping<t::Bool, t::Bool> map;
  map.value().entries() = {{true, false}, {false, true}};
  const auto mapping = write<msgpack::Writer<>>(map);
  decltype(map) decoded_map;
  ASSERT_TRUE(read<msgpack::Reader<>>(decoded_map, mapping).is_ok());
  EXPECT_EQ(decoded_map.value().entries(), map.value().entries());
  const std::string duplicate("\x82\xc3\xc2\xc3\xc3", 5);
  EXPECT_EQ(read<msgpack::Reader<>>(decoded_map, duplicate).code,
            EC::duplicate_member);
  map.value().entries()[1].first = true;
  Error error;
  (void)write<msgpack::Writer<>>(map, &error);
  EXPECT_EQ(error.code, EC::duplicate_member);
}
TEST(TypeContainers, ReaderOnlyNestedElementsCannotDisappearDuringEncoding) {
  using ReaderOnly =
      Elem<scan::scalar | decode::integer<int> | store::assign, noop_encoder>;
  t::Array<ReaderOnly> array;
  array.value() = {42};
  Error error;
  (void)write<msgpack::Writer<>>(array, &error);
  EXPECT_EQ(error.code, EC::unsupported_type);
  t::Mapping<t::Text, ReaderOnly> map;
  map.value().entries() = {{"x", 42}};
  (void)write<msgpack::Writer<>>(map, &error);
  EXPECT_EQ(error.code, EC::unsupported_type);
  struct S {
    Entry<Key<"x">, ReaderOnly> x;
  };
  t::Record<S> record;
  record.value().x.value().value() = 42;
  (void)write<msgpack::Writer<>>(record, &error);
  EXPECT_EQ(error.code, EC::unsupported_type);
}
TEST(TypeContainers, ValidationFailuresCannotBecomeDefaultValues) {
  using One = Elem<scan::scalar | decode::integer<int> | validate::match<1> |
                       store::assign,
                   encode::value>;
  t::Array<One> array;
  EXPECT_TRUE(read<json::SimpleReader>(array, "[1,1]").is_ok());
  EXPECT_EQ(array.value(), (std::vector<int>{1, 1}));
  EXPECT_EQ(read<json::SimpleReader>(array, "[1,2]").code, EC::invalid_choice);
  t::Mapping<t::Text, One> map;
  EXPECT_EQ(read<json::SimpleReader>(map, R"({"x":2})").code,
            EC::invalid_choice);
  struct S {
    Try<Entry<Key<"x">, One>> x;
  };
  t::Record<S> record;
  EXPECT_TRUE(read<json::SimpleReader>(record, "{}").is_ok());
  EXPECT_EQ(read<json::SimpleReader>(record, R"({"x":2})").code,
            EC::invalid_choice);
}
TEST(TypeContainers, MappingStorageSupportsValueEncoderAcrossFormats) {
  using Custom =
      Elem<decode::mapping<t::Text, t::Int64> | store::assign, encode::value>;
  Custom root;
  ASSERT_TRUE(read<json::SimpleReader>(root, R"({"x":42,"y":43})").is_ok());
  Custom round;
  ASSERT_TRUE(
      read<msgpack::Reader<>>(round, write<msgpack::Writer<>>(root)).is_ok());
  EXPECT_EQ(write<json::CompactWriter<>>(round), R"({"x":42,"y":43})");
}

}  // namespace
