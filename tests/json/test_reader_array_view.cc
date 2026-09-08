#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <type_traits>

#if defined(__unix__) || defined(__APPLE__)
#include <sys/mman.h>
#include <unistd.h>
#endif

#include "hosq/hosq.hh"
#include "hosq/json/fast_reader.hh"

namespace array_view_test {

using namespace hosq;

struct FusedTag;
struct OrdinaryTag;
struct CustomScanTag;
struct CustomDecodeTag;

template <class Tag, json::InputPadding Padding = json::InputPadding::none>
struct InstrumentedReader : json::FastReader<128, Padding> {
  using Base = json::FastReader<128, Padding>;
  static constexpr auto padding = Padding;
  using input_type = typename Base::input_type;
  using state_type = typename Base::state_type;

  static inline int array_probe_calls = 0;

  template <class T, std::size_t N>
    requires(!std::is_same_v<Tag, OrdinaryTag>)
  bool try_read_number_array(state_type& state,
                             std::array<T, N>& output) const {
    ++array_probe_calls;
    return Base::try_read_number_array(state, output);
  }
};

template <json::InputPadding Padding = json::InputPadding::none>
using FusedReader = json::FastReader<128, Padding>;

template <json::InputPadding Padding = json::InputPadding::none>
using OrdinaryReader = InstrumentedReader<OrdinaryTag, Padding>;

using CustomScanReader = InstrumentedReader<CustomScanTag>;
using CustomDecodeReader = InstrumentedReader<CustomDecodeTag>;

template <class Reader, class Storage, TryPhase Phase>
void resolve_fast_scalar(const DecodeContext<Storage, Reader, Phase>& ctx) {
  using Base = json::FastReader<128, Reader::padding>;
  auto& reader = const_cast<Base&>(static_cast<const Base&>(ctx.reader()));
  auto& state = const_cast<typename Base::state_type&>(ctx.reader_state());
  const DecodeContext<void, Base> canonical{reader, state};
  (void)ext::ScanScalar<Base>::resolved(canonical);
}

template <class Reader>
struct ResolvingScanScalar {
  static inline int calls = 0;

  template <class Storage, TryPhase Phase>
  static void resolve(const DecodeContext<Storage, Reader, Phase>& ctx) {
    ++calls;
    resolve_fast_scalar(ctx);
  }
};

template <class T, std::size_t N>
inline constexpr auto fixed_numbers =
    scan::array |
    decode::collect<scan::scalar | decode::number<T>, N>;

template <class Reader, class T, std::size_t N>
auto invoke_fixed(Reader& reader, typename Reader::state_type& state)
    -> std::array<std::remove_cvref_t<T>, N> {
  DecodeContext<void, Reader, TryPhase::normal> ctx{reader, state};
  return std::remove_cvref_t<decltype(fixed_numbers<T, N>)>::invoke(ctx);
}

}  // namespace array_view_test

namespace hosq::ext {

template <json::InputPadding Padding>
struct ScanScalar<array_view_test::InstrumentedReader<
    array_view_test::OrdinaryTag, Padding>>
    : array_view_test::ResolvingScanScalar<
          array_view_test::OrdinaryReader<Padding>> {

  template <class Storage, TryPhase Phase>
  auto operator()(const DecodeContext<
                  Storage, array_view_test::OrdinaryReader<Padding>, Phase>&
                      ctx) const -> ScalarView {
    this->resolve(ctx);
    return {};
  }
};

template <>
struct ScanScalar<array_view_test::CustomScanReader>
    : array_view_test::ResolvingScanScalar<
          array_view_test::CustomScanReader> {
  template <class Storage, TryPhase Phase>
  auto operator()(const DecodeContext<Storage,
                                      array_view_test::CustomScanReader,
                                      Phase>& ctx) const -> ScalarView {
    this->resolve(ctx);
    return {};
  }
};

template <>
struct ScanScalar<array_view_test::CustomDecodeReader>
    : array_view_test::ResolvingScanScalar<
          array_view_test::CustomDecodeReader> {

  template <class Storage, TryPhase Phase>
  auto operator()(const DecodeContext<Storage,
                                      array_view_test::CustomDecodeReader,
                                      Phase>& ctx) const -> ScalarView {
    this->resolve(ctx);
    return {};
  }
};

template <>
struct DecodeNumber<array_view_test::CustomDecodeReader> {
  static inline int calls = 0;

  template <class T, class Context>
  auto operator()(const ScalarView&, const Context&) const
      -> std::remove_cvref_t<T> {
    ++calls;
    return static_cast<std::remove_cvref_t<T>>(77);
  }
};

}  // namespace hosq::ext

namespace {

using namespace hosq;
using namespace array_view_test;

template <class State>
void expect_same_state(const State& actual, const State& expected) {
  EXPECT_EQ(actual.begin, expected.begin);
  EXPECT_EQ(actual.pos, expected.pos);
  EXPECT_EQ(actual.end, expected.end);
  EXPECT_EQ(actual.input_view.data(), expected.input_view.data());
  EXPECT_EQ(actual.input_view.size(), expected.input_view.size());
  EXPECT_EQ(actual.current.offset, expected.current.offset);
  EXPECT_EQ(actual.current.length, expected.current.length);
  EXPECT_EQ(actual.current.type, expected.current.type);
  EXPECT_EQ(actual.current.context, expected.current.context);
  EXPECT_EQ(actual.ctx_stack, expected.ctx_stack);
  EXPECT_EQ(actual.ctx_size, expected.ctx_size);
  EXPECT_EQ(actual.expect, expected.expect);
  EXPECT_EQ(actual.last_error.code, expected.last_error.code);
  EXPECT_EQ(actual.last_error.offset, expected.last_error.offset);
  EXPECT_EQ(actual.last_error.detail, expected.last_error.detail);
  EXPECT_EQ(actual.error_flag, expected.error_flag);
  EXPECT_EQ(actual.error_token.offset, expected.error_token.offset);
  EXPECT_EQ(actual.error_token.length, expected.error_token.length);
  EXPECT_EQ(actual.error_token.type, expected.error_token.type);
  EXPECT_EQ(actual.error_token.context, expected.error_token.context);
}

template <class T>
void expect_same_number(T actual, T expected) {
  if constexpr (std::is_floating_point_v<T>) {
    if (std::isnan(expected)) {
      EXPECT_TRUE(std::isnan(actual));
      return;
    }
    if (actual == 0 && expected == 0) {
      EXPECT_EQ(std::signbit(actual), std::signbit(expected));
    }
  }
  EXPECT_EQ(actual, expected);
}

template <class T, std::size_t N>
void expect_same_array(const std::array<T, N>& actual,
                       const std::array<T, N>& expected) {
  for (std::size_t index = 0; index < N; ++index) {
    SCOPED_TRACE(index);
    expect_same_number(actual[index], expected[index]);
  }
}

template <class T, std::size_t N,
          json::InputPadding Padding = json::InputPadding::none>
auto compare_fused_with_ordinary(std::string_view input,
                                 ErrorCode expected_error,
                                 bool check_expected_error = true)
    -> std::array<std::remove_cvref_t<T>, N> {
  SCOPED_TRACE(::testing::Message()
               << "padding=" << static_cast<unsigned>(Padding)
               << " input='" << input << "'");
  using Fused = FusedReader<Padding>;
  using Ordinary = OrdinaryReader<Padding>;

  Ordinary::array_probe_calls = 0;
  Fused fused_reader;
  Ordinary ordinary_reader;
  typename Fused::state_type fused_state;
  typename Ordinary::state_type ordinary_state;
  fused_reader.reset(fused_state, input);
  ordinary_reader.reset(ordinary_state, input);
  const bool fused_started = fused_reader.next_token(fused_state);
  const bool ordinary_started = ordinary_reader.next_token(ordinary_state);
  EXPECT_TRUE(fused_started);
  EXPECT_TRUE(ordinary_started);
  if (!fused_started || !ordinary_started) return {};
  EXPECT_EQ(fused_state.current.type, TokenType::ArrayBegin);
  EXPECT_EQ(ordinary_state.current.type, TokenType::ArrayBegin);
  if (fused_state.current.type != TokenType::ArrayBegin ||
      ordinary_state.current.type != TokenType::ArrayBegin) {
    return {};
  }

  const auto fused = invoke_fixed<Fused, T, N>(fused_reader, fused_state);
  const auto ordinary =
      invoke_fixed<Ordinary, T, N>(ordinary_reader, ordinary_state);

  EXPECT_EQ(Ordinary::array_probe_calls, 0);
  expect_same_array(fused, ordinary);
  expect_same_state(fused_state, ordinary_state);
  if (check_expected_error) {
    EXPECT_EQ(fused_reader.error(fused_state).code, expected_error);
  } else {
    EXPECT_NE(fused_reader.error(fused_state).code, ErrorCode::ok);
  }
  return fused;
}

TEST(ReaderArrayView, FixedSizesQuotedNumbersWhitespaceAndFinalStateMatch) {
  EXPECT_EQ((compare_fused_with_ordinary<std::int64_t, 0>(
                "[ \t\n\r ]", ErrorCode::ok)),
            (std::array<std::int64_t, 0>{}));
  EXPECT_EQ((compare_fused_with_ordinary<std::int64_t, 1>(
                R"(["42"])", ErrorCode::ok)),
            (std::array<std::int64_t, 1>{42}));
  EXPECT_EQ((compare_fused_with_ordinary<std::int64_t, 4>(
                "[ 1,\n\"2\", -0,\t4 ]", ErrorCode::ok)),
            (std::array<std::int64_t, 4>{1, 2, 0, 4}));
  EXPECT_EQ((compare_fused_with_ordinary<std::int64_t, 6>(
                "[-1,2,3,4,5,6]", ErrorCode::ok)),
            (std::array<std::int64_t, 6>{-1, 2, 3, 4, 5, 6}));

  const auto negative_zero = compare_fused_with_ordinary<double, 4>(
      R"([-0,-0.0,"-0",1.25])", ErrorCode::ok);
  EXPECT_TRUE(std::signbit(negative_zero[0]));
  EXPECT_TRUE(std::signbit(negative_zero[1]));
  EXPECT_TRUE(std::signbit(negative_zero[2]));
  EXPECT_EQ(negative_zero[3], 1.25);
}

TEST(ReaderArrayView, FailedProbePreservesConcreteStateAndOutput) {
  using Reader = json::FastReader<>;
  for (std::string_view input : {
           "[1,2]", "[1,2,3,4,5]", "[1,2x,3,4]",
           "[1,9223372036854775808,3,4]", "[1,2,3,]",
           "[1,2,3,4", "[1,2,3,4}", R"([1,"2x",3,4])"}) {
    SCOPED_TRACE(std::string(input));
    Reader reader;
    Reader::state_type state;
    reader.reset(state, input);
    ASSERT_TRUE(reader.next_token(state));
    ASSERT_EQ(state.current.type, TokenType::ArrayBegin);
    state.last_error = {.code = ErrorCode::candidate_rejected,
                        .offset = 91,
                        .detail = "probe sentinel"};
    state.error_token = {.offset = 17,
                         .length = 23,
                         .type = TokenType::Error,
                         .context = 29};
    const auto saved = state;
    std::array<std::int64_t, 4> output{71, 72, 73, 74};
    const auto saved_output = output;

    EXPECT_FALSE(reader.try_read_number_array(state, output));
    expect_same_state(state, saved);
    EXPECT_EQ(output, saved_output);
  }
}

TEST(ReaderArrayView, FallbackErrorsAndPartialResultsMatchOrdinaryCollect) {
  EXPECT_EQ((compare_fused_with_ordinary<std::int64_t, 4>(
                "[1,9223372036854775808,3,4]", ErrorCode::out_of_range)),
            (std::array<std::int64_t, 4>{1, 0, 0, 0}));
  EXPECT_EQ((compare_fused_with_ordinary<std::int64_t, 4>(
                "[1,2x,3,4]", ErrorCode::invalid_number)),
            (std::array<std::int64_t, 4>{1, 0, 0, 0}));
  EXPECT_EQ((compare_fused_with_ordinary<std::int64_t, 4>(
                "[1,2]", ErrorCode::invalid_length)),
            (std::array<std::int64_t, 4>{1, 2, 0, 0}));
  EXPECT_EQ((compare_fused_with_ordinary<std::int64_t, 4>(
                "[1,2,3,4,5]", ErrorCode::invalid_length)),
            (std::array<std::int64_t, 4>{1, 2, 3, 4}));
  EXPECT_EQ((compare_fused_with_ordinary<std::int64_t, 4>(
                "[1,2,3,4", ErrorCode::unexpected_eof)),
            (std::array<std::int64_t, 4>{1, 2, 3, 4}));
}

TEST(ReaderArrayView, CustomScalarAndNumberExtensionsOptOut) {
  CustomScanReader::array_probe_calls = 0;
  ext::ScanScalar<CustomScanReader>::calls = 0;
  CustomScanReader custom_scan;
  CustomScanReader::state_type scan_state;
  custom_scan.reset(scan_state, "[1,2,3,4]");
  ASSERT_TRUE(custom_scan.next_token(scan_state));
  const auto scanned =
      invoke_fixed<CustomScanReader, std::int64_t, 4>(custom_scan, scan_state);
  EXPECT_EQ(CustomScanReader::array_probe_calls, 0);
  EXPECT_EQ(ext::ScanScalar<CustomScanReader>::calls, 4);
  EXPECT_EQ(scanned, (std::array<std::int64_t, 4>{1, 2, 3, 4}));

  CustomDecodeReader::array_probe_calls = 0;
  ext::ScanScalar<CustomDecodeReader>::calls = 0;
  ext::DecodeNumber<CustomDecodeReader>::calls = 0;
  CustomDecodeReader custom_decode;
  CustomDecodeReader::state_type decode_state;
  custom_decode.reset(decode_state, "[1,2,3,4]");
  ASSERT_TRUE(custom_decode.next_token(decode_state));
  const auto decoded = invoke_fixed<CustomDecodeReader, std::int64_t, 4>(
      custom_decode, decode_state);
  EXPECT_EQ(CustomDecodeReader::array_probe_calls, 0);
  EXPECT_EQ(ext::ScanScalar<CustomDecodeReader>::calls, 4);
  EXPECT_EQ(ext::DecodeNumber<CustomDecodeReader>::calls, 4);
  EXPECT_EQ(decoded, (std::array<std::int64_t, 4>{77, 77, 77, 77}));
}

#if defined(__unix__) || defined(__APPLE__)
template <json::InputPadding Padding>
void compare_guarded_prefixes() {
  constexpr std::string_view input =
      R"([0,"1",-0,9223372036854775807,4,5])";
  const auto page = static_cast<std::size_t>(sysconf(_SC_PAGESIZE));
  auto* memory = static_cast<char*>(mmap(
      nullptr, 2 * page, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0));
  ASSERT_NE(memory, MAP_FAILED);
  ASSERT_EQ(mprotect(memory + page, page, PROT_NONE), 0);

  constexpr auto padding = static_cast<std::size_t>(Padding);
  for (std::size_t length = 1; length <= input.size(); ++length) {
    SCOPED_TRACE(::testing::Message()
                 << "guard padding=" << padding << " prefix=" << length);
    char* const begin = memory + page - padding - length;
    std::memcpy(begin, input.data(), length);
    if constexpr (padding != 0) std::memset(begin + length, 0, padding);
    const bool complete = length == input.size();
    (void)compare_fused_with_ordinary<std::int64_t, 6, Padding>(
        std::string_view(begin, length), ErrorCode::ok, complete);
  }

  EXPECT_EQ(munmap(memory, 2 * page), 0);
}

TEST(ReaderArrayView, GuardPageBoundsCoverAllPaddingContracts) {
  compare_guarded_prefixes<json::InputPadding::none>();
  compare_guarded_prefixes<json::InputPadding::zero_padded_4>();
  compare_guarded_prefixes<json::InputPadding::zero_padded_8>();
}
#endif

}  // namespace
