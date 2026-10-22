#include <gtest/gtest.h>

#include <array>
#include <concepts>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
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
constexpr auto integer = scan::scalar | decode::integer<int>;
constexpr auto one = integer | match::literal<1>;
constexpr auto key = scan::scalar | decode::string_view;
constexpr auto key_a =
    key | match::predicate<[](std::string_view text) { return text == "a"; }>;
template <auto P>
using Value = Elem<P | store::assign, encode::value>;
using Plain = Value<scan::array | decode::collect<integer>>;
using Dynamic = Value<scan::array | decode::collect<one>>;
using Fixed = Value<scan::array | decode::collect<one, 2>>;
using Object = Value<scan::object | decode::collect<key, one>>;
using FixedObject = Value<scan::object | decode::collect<key_a, integer, 2>>;
using BothObject = Value<scan::object | decode::collect<key_a, one>>;
using PlainObject = Value<scan::object | decode::collect<key, integer>>;
using Nested =
    Value<scan::array | decode::collect<scan::array | decode::collect<one>>>;
template <auto P>
using Terminal = detail::pipeline_terminal_t<
    detail::pipeline_analysis_t<detail::PipelineVerificationMode::no_storage,
                                detail::PipelineDecodeAnalysisContext, void,
                                std::remove_cvref_t<decltype(P)>>>;
static_assert(std::same_as<Terminal<scan::array | decode::collect<one>>,
                           MatchResult<std::vector<int>>>);
static_assert(std::same_as<Terminal<scan::array | decode::collect<one, 2>>,
                           MatchResult<std::array<int, 2>>>);
static_assert(
    std::same_as<
        Terminal<scan::object | decode::collect<key, one>>,
        MatchResult<Map<std::vector<std::string_view>, std::vector<int>>>>);
static_assert(
    std::same_as<
        Terminal<scan::object | decode::collect<key_a, integer, 2>>,
        MatchResult<Map<std::array<std::string_view, 2>, std::array<int, 2>>>>);
static_assert(std::same_as<Dynamic::decoder_terminal, MatchResult<void>>);
static_assert(std::same_as<Fixed::decoder_terminal, MatchResult<void>>);
static_assert(
    std::same_as<std::remove_cvref_t<decltype(std::declval<Dynamic&>().value())>,
                 std::vector<int>>);
static_assert(
    std::same_as<std::remove_cvref_t<decltype(std::declval<Fixed&>().value())>,
                 std::array<int, 2>>);
static_assert(
    std::same_as<std::remove_cvref_t<decltype(std::declval<Object&>().value())>,
                 Map<std::vector<std::string_view>, std::vector<int>>>);
static_assert(
    std::same_as<
        std::remove_cvref_t<decltype(std::declval<FixedObject&>().value())>,
        Map<std::array<std::string_view, 2>, std::array<int, 2>>>);
static_assert(std::same_as<Plain::decoder_terminal, void>);

template <class Candidate, class Fallback>
struct Choices {
  Try<Candidate> first;
  Try<Fallback> fallback;
};

struct FalsePayload {
  auto operator()(int) const -> bool { return false; }
};
struct RejectWithError {
  template <class C>
  auto operator()(int, C& ctx) const -> bool {
    ctx.reader().report_error(
        ctx.reader_state(),
        {ErrorCode::validation_failed, 17, "collect recorded rejection"});
    return false;
  }
};

struct OwnedPayload {
  auto operator()(int n) const -> std::unique_ptr<int> {
    return std::make_unique<int>(n);
  }
};
struct FalseKey {
  auto operator()(std::string_view) const -> bool { return false; }
};

template <class R>
void payload_cases() {
  using Owned = Value<scan::array | decode::collect<one | step<OwnedPayload>>>;
  Choices<Owned, Plain> owned;
  ASSERT_TRUE(decode_document_into<R>(owned, "[[1,1]]").is_ok());
  ASSERT_EQ(owned.first.value().size(), 2U);
  EXPECT_EQ(*owned.first.value()[0], 1);
  EXPECT_EQ(*owned.first.value()[1], 1);
  Choices<Owned, Plain> rejected;
  ASSERT_TRUE(decode_document_into<R>(rejected, "[[1,2]]").is_ok());
  EXPECT_EQ(rejected.fallback.value(), (std::vector<int>{1, 2}));
  using BoolKeys =
      Value<scan::object | decode::collect<key | step<FalseKey>, one>>;
  Choices<BoolKeys, PlainObject> bool_keys;
  ASSERT_TRUE(decode_document_into<R>(bool_keys, "[{\"a\":1}]").is_ok());
  EXPECT_EQ(bool_keys.first.value().keys(), (std::vector<bool>{false}));
  EXPECT_EQ(bool_keys.first.value().values(), (std::vector<int>{1}));
  using BoolValues =
      Value<scan::object |
            decode::collect<key_a, integer | step<FalsePayload>, 1>>;
  Choices<BoolValues, PlainObject> bool_values;
  ASSERT_TRUE(decode_document_into<R>(bool_values, "[{\"a\":1}]").is_ok());
  EXPECT_FALSE(bool_values.first.value().values()[0]);
  EXPECT_TRUE(bool_values.fallback.value().values().empty());
}
TEST(CollectMatching, MoveOnlyPayloadsAndMixedBoolObjectOperands) {
  payload_cases<json::SimpleReader>();
  payload_cases<json::FastReader<>>();
}

template <class R, class C>
void arrays() {
  Choices<C, Plain> accepted;
  ASSERT_TRUE(decode_document_into<R>(accepted, "[[1,1]]").is_ok());
  EXPECT_EQ(accepted.first.value()[0], 1);
  EXPECT_EQ(accepted.first.value()[1], 1);
  EXPECT_TRUE(accepted.fallback.value().empty());
  Choices<C, Plain> rejected;
  const auto rejected_error = decode_document_into<R>(rejected, "[[1,2]]");
  ASSERT_TRUE(rejected_error.is_ok()) << rejected_error.detail;
  EXPECT_EQ(rejected.fallback.value(), (std::vector<int>{1, 2}));
}
template <class R>
void json_cases() {
  SCOPED_TRACE(
      (std::same_as<R, json::SimpleReader> ? "SimpleReader" : "FastReader"));
  arrays<R, Dynamic>();
  arrays<R, Fixed>();
  Choices<Nested, Value<scan::array |
                        decode::collect<scan::array | decode::collect<integer>>>>
      nested;
  ASSERT_TRUE(decode_document_into<R>(nested, "[[[1,1],[1,2]]]").is_ok());
  EXPECT_EQ(nested.fallback.value(),
            (std::vector<std::vector<int>>{{1, 1}, {1, 2}}));
  for (const auto input : {"[[1]]", "[[1,1,1]]"}) {
    Choices<Fixed, Plain> value;
    EXPECT_EQ(decode_document_into<R>(value, input).code,
              ErrorCode::invalid_length);
    EXPECT_TRUE(value.fallback.value().empty());
  }
  using Empty = Value<scan::array | decode::collect<one, 0>>;
  Choices<Empty, Plain> empty;
  EXPECT_TRUE(decode_document_into<R>(empty, "[[]]").is_ok());
  EXPECT_EQ(decode_document_into<R>(empty, "[[1]]").code,
            ErrorCode::invalid_length);
  for (const auto input :
       {"[[1,\"x\"]]", "[[1,]", "[[1,999999999999999999999999999999]]"}) {
    Choices<Dynamic, Plain> value;
    const auto error = decode_document_into<R>(value, input);
    EXPECT_FALSE(error.is_ok());
    EXPECT_TRUE(value.fallback.value().empty());
  }
  using Validated =
      Value<scan::array |
            decode::collect<integer | validate::min<1> | match::literal<1>>>;
  Choices<Validated, Plain> validation;
  EXPECT_EQ(decode_document_into<R>(validation, "[[1,0]]").code,
            ErrorCode::out_of_range);
  EXPECT_TRUE(validation.fallback.value().empty());
  using Hard =
      Value<scan::array |
            decode::collect<integer | match::predicate<RejectWithError{}>>>;
  Choices<Hard, Plain> hard;
  const auto hard_error = decode_document_into<R>(hard, "[[1,2]]");
  EXPECT_EQ(hard_error.code, ErrorCode::validation_failed);
  EXPECT_EQ(hard_error.offset, 17U);
  EXPECT_EQ(hard_error.detail, "collect recorded rejection");
  EXPECT_TRUE(hard.fallback.value().empty());
  using FalseData =
      Value<scan::array | decode::collect<one | step<FalsePayload>>>;
  Choices<FalseData, Plain> false_data;
  ASSERT_TRUE(decode_document_into<R>(false_data, "[[1,1]]").is_ok());
  EXPECT_EQ(false_data.first.value(), (std::vector<bool>{false, false}));
  EXPECT_TRUE(false_data.fallback.value().empty());
  using Bools =
      Value<scan::array | decode::collect<scan::scalar | decode::typed_boolean>>;
  Bools booleans;
  ASSERT_TRUE(decode_document_into<R>(booleans, "[false,true]").is_ok());
  EXPECT_EQ(booleans.value(), (std::vector<bool>{false, true}));
}
TEST(CollectMatching, JsonArraysAndNestedEffects) {
  json_cases<json::SimpleReader>();
  json_cases<json::FastReader<>>();
}

template <class R, class Candidate>
void objects(std::string_view input, bool match) {
  Choices<Candidate, PlainObject> value;
  const auto error = decode_document_into<R>(value, input);
  ASSERT_TRUE(error.is_ok()) << error.detail;
  if (match) {
    EXPECT_EQ(value.first.value().values().size(), 2U);
    EXPECT_EQ(value.first.value().values()[0], 1);
    EXPECT_TRUE(value.fallback.value().values().empty());
  } else {
    EXPECT_EQ(value.fallback.value().keys().size(), 2U);
    EXPECT_EQ(value.fallback.value().values(), (std::vector<int>{1, 2}));
  }
}
template <class R>
void object_cases() {
  SCOPED_TRACE(
      (std::same_as<R, json::SimpleReader> ? "SimpleReader" : "FastReader"));
  objects<R, Object>("[{\"a\":1,\"b\":1}]", true);
  objects<R, Object>("[{\"a\":1,\"b\":2}]", false);
  objects<R, FixedObject>("[{\"a\":1,\"a\":1}]", true);
  objects<R, FixedObject>("[{\"a\":1,\"b\":2}]", false);
  objects<R, BothObject>("[{\"a\":1,\"a\":1}]", true);
  objects<R, BothObject>("[{\"a\":1,\"a\":2}]", false);
  for (const auto input : {"[{\"a\":1}]", "[{\"a\":1,\"a\":1,\"a\":1}]"}) {
    Choices<FixedObject, PlainObject> value;
    EXPECT_EQ(decode_document_into<R>(value, input).code,
              ErrorCode::invalid_length);
    EXPECT_TRUE(value.fallback.value().values().empty());
  }
  using EmptyObject = Value<scan::object | decode::collect<key_a, one, 0>>;
  Choices<EmptyObject, PlainObject> empty;
  EXPECT_TRUE(decode_document_into<R>(empty, "[{}]").is_ok());
  EXPECT_EQ(decode_document_into<R>(empty, "[{\"a\":1}]").code,
            ErrorCode::invalid_length);
}
TEST(CollectMatching, JsonObjectsMatchKeysValuesAndBoth) {
  object_cases<json::SimpleReader>();
  object_cases<json::FastReader<>>();
}

template <class R, class W>
void roundtrip() {
  struct Input {
    Entry<Key<"items">, Plain> items;
  } input;
  struct Output {
    Try<Entry<Value<key | match::predicate<[](std::string_view text) {
                      return text == "items";
                    }>>,
              Dynamic>>
        first;
    Try<Entry<Value<key>, Plain>> fallback;
  };
  for (const auto& values : {std::vector<int>{1, 1}, std::vector<int>{1, 2}}) {
    *input.items = values;
    std::string encoded;
    W writer(encoded);
    ASSERT_TRUE(encode_into(input, writer));
    Output output;
    ASSERT_TRUE(decode_document_into<R>(output, encoded).is_ok());
    if (values[1] == 1)
      EXPECT_EQ(*output.first, values);
    else
      EXPECT_EQ(*output.fallback, values);
  }
}
TEST(CollectMatching, ObjectRootRoundtripsAcrossFormats) {
  roundtrip<json::SimpleReader, json::CompactWriter<>>();
  roundtrip<json::FastReader<>, json::CompactWriter<>>();
  roundtrip<msgpack::Reader<>, msgpack::Writer<>>();
  roundtrip<bson::Reader<>, bson::Writer<>>();
  roundtrip<toml::Reader<>, toml::Writer<>>();
}
}  // namespace
