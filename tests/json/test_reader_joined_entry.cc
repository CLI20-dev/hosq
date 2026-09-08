#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <string_view>

#if defined(__unix__) || defined(__APPLE__)
#include <sys/mman.h>
#include <unistd.h>
#endif

#include "hosq/hosq.hh"
#include "hosq/json/fast_reader.hh"

namespace {

using namespace hosq;

template <class State>
void expect_joined_state_eq(const State& actual, const State& expected) {
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

template <class Reader>
bool ordinary_member_transition(Reader& reader,
                                typename Reader::state_type& state,
                                std::string_view& key) {
  if (!next_token<Expect::next_member_or_end>(reader, state)) return false;
  DecodeContext<void, Reader> key_context{reader, state};
  key = (scan::scalar | decode::string_view).invoke(key_context);
  if (reader.error(state).has_error()) return false;
  return next_token<ReadPosition::member_value>(reader, state);
}

template <class Reader>
void advance_past_pre_member(Reader& reader,
                             typename Reader::state_type& state) {
  std::string_view key;
  ASSERT_TRUE(ordinary_member_transition(reader, state, key));
  EXPECT_EQ(key, "pre");
  int value = -1;
  ASSERT_TRUE(reader.read_number(state, value));
  EXPECT_EQ(value, 0);
}

template <StringLiteral Key,
          json::InputPadding Padding = json::InputPadding::none>
void compare_joined_probe(std::string_view input, bool after_comma,
                          bool* probe_result = nullptr,
                          bool* ordinary_result = nullptr,
                          std::string* ordinary_key_result = nullptr) {
  SCOPED_TRACE(::testing::Message()
               << "key='" << Key.view() << "' after_comma=" << after_comma
               << " padding=" << static_cast<unsigned>(Padding) << " input='"
               << input << "'");
  using Reader = json::FastReader<128, Padding>;
  Reader reader;
  typename Reader::state_type probe_state;
  typename Reader::state_type ordinary_state;
  reader.reset(probe_state, input);
  reader.reset(ordinary_state, input);
  ASSERT_EQ(reader.next_token(probe_state), reader.next_token(ordinary_state));
  ASSERT_EQ(probe_state.current.type, TokenType::ObjectBegin);

  if (after_comma) {
    advance_past_pre_member(reader, probe_state);
    advance_past_pre_member(reader, ordinary_state);
  }

  std::string_view ordinary_key;
  const bool ordinary_ok =
      ordinary_member_transition(reader, ordinary_state, ordinary_key);
  const auto saved = probe_state;
  const bool probe_ok = reader.template try_read_const_member<Key>(probe_state);

  if (probe_result) *probe_result = probe_ok;
  if (ordinary_result) *ordinary_result = ordinary_ok;
  if (ordinary_key_result) *ordinary_key_result = std::string(ordinary_key);

  if (probe_ok) {
    ASSERT_TRUE(ordinary_ok);
    EXPECT_EQ(ordinary_key, Key.view());
    expect_joined_state_eq(probe_state, ordinary_state);
    return;
  }

  // A short/mismatching joined prefix is a probe miss. Retrying the ordinary
  // transition must produce the same token, cursor, and error as an independent
  // ordinary traversal.
  expect_joined_state_eq(probe_state, saved);
  std::string_view fallback_key;
  const bool fallback_ok =
      ordinary_member_transition(reader, probe_state, fallback_key);
  EXPECT_EQ(fallback_ok, ordinary_ok);
  EXPECT_EQ(fallback_key, ordinary_key);
  expect_joined_state_eq(probe_state, ordinary_state);
}

TEST(ConstEntryJoinedPrefix, CompactWhitespaceEmptyAndLongKeysMatchOrdinary) {
  compare_joined_probe<"id">(R"({"id":12})", false);
  compare_joined_probe<"id">(R"({"pre":0,"id":12})", true);
  compare_joined_probe<"">(R"({"":12})", false);
  compare_joined_probe<"">(R"({"pre":0,"":12})", true);
  compare_joined_probe<
      "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_long">(
      R"({"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_long":12})",
      false);
  compare_joined_probe<
      "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_long">(
      R"({"pre":0,"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_long":12})",
      true);

  for (std::string_view ws : {" ", "\t", "\n", "\r", " \t\n\r"}) {
    const std::string first = std::string("{") + std::string(ws) + "\"id\"" +
                              std::string(ws) + ":" + std::string(ws) + "12}";
    compare_joined_probe<"id">(first, false);

    const std::string comma = std::string("{\"pre\":0") + std::string(ws) + "," +
                              std::string(ws) + "\"id\"" + std::string(ws) +
                              ":" + std::string(ws) + "12}";
    compare_joined_probe<"id">(comma, true);
  }
}

using JoinedNumber =
    Elem<scan::scalar | decode::number<int> | store::assign, noop_encoder>;
struct JoinedEscapedKeyRecord {
  Entry<hosq::Key<R"(a\\b)">, JoinedNumber> escaped;
};

TEST(ConstEntryJoinedPrefix, ShortPrefixesAreImmutableAndEscapedKeysFallback) {
  const std::string first = R"({"id":12})";
  for (std::size_t length = 1; length <= first.size(); ++length) {
    SCOPED_TRACE(::testing::Message() << "first prefix=" << length);
    compare_joined_probe<"id">(std::string_view(first.data(), length), false);
  }

  const std::string comma = R"({"pre":0,"id":12})";
  constexpr std::size_t pre_value_end = sizeof(R"({"pre":0)") - 1;
  for (std::size_t length = pre_value_end; length <= comma.size(); ++length) {
    SCOPED_TRACE(::testing::Message() << "comma prefix=" << length);
    compare_joined_probe<"id">(std::string_view(comma.data(), length), true);
  }

  bool probe_ok = true;
  bool ordinary_ok = false;
  std::string ordinary_key;
  compare_joined_probe<R"(a\\b)">(R"({"a\\b":12})", false, &probe_ok,
                                  &ordinary_ok, &ordinary_key);
  EXPECT_FALSE(probe_ok);  // Backslash keys deliberately use the normal path.
  EXPECT_TRUE(ordinary_ok);
  EXPECT_EQ(ordinary_key, R"(a\\b)");

  JoinedEscapedKeyRecord matching;
  EXPECT_TRUE(
      decode_into<json::FastReader<>>(matching, R"({"a\\b":12})").is_ok());
  EXPECT_EQ(matching.escaped.value().value(), 12);

  JoinedEscapedKeyRecord mismatch;
  const auto mismatch_error =
      decode_into<json::FastReader<>>(mismatch, R"({"a\\c":12})");
  EXPECT_EQ(mismatch_error.code, ErrorCode::unknown_member);
}

#if defined(__unix__) || defined(__APPLE__)
template <json::InputPadding Padding, StringLiteral Key>
void compare_guarded_prefixes(std::string_view full_input,
                              std::size_t first_length, bool after_comma) {
  const auto page = static_cast<std::size_t>(sysconf(_SC_PAGESIZE));
  auto* memory = static_cast<char*>(mmap(
      nullptr, 2 * page, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0));
  ASSERT_NE(memory, MAP_FAILED);
  ASSERT_EQ(mprotect(memory + page, page, PROT_NONE), 0);

  constexpr auto padding = static_cast<std::size_t>(Padding);
  ASSERT_LT(full_input.size() + padding, page);
  for (std::size_t length = first_length; length <= full_input.size();
       ++length) {
    SCOPED_TRACE(::testing::Message()
                 << "guard padding=" << padding << " prefix=" << length);
    char* const begin = memory + page - padding - length;
    std::memcpy(begin, full_input.data(), length);
    if constexpr (padding != 0) std::memset(begin + length, 0, padding);
    compare_joined_probe<Key, Padding>(std::string_view(begin, length),
                                       after_comma);
  }
  EXPECT_EQ(munmap(memory, 2 * page), 0);
}

TEST(ConstEntryJoinedPrefix, GuardPageBoundsCoverAllPaddingContracts) {
  constexpr std::string_view first = R"({"id":12})";
  constexpr std::string_view comma = R"({"pre":0,"id":12})";
  constexpr std::size_t pre_value_end = sizeof(R"({"pre":0)") - 1;

  compare_guarded_prefixes<json::InputPadding::none, "id">(first, 1, false);
  compare_guarded_prefixes<json::InputPadding::zero_padded_4, "id">(first, 1,
                                                                    false);
  compare_guarded_prefixes<json::InputPadding::zero_padded_8, "id">(first, 1,
                                                                    false);
  compare_guarded_prefixes<json::InputPadding::none, "id">(comma, pre_value_end,
                                                           true);
  compare_guarded_prefixes<json::InputPadding::zero_padded_4, "id">(
      comma, pre_value_end, true);
  compare_guarded_prefixes<json::InputPadding::zero_padded_8, "id">(
      comma, pre_value_end, true);
}
#endif

}  // namespace
