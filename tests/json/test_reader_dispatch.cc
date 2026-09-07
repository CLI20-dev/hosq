#include <gtest/gtest.h>

#include <random>
#include <string>
#include <type_traits>

#include "hosq/hosq.hh"
#include "hosq/json/fast_reader.hh"
#include "hosq/json/simple_reader.hh"

namespace {
using hosq::Expect;
using hosq::JsonTokenContext;
using hosq::TokenType;

enum class CustomHint { string, number };

struct FallbackReader : hosq::ReaderBase<FallbackReader> {
  bool next_token_impl(int& state) noexcept { ++state; return true; }
};
struct SpecializedReader : hosq::ReaderBase<SpecializedReader> {
  bool next_token_impl(int& state) const noexcept { state = 1; return true; }
  template <auto... Hints>
    requires(sizeof...(Hints) == 2)
  bool next_token_impl(int& state) const noexcept { state = 2; return true; }
};
struct LegacyReader {
  bool next_token(int& state) { ++state; return true; }
};
template <class R>
concept ConstNext = requires(const R& r, int& state) { r.next_token(state); };
static_assert(!ConstNext<FallbackReader>);
static_assert(ConstNext<SpecializedReader>);
static_assert(noexcept(hosq::next_token<Expect::value>(
    std::declval<FallbackReader&>(), std::declval<int&>())));
static_assert(!noexcept(hosq::next_token<Expect::value>(
    std::declval<LegacyReader&>(), std::declval<int&>())));

static_assert(std::is_empty_v<hosq::json::FastReader<>>);
static_assert(std::is_empty_v<hosq::json::SimpleReader>);
static_assert(std::is_empty_v<FallbackReader>);
static_assert(std::is_empty_v<SpecializedReader>);
static_assert(noexcept(std::declval<FallbackReader&>().next_token<Expect::value>(
    std::declval<int&>())));
static_assert(noexcept(std::declval<const SpecializedReader&>().next_token<
    CustomHint::string, CustomHint::number>(std::declval<int&>())));
static_assert(hosq::ReaderConcept<hosq::json::SimpleReader>);
static_assert(hosq::ReaderConcept<hosq::json::FastReader<>>);

TEST(ReaderDispatch, OptionalSpecializationAndCustomVocabulary) {
  FallbackReader fallback;
  int state = 0;
  EXPECT_TRUE((fallback.next_token<CustomHint::string>(state)));
  EXPECT_EQ(state, 1);
  EXPECT_TRUE(fallback.next_token(state));
  EXPECT_EQ(state, 2);
  const SpecializedReader specialized;
  EXPECT_TRUE((specialized.next_token<CustomHint::string, CustomHint::number>(state)));
  EXPECT_EQ(state, 2);
  EXPECT_TRUE(specialized.next_token<Expect::value>(state));
  EXPECT_EQ(state, 1);
  LegacyReader legacy;
  EXPECT_TRUE(hosq::next_token<Expect::value>(legacy, state));
  EXPECT_EQ(state, 2);
}

struct LegacySimpleReader : hosq::json::SimpleReader {
  bool next_token(state_type& state) { return next_token_impl(state); }
};
TEST(ReaderDispatch, DecoderAcceptsLegacyReaderWithoutTemplateAPI) {
  using namespace hosq;
  using Values = Elem<scan::array | decode::each<scan::scalar |
                         decode::number_into<int>>, noop_encoder>;
  Values values;
  auto error = decode_into<LegacySimpleReader>(values, "[1,2,3]");
  ASSERT_TRUE(error.is_ok());
  EXPECT_EQ(values.storage(), (std::vector<int>{1, 2, 3}));
}

// Every hint must preserve the generic protocol even when an extension has
// advanced the cursor to a different phase, or input has the wrong value kind.
template <Expect E, JsonTokenContext Kind = JsonTokenContext::none>
void compare_stream(std::string_view input) {
  const hosq::json::FastReader<8> reader;
  hosq::json::FastReader<8>::state_type generic, specialized;
  reader.reset(generic, input);
  reader.reset(specialized, input);
  for (unsigned step = 0; step < 256; ++step) {
    const bool a = reader.next_token(generic);
    const bool b = reader.next_token<E, Kind>(specialized);
    ASSERT_EQ(a, b);
    EXPECT_EQ(reader.error(generic).code, reader.error(specialized).code);
    EXPECT_EQ(reader.error(generic).offset, reader.error(specialized).offset);
    EXPECT_EQ(reader.error(generic).detail, reader.error(specialized).detail);
    EXPECT_EQ(generic.pos - generic.begin, specialized.pos - specialized.begin);
    EXPECT_EQ(generic.expect, specialized.expect);
    EXPECT_EQ(generic.ctx_size, specialized.ctx_size);
    if (!a) return;
    const auto& x = reader.current_token(generic);
    const auto& y = reader.current_token(specialized);
    EXPECT_EQ(x.type, y.type);
    EXPECT_EQ(x.context, y.context);
    EXPECT_EQ(x.offset, y.offset);
    EXPECT_EQ(x.length, y.length);
    if (x.type == TokenType::End) return;
  }
  FAIL() << "reader did not finish";
}

void compare_hints(std::string_view input) {
  SCOPED_TRACE(std::string(input));
  compare_stream<Expect::value>(input);
  compare_stream<Expect::member_value>(input);
  compare_stream<Expect::next_member_or_end>(input);
  compare_stream<Expect::next_element_or_end>(input);
  compare_stream<Expect::document_end>(input);
  compare_stream<Expect::value, JsonTokenContext::string>(input);
  compare_stream<Expect::member_value, JsonTokenContext::string>(input);
  compare_stream<Expect::next_element_or_end, JsonTokenContext::string>(input);
  compare_stream<Expect::value, JsonTokenContext::literal>(input);
  compare_stream<Expect::member_value, JsonTokenContext::literal>(input);
  compare_stream<Expect::next_element_or_end, JsonTokenContext::literal>(input);
}

TEST(ReaderDispatch, ValidMalformedAndTruncatedStreamsPreserveBehavior) {
  for (std::string_view input : {
           "", "1", "true", "null", "\"text\"", "[]", "{}",
           "[1,\"two\",true,null,{},[3]]",
           "{\"a\":[1,2],\"b\":{\"c\":\"d\"}}",
           " { \"escaped\\\"key\" : \"a\\nb\", \"x\": 1.2e-3 } ",
           "[1,]", "{\"a\":1,}", "{\"a\" 1}", "[1}", "{]",
           "[1 2]", "1 2", "\"\\uZZZZ\"", "[[[[[[[[[0]]]]]]]]]"}) {
    compare_hints(input);
    for (std::size_t n = 0; n < input.size(); ++n)
      compare_hints(input.substr(0, n));
  }
}

TEST(ReaderDispatch, DeterministicMalformedInputsPreserveBehavior) {
  std::mt19937 random(20260906);
  constexpr std::string_view alphabet = "{}[],:\"\\ 0129.e-+truefalsnux\n";
  for (int i = 0; i < 1000; ++i) {
    std::string input;
    const auto length = random() % 64;
    for (unsigned j = 0; j < length; ++j)
      input += alphabet[random() % alphabet.size()];
    compare_hints(input);
  }
}

TEST(ReaderDispatch, LatchedErrorsAndUnsupportedPacksUseSameProtocol) {
  hosq::json::FastReader<> reader;
  hosq::json::FastReader<>::state_type state;
  reader.reset(state, "42");
  EXPECT_TRUE((reader.next_token<CustomHint::string, CustomHint::number>(state)));
  EXPECT_EQ(reader.current_token(state).type, TokenType::Scalar);
  reader.report_error(state, {.code = hosq::ErrorCode::type_mismatch});
  EXPECT_FALSE(reader.next_token<Expect::member_value>(state));
  EXPECT_EQ(reader.error(state).code, hosq::ErrorCode::type_mismatch);
}
}  // namespace
