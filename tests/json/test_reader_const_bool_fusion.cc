#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
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

namespace const_bool_fusion_test {

using namespace hosq;

struct FusedTag {};
struct OrdinaryTag {};
struct OptOutTag {};

template <class Tag, json::InputPadding Padding = json::InputPadding::none>
struct InstrumentedReader : json::FastReader<128, Padding> {
  using Base = json::FastReader<128, Padding>;
  using input_type = typename Base::input_type;
  using state_type = typename Base::state_type;

  static inline int const_bool_calls = 0;
  static inline int scan_scalar_calls = 0;

  template <StringLiteral Key>
    requires(!std::is_same_v<Tag, OrdinaryTag>)
  bool try_read_const_boolean(state_type& state, bool& value) const {
    ++const_bool_calls;
    return Base::template try_read_const_boolean<Key>(state, value);
  }
};

template <json::InputPadding Padding = json::InputPadding::none>
using FusedReader = InstrumentedReader<FusedTag, Padding>;

template <json::InputPadding Padding = json::InputPadding::none>
using OrdinaryReader = InstrumentedReader<OrdinaryTag, Padding>;

using OptOutReader = InstrumentedReader<OptOutTag>;

template <class Reader, class Storage, TryPhase Phase>
void resolve_fast_scalar(const DecodeContext<Storage, Reader, Phase>& ctx) {
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
    if (!reader.resolve_skip(state)) {
      reader.report_error(state, reader.error(state));
    }
  } else if (token.context ==
             static_cast<std::uint8_t>(JsonTokenContext::string)) {
    state.pos = state.begin + token.offset + token.length + 1;
  } else {
    state.pos = state.begin + token.offset + token.length;
  }
}

struct ObservedAssign {
  template <class Input>
  using storage_type = std::remove_cvref_t<Input>;

  static inline int calls = 0;
  static inline Token token{};
  static inline std::size_t pos_offset = 0;

  template <class Input, class Context>
    requires(!std::is_const_v<Context>)
  void operator()(Input&& value, Context& ctx) const {
    ++calls;
    token = ctx.reader().current_token(ctx.reader_state());
    pos_offset = static_cast<std::size_t>(ctx.reader_state().pos -
                                          ctx.reader_state().begin);
    ctx.value() = std::forward<Input>(value);
  }
};

using HBool =
    Elem<scan::scalar | decode::boolean | store::assign, noop_encoder>;
using ObservedBool =
    Elem<scan::scalar | decode::boolean | step<ObservedAssign>, noop_encoder>;

template <class Value>
struct BoolObject {
  Entry<Key<"id">, Value> id;
};

using PlainObject = BoolObject<HBool>;
using ObservedObject = BoolObject<ObservedBool>;

}  // namespace const_bool_fusion_test

namespace hosq::ext {

template <json::InputPadding Padding>
struct ScanScalar<const_bool_fusion_test::InstrumentedReader<
    const_bool_fusion_test::FusedTag, Padding>> {
  using Reader = const_bool_fusion_test::FusedReader<Padding>;
  using reader_fusion = ScanScalar;

  template <class Storage, TryPhase Phase>
  auto operator()(const DecodeContext<Storage, Reader, Phase>& ctx) const
      -> ScalarView {
    ++Reader::scan_scalar_calls;
    const_bool_fusion_test::resolve_fast_scalar(ctx);
    return {};
  }
};

template <json::InputPadding Padding>
struct ScanScalar<const_bool_fusion_test::InstrumentedReader<
    const_bool_fusion_test::OrdinaryTag, Padding>> {
  using Reader = const_bool_fusion_test::OrdinaryReader<Padding>;
  using reader_fusion = ScanScalar;

  template <class Storage, TryPhase Phase>
  auto operator()(const DecodeContext<Storage, Reader, Phase>& ctx) const
      -> ScalarView {
    ++Reader::scan_scalar_calls;
    const_bool_fusion_test::resolve_fast_scalar(ctx);
    return {};
  }
};

template <>
struct ScanScalar<const_bool_fusion_test::OptOutReader> {
  using Reader = const_bool_fusion_test::OptOutReader;
  // Deliberately no reader_fusion self tag.
  template <class Storage, TryPhase Phase>
  auto operator()(const DecodeContext<Storage, Reader, Phase>& ctx) const
      -> ScalarView {
    ++Reader::scan_scalar_calls;
    const_bool_fusion_test::resolve_fast_scalar(ctx);
    return {};
  }
};

}  // namespace hosq::ext

namespace {

using namespace hosq;
using namespace const_bool_fusion_test;

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

template <class Reader>
bool read_member_ordinary(Reader& reader, typename Reader::state_type& state,
                          std::string_view& key, bool& value) {
  if (!next_token<Expect::next_member_or_end>(reader, state)) return false;
  DecodeContext<void, Reader> key_context{reader, state};
  key = (scan::scalar | decode::string_view).invoke(key_context);
  if (reader.error(state).has_error()) return false;
  if (!next_token<ReadPosition::member_value>(reader, state)) return false;
  DecodeContext<void, Reader> value_context{reader, state};
  value = (scan::scalar | decode::boolean).invoke(value_context);
  return reader.error(state).is_ok();
}

template <json::InputPadding Padding>
void compare_probe_with_ordinary(std::string_view input) {
  SCOPED_TRACE(std::string(input));
  using Fused = FusedReader<Padding>;
  using Ordinary = OrdinaryReader<Padding>;
  using State = typename Fused::state_type;

  Fused fused_reader;
  Ordinary ordinary_reader;
  State fused_state;
  State ordinary_state;
  fused_reader.reset(fused_state, input);
  ordinary_reader.reset(ordinary_state, input);

  const bool fused_root = fused_reader.next_token(fused_state);
  EXPECT_EQ(fused_root, ordinary_reader.next_token(ordinary_state));
  expect_same_state(fused_state, ordinary_state);
  if (!fused_root || fused_state.current.type != TokenType::ObjectBegin) {
    const State saved = fused_state;
    bool value = true;
    EXPECT_FALSE((fused_reader.template try_read_const_boolean<"id">(
        fused_state, value)));
    expect_same_state(fused_state, saved);
    EXPECT_TRUE(value);
    return;
  }

  fused_state.last_error = {.code = ErrorCode::candidate_rejected,
                            .offset = 91,
                            .detail = "probe sentinel"};
  ordinary_state.last_error = fused_state.last_error;
  // Keep the observer token inside every tested prefix. BooleanImpl still runs
  // after a scalar-resolution error and reads current_token().
  fused_state.error_token = {.offset = 0,
                             .length = 0,
                             .type = TokenType::Error,
                             .context = 29};
  ordinary_state.error_token = fused_state.error_token;

  std::string_view ordinary_key;
  bool ordinary_value = true;
  const bool ordinary_ok = read_member_ordinary(
      ordinary_reader, ordinary_state, ordinary_key, ordinary_value);

  const State saved = fused_state;
  bool fused_value = true;
  const bool fused_ok = fused_reader.template try_read_const_boolean<"id">(
      fused_state, fused_value);
  EXPECT_EQ(fused_ok, ordinary_ok && ordinary_key == "id");

  if (fused_ok) {
    EXPECT_EQ(fused_value, ordinary_value);
    expect_same_state(fused_state, ordinary_state);
    State fused_next = fused_state;
    State ordinary_next = ordinary_state;
    EXPECT_EQ(fused_reader.next_token(fused_next),
              ordinary_reader.next_token(ordinary_next));
    expect_same_state(fused_next, ordinary_next);
    return;
  }

  expect_same_state(fused_state, saved);
  EXPECT_TRUE(fused_value);
  std::string_view fallback_key;
  bool fallback_value = true;
  const bool fallback_ok = read_member_ordinary(
      fused_reader, fused_state, fallback_key, fallback_value);
  EXPECT_EQ(fallback_ok, ordinary_ok);
  EXPECT_EQ(fallback_key, ordinary_key);
  EXPECT_EQ(fallback_value, ordinary_value);
  expect_same_state(fused_state, ordinary_state);
}

template <json::InputPadding Padding>
void compare_every_prefix(std::string_view input) {
  for (std::size_t length = 0; length <= input.size(); ++length) {
    SCOPED_TRACE(length);
    compare_probe_with_ordinary<Padding>(input.substr(0, length));
  }
}

TEST(ConstBoolFusion, ProbeMatchesOrdinaryAcrossValuesSuffixesAndPrefixes) {
  for (std::string input : {
           R"({"id":true})", R"({ "id" : false })",
           R"({"id":"true"})", R"({"id":"false"})",
           R"({"id":trueX})", R"({"id":falseX})",
           R"({"id":truefalse})", R"({"id":tru})", R"({"id":fals})",
           R"({"id":"trueX"})", R"({"id":"falseX"})",
           R"({"id":"true})", R"({"id":TRUE})", R"({"id":null})",
           R"({"id":1})", R"({"idx":true})", R"({"i\u0064":true})",
           R"({"id":true:})", R"({"id":"true"X})"}) {
    compare_every_prefix<json::InputPadding::none>(input);
  }

  std::string control = R"({"id":true)";
  control.push_back('\x01');
  control.push_back('}');
  compare_every_prefix<json::InputPadding::none>(control);
}

TEST(ConstBoolFusion, ExistingErrorMakesProbeMutationFree) {
  FusedReader<> reader;
  FusedReader<>::state_type state;
  reader.reset(state, R"({"id":true})");
  ASSERT_TRUE(reader.next_token(state));
  ASSERT_EQ(state.current.type, TokenType::ObjectBegin);

  state.last_error = {.code = ErrorCode::invalid_number,
                      .offset = 6,
                      .detail = "pre-existing error"};
  state.error_flag = true;
  state.error_token = {.offset = 6,
                       .length = 4,
                       .type = TokenType::Error,
                       .context = 31};
  const auto saved = state;
  bool value = true;
  EXPECT_FALSE(reader.template try_read_const_boolean<"id">(state, value));
  expect_same_state(state, saved);
  EXPECT_TRUE(value);
}

TEST(ConstBoolFusion, DecoderSelectsFusionAndMutableTailSeesResolvedToken) {
  FusedReader<>::const_bool_calls = 0;
  FusedReader<>::scan_scalar_calls = 0;
  ObservedAssign::calls = 0;
  ObservedAssign::token = {};
  ObservedAssign::pos_offset = 0;

  ObservedObject output;
  const auto error = decode_into<FusedReader<>>(output, R"({"id":"false"})");
  ASSERT_TRUE(error.is_ok()) << error.detail;
  EXPECT_EQ(FusedReader<>::const_bool_calls, 1);
  EXPECT_EQ(FusedReader<>::scan_scalar_calls, 0);
  EXPECT_EQ(ObservedAssign::calls, 1);
  EXPECT_FALSE(output.id.value().value());
  EXPECT_EQ(ObservedAssign::token.type, TokenType::Scalar);
  EXPECT_EQ(ObservedAssign::token.context,
            static_cast<std::uint8_t>(JsonTokenContext::string));
  EXPECT_EQ(ObservedAssign::token.length, 5U);
  EXPECT_EQ(ObservedAssign::pos_offset, 13U);
}

TEST(ConstBoolFusion, InvalidValuesFallbackWithIdenticalPartialOutput) {
  for (std::string_view input : {R"({"id":trueX})", R"({"id":null})",
                                 R"({"id":"falseX"})"}) {
    SCOPED_TRACE(std::string(input));
    FusedReader<>::const_bool_calls = 0;
    OrdinaryReader<>::const_bool_calls = 0;
    PlainObject fused;
    PlainObject ordinary;
    fused.id.value().value() = true;
    ordinary.id.value().value() = true;
    const auto fused_error = decode_into<FusedReader<>>(fused, input);
    const auto ordinary_error = decode_into<OrdinaryReader<>>(ordinary, input);
    EXPECT_EQ(FusedReader<>::const_bool_calls, 1);
    EXPECT_EQ(OrdinaryReader<>::const_bool_calls, 0);
    expect_same_error(fused_error, ordinary_error);
    EXPECT_FALSE(fused_error.is_ok());
    EXPECT_EQ(fused.id.value().value(), ordinary.id.value().value());
    EXPECT_FALSE(fused.id.value().value());
  }
}

TEST(ConstBoolFusion, CustomScalarExtensionPreventsFusion) {
  OptOutReader::const_bool_calls = 0;
  OptOutReader::scan_scalar_calls = 0;
  PlainObject output;
  const auto error = decode_into<OptOutReader>(output, R"({"id":true})");
  ASSERT_TRUE(error.is_ok()) << error.detail;
  EXPECT_EQ(OptOutReader::const_bool_calls, 0);
  EXPECT_EQ(OptOutReader::scan_scalar_calls, 1);
  EXPECT_TRUE(output.id.value().value());
}

#if defined(__unix__) || defined(__APPLE__)
template <json::InputPadding Padding>
void compare_guarded_prefixes(std::string_view input) {
  const auto page = static_cast<std::size_t>(sysconf(_SC_PAGESIZE));
  auto* memory = static_cast<char*>(mmap(
      nullptr, 2 * page, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0));
  ASSERT_NE(memory, MAP_FAILED);
  ASSERT_EQ(mprotect(memory + page, page, PROT_NONE), 0);

  constexpr auto padding = static_cast<std::size_t>(Padding);
  ASSERT_LT(input.size() + padding, page);
  for (std::size_t length = 0; length <= input.size(); ++length) {
    SCOPED_TRACE(::testing::Message()
                 << "padding=" << padding << " prefix=" << length);
    char* const begin = memory + page - padding - length;
    if (length != 0) std::memcpy(begin, input.data(), length);
    if constexpr (padding != 0) std::memset(begin + length, 0, padding);
    compare_probe_with_ordinary<Padding>(std::string_view(begin, length));
  }

  EXPECT_EQ(munmap(memory, 2 * page), 0);
}

TEST(ConstBoolFusion, GuardPageBoundsCoverTrueFalseAndAllPaddingContracts) {
  for (std::string_view input : {R"({"id":trueX})", R"({"id":falseX})",
                                 R"({"id":"false"})"}) {
    compare_guarded_prefixes<json::InputPadding::none>(input);
    compare_guarded_prefixes<json::InputPadding::zero_padded_4>(input);
    compare_guarded_prefixes<json::InputPadding::zero_padded_8>(input);
  }
}
#endif

}  // namespace
