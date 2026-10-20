#include <gtest/gtest.h>

#include "hosq/hosq.hh"
#include "hosq/json/fast_reader.hh"
#include "hosq/json/simple_reader.hh"
#include "hosq/msgpack/reader.hh"

namespace {
using namespace hosq;

using One =
    Elem<scan::scalar | decode::integer<int> | match::literal<1> | store::assign,
         encode::value>;
using Positive =
    Elem<scan::scalar | decode::integer<int> | validate::min<1> | store::assign,
         encode::value>;

template <class R>
void check_outcomes() {
  R reader;
  typename R::state_type state;
  One one;
  one.value() = 99;
  auto invoke = [&](std::string_view input, auto& element) {
    reader.reset(state, input);
    EXPECT_TRUE(next_token<ReadPosition::value>(reader, state));
    DecodeContext<
        typename std::remove_reference_t<decltype(element)>::storage_type, R,
        TryPhase::first>
        ctx{element.storage(), reader, state};
    return detail::invoke_decode(element, ctx);
  };
  auto result = invoke("2", one);
  EXPECT_EQ(result.status, DecodeStatus::no_match);
  EXPECT_TRUE(result.error.is_ok());
  EXPECT_TRUE(reader.error(state).is_ok());
  EXPECT_EQ(one.value(), 99);

  result = invoke("1", one);
  EXPECT_EQ(result.status, DecodeStatus::matched);
  EXPECT_EQ(one.value(), 1);

  Positive positive;
  result = invoke("-1", positive);
  EXPECT_EQ(result.status, DecodeStatus::error);
  EXPECT_EQ(result.error.code, ErrorCode::out_of_range);
  EXPECT_EQ(reader.error(state).code, ErrorCode::out_of_range);
}

TEST(DecodeOutcome, MatchRejectionAndValidationErrorAreDistinct) {
  check_outcomes<json::SimpleReader>();
  check_outcomes<json::FastReader<>>();
}

struct HardReject {
  inline static int calls = 0;
  template <class Context>
  auto operator()(Context& ctx) const -> bool {
    ++calls;
    ctx.reader().report_error(
        ctx.reader_state(), {ErrorCode::validation_failed, 7, "specific error"});
    return false;
  }
};

TEST(DecodeOutcome, HardErrorTakesPrecedenceOverFalseAndPreventsReinvocation) {
  using E = Elem<match::predicate<HardReject{}>, noop_encoder>;
  E element;
  HardReject::calls = 0;
  json::SimpleReader reader;
  json::SimpleReader::state_type state;
  reader.reset(state, "0");
  ASSERT_TRUE(next_token<ReadPosition::value>(reader, state));
  DecodeContext<E::storage_type, json::SimpleReader, TryPhase::first> ctx{
      element.storage(), reader, state};
  const auto result = detail::invoke_decode(element, ctx);
  EXPECT_EQ(HardReject::calls, 1);
  EXPECT_EQ(detail::invoke_decode(element, ctx).status, DecodeStatus::error);
  EXPECT_EQ(HardReject::calls, 1);
  EXPECT_EQ(result.status, DecodeStatus::error);
  EXPECT_EQ(result.error.code, ErrorCode::validation_failed);
  EXPECT_EQ(result.error.offset, 7U);
  EXPECT_EQ(result.error.detail, "specific error");
  EXPECT_EQ(reader.error(state).detail, "specific error");
}

struct ConsumeAndFail {
  template <class Context>
    requires(!std::is_const_v<Context>)
  auto operator()(Context& ctx) const -> bool {
    (void)ctx.reader().next_token(ctx.reader_state());
    ctx.reader().report_error(
        ctx.reader_state(), {ErrorCode::validation_failed, 7, "consumed error"});
    return false;
  }
};
using ConsumingCandidate =
    Elem<scan::scalar | decode::integer<int> | store::assign |
             match::predicate<ConsumeAndFail{}>,
         encode::value>;
using DynamicKey =
    Elem<scan::scalar | decode::text | store::assign, encode::value>;

template <class Schema>
void check_consumed_error(Schema& schema, std::string_view input,
                          TokenType token, std::size_t offset) {
  msgpack::Reader<> reader;
  msgpack::Reader<>::state_type state;
  reader.reset(state, input);
  ASSERT_TRUE(reader.next_token(state));
  const auto error =
      Decoder<msgpack::Reader<>>::decode_nested(schema, reader, state);
  EXPECT_EQ(error.code, ErrorCode::validation_failed);
  EXPECT_EQ(error.offset, 7U);
  EXPECT_EQ(error.detail, "consumed error");
  EXPECT_EQ(reader.current_token(state).type, token);
  EXPECT_EQ(reader.current_token(state).offset, offset);
  EXPECT_EQ(state.pos, input.size());
}

TEST(DecodeOutcome, HardErrorsDoNotRestoreCandidateCursor) {
  struct Array {
    Try<ConsumingCandidate> value;
  } array;
  check_consumed_error(array, std::string("\x92\x01\x02", 3), TokenType::Scalar,
                       2);
  struct Named {
    Try<Entry<Key<"n">, ConsumingCandidate>> value;
  } named;
  check_consumed_error(named, std::string("\x81\xa1n\x01", 4),
                       TokenType::ObjectEnd, 4);
  struct Dynamic {
    Try<Entry<DynamicKey, ConsumingCandidate>> value;
  } dynamic;
  check_consumed_error(dynamic, std::string("\x81\xa1n\x01", 4),
                       TokenType::ObjectEnd, 4);
}
}  // namespace
