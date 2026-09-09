#include <gtest/gtest.h>

#include <array>
#include <cstring>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#if defined(__unix__) || defined(__APPLE__)
#include <sys/mman.h>
#include <unistd.h>
#endif

#include "hosq/hosq.hh"
#include "hosq/json/fast_reader.hh"

namespace {
using hosq::ErrorCode;
using hosq::Token;
using hosq::json::InputPadding;
using hosq::json::WhitespaceMode;

using DefaultReader = hosq::json::FastReader<>;
using CompactReader =
    hosq::json::FastReader<128, InputPadding::none, WhitespaceMode::Compact>;
using PrettyReader =
    hosq::json::FastReader<128, InputPadding::none, WhitespaceMode::Pretty>;
static_assert(DefaultReader::whitespace_mode == WhitespaceMode::Auto);
static_assert(std::is_empty_v<DefaultReader>);
static_assert(std::is_empty_v<CompactReader>);
static_assert(std::is_empty_v<PrettyReader>);
static_assert(
    std::is_same_v<DefaultReader::state_type, CompactReader::state_type>);
static_assert(
    std::is_same_v<DefaultReader::state_type, PrettyReader::state_type>);

struct Trace {
  std::vector<Token> tokens;
  ErrorCode error{ErrorCode::ok};
  std::size_t error_offset{};
};

template <InputPadding Padding, WhitespaceMode Mode>
Trace trace(std::string_view input) {
  using Reader = hosq::json::FastReader<128, Padding, Mode>;
  Reader reader;
  typename Reader::state_type state;
  reader.reset(state, input);
  Trace result;
  for (std::size_t count = 0; count != input.size() * 2 + 8; ++count) {
    if (!reader.next_token(state)) {
      result.error = reader.error(state).code;
      result.error_offset = reader.error(state).offset;
      return result;
    }
    result.tokens.push_back(reader.current_token(state));
    if (result.tokens.back().type == hosq::TokenType::End) return result;
  }
  ADD_FAILURE() << "reader did not terminate";
  return result;
}

template <InputPadding Padding>
void expect_all_modes_match(std::string_view input) {
  const auto compact = trace<Padding, WhitespaceMode::Compact>(input);
  const auto pretty = trace<Padding, WhitespaceMode::Pretty>(input);
  const auto automatic = trace<Padding, WhitespaceMode::Auto>(input);
  EXPECT_EQ(pretty.error, compact.error);
  EXPECT_EQ(pretty.error_offset, compact.error_offset);
  EXPECT_EQ(automatic.error, compact.error);
  EXPECT_EQ(automatic.error_offset, compact.error_offset);
  ASSERT_EQ(pretty.tokens.size(), compact.tokens.size());
  ASSERT_EQ(automatic.tokens.size(), compact.tokens.size());
  for (std::size_t index = 0; index != compact.tokens.size(); ++index) {
    EXPECT_EQ(pretty.tokens[index].offset, compact.tokens[index].offset);
    EXPECT_EQ(pretty.tokens[index].length, compact.tokens[index].length);
    EXPECT_EQ(pretty.tokens[index].type, compact.tokens[index].type);
    EXPECT_EQ(pretty.tokens[index].context, compact.tokens[index].context);
    EXPECT_EQ(automatic.tokens[index].offset, compact.tokens[index].offset);
    EXPECT_EQ(automatic.tokens[index].length, compact.tokens[index].length);
    EXPECT_EQ(automatic.tokens[index].type, compact.tokens[index].type);
    EXPECT_EQ(automatic.tokens[index].context, compact.tokens[index].context);
  }
}

template <WhitespaceMode Mode>
void check_rollback() {
  using Reader = hosq::json::FastReader<128, InputPadding::none, Mode>;
  Reader reader;
  typename Reader::state_type state;
  reader.reset(state, "[\n    1,\t2]");
  ASSERT_TRUE(reader.next_token(state));
  const auto checkpoint = state.checkpoint();
  ASSERT_TRUE(reader.next_token(state));
  const Token first = reader.current_token(state);
  const auto first_pos = state.pos;
  state.rollback(checkpoint);
  ASSERT_TRUE(reader.next_token(state));
  const Token second = reader.current_token(state);
  EXPECT_EQ(second.offset, first.offset);
  EXPECT_EQ(second.length, first.length);
  EXPECT_EQ(second.type, first.type);
  EXPECT_EQ(second.context, first.context);
  EXPECT_EQ(state.pos, first_pos);
}

template <InputPadding Padding, WhitespaceMode Mode>
void check_skip(std::string_view input) {
  using Reader = hosq::json::FastReader<128, Padding, Mode>;
  Reader reader;
  typename Reader::state_type state;
  reader.reset(state, input);
  ASSERT_TRUE(reader.next_token(state));
  hosq::DecodeContext<void, Reader, hosq::TryPhase::first> ctx{reader, state};
  hosq::ext::DecodeSkipValue<Reader>{}(ctx);
  EXPECT_EQ(reader.error(state).code, ErrorCode::ok);
  EXPECT_EQ(state.pos, state.end);
}

using Number = hosq::Elem<hosq::scan::scalar | hosq::decode::number<int> |
                              hosq::store::assign,
                          hosq::noop_encoder>;
using Text = hosq::Elem<hosq::scan::scalar | hosq::decode::string_view |
                            hosq::store::assign,
                        hosq::noop_encoder>;
using Boolean =
    hosq::Elem<hosq::scan::scalar | hosq::decode::boolean | hosq::store::assign,
               hosq::noop_encoder>;
using Numbers =
    hosq::Elem<hosq::scan::array |
                   hosq::decode::collect<
                       hosq::scan::scalar | hosq::decode::number<int>, 3> |
                   hosq::store::assign,
               hosq::noop_encoder>;
struct Fields {
  hosq::Entry<hosq::Key<"number">, Number> number;
  hosq::Entry<hosq::Key<"text">, Text> text;
  hosq::Entry<hosq::Key<"flag">, Boolean> flag;
  hosq::Entry<hosq::Key<"values">, Numbers> values;
};

template <InputPadding Padding, WhitespaceMode Mode>
void check_typed_pipeline(std::string input) {
  const auto length = input.size();
  input.append(8, '\0');
  Fields fields;
  const auto result =
      hosq::decode_into<hosq::json::FastReader<128, Padding, Mode>>(
          fields, std::string_view{input.data(), length});
  ASSERT_TRUE(result.is_ok());
  EXPECT_EQ(fields.number.value().value(), 7);
  EXPECT_EQ(fields.text.value().value(), "mode");
  EXPECT_TRUE(fields.flag.value().value());
  EXPECT_EQ(fields.values.value().value(), (std::array<int, 3>{1, 2, 3}));
}
}  // namespace

TEST(ReaderWhitespaceMode, AllModesAcceptCompactPrettyAndMixedWhitespace) {
  for (std::string input : {
           R"({"a":[1,2,{"b":false}],"c":null})",
           "{\n    \"a\": [\n        1,\n        2\n    ],\n    \"c\": null\n}",
           "\t {\r\n \t\"a\"\t:\n[ 1 ,\r2 ] , \"c\" : true } \r\n",
       }) {
    expect_all_modes_match<InputPadding::none>(input);
    input.append(8, '\0');
    expect_all_modes_match<InputPadding::zero_padded_4>(
        {input.data(), input.size() - 8});
    expect_all_modes_match<InputPadding::zero_padded_8>(
        {input.data(), input.size() - 8});
  }
}

TEST(ReaderWhitespaceMode, AllModesPreserveEofAndErrorBoundaries) {
  for (std::string_view input : {
           "",
           " ",
           "\n    ",
           "[\n    ",
           "{\n  \"a\"\t:",
           "[1,\n    ",
           "{\"a\"\t 1}",
           "[1 \t 2]",
       }) {
    expect_all_modes_match<InputPadding::none>(input);
    std::string padded{input};
    padded.append(8, '\0');
    expect_all_modes_match<InputPadding::zero_padded_4>(
        {padded.data(), input.size()});
    expect_all_modes_match<InputPadding::zero_padded_8>(
        {padded.data(), input.size()});
  }
}

TEST(ReaderWhitespaceMode, RollbackDoesNotDependOnWhitespaceHint) {
  check_rollback<WhitespaceMode::Compact>();
  check_rollback<WhitespaceMode::Pretty>();
  check_rollback<WhitespaceMode::Auto>();
}

TEST(ReaderWhitespaceMode, SkipUsesTheSameWhitespaceModes) {
  std::string input =
      "{\n    \"a\": [1,\t{\"b\": null}],\r\n    \"c\": false\n}";
  check_skip<InputPadding::none, WhitespaceMode::Compact>(input);
  check_skip<InputPadding::none, WhitespaceMode::Pretty>(input);
  check_skip<InputPadding::none, WhitespaceMode::Auto>(input);
  input.append(8, '\0');
  const std::string_view padded{input.data(), input.size() - 8};
  check_skip<InputPadding::zero_padded_4, WhitespaceMode::Compact>(padded);
  check_skip<InputPadding::zero_padded_4, WhitespaceMode::Pretty>(padded);
  check_skip<InputPadding::zero_padded_4, WhitespaceMode::Auto>(padded);
  check_skip<InputPadding::zero_padded_8, WhitespaceMode::Compact>(padded);
  check_skip<InputPadding::zero_padded_8, WhitespaceMode::Pretty>(padded);
  check_skip<InputPadding::zero_padded_8, WhitespaceMode::Auto>(padded);
}

TEST(ReaderWhitespaceMode,
     EntryAndFixedArrayPipelinesWorkForEveryModeAndPadding) {
  const std::string input =
      "{\n    \"number\" : 7,\n    \"text\" : \"mode\",\n"
      "\t\"flag\" : true,\n    \"values\" : [1,\n        2, 3]\n}";
  check_typed_pipeline<InputPadding::none, WhitespaceMode::Compact>(input);
  check_typed_pipeline<InputPadding::none, WhitespaceMode::Pretty>(input);
  check_typed_pipeline<InputPadding::none, WhitespaceMode::Auto>(input);
  check_typed_pipeline<InputPadding::zero_padded_4, WhitespaceMode::Compact>(
      input);
  check_typed_pipeline<InputPadding::zero_padded_4, WhitespaceMode::Pretty>(
      input);
  check_typed_pipeline<InputPadding::zero_padded_4, WhitespaceMode::Auto>(input);
  check_typed_pipeline<InputPadding::zero_padded_8, WhitespaceMode::Compact>(
      input);
  check_typed_pipeline<InputPadding::zero_padded_8, WhitespaceMode::Pretty>(
      input);
  check_typed_pipeline<InputPadding::zero_padded_8, WhitespaceMode::Auto>(input);
}

#if defined(__unix__) || defined(__APPLE__)
template <InputPadding Padding, WhitespaceMode Mode>
void check_guard_page_whitespace() {
  using Reader = hosq::json::FastReader<128, Padding, Mode>;
  const auto page = static_cast<std::size_t>(sysconf(_SC_PAGESIZE));
  auto* memory = static_cast<char*>(mmap(
      nullptr, page * 2, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0));
  ASSERT_NE(memory, MAP_FAILED);
  char* const end = memory + page - static_cast<std::size_t>(Padding);
  ASSERT_EQ(mprotect(memory + page, page, PROT_NONE), 0);
  std::memset(end, 0, static_cast<std::size_t>(Padding));
  for (std::size_t size = 0; size <= 24; ++size) {
    char* const begin = end - size;
    std::memset(begin, ' ', size);
    const char* cursor = begin;
    EXPECT_FALSE(Reader::skip_whitespace(cursor, end));
    EXPECT_EQ(cursor, end);
  }
  EXPECT_EQ(munmap(memory, page * 2), 0);
}

TEST(ReaderWhitespaceMode, WhitespaceHelperStaysWithinBoundedAndPaddedInput) {
  check_guard_page_whitespace<InputPadding::none, WhitespaceMode::Compact>();
  check_guard_page_whitespace<InputPadding::none, WhitespaceMode::Pretty>();
  check_guard_page_whitespace<InputPadding::none, WhitespaceMode::Auto>();
  check_guard_page_whitespace<InputPadding::zero_padded_4,
                              WhitespaceMode::Compact>();
  check_guard_page_whitespace<InputPadding::zero_padded_4,
                              WhitespaceMode::Pretty>();
  check_guard_page_whitespace<InputPadding::zero_padded_4,
                              WhitespaceMode::Auto>();
  check_guard_page_whitespace<InputPadding::zero_padded_8,
                              WhitespaceMode::Compact>();
  check_guard_page_whitespace<InputPadding::zero_padded_8,
                              WhitespaceMode::Pretty>();
  check_guard_page_whitespace<InputPadding::zero_padded_8,
                              WhitespaceMode::Auto>();
}
#endif

namespace {
struct ObserveConcreteReader {
  static inline int calls = 0;
  template <class Context>
  auto operator()(Context& ctx) const -> WhitespaceMode {
    ++calls;
    using Reader = std::remove_cvref_t<decltype(ctx.reader())>;
    static_assert(Reader::whitespace_mode != WhitespaceMode::Auto);
    return Reader::whitespace_mode;
  }
};
}  // namespace

TEST(ReaderWhitespaceMode,
     AutoDispatchesDecoderToConcreteReaderAtDocumentStart) {
  using Probe =
      hosq::Elem<hosq::step<ObserveConcreteReader> | hosq::store::assign,
                 hosq::noop_encoder>;
  hosq::Decoder<hosq::json::FastReader<>> decoder;
  for (const auto& [input, expected] : {
           std::pair{"{}", WhitespaceMode::Compact},
           std::pair{"{ \"x\": 1}", WhitespaceMode::Compact},
           std::pair{"{\n    \"x\":1}", WhitespaceMode::Pretty},
           std::pair{" \t[\n    1]", WhitespaceMode::Pretty},
           std::pair{"[1,\n    2]", WhitespaceMode::Compact},
           std::pair{"42", WhitespaceMode::Compact},
       }) {
    Probe probe;
    ObserveConcreteReader::calls = 0;
    ASSERT_TRUE(decoder.decode(probe, input).is_ok());
    EXPECT_EQ(probe.value(), expected);
    EXPECT_EQ(ObserveConcreteReader::calls, 1);
  }
}

TEST(ReaderWhitespaceMode,
     ExplicitPointerScopeRetainsConcreteTypeAndSharedState) {
  using Auto = hosq::json::FastReader<>;
  Auto reader;
  Auto::state_type state;
  reader.reset(state, "[\n    1, 2]");
  int calls = 0;
  reader.with_reader(state, [&](auto* concrete) {
    ++calls;
    using Concrete = std::remove_pointer_t<decltype(concrete)>;
    static_assert(Concrete::whitespace_mode != WhitespaceMode::Auto);
    EXPECT_EQ(Concrete::whitespace_mode, WhitespaceMode::Pretty);
    while (concrete->next_token(state)) {
      if (concrete->current_token(state).type == hosq::TokenType::End) break;
    }
  });
  EXPECT_EQ(calls, 1);
  EXPECT_EQ(state.pos, state.end);
  EXPECT_TRUE(reader.error(state).is_ok());
}

#if defined(__unix__) || defined(__APPLE__)
TEST(ReaderWhitespaceMode, AutoSelectorDoesNotReadPastShortInput) {
  using Reader = hosq::json::FastReader<>;
  const auto page = static_cast<std::size_t>(sysconf(_SC_PAGESIZE));
  auto* memory = static_cast<char*>(mmap(
      nullptr, page * 2, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0));
  ASSERT_NE(memory, MAP_FAILED);
  ASSERT_EQ(mprotect(memory + page, page, PROT_NONE), 0);
  char* const end = memory + page;
  for (const auto& [input, expected] : {
           std::pair{"", WhitespaceMode::Compact},
           std::pair{" ", WhitespaceMode::Compact},
           std::pair{"{", WhitespaceMode::Compact},
           std::pair{"{ ", WhitespaceMode::Compact},
           std::pair{"{  ", WhitespaceMode::Pretty},
           std::pair{"[\n ", WhitespaceMode::Pretty},
           std::pair{"\t{\n ", WhitespaceMode::Pretty},
       }) {
    const std::string_view text{input};
    char* const begin = end - text.size();
    std::memcpy(begin, text.data(), text.size());
    Reader reader;
    Reader::state_type state;
    reader.reset(state, {begin, text.size()});
    int calls = 0;
    reader.with_reader(state, [&](auto* concrete) {
      ++calls;
      using Concrete = std::remove_pointer_t<decltype(concrete)>;
      EXPECT_EQ(Concrete::whitespace_mode, expected);
    });
    EXPECT_EQ(calls, 1);
    EXPECT_EQ(state.pos, begin);
  }
  EXPECT_EQ(munmap(memory, page * 2), 0);
}
#endif
