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
  t::Array<"items", v::String> items;
};
// Ordinary object schemas consume required entries in declaration order.
static_assert(detail::RequiredDecoderAcceptableT<Schema>);
// Presence/candidate traversal uses the existing all-Try/Optional aggregate.
struct PresenceSchema {
  Try<t::Int8<"count">> count;
  Try<t::Array<"items", v::String>> items;
  t::Optional<"note", v::String> note;
};
static_assert(detail::RequiredDecoderAcceptableT<PresenceSchema>);
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
static_assert(!std::is_constructible_v<IntegerResult, Error>);
static_assert(!std::is_constructible_v<IntegerResult, v::Int64&&>);

template <class R>
void check_json_success_and_errors() {
  auto result = parse<R, Schema>(R"({"count":7,"items":["a","b"]})");
  ASSERT_TRUE(result) << result.error().detail;
  EXPECT_TRUE(result.has_value());
  EXPECT_TRUE(result.error().is_ok());
  EXPECT_EQ(result->count.value().value(), 7);
  EXPECT_EQ((*result).items.value().value(),
            (std::vector<std::string>{"a", "b"}));
  const auto& const_result = result;
  EXPECT_EQ(const_result->count.value().value(), 7);
  EXPECT_EQ(const_result.value().count.value().value(), 7);

  struct Failure {
    std::string_view input;
  };
  for (const auto& failure : {
           Failure{R"({"count":1,"items":[]} true)"},
           Failure{R"({"count":1,"items":{}})"},
           Failure{R"({"count":"1","items":[]})"},
           Failure{R"({"count":128,"items":[]})"},
       }) {
    SCOPED_TRACE(failure.input);
    auto failed = parse<R, Schema>(failure.input);
    EXPECT_FALSE(failed);
    EXPECT_FALSE(failed.has_value());
    Schema destination;
    const auto expected = decode_document_into<R>(destination, failure.input);
    EXPECT_EQ(failed.error().code, expected.code);
    EXPECT_EQ(failed.error().offset, expected.offset);
    EXPECT_EQ(failed.error().detail, expected.detail);
    EXPECT_EQ(failed.error().path, expected.path);
    EXPECT_THROW((void)failed.value(), std::bad_variant_access);
    EXPECT_THROW((void)*failed, std::bad_variant_access);
    EXPECT_THROW((void)failed.operator->(), std::bad_variant_access);
    EXPECT_THROW((void)std::move(failed).value(), std::bad_variant_access);
    const auto& const_failed = failed;
    EXPECT_THROW((void)const_failed.value(), std::bad_variant_access);
    EXPECT_THROW((void)const_failed.operator->(), std::bad_variant_access);
  }
  // A separate failed parse cannot partially change an earlier result.
  EXPECT_EQ(result->count.value().value(), 7);
}
TEST(Parse, JsonReadersPublishOnlyFullyValidatedValues) {
  check_json_success_and_errors<json::SimpleReader>();
  check_json_success_and_errors<json::FastReader<>>();
}

template <class R>
void check_document_parity() {
  struct Ordered {
    t::Int8<"count"> count;
    t::Array<"items", v::String> items;
  };
  for (const auto input :
       {R"({"count":1,"items":[]})", R"({"items":[],"count":1})",
        R"({"count":1})", R"({"count":1,"items":[],"count":2})",
        R"({"count":1,"items":[],"other":2})"}) {
    SCOPED_TRACE(input);
    Ordered destination;
    const auto expected = decode_document_into<R>(destination, input);
    const auto result = parse<R, Ordered>(input);
    EXPECT_EQ(result.has_value(), expected.is_ok());
    EXPECT_EQ(result.error().code, expected.code);
    EXPECT_EQ(result.error().offset, expected.offset);
    EXPECT_EQ(result.error().detail, expected.detail);
    EXPECT_EQ(result.error().path, expected.path);
    if (result) {
      EXPECT_EQ(result->count.value().value(),
                destination.count.value().value());
      EXPECT_EQ(result->items.value().value(),
                destination.items.value().value());
    }
  }
}
TEST(Parse, OrdinaryEntryAggregatesFollowDocumentDeclarationOrderSemantics) {
  check_document_parity<json::SimpleReader>();
  check_document_parity<json::FastReader<>>();
}

TEST(Parse, DiagnosticPathsAndOptOutMatchDocumentDecoding) {
  constexpr auto input = R"({"count":1,"items":["ok",3]})";
  Decoder<json::FastReader<>> decoder;
  Schema destination;
  const auto expected = decoder.decode_document(destination, input);
  const auto result = decoder.parse<Schema>(input);
  ASSERT_FALSE(result);
  EXPECT_FALSE(expected.path.empty());
  EXPECT_EQ(result.error().code, expected.code);
  EXPECT_EQ(result.error().offset, expected.offset);
  EXPECT_EQ(result.error().detail, expected.detail);
  EXPECT_EQ(result.error().path, expected.path);
  const DecodeOptions options{.error_path = false};
  const auto plain = decoder.parse<Schema>(input, options);
  const auto free_plain = parse<json::FastReader<>, Schema>(input, options);
  for (const auto& error : {plain.error(), free_plain.error()}) {
    EXPECT_EQ(error.code, expected.code);
    EXPECT_EQ(error.offset, expected.offset);
    EXPECT_EQ(error.detail, expected.detail);
    EXPECT_TRUE(error.path.empty());
  }
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
  // Direct aggregate schemas decode their elements in declaration order.
  struct Pair {
    v::Int64 number;
    v::String text;
  };
  auto aggregate = parse<json::FastReader<>, Pair>(R"([42,"answer"])");
  ASSERT_TRUE(aggregate);
  EXPECT_EQ(aggregate->number.value(), 42);
  EXPECT_EQ(aggregate->text.value(), "answer");
}

using MatchOne =
    Elem<scan::scalar | decode::integer<int> | match::literal<1> | store::assign,
         encode::value>;
struct RequiredMatch {
  MatchOne value;
};
struct NamedMatch {
  Entry<Key<"n">, MatchOne> value;
};
struct Choices {
  Try<MatchOne> first;
  Try<v::Int64> fallback;
};
template <class T>
concept FreeParsable =
    requires(std::string_view input) { parse<json::FastReader<>, T>(input); };
template <class T>
concept MemberParsable =
    requires(Decoder<json::FastReader<>>& decoder, std::string_view input) {
      decoder.template parse<T>(input);
    };
static_assert(!FreeParsable<MatchOne> && !MemberParsable<MatchOne>);
static_assert(!FreeParsable<RequiredMatch> && !MemberParsable<RequiredMatch>);
static_assert(!FreeParsable<NamedMatch> && !MemberParsable<NamedMatch>);
static_assert(FreeParsable<Choices> && MemberParsable<Choices>);
static_assert(!std::is_default_constructible_v<MatchResult<int>>);
static_assert(!std::is_constructible_v<MatchResult<int>, bool, int>);

template <class R>
void check_match_contract() {
  auto matched = parse<R, Choices>("[1,2]");
  ASSERT_TRUE(matched);
  EXPECT_EQ(matched->first.value(), 1);
  EXPECT_EQ(matched->fallback.value(), 2);
  auto rejected = parse<R, Choices>("[2]");
  ASSERT_TRUE(rejected);
  EXPECT_EQ(rejected->first.value(), 0);
  EXPECT_EQ(rejected->fallback.value(), 2);
  // A hard error must not become a soft rejection followed by fallback.
  auto failed = parse<R, Choices>(R"(["wrong"])");
  Choices destination;
  const auto error = decode_document_into<R>(destination, R"(["wrong"])");
  ASSERT_TRUE(error);
  ASSERT_FALSE(failed);
  EXPECT_EQ(failed.error().code, error.code);
  EXPECT_EQ(failed.error().offset, error.offset);
  EXPECT_EQ(failed.error().detail, error.detail);
  EXPECT_EQ(failed.error().path, error.path);
  using ValidateOne = Elem<scan::scalar | decode::integer<int> |
                               validate::match<1> | store::assign,
                           encode::value>;
  auto validated = parse<R, ValidateOne>("2");
  EXPECT_FALSE(validated);
  EXPECT_EQ(validated.error().code, ErrorCode::invalid_choice);
}
TEST(Parse, MatchingIsConfinedToTryAndPreservesHardErrors) {
  check_match_contract<json::SimpleReader>();
  check_match_contract<json::FastReader<>>();
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
  Schema original;
  original.count.value().value() = 5;
  original.items.value().value() = {"one", "two"};
  std::string input;
  W writer(input);
  ASSERT_TRUE(encode_into(original, writer)) << writer.error().detail;
  auto result = parse<R, Schema>(input);
  ASSERT_TRUE(result) << result.error().detail;
  EXPECT_EQ(result->count.value().value(), 5);
  EXPECT_EQ(result->items.value().value(),
            (std::vector<std::string>{"one", "two"}));
}
TEST(Parse, CompleteObjectDocumentsAcrossAllFourFormats) {
  check_format<json::FastReader<>, json::CompactWriter<>>();
  check_format<msgpack::Reader<>, msgpack::Writer<>>();
  check_format<bson::Reader<>, bson::Writer<>>();
  check_format<toml::Reader<>, toml::Writer<>>();
}

TEST(Parse, FormatRootAndTrailingInputRulesArePreserved) {
  struct EmptyObject {
    t::Optional<"unused", v::Int64> unused;
  };
  const std::string messagepack("\x01\x02", 2);
  auto trailing = parse<msgpack::Reader<>, v::Int64>(messagepack);
  EXPECT_FALSE(trailing);
  EXPECT_EQ(trailing.error().code, ErrorCode::trailing_data);
  const std::string bson("\x05\0\0\0\0", 5);
  auto bson_trailing = parse<bson::Reader<>, EmptyObject>(bson + bson);
  EXPECT_FALSE(bson_trailing);
  EXPECT_EQ(bson_trailing.error().code, ErrorCode::trailing_data);
  auto bson_scalar = parse<bson::Reader<>, v::Int64>(bson);
  EXPECT_FALSE(bson_scalar);
  EXPECT_EQ(bson_scalar.error().code, ErrorCode::type_mismatch);
  auto toml_scalar = parse<toml::Reader<>, v::Int64>("value=1");
  EXPECT_FALSE(toml_scalar);
  EXPECT_EQ(toml_scalar.error().code, ErrorCode::type_mismatch);
  auto toml_suffix = parse<toml::Reader<>, EmptyObject>("[]");
  EXPECT_FALSE(toml_suffix);
}

TEST(Parse, DecoderReuseStartsFreshAndErrorsRemainIndependent) {
  Decoder<json::FastReader<>> decoder;
  auto first =
      decoder.parse<PresenceSchema>(R"({"count":3,"items":[],"note":"set"})");
  ASSERT_TRUE(first);
  ASSERT_TRUE(first->note.value().value());
  auto failed = decoder.parse<PresenceSchema>(R"({"count":7,"items":[]} true)");
  ASSERT_FALSE(failed);
  const Error saved = failed.error();
  auto copied_failure = failed;
  EXPECT_EQ(saved.code, ErrorCode::trailing_data);
  EXPECT_EQ(saved.offset, 23u);
  auto next = decoder.parse<PresenceSchema>(R"({"count":4,"items":[]})");
  ASSERT_TRUE(next);
  EXPECT_FALSE(next->note.value().value());
  EXPECT_EQ(first->count.value().value(), 3);
  EXPECT_EQ(failed.error().code, saved.code);
  EXPECT_EQ(copied_failure.error().offset, saved.offset);
  EXPECT_EQ(copied_failure.error().detail, saved.detail);
  auto copied_success = first;
  copied_success->count.value().value() = 6;
  EXPECT_EQ(first->count.value().value(), 3);
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

using BorrowedString =
    Elem<scan::scalar | decode::string_view | store::assign, noop_encoder>;
TEST(Parse, BorrowedStringAndOwnedStringRetainTheirLifetimeContracts) {
  Decoder<json::FastReader<>> decoder;
  std::string input = R"("hello")";
  auto raw = decoder.parse<BorrowedString>(input);
  auto free_raw = parse<json::FastReader<>, BorrowedString>(input);
  auto owned = decoder.parse<v::String>(input);
  ASSERT_TRUE(raw);
  ASSERT_TRUE(free_raw);
  ASSERT_TRUE(owned);
  EXPECT_EQ(raw->value().data(), input.data() + 1);
  EXPECT_EQ(free_raw->value().data(), input.data() + 1);
  input[1] = 'H';
  EXPECT_EQ(raw->value(), "Hello");
  EXPECT_EQ(free_raw->value(), "Hello");
  EXPECT_EQ(owned->value(), "hello");
  auto next = decoder.parse<BorrowedString>(R"("other")");
  ASSERT_TRUE(next);
  EXPECT_EQ(raw->value(), "Hello");
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
