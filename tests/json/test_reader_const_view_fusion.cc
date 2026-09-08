#include <gtest/gtest.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#if defined(__unix__) || defined(__APPLE__)
#include <sys/mman.h>
#include <unistd.h>
#endif

#include "hosq/hosq.hh"
#include "hosq/json/fast_reader.hh"

namespace const_view_fusion_test {

using namespace hosq;

template <json::InputPadding Padding = json::InputPadding::none>
struct CountingReaderT : json::FastReader<128, Padding> {
  using BaseReader = json::FastReader<128, Padding>;
  using state_type = typename BaseReader::state_type;
  using input_type = typename BaseReader::input_type;

  static inline int const_view_calls = 0;
  static inline int scan_scalar_calls = 0;

  template <StringLiteral Key>
  bool try_read_const_string_view(state_type& state,
                                  std::string_view& value) const {
    ++const_view_calls;
    return BaseReader::template try_read_const_string_view<Key>(state, value);
  }
};

// This control disables only the new optional hook. All inherited token and
// number paths, plus its ScanScalar self tag, match CountingReaderT.
template <json::InputPadding Padding = json::InputPadding::none>
struct OrdinaryReaderT : json::FastReader<128, Padding> {
  using BaseReader = json::FastReader<128, Padding>;
  using state_type = typename BaseReader::state_type;
  using input_type = typename BaseReader::input_type;

  static inline int disabled_const_view_calls = 0;
  static inline int scan_scalar_calls = 0;

  template <StringLiteral>
  bool try_read_const_string_view(state_type&, std::string_view&) const {
    ++disabled_const_view_calls;
    return false;
  }
};

using CountingReader = CountingReaderT<>;
using OrdinaryReader = OrdinaryReaderT<>;

// A custom ScanScalar specialization without the reader_fusion self tag opts
// out of every fusion that would otherwise bypass that extension.
struct OptOutReader : json::FastReader<> {
  using BaseReader = json::FastReader<>;
  using state_type = typename BaseReader::state_type;
  using input_type = typename BaseReader::input_type;

  static inline int const_view_calls = 0;
  static inline int scan_scalar_calls = 0;

  template <StringLiteral Key>
  bool try_read_const_string_view(state_type& state,
                                  std::string_view& value) const {
    ++const_view_calls;
    return BaseReader::template try_read_const_string_view<Key>(state, value);
  }
};

template <class Reader, class T, TryPhase Phase>
void resolve_fast_scalar(const DecodeContext<T, Reader, Phase>& ctx) {
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
    if (!reader.resolve_skip(state))
      reader.report_error(state, reader.error(state));
  } else if (token.context ==
             static_cast<std::uint8_t>(JsonTokenContext::string)) {
    state.pos = state.begin + token.offset + token.length + 1;
  } else {
    state.pos = state.begin + token.offset + token.length;
  }
}

struct ConstObservedAssign {
  template <class Input>
  using storage_type = std::remove_cvref_t<Input>;

  static inline int const_calls = 0;
  static inline int mutable_calls = 0;
  static inline Token observed_token{};
  static inline std::size_t observed_pos_offset = 0;

  template <class Context>
  static void observe(const Context& ctx) {
    observed_token = ctx.reader().current_token(ctx.reader_state());
    observed_pos_offset = static_cast<std::size_t>(
        ctx.reader_state().pos - ctx.reader_state().begin);
  }

  template <class Input, class Context>
  void operator()(Input&& value, const Context& ctx) const {
    ++const_calls;
    observe(ctx);
    ctx.value() = std::forward<Input>(value);
  }

  template <class Input, class Context>
    requires(!std::is_const_v<Context>)
  void operator()(Input&& value, Context& ctx) const {
    ++mutable_calls;
    observe(ctx);
    ctx.value() = std::forward<Input>(value);
  }
};

struct MutableObservedAssign {
  template <class Input>
  using storage_type = std::remove_cvref_t<Input>;

  static inline int calls = 0;

  template <class Input, class Context>
    requires(!std::is_const_v<Context>)
  void operator()(Input&& value, Context& ctx) const {
    ++calls;
    ctx.value() = std::forward<Input>(value);
  }
};

struct ConstOnlyStep {
  template <class>
  using storage_type = int;

  static inline int calls = 0;
  static inline int forbidden_mutable_fusion_calls = 0;

  template <class Context>
  void operator()(const Context& ctx) const {
    ++calls;
    ctx.value() = 91;
  }
};

using HString = Elem<scan::scalar | decode::string_view | store::assign,
                     noop_encoder>;
inline constexpr auto null_value = decode::custom<[](std::string_view value)
                                                       -> std::nullptr_t {
  if (value != "null") std::abort();
  return nullptr;
}>;
using HNull = Elem<scan::scalar | decode::string_view | null_value |
                       store::assign,
                   noop_encoder>;
using ConstObservedString =
    Elem<scan::scalar | decode::string_view | step<ConstObservedAssign>,
         noop_encoder>;
using MutableObservedString =
    Elem<scan::scalar | decode::string_view | step<MutableObservedAssign>,
         noop_encoder>;
using ConstOnlyPipeline = Pipeline<ConstOnlyStep{}>;
using ConstOnlyValue = Elem<ConstOnlyPipeline{}, noop_encoder>;

struct ViewObject {
  Entry<Key<"name">, HString> name;
  Entry<Key<"none">, HNull> none;
};
struct TwoStrings {
  Entry<Key<"first">, HString> first;
  Entry<Key<"second">, HString> second;
};
struct ConstTailObject {
  Entry<Key<"name">, ConstObservedString> name;
};
struct MutableTailObject {
  Entry<Key<"name">, MutableObservedString> name;
};
struct ConstOnlyObject {
  Entry<Key<"id">, ConstOnlyValue> id;
};

}  // namespace const_view_fusion_test

namespace hosq::detail {

template <>
struct ConstMemberFusion<const_view_fusion_test::ConstOnlyPipeline> {
  template <auto Key, class Context>
  auto try_invoke(Context&) const -> bool {
    (void)Key;
    ++const_view_fusion_test::ConstOnlyStep::forbidden_mutable_fusion_calls;
    return true;
  }
};

}  // namespace hosq::detail

namespace hosq::ext {

template <json::InputPadding Padding>
struct ScanScalar<const_view_fusion_test::CountingReaderT<Padding>> {
  using reader_fusion = ScanScalar;

  template <class T, TryPhase Phase>
  auto operator()(
      const DecodeContext<T,
                          const_view_fusion_test::CountingReaderT<Padding>,
                          Phase>& ctx) const -> ScalarView {
    ++const_view_fusion_test::CountingReaderT<Padding>::scan_scalar_calls;
    const_view_fusion_test::resolve_fast_scalar(ctx);
    return {};
  }
};

template <json::InputPadding Padding>
struct ScanScalar<const_view_fusion_test::OrdinaryReaderT<Padding>> {
  using reader_fusion = ScanScalar;

  template <class T, TryPhase Phase>
  auto operator()(
      const DecodeContext<T,
                          const_view_fusion_test::OrdinaryReaderT<Padding>,
                          Phase>& ctx) const -> ScalarView {
    ++const_view_fusion_test::OrdinaryReaderT<Padding>::scan_scalar_calls;
    const_view_fusion_test::resolve_fast_scalar(ctx);
    return {};
  }
};

template <>
struct ScanScalar<const_view_fusion_test::OptOutReader> {
  // Deliberately no reader_fusion self tag.
  template <class T, TryPhase Phase>
  auto operator()(
      const DecodeContext<T, const_view_fusion_test::OptOutReader, Phase>& ctx)
      const -> ScalarView {
    ++const_view_fusion_test::OptOutReader::scan_scalar_calls;
    const_view_fusion_test::resolve_fast_scalar(ctx);
    return {};
  }
};

}  // namespace hosq::ext

namespace {

using namespace hosq;
using namespace const_view_fusion_test;

using StringContext = DecodeContext<std::string_view, CountingReader>;
using NullContext = DecodeContext<std::nullptr_t, CountingReader>;
static_assert(HString::template decoder_const_invokable<StringContext>);
static_assert(HNull::template decoder_const_invokable<NullContext>);
static_assert(
    ConstObservedString::template decoder_const_invokable<StringContext>);
static_assert(
    !MutableObservedString::template decoder_const_invokable<StringContext>);
static_assert(detail::ReaderFusion<ext::ScanScalar<CountingReader>>);
static_assert(detail::ReaderFusion<ext::ScanScalar<OrdinaryReader>>);
static_assert(detail::ReaderFusion<ext::DecodeNumber<CountingReader>>);
static_assert(detail::ReaderFusion<ext::DecodeNumber<OrdinaryReader>>);
static_assert(detail::ReaderFusion<ext::DecodeNumberInto<CountingReader>>);
static_assert(detail::ReaderFusion<ext::DecodeNumberInto<OrdinaryReader>>);
static_assert(!detail::ReaderFusion<ext::ScanScalar<OptOutReader>>);

void expect_same_token(const Token& actual, const Token& expected) {
  EXPECT_EQ(actual.offset, expected.offset);
  EXPECT_EQ(actual.length, expected.length);
  EXPECT_EQ(actual.type, expected.type);
  EXPECT_EQ(actual.context, expected.context);
}

void expect_same_error(const Error& actual, const Error& expected) {
  EXPECT_EQ(actual.code, expected.code);
  EXPECT_EQ(actual.offset, expected.offset);
  EXPECT_EQ(actual.detail, expected.detail);
}

template <class State>
void expect_same_state(const State& actual, const State& expected) {
  EXPECT_EQ(actual.begin, expected.begin);
  EXPECT_EQ(actual.pos, expected.pos);
  EXPECT_EQ(actual.end, expected.end);
  EXPECT_EQ(actual.input_view.data(), expected.input_view.data());
  EXPECT_EQ(actual.input_view.size(), expected.input_view.size());
  expect_same_token(actual.current, expected.current);
  EXPECT_EQ(actual.ctx_stack, expected.ctx_stack);
  EXPECT_EQ(actual.ctx_size, expected.ctx_size);
  EXPECT_EQ(actual.expect, expected.expect);
  expect_same_error(actual.last_error, expected.last_error);
  EXPECT_EQ(actual.error_flag, expected.error_flag);
  expect_same_token(actual.error_token, expected.error_token);
}

void expect_same_view(std::string_view actual, std::string_view expected) {
  EXPECT_EQ(actual.data(), expected.data());
  EXPECT_EQ(actual.size(), expected.size());
}

template <class Reader>
bool read_member_ordinary(Reader& reader, typename Reader::state_type& state,
                          std::string_view& key, std::string_view& value) {
  if (!next_token<Expect::next_member_or_end>(reader, state)) return false;
  DecodeContext<void, Reader> key_context{reader, state};
  key = (scan::scalar | decode::string_view).invoke(key_context);
  if (reader.error(state).has_error()) return false;
  if (!next_token<ReadPosition::member_value>(reader, state)) return false;
  DecodeContext<void, Reader> value_context{reader, state};
  value = (scan::scalar | decode::string_view).invoke(value_context);
  return reader.error(state).is_ok();
}

template <json::InputPadding Padding>
void compare_probe_with_ordinary(std::string_view input) {
  SCOPED_TRACE(std::string(input));
  using FusedReader = CountingReaderT<Padding>;
  using ControlReader = OrdinaryReaderT<Padding>;
  using State = typename FusedReader::state_type;

  FusedReader fused_reader;
  ControlReader ordinary_reader;
  State fused_state;
  State ordinary_state;
  fused_reader.reset(fused_state, input);
  ordinary_reader.reset(ordinary_state, input);

  const bool fused_root = fused_reader.next_token(fused_state);
  ASSERT_EQ(fused_root, ordinary_reader.next_token(ordinary_state));
  expect_same_state(fused_state, ordinary_state);
  if (!fused_root || fused_state.current.type != TokenType::ObjectBegin) {
    const State saved = fused_state;
    constexpr std::string_view sentinel = "unchanged output";
    auto fused_value = sentinel;
    EXPECT_FALSE((fused_reader.template try_read_const_string_view<"id">(
        fused_state, fused_value)));
    expect_same_state(fused_state, saved);
    expect_same_view(fused_value, sentinel);
    return;
  }

  fused_state.last_error = {.code = ErrorCode::candidate_rejected,
                            .offset = 91,
                            .detail = "probe sentinel"};
  ordinary_state.last_error = fused_state.last_error;
  // The ordinary pipeline observes error_token after reporting a malformed
  // scalar. Keep this sentinel in range so StringViewImpl can produce the same
  // partial output instead of making the test fixture itself throw substr().
  // The dedicated errored-state test below retains arbitrary observer fields.
  fused_state.error_token = {.offset = 0,
                             .length = 0,
                             .type = TokenType::Error,
                             .context = 29};
  ordinary_state.error_token = fused_state.error_token;

  std::string_view ordinary_key;
  constexpr std::string_view sentinel = "unchanged output";
  auto ordinary_value = sentinel;
  const bool ordinary_ok = read_member_ordinary(
      ordinary_reader, ordinary_state, ordinary_key, ordinary_value);

  const State saved = fused_state;
  auto fused_value = sentinel;
  const bool fused_ok =
      fused_reader.template try_read_const_string_view<"id">(fused_state,
                                                               fused_value);
  EXPECT_EQ(fused_ok, ordinary_ok && ordinary_key == "id");

  if (fused_ok) {
    expect_same_view(fused_value, ordinary_value);
    expect_same_state(fused_state, ordinary_state);
    State fused_next = fused_state;
    State ordinary_next = ordinary_state;
    EXPECT_EQ(fused_reader.next_token(fused_next),
              ordinary_reader.next_token(ordinary_next));
    expect_same_state(fused_next, ordinary_next);
    return;
  }

  expect_same_state(fused_state, saved);
  expect_same_view(fused_value, sentinel);
  std::string_view fallback_key;
  auto fallback_value = sentinel;
  const bool fallback_ok = read_member_ordinary(
      fused_reader, fused_state, fallback_key, fallback_value);
  EXPECT_EQ(fallback_ok, ordinary_ok);
  expect_same_view(fallback_key, ordinary_key);
  expect_same_view(fallback_value, ordinary_value);
  expect_same_state(fused_state, ordinary_state);
}

template <json::InputPadding Padding>
void compare_every_prefix(std::string_view input) {
  for (std::size_t length = 0; length <= input.size(); ++length) {
    SCOPED_TRACE(length);
    compare_probe_with_ordinary<Padding>(input.substr(0, length));
  }
}

TEST(ConstViewFusion, SelectsReaderProbeForConstCallableStoreAndNullTails) {
  CountingReader::const_view_calls = 0;
  CountingReader::scan_scalar_calls = 0;
  ViewObject output;
  const auto error =
      decode_into<CountingReader>(output, R"({"name":"a\"b","none":null})");
  ASSERT_TRUE(error.is_ok()) << error.detail;
  EXPECT_EQ(CountingReader::const_view_calls, 2);
  EXPECT_EQ(CountingReader::scan_scalar_calls, 0);
  EXPECT_EQ(output.name.value().value(), R"(a\"b)");
  EXPECT_EQ(output.none.value().value(), nullptr);
}

TEST(ConstViewFusion, InvokesConstTailOverloadAfterMutableReaderProbe) {
  CountingReader::const_view_calls = 0;
  ConstObservedAssign::const_calls = 0;
  ConstObservedAssign::mutable_calls = 0;
  ConstTailObject output;
  const auto error = decode_into<CountingReader>(output, R"({"name":"x"})");
  ASSERT_TRUE(error.is_ok()) << error.detail;
  EXPECT_EQ(CountingReader::const_view_calls, 1);
  EXPECT_EQ(ConstObservedAssign::const_calls, 1);
  EXPECT_EQ(ConstObservedAssign::mutable_calls, 0);
  EXPECT_EQ(output.name.value().value(), "x");

  const Token fused_token = ConstObservedAssign::observed_token;
  const auto fused_pos_offset = ConstObservedAssign::observed_pos_offset;
  ConstObservedAssign::const_calls = 0;
  ConstObservedAssign::mutable_calls = 0;
  OrdinaryReader::disabled_const_view_calls = 0;
  OrdinaryReader::scan_scalar_calls = 0;
  ConstTailObject ordinary_output;
  const auto ordinary_error =
      decode_into<OrdinaryReader>(ordinary_output, R"({"name":"x"})");
  ASSERT_TRUE(ordinary_error.is_ok()) << ordinary_error.detail;
  EXPECT_EQ(ConstObservedAssign::const_calls, 1);
  EXPECT_EQ(ConstObservedAssign::mutable_calls, 0);
  EXPECT_EQ(OrdinaryReader::disabled_const_view_calls, 1);
  EXPECT_EQ(OrdinaryReader::scan_scalar_calls, 1);
  expect_same_token(fused_token, ConstObservedAssign::observed_token);
  EXPECT_EQ(fused_pos_offset, ConstObservedAssign::observed_pos_offset);
  EXPECT_EQ(ordinary_output.name.value().value(), output.name.value().value());
}

TEST(ConstViewFusion, KeepsExistingMutableTailGate) {
  CountingReader::const_view_calls = 0;
  MutableObservedAssign::calls = 0;
  MutableTailObject output;
  const auto error = decode_into<CountingReader>(output, R"({"name":"x"})");
  ASSERT_TRUE(error.is_ok()) << error.detail;
  EXPECT_EQ(CountingReader::const_view_calls, 1);
  EXPECT_EQ(MutableObservedAssign::calls, 1);
  EXPECT_EQ(output.name.value().value(), "x");
}

TEST(ConstViewFusion, DoesNotCallMutableHookForUnrelatedConstOnlyPipeline) {
  ConstOnlyStep::calls = 0;
  ConstOnlyStep::forbidden_mutable_fusion_calls = 0;
  ConstOnlyObject output;
  const auto error = decode_into<CountingReader>(output, R"({"id":42})");
  ASSERT_TRUE(error.is_ok()) << error.detail;
  EXPECT_EQ(ConstOnlyStep::forbidden_mutable_fusion_calls, 0);
  EXPECT_EQ(ConstOnlyStep::calls, 1);
  EXPECT_EQ(output.id.value().value(), 91);
}

TEST(ConstViewFusion, CustomScanWithoutSelfTagOptsOutOfConstAndMutableFusion) {
  OptOutReader::const_view_calls = 0;
  OptOutReader::scan_scalar_calls = 0;
  MutableObservedAssign::calls = 0;

  ViewObject const_output;
  const auto const_error = decode_into<OptOutReader>(
      const_output, R"({"name":"raw\"view","none":null})");
  ASSERT_TRUE(const_error.is_ok()) << const_error.detail;
  EXPECT_EQ(const_output.name.value().value(), R"(raw\"view)");
  EXPECT_EQ(const_output.none.value().value(), nullptr);

  MutableTailObject mutable_output;
  const auto mutable_error =
      decode_into<OptOutReader>(mutable_output, R"({"name":"mutable"})");
  ASSERT_TRUE(mutable_error.is_ok()) << mutable_error.detail;
  EXPECT_EQ(mutable_output.name.value().value(), "mutable");

  EXPECT_EQ(OptOutReader::const_view_calls, 0);
  EXPECT_EQ(OptOutReader::scan_scalar_calls, 3);
  EXPECT_EQ(MutableObservedAssign::calls, 1);
}

TEST(ConstViewFusion, HookMatchesOrdinaryScalarViewsAtEveryPrefix) {
  for (std::string_view input : {
           R"({"id":"plain"})", R"({ "id" : "a\"b\u263a" })",
           R"({"id":"raw invalid escape \q stays a view"})",
           R"({"id":""})", R"({"id":123})", R"({"id":-2.5e+3})",
           R"({"id":true})", R"({"id":false})", R"({"id":null})",
           R"({"id":"123"})", R"({"id":"true"})",
           R"({"id":"null"})", R"({"id":"unterminated})",
           R"({"id":})", R"({"id":{}})", R"({"id":[]})",
           R"({"idx":"wrong key"})", R"({"i)", R"({id:"value"})",
           R"({"id" "missing colon"})", R"({"id":truX})"}) {
    compare_every_prefix<json::InputPadding::none>(input);
  }
}

TEST(ConstViewFusion, FailedProbePreservesPreexistingErrorAndOutput) {
  CountingReader reader;
  CountingReader::state_type state;
  reader.reset(state, R"({"id":"x"})");
  ASSERT_TRUE(reader.next_token(state));
  state.last_error = {.code = ErrorCode::candidate_rejected,
                      .offset = 91,
                      .detail = "preexisting error"};
  state.error_flag = true;
  state.error_token = {.offset = 17,
                       .length = 23,
                       .type = TokenType::Error,
                       .context = 29};
  const auto saved = state;
  constexpr std::string_view sentinel = "unchanged output";
  auto output = sentinel;
  EXPECT_FALSE((reader.template try_read_const_string_view<"id">(state,
                                                                   output)));
  expect_same_state(state, saved);
  expect_same_view(output, sentinel);
}

void compare_decoder_fallback(std::string_view input) {
  TwoStrings actual;
  TwoStrings expected;
  actual.first.value().value() = "seed first";
  actual.second.value().value() = "seed second";
  expected.first.value().value() = "seed first";
  expected.second.value().value() = "seed second";
  const auto actual_error = decode_into<CountingReader>(actual, input);
  const auto expected_error = decode_into<OrdinaryReader>(expected, input);
  expect_same_error(actual_error, expected_error);
  EXPECT_EQ(actual.first.value().value(), expected.first.value().value());
  EXPECT_EQ(actual.second.value().value(), expected.second.value().value());
}

TEST(ConstViewFusion, DecoderFallbackPreservesErrorsAndPartialOutput) {
  for (std::string_view input : {
           R"({"first":"ok","second":"done"})",
           R"({"first":"ok","second":"unterminated})",
           R"({"first":"ok","second":{}})",
           R"({"first":"ok","second":[]})",
           R"({"first":"ok","second":})",
           R"({"first":"ok","wrong":"x"})",
           R"({"first":"ok","second" "missing colon"})",
           R"({"first":"ok","second":true} trailing)"}) {
    SCOPED_TRACE(std::string(input));
    compare_decoder_fallback(input);
  }
}

#if defined(__unix__) || defined(__APPLE__)
template <json::InputPadding Padding>
void compare_guarded_prefixes(std::string_view input) {
  const auto page = static_cast<std::size_t>(sysconf(_SC_PAGESIZE));
  auto* memory = static_cast<char*>(mmap(nullptr, 2 * page,
      PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0));
  ASSERT_NE(memory, MAP_FAILED);
  ASSERT_EQ(mprotect(memory + page, page, PROT_NONE), 0);
  constexpr auto padding = static_cast<std::size_t>(Padding);
  ASSERT_LT(input.size() + padding, page);
  for (std::size_t length = 0; length <= input.size(); ++length) {
    SCOPED_TRACE(length);
    char* begin = memory + page - padding - length;
    std::memcpy(begin, input.data(), length);
    if constexpr (padding != 0) std::memset(begin + length, 0, padding);
    compare_probe_with_ordinary<Padding>(std::string_view(begin, length));
  }
  EXPECT_EQ(munmap(memory, 2 * page), 0);
}

TEST(ConstViewFusion, NoneAndPaddedReadersStayWithinGuardPage) {
  for (std::string_view input : {
           R"({ "id" : "escaped\"view\u263a" })",
           R"({ "id" : true })", R"({ "id" : "unterminated)"}) {
    SCOPED_TRACE(std::string(input));
    compare_guarded_prefixes<json::InputPadding::none>(input);
    compare_guarded_prefixes<json::InputPadding::zero_padded_4>(input);
    compare_guarded_prefixes<json::InputPadding::zero_padded_8>(input);
  }
}
#endif

}  // namespace
