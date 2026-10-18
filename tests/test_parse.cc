#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

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
namespace t = hosq::types;
struct Schema {
  t::Int8<"count"> count;
  t::Array<"items", v::Text> items;
  t::Optional<"note", v::Text> note;
};
using Record = v::Record<Schema>;
using IntegerResult = ParseResult<v::Int64>;
static_assert(std::same_as<IntegerResult::value_type, v::Int64>);
static_assert(
    std::same_as<decltype(std::declval<IntegerResult&>().value()), v::Int64&>);
static_assert(
    std::same_as<decltype(std::declval<const IntegerResult&>().value()),
                 const v::Int64&>);
static_assert(
    std::same_as<decltype(std::declval<IntegerResult&&>().value()), v::Int64&&>);
static_assert(std::same_as<decltype(*std::declval<const IntegerResult&&>()),
                           const v::Int64&&>);
static_assert(!std::is_convertible_v<IntegerResult, bool>);
static_assert(!std::is_default_constructible_v<IntegerResult>);

template <class R>
void check_json_success_and_errors() {
  auto result = parse<R, Record>(R"({"items":["a","b"],"count":7,"note":""})");
  ASSERT_TRUE(result) << result.error().detail;
  EXPECT_TRUE(result.has_value());
  EXPECT_TRUE(result.error().is_ok());
  EXPECT_EQ(result->value().count.value().value(), 7);
  EXPECT_EQ((*result).value().items.value().value(),
            (std::vector<std::string>{"a", "b"}));
  ASSERT_TRUE(result->value().note.value().value());
  EXPECT_TRUE(result->value().note.value().value()->empty());
  const auto& const_result = result;
  EXPECT_EQ(const_result->value().count.value().value(), 7);
  EXPECT_EQ(const_result.value().value().count.value().value(), 7);

  struct Failure {
    std::string_view input;
    ErrorCode code;
  };
  for (const auto& failure : {
           Failure{R"({"count":1,"items":[]} true)", ErrorCode::trailing_data},
           Failure{R"({"count":1,"items":{}})", ErrorCode::type_mismatch},
           Failure{R"({"count":"1","items":[]})", ErrorCode::type_mismatch},
           Failure{R"({"count":1})", ErrorCode::missing_required},
           Failure{R"({"count":128,"items":[]})", ErrorCode::out_of_range},
           Failure{R"({"count":1,"items":[],"other":2})",
                   ErrorCode::unknown_member},
           Failure{R"({"count":1,"items":[],"count":2})",
                   ErrorCode::duplicate_member},
       }) {
    SCOPED_TRACE(failure.input);
    auto failed = parse<R, Record>(failure.input);
    EXPECT_FALSE(failed);
    EXPECT_FALSE(failed.has_value());
    EXPECT_EQ(failed.error().code, failure.code);
    EXPECT_THROW((void)failed.value(), std::bad_variant_access);
    EXPECT_THROW((void)*failed, std::bad_variant_access);
    EXPECT_THROW((void)failed.operator->(), std::bad_variant_access);
    EXPECT_THROW((void)std::move(failed).value(), std::bad_variant_access);
    const auto& const_failed = failed;
    EXPECT_THROW((void)const_failed.value(), std::bad_variant_access);
    EXPECT_THROW((void)const_failed.operator->(), std::bad_variant_access);
  }
  // A separate failed parse cannot partially change an earlier result.
  EXPECT_EQ(result->value().count.value().value(), 7);
}
TEST(Parse, JsonReadersPublishOnlyFullyValidatedValues) {
  check_json_success_and_errors<json::SimpleReader>();
  check_json_success_and_errors<json::FastReader<>>();
}

TEST(Parse, ExplicitScalarArrayAndAggregateTargets) {
  auto integer = parse<json::FastReader<>, v::Int64>("42 \n\t");
  ASSERT_TRUE(integer);
  EXPECT_EQ(integer->value(), 42);
  auto array = parse<json::FastReader<>, v::Array<v::Int8>>("[1,2]");
  ASSERT_TRUE(array);
  EXPECT_EQ(array->value(), (std::vector<std::int8_t>{1, 2}));
  auto bad_array = parse<json::FastReader<>, v::Array<v::Int8>>("[1,128]");
  EXPECT_FALSE(bad_array);
  EXPECT_EQ(bad_array.error().code, ErrorCode::out_of_range);
  auto wrong_array = parse<json::FastReader<>, v::Array<v::Int8>>("{}");
  EXPECT_FALSE(wrong_array);
  EXPECT_EQ(wrong_array.error().code, ErrorCode::type_mismatch);
  // Existing direct aggregate schemas retain their legacy decoding rules.
  struct Pair {
    v::Int64 number;
    v::Text text;
  };
  auto aggregate = parse<json::FastReader<>, Pair>(R"([42,"answer"])");
  ASSERT_TRUE(aggregate);
  EXPECT_EQ(aggregate->number.value(), 42);
  EXPECT_EQ(aggregate->text.value(), "answer");
}

template <class R>
void check_soft_match_contract() {
  using MatchOne = Elem<scan::scalar | decode::integer<int> | match::literal<1> |
                            store::assign,
                        encode::value>;
  // Direct roots and legacy aggregate fields preserve decode_document's
  // dispatch-predicate contract: soft rejection is not a hard error.
  auto root = parse<R, MatchOne>("2");
  ASSERT_TRUE(root);
  EXPECT_EQ(root->value(), 0);
  struct LegacyAggregate {
    MatchOne matched;
    v::Int64 number;
  };
  auto aggregate = parse<R, LegacyAggregate>("[2,7]");
  ASSERT_TRUE(aggregate);
  EXPECT_EQ(aggregate->matched.value(), 0);
  EXPECT_EQ(aggregate->number.value(), 7);

  using ValidateOne = Elem<scan::scalar | decode::integer<int> |
                               validate::match<1> | store::assign,
                           encode::value>;
  auto validated = parse<R, ValidateOne>("2");
  EXPECT_FALSE(validated);
  EXPECT_EQ(validated.error().code, ErrorCode::invalid_choice);
  auto typed_array = parse<R, v::Array<MatchOne>>("[2]");
  EXPECT_FALSE(typed_array);
  EXPECT_EQ(typed_array.error().code, ErrorCode::candidate_rejected);
}
TEST(Parse, SoftMatchAndHardValidationRetainDocumentSemantics) {
  check_soft_match_contract<json::SimpleReader>();
  check_soft_match_contract<json::FastReader<>>();
}

TEST(Parse, ValidationAndMalformedSuffixErrorsArePreserved) {
  using Bounded = Elem<scan::scalar | decode::integer<int> |
                           validate::range<1, 10> | store::assign,
                       encode::value>;
  auto valid = parse<json::FastReader<>, Bounded>("5");
  ASSERT_TRUE(valid);
  EXPECT_EQ(valid->value(), 5);
  auto failed = parse<json::FastReader<>, Bounded>("11");
  EXPECT_FALSE(failed);
  EXPECT_EQ(failed.error().code, ErrorCode::out_of_range);
  EXPECT_EQ(failed.error().offset, 0u);
  EXPECT_EQ(failed.error().detail, "validate::range value outside bounds");
  auto malformed = parse<json::FastReader<>, v::Int64>("5 [");
  EXPECT_FALSE(malformed);
  EXPECT_TRUE(malformed.error().has_error());
}

template <class R, class W>
void check_format() {
  Record original;
  original.value().count.value().value() = 5;
  original.value().items.value().value() = {"one", "two"};
  std::string input;
  W writer(input);
  ASSERT_TRUE(encode_into(original, writer)) << writer.error().detail;
  auto result = parse<R, Record>(input);
  ASSERT_TRUE(result) << result.error().detail;
  EXPECT_EQ(result->value().count.value().value(), 5);
  EXPECT_EQ(result->value().items.value().value(),
            (std::vector<std::string>{"one", "two"}));
  EXPECT_FALSE(result->value().note.value().value());
}
TEST(Parse, CompleteObjectDocumentsAcrossAllFourFormats) {
  check_format<json::FastReader<>, json::CompactWriter<>>();
  check_format<msgpack::Reader<>, msgpack::Writer<>>();
  check_format<bson::Reader<>, bson::Writer<>>();
  check_format<toml::Reader<>, toml::Writer<>>();
}

TEST(Parse, FormatRootAndTrailingInputRulesArePreserved) {
  const std::string messagepack("\x01\x02", 2);
  auto trailing = parse<msgpack::Reader<>, v::Int64>(messagepack);
  EXPECT_FALSE(trailing);
  EXPECT_EQ(trailing.error().code, ErrorCode::trailing_data);
  const std::string bson("\x05\0\0\0\0", 5);
  auto bson_trailing =
      parse<bson::Reader<>, v::Record<std::tuple<>>>(bson + bson);
  EXPECT_FALSE(bson_trailing);
  EXPECT_EQ(bson_trailing.error().code, ErrorCode::trailing_data);
  auto bson_scalar = parse<bson::Reader<>, v::Int64>(bson);
  EXPECT_FALSE(bson_scalar);
  EXPECT_EQ(bson_scalar.error().code, ErrorCode::type_mismatch);
  auto toml_scalar = parse<toml::Reader<>, v::Int64>("value=1");
  EXPECT_FALSE(toml_scalar);
  EXPECT_EQ(toml_scalar.error().code, ErrorCode::type_mismatch);
  auto toml_suffix = parse<toml::Reader<>, v::Record<std::tuple<>>>("[]");
  EXPECT_FALSE(toml_suffix);
}

TEST(Parse, DecoderReuseStartsFreshAndErrorsRemainIndependent) {
  Decoder<json::FastReader<>> decoder;
  auto first = decoder.parse<Record>(R"({"count":3,"items":[],"note":"set"})");
  ASSERT_TRUE(first);
  ASSERT_TRUE(first->value().note.value().value());
  auto failed = decoder.parse<Record>(R"({"count":7,"items":[]} true)");
  ASSERT_FALSE(failed);
  const Error saved = failed.error();
  auto copied_failure = failed;
  EXPECT_EQ(saved.code, ErrorCode::trailing_data);
  EXPECT_EQ(saved.offset, 23u);
  auto next = decoder.parse<Record>(R"({"count":4,"items":[]})");
  ASSERT_TRUE(next);
  EXPECT_FALSE(next->value().note.value().value());
  EXPECT_EQ(first->value().count.value().value(), 3);
  EXPECT_EQ(failed.error().code, saved.code);
  EXPECT_EQ(copied_failure.error().offset, saved.offset);
  EXPECT_EQ(copied_failure.error().detail, saved.detail);
  auto copied_success = first;
  copied_success->value().count.value().value() = 6;
  EXPECT_EQ(first->value().count.value().value(), 3);
}

int callback_effects = 0;
using MoveOnly = Elem<scan::scalar | decode::integer<int> |
                          action<int, std::unique_ptr<int>,
                                 [](int value) {
                                   ++callback_effects;
                                   return std::make_unique<int>(value);
                                 }> |
                          store::assign,
                      noop_encoder>;
static_assert(std::move_constructible<ParseResult<MoveOnly>>);
static_assert(!std::copy_constructible<ParseResult<MoveOnly>>);
TEST(Parse, MoveOnlyStorageAndExternalCallbackEffects) {
  callback_effects = 0;
  auto result = parse<json::FastReader<>, MoveOnly>("42");
  ASSERT_TRUE(result);
  ASSERT_TRUE(result->value());
  EXPECT_EQ(*result->value(), 42);
  auto moved = std::move(result);
  ASSERT_TRUE(moved);
  EXPECT_EQ(*moved->value(), 42);
  auto value = std::move(moved).value();
  ASSERT_TRUE(value.value());
  EXPECT_EQ(*value.value(), 42);
  EXPECT_TRUE(moved.has_value());
  EXPECT_FALSE(moved->value());
  auto failed = parse<json::FastReader<>, MoveOnly>("7 true");
  EXPECT_FALSE(failed);
  EXPECT_EQ(failed.error().code, ErrorCode::trailing_data);
  EXPECT_EQ(callback_effects, 2);  // Callback effects are not rolled back.
}

TEST(Parse, RawTextRemainsBorrowedAndOwnedTextSurvivesInputChanges) {
  Decoder<json::FastReader<>> decoder;
  std::string input = R"("hello")";
  auto raw = decoder.parse<v::RawText>(input);
  auto owned = decoder.parse<v::Text>(input);
  ASSERT_TRUE(raw);
  ASSERT_TRUE(owned);
  EXPECT_EQ(raw->value().value.data(), input.data() + 1);
  EXPECT_EQ(raw->value().format, Format::json);
  input[1] = 'H';
  EXPECT_EQ(raw->value().value, "Hello");
  EXPECT_EQ(owned->value(), "hello");
  auto next = decoder.parse<v::RawText>(R"("other")");
  ASSERT_TRUE(next);
  EXPECT_EQ(raw->value().value, "Hello");
}

TEST(Parse, LegacyDecodeContractsRemainAvailable) {
  Decoder<json::FastReader<>> decoder;
  v::Int64 value;
  EXPECT_TRUE(decoder.decode(value, "1 true").is_ok());
  EXPECT_EQ(value.value(), 1);
  auto error = decoder.decode_document(value, "2 true");
  EXPECT_EQ(error.code, ErrorCode::trailing_data);
  EXPECT_EQ(value.value(), 2);  // Existing destination APIs can update storage.
  auto result = decoder.parse<v::Int64>("3 true");
  EXPECT_FALSE(result);
  EXPECT_EQ(value.value(), 2);
}
}  // namespace
