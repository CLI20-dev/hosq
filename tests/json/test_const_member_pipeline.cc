#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <type_traits>

#include "hosq/hosq.hh"
#include "hosq/json/fast_reader.hh"

namespace const_member_pipeline_test {

using namespace hosq;
using FastReader = json::FastReader<>;

template <class Tag>
struct CountingReader : FastReader {
  using state_type = typename FastReader::state_type;
  using input_type = typename FastReader::input_type;

  static inline int const_number_calls = 0;

  template <StringLiteral Key, class T>
  bool try_read_const_number(state_type& state, T& value) const {
    ++const_number_calls;
    return FastReader::template try_read_const_number<Key>(state, value);
  }
};

struct FusedReaderTag;
struct CustomDecodeReaderTag;
struct CustomScanReaderTag;

using FusedReader = CountingReader<FusedReaderTag>;
using CustomDecodeReader = CountingReader<CustomDecodeReaderTag>;
using CustomScanReader = CountingReader<CustomScanReaderTag>;

template <class T, class Reader, TryPhase Phase>
auto resolve_fast_scalar(const DecodeContext<T, Reader, Phase>& ctx)
    -> void {
  const auto& reader = ctx.reader();
  auto& state = const_cast<typename Reader::state_type&>(ctx.reader_state());
  auto& token = reader.current_token(state);
  if (token.type != TokenType::Scalar) {
    reader.report_error(
        state, Error{.code = ErrorCode::type_mismatch,
                     .offset = token.offset,
                     .detail = "instrumented scan expected scalar"});
    return;
  }
  if (token.length == unresolved_length) {
    if (!reader.resolve_skip(state)) reader.report_error(state, reader.error(state));
  } else if (token.context ==
             static_cast<std::uint8_t>(JsonTokenContext::string)) {
    state.pos = state.begin + token.offset + token.length + 1;
  } else {
    state.pos = state.begin + token.offset + token.length;
  }
}

struct ObserveTail {
  static inline int calls = 0;
  static inline Token token{};

  template <class Context>
  auto operator()(int value, Context& ctx) const -> int {
    ++calls;
    token = ctx.reader().current_token(ctx.reader_state());
    return value;
  }
};

struct RejectTail {
  static inline int calls = 0;
  static inline Token token{};

  template <class Context>
  auto operator()(int value, Context& ctx) const -> MatchResult<int> {
    ++calls;
    token = ctx.reader().current_token(ctx.reader_state());
    return {false, value};
  }
};

struct OpaqueNumber {
  template <class>
  using storage_type = int;

  static inline int calls = 0;

  template <class Context>
    requires(!std::is_const_v<Context>)
  void operator()(Context& ctx) const {
    ++calls;
    int value = 0;
    if (ctx.reader().template read_number<int>(ctx.reader_state(), value))
      ctx.value() = value;
  }
};

struct ConstOnlyStep {
  template <class>
  using storage_type = int;

  static inline int calls = 0;
  static inline int forbidden_fusion_calls = 0;

  template <class Context>
  void operator()(const Context& ctx) const {
    ++calls;
    ctx.value() = 91;
  }
};

using ConstOnlyPipeline = Pipeline<ConstOnlyStep{}>;

template <class Tail>
using CanonicalNumber =
    Elem<scan::scalar | decode::number<int> | step<Tail> | store::assign,
         noop_encoder>;

template <class Value>
struct NumberObject {
  Entry<Key<"id">, Value> id;
};

using ObservedObject = NumberObject<CanonicalNumber<ObserveTail>>;
using RejectedObject = NumberObject<CanonicalNumber<RejectTail>>;
using OpaqueObject =
    NumberObject<Elem<step<OpaqueNumber>, noop_encoder>>;
using ConstObject =
    NumberObject<Elem<ConstOnlyPipeline{}, noop_encoder>>;

}  // namespace const_member_pipeline_test

namespace hosq::detail {

// If Decoder ignored Elem's const-dispatch decision, this deliberately viable
// test fusion would run instead of the const-only action.
template <>
struct ConstMemberFusion<
    const_member_pipeline_test::ConstOnlyPipeline> {
  template <auto Key, class Context>
  auto try_invoke(Context&) const -> bool {
    (void)Key;
    ++const_member_pipeline_test::ConstOnlyStep::forbidden_fusion_calls;
    return true;
  }
};

}  // namespace hosq::detail

namespace hosq::ext {

template <>
struct ScanScalar<const_member_pipeline_test::FusedReader> {
  using reader_fusion = ScanScalar;

  template <class T, TryPhase Phase>
  auto operator()(
      const DecodeContext<T, const_member_pipeline_test::FusedReader, Phase>&
          ctx) const -> ScalarView {
    const_member_pipeline_test::resolve_fast_scalar(ctx);
    return {};
  }
};

template <>
struct ScanScalar<const_member_pipeline_test::CustomDecodeReader> {
  using reader_fusion = ScanScalar;

  template <class T, TryPhase Phase>
  auto operator()(
      const DecodeContext<T,
                          const_member_pipeline_test::CustomDecodeReader,
                          Phase>& ctx) const -> ScalarView {
    const_member_pipeline_test::resolve_fast_scalar(ctx);
    return {};
  }
};

template <>
struct DecodeNumber<const_member_pipeline_test::CustomDecodeReader> {
  static inline int calls = 0;

  template <class T, class Context>
  auto operator()(const ScalarView&, const Context&) const
      -> std::remove_cvref_t<T> {
    ++calls;
    return static_cast<std::remove_cvref_t<T>>(77);
  }
};

template <>
struct ScanScalar<const_member_pipeline_test::CustomScanReader> {
  static inline int calls = 0;

  template <class T, TryPhase Phase>
  auto operator()(
      const DecodeContext<T, const_member_pipeline_test::CustomScanReader,
                          Phase>& ctx) const -> ScalarView {
    ++calls;
    const_member_pipeline_test::resolve_fast_scalar(ctx);
    return {};
  }
};

}  // namespace hosq::ext

namespace {

using namespace hosq;
using namespace const_member_pipeline_test;

template <class Reader>
void reset_reader_counters() {
  Reader::const_number_calls = 0;
}

void expect_resolved_42(const Token& token) {
  EXPECT_EQ(token.type, TokenType::Scalar);
  EXPECT_EQ(token.context,
            static_cast<std::uint8_t>(JsonTokenContext::literal));
  EXPECT_EQ(token.offset, 6U);
  EXPECT_EQ(token.length, 2U);
}

TEST(ConstMemberPipeline, DecoderSelectsFusionAndTailSeesResolvedTokenOnce) {
  reset_reader_counters<FusedReader>();
  ObserveTail::calls = 0;
  ObserveTail::token = {};

  ObservedObject output;
  const Error error = decode_into<FusedReader>(output, R"({"id":42})");

  ASSERT_TRUE(error.is_ok()) << error.detail;
  EXPECT_EQ(FusedReader::const_number_calls, 1);
  EXPECT_EQ(ObserveTail::calls, 1);
  EXPECT_EQ(output.id.value().value(), 42);
  expect_resolved_42(ObserveTail::token);
}

TEST(ConstMemberPipeline, MismatchShortCircuitsStoreInFusedAndOrdinaryPaths) {
  reset_reader_counters<FusedReader>();
  RejectTail::calls = 0;
  RejectTail::token = {};
  RejectedObject fused;
  fused.id.value().value() = 19;

  ASSERT_TRUE(
      decode_into<FusedReader>(fused, R"({"id":42})").is_ok());
  EXPECT_EQ(FusedReader::const_number_calls, 1);
  EXPECT_EQ(RejectTail::calls, 1);
  EXPECT_EQ(fused.id.value().value(), 19);
  expect_resolved_42(RejectTail::token);

  reset_reader_counters<CustomScanReader>();
  ext::ScanScalar<CustomScanReader>::calls = 0;
  RejectTail::calls = 0;
  RejectTail::token = {};
  RejectedObject ordinary;
  ordinary.id.value().value() = 19;

  ASSERT_TRUE(
      decode_into<CustomScanReader>(ordinary, R"({"id":42})").is_ok());
  EXPECT_EQ(CustomScanReader::const_number_calls, 0);
  EXPECT_EQ(ext::ScanScalar<CustomScanReader>::calls, 1);
  EXPECT_EQ(RejectTail::calls, 1);
  EXPECT_EQ(ordinary.id.value().value(), 19);
  expect_resolved_42(RejectTail::token);
}

TEST(ConstMemberPipeline, CustomDecodeNumberPreventsMemberFusion) {
  reset_reader_counters<CustomDecodeReader>();
  ext::DecodeNumber<CustomDecodeReader>::calls = 0;
  ObserveTail::calls = 0;

  ObservedObject output;
  ASSERT_TRUE(
      decode_into<CustomDecodeReader>(output, R"({"id":42})").is_ok());

  EXPECT_EQ(CustomDecodeReader::const_number_calls, 0);
  EXPECT_EQ(ext::DecodeNumber<CustomDecodeReader>::calls, 1);
  EXPECT_EQ(ObserveTail::calls, 1);
  EXPECT_EQ(output.id.value().value(), 77);
}

TEST(ConstMemberPipeline, CustomScanScalarPreventsMemberFusion) {
  reset_reader_counters<CustomScanReader>();
  ext::ScanScalar<CustomScanReader>::calls = 0;
  ObserveTail::calls = 0;

  ObservedObject output;
  ASSERT_TRUE(
      decode_into<CustomScanReader>(output, R"({"id":42})").is_ok());

  EXPECT_EQ(CustomScanReader::const_number_calls, 0);
  EXPECT_EQ(ext::ScanScalar<CustomScanReader>::calls, 1);
  EXPECT_EQ(ObserveTail::calls, 1);
  EXPECT_EQ(output.id.value().value(), 42);
}

TEST(ConstMemberPipeline, OpaqueCustomValueStepStillExecutes) {
  reset_reader_counters<FusedReader>();
  OpaqueNumber::calls = 0;

  OpaqueObject output;
  ASSERT_TRUE(decode_into<FusedReader>(output, R"({"id":42})").is_ok());

  EXPECT_EQ(FusedReader::const_number_calls, 0);
  EXPECT_EQ(OpaqueNumber::calls, 1);
  EXPECT_EQ(output.id.value().value(), 42);
}

TEST(ConstMemberPipeline, ConstInvokableDecoderDeclinesAvailableFusion) {
  using Context = DecodeContext<int, FusedReader>;
  static_assert(Elem<ConstOnlyPipeline{}, noop_encoder>::
                    template decoder_const_invokable<Context>);

  reset_reader_counters<FusedReader>();
  ConstOnlyStep::calls = 0;
  ConstOnlyStep::forbidden_fusion_calls = 0;

  ConstObject output;
  ASSERT_TRUE(decode_into<FusedReader>(output, R"({"id":42})").is_ok());

  EXPECT_EQ(FusedReader::const_number_calls, 0);
  EXPECT_EQ(ConstOnlyStep::forbidden_fusion_calls, 0);
  EXPECT_EQ(ConstOnlyStep::calls, 1);
  EXPECT_EQ(output.id.value().value(), 91);
}

}  // namespace
