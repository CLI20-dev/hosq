#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <limits>
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
namespace t = hosq::types;
namespace v = hosq::types::value;
namespace check = hosq::validate;
using EC = hosq::ErrorCode;

constexpr auto even = [](const auto& value) { return value % 2 == 0; };
constexpr auto age_rules = check::range<0, 120> | check::predicate<even>;
using Age = v::Constrained<v::Int8, age_rules>;
using Name = v::Constrained<v::Text, check::non_empty | check::size_range<1, 8>>;
using Code = v::Constrained<v::Text, check::size<3>>;
using NamedAge = t::Constrained<"age", v::Int8, age_rules>;
static_assert(hosq::detail::ElemT<Age>);
static_assert(hosq::detail::EntryT<NamedAge>);
static_assert(std::same_as<NamedAge, t::Field<"age", Age>>);
static_assert(std::same_as<Age::storage_type, v::Int8::storage_type>);
static_assert(std::same_as<Name::storage_type, std::string>);
static_assert(sizeof(Age) == sizeof(v::Int8));
static_assert(sizeof(Name) == sizeof(v::Text));
static_assert(std::same_as<decltype(*std::declval<Age&>()), std::int8_t&>);
static_assert(
    std::same_as<decltype(std::declval<const NamedAge&>().value()), const Age&>);

template <class E, auto Rules>
concept CanConstrain = requires { typename v::Constrained<E, Rules>; };
static_assert(!CanConstrain<v::Int8, check::non_empty>);
static_assert(!CanConstrain<v::Text, check::range<0, 1>>);
static_assert(!CanConstrain<NamedAge, age_rules>);
static_assert(!CanConstrain<v::Int8, hosq::store::assign>);
static_assert(!CanConstrain<v::Int8, check::custom<even>>);
static_assert(
    !CanConstrain<v::Text, check::predicate<[](std::string&) { return true; }>>);

template <class E, class Reader = hosq::json::SimpleReader>
hosq::Error read_error(std::string_view input) {
  E value;
  return hosq::decode_document_into<Reader>(value, input);
}

template <class E>
void json_error(std::string_view input, EC expected) {
  EXPECT_EQ((read_error<E>(input)).code, expected) << input;
  EXPECT_EQ((read_error<E, hosq::json::FastReader<>>(input)).code, expected)
      << input;
}

TEST(ConstraintTypes, RangeAndPredicateComposeWithClosedBounds) {
  for (const auto& [input, expected] : {std::pair{"0", EC::ok},
                                        {"120", EC::ok},
                                        {"42", EC::ok},
                                        {"-1", EC::out_of_range},
                                        {"121", EC::out_of_range},
                                        {"43", EC::validation_failed},
                                        {"128", EC::out_of_range}})
    json_error<Age>(input, expected);
  using Bounds = v::Constrained<v::Int8, check::min<-2> | check::max<2>>;
  json_error<Bounds>("-2", EC::ok);
  json_error<Bounds>("2", EC::ok);
  json_error<Bounds>("-3", EC::out_of_range);
  json_error<Bounds>("3", EC::out_of_range);
}

TEST(ConstraintTypes, RetainsStrictScalarTypesAndOriginalFailure) {
  for (auto input : {"42.0", "4.2e1", R"("42")", "true", "null"})
    json_error<Age>(input, EC::type_mismatch);
  using Unsigned = v::Constrained<v::Uint8, check::range<0U, 255U>>;
  json_error<Unsigned>("-1", EC::out_of_range);
  json_error<Unsigned>("255", EC::ok);
  json_error<Unsigned>("256", EC::out_of_range);
  using Float = v::Constrained<v::Float32, check::range<-2, 2>>;
  json_error<Float>("1", EC::type_mismatch);
  json_error<Float>("1.5", EC::ok);
  json_error<Float>("0.1", EC::out_of_range);
  json_error<Name>("42", EC::type_mismatch);
}

TEST(ConstraintTypes, SizeAndNonEmptyCheckDecodedStorage) {
  json_error<Name>(R"("")", EC::validation_failed);
  json_error<Name>(R"("a")", EC::ok);
  json_error<Name>(R"("12345678")", EC::ok);
  json_error<Name>(R"("123456789")", EC::validation_failed);
  json_error<Code>(R"("ab")", EC::validation_failed);
  json_error<Code>(R"("abc")", EC::ok);
  json_error<Code>(R"("abcd")", EC::validation_failed);
  // UTF-8 byte size after unescaping, not the input spelling or character count.
  json_error<Code>(R"("\u65e5")", EC::ok);
  using Pair = v::Constrained<v::Array<Age>, check::size<2>>;
  json_error<Pair>("[0,120]", EC::ok);
  json_error<Pair>("[]", EC::validation_failed);
  json_error<Pair>("[0]", EC::validation_failed);
  json_error<Pair>("[0,2,4]", EC::validation_failed);
  json_error<Pair>("[0,121]", EC::out_of_range);
}

TEST(ConstraintTypes, ReuseReplacesStorageAndSkipsRulesOnDecodeFailure) {
  Age age;
  ASSERT_TRUE(
      hosq::decode_document_into<hosq::json::SimpleReader>(age, "42").is_ok());
  EXPECT_EQ(age.value(), 42);
  ASSERT_TRUE(
      hosq::decode_document_into<hosq::json::SimpleReader>(age, "120").is_ok());
  EXPECT_EQ(age.value(), 120);
  EXPECT_EQ(
      hosq::decode_document_into<hosq::json::SimpleReader>(age, "121").code,
      EC::out_of_range);
  // Constraints run after the underlying decoder; failed validation is not
  // transactional and may leave the newly decoded storage available.
  EXPECT_EQ(age.value(), 121);
  ASSERT_TRUE(
      hosq::decode_document_into<hosq::json::SimpleReader>(age, "0").is_ok());
  EXPECT_EQ(age.value(), 0);
}

inline int predicate_calls = 0;
constexpr auto counted = [](const std::int8_t&) {
  ++predicate_calls;
  return true;
};
TEST(ConstraintTypes, PredicateSeesConstStorageOnlyAfterSuccessfulDecode) {
  using Counted = v::Constrained<v::Int8, check::predicate<counted>>;
  predicate_calls = 0;
  json_error<Counted>("null", EC::type_mismatch);
  json_error<Counted>("128", EC::out_of_range);
  EXPECT_EQ(predicate_calls, 0);
  json_error<Counted>("42", EC::ok);
  EXPECT_EQ(predicate_calls, 2);
}

template <class Writer, class E>
void rejects_encoding(const E& value, EC expected) {
  std::string output;
  Writer writer(output);
  EXPECT_FALSE(hosq::encode_into(value, writer));
  EXPECT_EQ(writer.error().code, expected);
  EXPECT_TRUE(output.empty());
}

TEST(ConstraintTypes, MutableAndDefaultStorageAreRecheckedBeforeEncoding) {
  Age age;
  age.value() = 121;
  rejects_encoding<hosq::json::CompactWriter<>>(age, EC::invalid_token);
  rejects_encoding<hosq::msgpack::Writer<>>(age, EC::out_of_range);
  *age = 43;
  rejects_encoding<hosq::json::CompactWriter<>>(age, EC::invalid_token);
  Name name;
  rejects_encoding<hosq::json::CompactWriter<>>(name, EC::invalid_token);
  name.value() = "valid";
  std::string output;
  hosq::json::CompactWriter writer(output);
  ASSERT_TRUE(hosq::encode_into(name, writer));
  EXPECT_EQ(output, R"("valid")");
  name.value().clear();
  rejects_encoding<hosq::json::CompactWriter<>>(name, EC::invalid_token);
}

TEST(ConstraintTypes, EncodingPreservesExistingWriterErrorAndSkipsPredicate) {
  using Counted = v::Constrained<v::Int8, check::predicate<counted>>;
  Counted value;
  std::string output;
  hosq::msgpack::Writer writer(output);
  writer.report_error({EC::output_too_large, 7, "earlier error"});
  predicate_calls = 0;
  EXPECT_FALSE(hosq::encode_into(value, writer));
  EXPECT_EQ(writer.error().code, EC::output_too_large);
  EXPECT_EQ(writer.error().offset, 7U);
  EXPECT_EQ(writer.error().detail, "earlier error");
  EXPECT_EQ(predicate_calls, 0);
  EXPECT_TRUE(output.empty());
}

TEST(ConstraintTypes, RulesStopAtFirstFailureAndInspectConstStorage) {
  using FirstFails =
      v::Constrained<v::Int8, check::range<0, 10> | check::predicate<counted>>;
  predicate_calls = 0;
  json_error<FirstFails>("11", EC::out_of_range);
  EXPECT_EQ(predicate_calls, 0);
  FirstFails value;
  value.value() = 11;
  rejects_encoding<hosq::msgpack::Writer<>>(value, EC::out_of_range);
  EXPECT_EQ(predicate_calls, 0);
  struct ConstOnly {
    auto operator()(const std::string& text) const -> bool {
      return text == "abc";
    }
    auto operator()(std::string&) const -> bool = delete;
  };
  using ConstText = v::Constrained<v::Text, check::predicate<ConstOnly{}>>;
  json_error<ConstText>(R"("abc")", EC::ok);
  json_error<ConstText>(R"("def")", EC::validation_failed);
}

TEST(ConstraintTypes, PreservesBoolDecoderCandidatesAndReaderOnlyEncoding) {
  using Candidate = hosq::Elem<hosq::scan::scalar | hosq::decode::integer<int> |
                                   hosq::match::literal<2> | hosq::store::assign,
                               hosq::encode::value>;
  using CheckedCandidate = v::Constrained<Candidate, check::range<0, 10>>;
  static_assert(std::same_as<CheckedCandidate::decoder_terminal, bool>);
  json_error<CheckedCandidate>("2", EC::ok);
  // Preserve the existing root bool-terminal contract; typed containers turn
  // a rejected element into candidate_rejected.
  EXPECT_EQ(read_error<CheckedCandidate>("3").code,
            read_error<Candidate>("3").code);
  json_error<v::Array<CheckedCandidate>>("[3]", EC::candidate_rejected);
  using ReadOnly = hosq::Elem<hosq::scan::scalar | hosq::decode::integer<int> |
                                  hosq::store::assign,
                              hosq::noop_encoder>;
  using CheckedReadOnly = v::Constrained<ReadOnly, check::range<0, 10>>;
  CheckedReadOnly read_only;
  rejects_encoding<hosq::msgpack::Writer<>>(read_only, EC::unsupported_type);
}

template <bool AnalysisBool>
struct ContextTerminal {
  template <class>
  using storage_type = int;

  template <class Context>
  auto operator()(Context& ctx) const {
    if constexpr (std::same_as<typename Context::reader_type,
                               hosq::detail::PipelineAnalysisReader>) {
      if constexpr (AnalysisBool) return true;
    } else {
      ctx.value() = 7;
      if constexpr (!AnalysisBool) return false;
    }
  }
};

TEST(ConstraintTypes, PreservesActualReaderDependentDecoderTerminal) {
  using Reject =
      hosq::Elem<hosq::step<ContextTerminal<false>>, hosq::encode::value>;
  using Accept =
      hosq::Elem<hosq::step<ContextTerminal<true>>, hosq::encode::value>;
  using CheckedReject = v::Constrained<Reject, check::range<0, 10>>;
  using CheckedAccept = v::Constrained<Accept, check::range<0, 10>>;
  static_assert(std::same_as<Reject::decoder_terminal, void>);
  static_assert(std::same_as<CheckedReject::decoder_terminal, void>);
  static_assert(std::same_as<Accept::decoder_terminal, bool>);
  static_assert(std::same_as<CheckedAccept::decoder_terminal, bool>);
  using RealContext =
      hosq::DecodeContext<int, hosq::json::SimpleReader, hosq::TryPhase::first>;
  static_assert(std::same_as<decltype(std::declval<CheckedReject&>().decode(
                                 std::declval<RealContext&>())),
                             bool>);
  static_assert(std::same_as<decltype(std::declval<CheckedAccept&>().decode(
                                 std::declval<RealContext&>())),
                             void>);
  json_error<v::Array<Reject>>("[7]", EC::candidate_rejected);
  json_error<v::Array<CheckedReject>>("[7]", EC::candidate_rejected);
  json_error<v::Array<Accept>>("[7]", EC::ok);
  json_error<v::Array<CheckedAccept>>("[7]", EC::ok);
}

TEST(ConstraintTypes, FloatingBoundsKeepExistingNanComparisonSemantics) {
  using Range = v::Constrained<v::Float64, check::range<-2, 2>>;
  using Min = v::Constrained<v::Float64, check::min<-2>>;
  using Max = v::Constrained<v::Float64, check::max<2>>;
  constexpr auto finite = [](double value) { return std::isfinite(value); };
  using FiniteRange =
      v::Constrained<v::Float64, check::range<-2, 2> | check::predicate<finite>>;
  v::Float64 source;
  source.value() = std::numeric_limits<double>::quiet_NaN();
  std::string wire;
  hosq::msgpack::Writer writer(wire);
  ASSERT_TRUE(hosq::encode_into(source, writer));
  EXPECT_EQ((read_error<Range, hosq::msgpack::Reader<>>(wire)).code, EC::ok);
  EXPECT_EQ((read_error<Min, hosq::msgpack::Reader<>>(wire)).code, EC::ok);
  EXPECT_EQ((read_error<Max, hosq::msgpack::Reader<>>(wire)).code, EC::ok);
  EXPECT_EQ((read_error<FiniteRange, hosq::msgpack::Reader<>>(wire)).code,
            EC::validation_failed);
  Range range;
  range.value() = source.value();
  std::string output;
  hosq::msgpack::Writer nan_writer(output);
  ASSERT_TRUE(hosq::encode_into(range, nan_writer));
  FiniteRange finite_range;
  finite_range.value() = source.value();
  rejects_encoding<hosq::msgpack::Writer<>>(finite_range, EC::validation_failed);
}

struct Person {
  NamedAge age;
  t::Constrained<"name", v::Text, check::non_empty> name;
  t::Constrained<"code", v::Text, check::size<3>> code;
  t::Array<"children", Age> children;
};

template <class Writer, class Reader>
void roundtrip(const v::Record<Person>& source) {
  std::string output;
  Writer writer(output);
  ASSERT_TRUE(hosq::encode_into(source, writer)) << writer.error().detail;
  v::Record<Person> decoded;
  const auto status = hosq::decode_document_into<Reader>(decoded, output);
  ASSERT_TRUE(status.is_ok()) << status.detail;
  EXPECT_EQ(decoded.value().age.value().value(), 42);
  EXPECT_EQ(decoded.value().name.value().value(), "Gen");
  EXPECT_EQ(decoded.value().code.value().value(), "abc");
  EXPECT_EQ(decoded.value().children.value().value(),
            (std::vector<std::int8_t>{0, 120}));
}

TEST(ConstraintTypes, KeyedAndNestedReuseRoundTripsEveryFormat) {
  v::Record<Person> person;
  const auto status = hosq::decode_document_into<hosq::json::FastReader<>>(
      person, R"({"children":[0,120],"name":"Gen","code":"abc","age":42})");
  ASSERT_TRUE(status.is_ok()) << status.detail;
  roundtrip<hosq::json::CompactWriter<>, hosq::json::SimpleReader>(person);
  roundtrip<hosq::msgpack::Writer<>, hosq::msgpack::Reader<>>(person);
  roundtrip<hosq::bson::Writer<>, hosq::bson::Reader<>>(person);
  roundtrip<hosq::toml::Writer<>, hosq::toml::Reader<>>(person);
  person.value().age.value().value() = 121;
  std::string output;
  hosq::json::CompactWriter writer(output);
  EXPECT_FALSE(hosq::encode_into(person, writer));
  EXPECT_EQ(writer.error().code, EC::invalid_token);
}

TEST(ConstraintTypes, ValidationErrorsRetainSourceLocationAndDetail) {
  struct Schema {
    t::Constrained<"n", v::Int8, check::range<0, 10>> n;
  };
  v::Record<Schema> value;
  const auto status =
      hosq::decode_document_into<hosq::json::SimpleReader>(value, R"({"n":11})");
  EXPECT_EQ(status.code, EC::out_of_range);
  EXPECT_EQ(status.offset, 5U);
  EXPECT_EQ(status.detail, "validate::range value outside bounds");
}
template <class Writer, class Reader>
void rejects_invalid_format_value() {
  struct Plain {
    t::Int8<"n"> n;
  };
  struct Checked {
    t::Constrained<"n", v::Int8, check::range<0, 10>> n;
  };
  v::Record<Plain> source;
  source.value().n.value().value() = 11;
  std::string wire;
  Writer writer(wire);
  ASSERT_TRUE(hosq::encode_into(source, writer));
  EXPECT_EQ((read_error<v::Record<Checked>, Reader>(wire)).code,
            EC::out_of_range);
  v::Record<Checked> target;
  target.value().n.value().value() = 11;
  std::string output;
  Writer bad_writer(output);
  EXPECT_FALSE(hosq::encode_into(target, bad_writer));
  if constexpr (requires { bad_writer.report_error(hosq::Error{}); })
    EXPECT_EQ(bad_writer.error().code, EC::out_of_range);
  else
    EXPECT_EQ(bad_writer.error().code, EC::invalid_token);
}

TEST(ConstraintTypes, InvalidKeyedValuesAreRejectedByEveryFormat) {
  rejects_invalid_format_value<hosq::json::CompactWriter<>,
                               hosq::json::SimpleReader>();
  rejects_invalid_format_value<hosq::msgpack::Writer<>,
                               hosq::msgpack::Reader<>>();
  rejects_invalid_format_value<hosq::bson::Writer<>, hosq::bson::Reader<>>();
  rejects_invalid_format_value<hosq::toml::Writer<>, hosq::toml::Reader<>>();
}
}  // namespace
