#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>

#include "hosq/bson/reader.hh"
#include "hosq/bson/writer.hh"
#include "hosq/hosq.hh"
#include "hosq/json/compact_writer.hh"
#include "hosq/json/fast_reader.hh"
#include "hosq/json/simple_reader.hh"
#include "hosq/msgpack/reader.hh"
#include "hosq/msgpack/writer.hh"
#include "hosq/types.hh"

namespace {
using namespace hosq;
namespace v = hosq::types::value;
using EC = ErrorCode;
using AnyKey =
    Elem<scan::scalar | decode::string_view | store::assign, noop_encoder>;

template <class W, class T>
auto wire(const T& value) -> std::string {
  std::string output;
  W writer(output);
  EXPECT_TRUE(encode_into(value, writer)) << writer.error().detail;
  return output;
}

template <class Scalar>
struct ArrayCandidates {
  Try<Scalar> narrow;
  Try<v::Int64> fallback;
};
template <class Scalar>
struct NamedCandidates {
  Try<Entry<Key<"n">, Scalar>> narrow;
  Try<Entry<AnyKey, v::Int64>> fallback;
};
struct ArrayInput {
  v::Int64 number;
};
struct NamedInput {
  types::Int64<"n"> number;
};

template <class R, class W, class Scalar>
void check_bounds(std::int64_t low, std::int64_t high) {
  for (auto number : {low, high, low - 1, high + 1}) {
    SCOPED_TRACE(number);
    const auto expected =
        number < low || number > high ? EC::out_of_range : EC::ok;
    NamedInput input;
    input.number.value().value() = number;
    NamedCandidates<Scalar> value;
    value.narrow.value().value() = 11;
    value.fallback.value().value() = 47;
    const auto error = decode_into<R>(value, wire<W>(input));
    EXPECT_EQ(error.code, expected) << error.detail;
    if (expected == EC::ok) EXPECT_EQ(value.narrow.value().value(), number);
    EXPECT_EQ(value.fallback.value().value(), 47);

    if constexpr (!std::same_as<R, bson::Reader<>>) {
      ArrayInput array_input;
      array_input.number.value() = number;
      ArrayCandidates<Scalar> array_value;
      array_value.narrow.value() = 11;
      array_value.fallback.value() = 47;
      const auto array_error = decode_into<R>(array_value, wire<W>(array_input));
      EXPECT_EQ(array_error.code, expected) << array_error.detail;
      if (expected == EC::ok) EXPECT_EQ(array_value.narrow.value(), number);
      // An out-of-range typed candidate must not fall through to Int64.
      EXPECT_EQ(array_value.fallback.value(), 47);
    }
  }
}

template <class R, class W>
void check_integer_boundaries() {
  check_bounds<R, W, v::Int8>(-128, 127);
  check_bounds<R, W, v::Uint8>(0, 255);
}

TEST(TryFailures, TypedIntegerBoundariesRetainConversionErrorsAcrossFormats) {
  check_integer_boundaries<json::SimpleReader, json::CompactWriter<>>();
  check_integer_boundaries<json::FastReader<>, json::CompactWriter<>>();
  check_integer_boundaries<msgpack::Reader<>, msgpack::Writer<>>();
  check_integer_boundaries<bson::Reader<>, bson::Writer<>>();
}

template <class R, class W>
void check_float_narrowing() {
  struct Input {
    v::Float64 number;
  } input;
  struct Value {
    Try<v::Float32> narrow;
    Try<v::Float64> fallback;
  } value;
  value.fallback.value() = 47.0;
  input.number.value() = 1.5;
  ASSERT_TRUE(decode_into<R>(value, wire<W>(input)).is_ok());
  EXPECT_EQ(value.narrow.value(), 1.5F);
  input.number.value() = 0.1;
  const auto error = decode_into<R>(value, wire<W>(input));
  EXPECT_EQ(error.code, EC::out_of_range) << error.detail;
  EXPECT_EQ(value.fallback.value(), 47.0);
}

TEST(TryFailures, LossyFloatNarrowingDoesNotFallThroughToFloat64) {
  check_float_narrowing<json::SimpleReader, json::CompactWriter<>>();
  check_float_narrowing<json::FastReader<>, json::CompactWriter<>>();
  check_float_narrowing<msgpack::Reader<>, msgpack::Writer<>>();
}

template <class R, class W>
void check_text_is_not_numeric_fallback() {
  struct Input {
    types::Text<"n"> text;
  } input;
  input.text.value().value() = "42";
  struct Value {
    Try<types::Int8<"n">> integer;
    Try<Entry<AnyKey, v::Text>> text;
  } value;
  value.text.value().value() = "unchanged";
  const auto error = decode_into<R>(value, wire<W>(input));
  EXPECT_EQ(error.code, EC::type_mismatch) << error.detail;
  EXPECT_EQ(value.text.value().value(), "unchanged");
}

TEST(TryFailures, NamedTypedMismatchDoesNotTryAnotherValueSchemaForTheKey) {
  check_text_is_not_numeric_fallback<json::SimpleReader,
                                     json::CompactWriter<>>();
  check_text_is_not_numeric_fallback<json::FastReader<>,
                                     json::CompactWriter<>>();
  check_text_is_not_numeric_fallback<msgpack::Reader<>, msgpack::Writer<>>();
  check_text_is_not_numeric_fallback<bson::Reader<>, bson::Writer<>>();
}

struct RejectValue {
  auto operator()(int value) const -> MatchResult<int> { return {false, value}; }
};
struct RejectWithError {
  template <class C>
    requires(!std::is_const_v<C>)
  auto operator()(int value, C& ctx) const -> MatchResult<int> {
    ctx.reader().report_error(ctx.reader_state(),
                              {EC::validation_failed, 0, "rejected Try value"});
    return {false, value};
  }
};
struct ObserveAccepted {
  static inline int calls = 0;
  auto operator()(int value) const -> MatchResult<int> {
    ++calls;
    return {true, value};
  }
};
template <class Predicate>
using CheckedNumber =
    Elem<scan::scalar | decode::number<int> | step<Predicate> | store::assign,
         encode::value>;
template <class Predicate>
struct CheckedArray {
  Try<CheckedNumber<Predicate>> rejected;
  Try<CheckedNumber<ObserveAccepted>> fallback;
};
template <class Predicate>
struct CheckedObject {
  Try<Entry<Key<"n">, CheckedNumber<Predicate>>> rejected;
  Try<Entry<AnyKey, CheckedNumber<ObserveAccepted>>> fallback;
};

template <class R, class W>
void check_validation_dispatch() {
  NamedInput input;
  input.number.value().value() = 7;
  const auto encoded = wire<W>(input);
  CheckedObject<RejectValue> soft;
  ObserveAccepted::calls = 0;
  const auto accepted = decode_into<R>(soft, encoded);
  ASSERT_TRUE(accepted.is_ok()) << accepted.detail;
  EXPECT_EQ(ObserveAccepted::calls, 1);
  EXPECT_EQ(soft.rejected.value().value(), 0);
  EXPECT_EQ(soft.fallback.value().value(), 7);

  CheckedObject<RejectWithError> hard;
  ObserveAccepted::calls = 0;
  const auto rejected = decode_into<R>(hard, encoded);
  EXPECT_EQ(rejected.code, EC::validation_failed) << rejected.detail;
  EXPECT_EQ(ObserveAccepted::calls, 0);

  if constexpr (!std::same_as<R, bson::Reader<>>) {
    ArrayInput array_input;
    array_input.number.value() = 7;
    const auto array_encoded = wire<W>(array_input);
    CheckedArray<RejectValue> array_soft;
    ObserveAccepted::calls = 0;
    const auto array_accepted = decode_into<R>(array_soft, array_encoded);
    ASSERT_TRUE(array_accepted.is_ok()) << array_accepted.detail;
    EXPECT_EQ(ObserveAccepted::calls, 1);
    EXPECT_EQ(array_soft.rejected.value(), 0);
    EXPECT_EQ(array_soft.fallback.value(), 7);

    CheckedArray<RejectWithError> array_hard;
    ObserveAccepted::calls = 0;
    const auto array_rejected = decode_into<R>(array_hard, array_encoded);
    EXPECT_EQ(array_rejected.code, EC::validation_failed)
        << array_rejected.detail;
    EXPECT_EQ(ObserveAccepted::calls, 0);
  }
}

TEST(TryFailures, SoftValidationAllowsFallbackButReportedErrorAbortsDispatch) {
  check_validation_dispatch<json::SimpleReader, json::CompactWriter<>>();
  check_validation_dispatch<json::FastReader<>, json::CompactWriter<>>();
  check_validation_dispatch<msgpack::Reader<>, msgpack::Writer<>>();
  check_validation_dispatch<bson::Reader<>, bson::Writer<>>();
}

using SkipKey = Elem<scan::scalar | decode::skip, noop_encoder>;
using SkipValue = Elem<decode::skip, noop_encoder>;
struct NestedValues {
  Try<types::Array<"n", v::Int8>> numbers;
  Try<Entry<SkipKey, SkipValue>> skip;
};
template <class R>
void check_malformed_nested(std::string_view matched, std::string_view skipped,
                            EC expected) {
  for (const auto input : {matched, skipped}) {
    NestedValues value;
    const auto error = decode_into<R>(value, input);
    EXPECT_EQ(error.code, expected) << error.detail;
  }
}

auto octets(std::initializer_list<unsigned> values) -> std::string {
  std::string output;
  for (auto value : values) output += static_cast<char>(value);
  return output;
}
auto little_endian(std::uint32_t value) -> std::string {
  std::string output;
  for (unsigned i = 0; i < 4; ++i) output += static_cast<char>(value >> (i * 8));
  return output;
}
auto bson_document(std::string elements) -> std::string {
  return little_endian(static_cast<std::uint32_t>(elements.size() + 5)) +
         elements + '\0';
}

TEST(TryFailures, TruncatedMatchedAndSkippedNestedValuesKeepReaderError) {
  check_malformed_nested<json::SimpleReader>(R"({"n":[1,)", R"({"other":[[1,)",
                                             EC::unexpected_eof);
  check_malformed_nested<json::FastReader<>>(R"({"n":[1,)", R"({"other":[[1,)",
                                             EC::unexpected_eof);
  check_malformed_nested<msgpack::Reader<>>(
      octets({0x81, 0xa1, 'n', 0x92, 1}),
      octets({0x81, 0xa1, 'x', 0x91, 0x92, 1}), EC::unexpected_eof);
  // Valid enclosing lengths, but the nested Int32 payload contains one byte.
  const auto short_array = bson_document(octets({0x10, '0', 0, 1}));
  check_malformed_nested<bson::Reader<>>(
      bson_document(octets({4, 'n', 0}) + short_array),
      bson_document(octets({4, 'x', 0}) + short_array), EC::unexpected_eof);
}

template <class R>
void check_missing_matched_value(std::string_view input) {
  CheckedObject<RejectValue> value;
  ObserveAccepted::calls = 0;
  const auto error = decode_into<R>(value, input);
  EXPECT_EQ(error.code, EC::unexpected_eof) << error.detail;
  EXPECT_EQ(ObserveAccepted::calls, 0);
}

TEST(TryFailures, MissingMatchedMemberValueIsFatalBeforeFallback) {
  check_missing_matched_value<json::SimpleReader>(R"({"n":)");
  check_missing_matched_value<json::FastReader<>>(R"({"n":)");
  check_missing_matched_value<msgpack::Reader<>>(octets({0x81, 0xa1, 'n'}));
  check_missing_matched_value<bson::Reader<>>(
      bson_document(octets({0x10, 'n', 0})));
}

template <class R, class W>
void check_decoder_reuse() {
  struct Value {
    Try<types::Uint8<"n">> number;
  } value;
  Decoder<R> decoder;
  auto decode_number = [&](std::int64_t number) {
    NamedInput input;
    input.number.value().value() = number;
    return decoder.decode(value, wire<W>(input));
  };
  ASSERT_TRUE(decode_number(255).is_ok());
  EXPECT_EQ(value.number.value().value(), 255);
  EXPECT_EQ(decode_number(256).code, EC::out_of_range);
  ASSERT_TRUE(decode_number(9).is_ok());
  EXPECT_EQ(value.number.value().value(), 9);
  // Empty input objects reset reader state, but do not clear legacy storage.
  std::string empty = "{}";
  if constexpr (std::same_as<R, msgpack::Reader<>>)
    empty = octets({0x80});
  else if constexpr (std::same_as<R, bson::Reader<>>)
    empty = bson_document("");
  ASSERT_TRUE(decoder.decode(value, empty).is_ok());
  EXPECT_EQ(value.number.value().value(), 9);
}

TEST(TryFailures, ReusedDecoderClearsStickyErrorsWithoutClearingMissingStorage) {
  check_decoder_reuse<json::SimpleReader, json::CompactWriter<>>();
  check_decoder_reuse<json::FastReader<>, json::CompactWriter<>>();
  check_decoder_reuse<msgpack::Reader<>, msgpack::Writer<>>();
  check_decoder_reuse<bson::Reader<>, bson::Writer<>>();
}
}  // namespace
