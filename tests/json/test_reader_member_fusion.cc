#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>

#if defined(__unix__) || defined(__APPLE__)
#include <sys/mman.h>
#include <unistd.h>
#endif

#include "hosq/hosq.hh"
#include "hosq/json/fast_reader.hh"

namespace {

using Reader = hosq::json::FastReader<>;
using State = Reader::state_type;
using namespace hosq;

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

// Compare the concrete state, rather than current_token(), because an error
// makes current_token() expose error_token and could hide a cache mutation.
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

template <class T>
void expect_same_number(T actual, T expected) {
  if constexpr (std::is_floating_point_v<T>) {
    if (std::isnan(expected)) {
      EXPECT_TRUE(std::isnan(actual));
      return;
    }
    if (actual == 0 && expected == 0)
      EXPECT_EQ(std::signbit(actual), std::signbit(expected));
  }
  EXPECT_EQ(actual, expected);
}

template <class T>
bool read_member_ordinary(Reader& reader, State& state, std::string_view& key,
                          T& value) {
  if (!next_token<Expect::next_member_or_end>(reader, state)) return false;
  DecodeContext<void, Reader> key_context{reader, state};
  key = (scan::scalar | decode::string_view).invoke(key_context);
  if (reader.error(state).has_error()) return false;
  if (!next_token<ReadPosition::member_value>(reader, state)) return false;
  return reader.read_number(state, value);
}

template <class T>
void compare_probe_with_ordinary(std::string_view input) {
  SCOPED_TRACE(std::string(input));
  Reader reader;
  State fused_state;
  State ordinary_state;
  reader.reset(fused_state, input);
  reader.reset(ordinary_state, input);

  const bool fused_root = reader.next_token(fused_state);
  ASSERT_EQ(fused_root, reader.next_token(ordinary_state));
  if (!fused_root || fused_state.current.type != TokenType::ObjectBegin) {
    const State saved = fused_state;
    T value = static_cast<T>(77);
    EXPECT_FALSE(
        (reader.template try_read_const_number<"id">(fused_state, value)));
    expect_same_state(fused_state, saved);
    expect_same_number(value, static_cast<T>(77));
    return;
  }

  T ordinary_value = static_cast<T>(77);
  std::string_view ordinary_key;
  const bool ordinary_ok = read_member_ordinary(
      reader, ordinary_state, ordinary_key, ordinary_value);

  const State saved = fused_state;
  T fused_value = static_cast<T>(77);
  const bool fused_ok =
      reader.template try_read_const_number<"id">(fused_state, fused_value);
  const bool ordinary_is_fusible = ordinary_ok && ordinary_key == "id";
  EXPECT_EQ(fused_ok, ordinary_is_fusible);

  if (fused_ok) {
    expect_same_number(fused_value, ordinary_value);
    expect_same_state(fused_state, ordinary_state);

    // A later token observer must see the same cache and cursor state.
    State fused_next = fused_state;
    State ordinary_next = ordinary_state;
    EXPECT_EQ(reader.next_token(fused_next), reader.next_token(ordinary_next));
    expect_same_state(fused_next, ordinary_next);
    return;
  }

  // A miss is a probe, not a partial parse. Retry the ordinary route from the
  // exact same state and compare it with an independent ordinary traversal.
  expect_same_state(fused_state, saved);
  expect_same_number(fused_value, static_cast<T>(77));
  std::string_view fallback_key;
  const bool fallback_ok = read_member_ordinary(
      reader, fused_state, fallback_key, fused_value);
  EXPECT_EQ(fallback_ok, ordinary_ok);
  EXPECT_EQ(fallback_key, ordinary_key);
  expect_same_number(fused_value, ordinary_value);
  expect_same_state(fused_state, ordinary_state);
}

template <class T>
void compare_every_prefix(std::string_view input) {
  for (std::size_t length = 0; length <= input.size(); ++length) {
    SCOPED_TRACE(length);
    compare_probe_with_ordinary<T>(input.substr(0, length));
  }
}

TEST(ReaderMemberFusion, IntegerSuccessAndMissMatchOrdinaryAtEveryPrefix) {
  for (std::string_view input : {
           R"({"id":0})", R"({ "id" : 42 })", R"({"id":"-42"})",
           R"({"id":-0,"tail":1})", R"({"id":9223372036854775807})",
           R"({"id":-9223372036854775808})",
           R"({"id":9223372036854775808})",
           R"({"id":-9223372036854775809})", R"({"id":01})",
           R"({"id":1xyz})", R"({"id":1e+})", R"({"id":})",
           R"({"id":""})", R"({"id":"12})", R"({"id":"1\u0032"})",
           R"({"idx":12})", R"({"i\u0064":12})", R"({id:12})",
           R"({"id" 12})", R"({"id":true})", R"({"id":null})"}) {
    compare_every_prefix<std::int64_t>(input);
  }

  for (std::string_view input : {
           R"({"id":0})", R"({"id":18446744073709551615})",
           R"({"id":18446744073709551616})", R"({"id":-1})",
           R"({"id":"18446744073709551615"})"}) {
    compare_every_prefix<std::uint64_t>(input);
  }
}

TEST(ReaderMemberFusion, FloatingPointEdgesMatchOrdinaryAtEveryPrefix) {
  for (std::string_view input : {
           R"({"id":-0})", R"({"id":-0.0})", R"({"id":1.25e-3})",
           R"({"id":5e-324})", R"({"id":1e-999})", R"({"id":1e309})",
           R"({"id":"-0.0"})", R"({"id":"1.25e-3"})",
           R"({"id":nan})", R"({"id":-inf})", R"({"id":1.2.3})"}) {
    compare_every_prefix<double>(input);
  }
}

TEST(ReaderMemberFusion, MissPreservesExistingErrorsAndObserverFields) {
  Reader reader;
  State state;
  reader.reset(state, R"({"idx":12})");
  ASSERT_TRUE(reader.next_token(state));
  state.last_error = {.code = ErrorCode::candidate_rejected,
                      .offset = 91,
                      .detail = "probe sentinel"};
  state.error_token = {.offset = 17,
                       .length = 23,
                       .type = TokenType::Error,
                       .context = 29};
  const State saved = state;
  std::int64_t value = -1234;
  EXPECT_FALSE(reader.template try_read_const_number<"id">(state, value));
  expect_same_state(state, saved);
  EXPECT_EQ(value, -1234);

  state.error_flag = true;
  const State errored = state;
  EXPECT_FALSE(reader.template try_read_const_number<"id">(state, value));
  expect_same_state(state, errored);
  EXPECT_EQ(value, -1234);
}

void advance_to_first_value_without_resolving(Reader& reader, State& state) {
  ASSERT_TRUE(reader.next_token(state));
  ASSERT_TRUE((next_token<Expect::next_member_or_end>(reader, state)));
  DecodeContext<void, Reader> key_context{reader, state};
  EXPECT_EQ((scan::scalar | decode::string_view).invoke(key_context), "skip");
  ASSERT_TRUE((next_token<ReadPosition::member_value>(reader, state)));
  ASSERT_EQ(state.current.length, unresolved_length);
}

TEST(ReaderMemberFusion, UnresolvedPreviousValueMissIsMutationFreeAndRetryable) {
  constexpr std::string_view input = R"({"skip":"cached\"value","id":12})";
  Reader reader;
  State fused_state;
  State ordinary_state;
  reader.reset(fused_state, input);
  reader.reset(ordinary_state, input);
  advance_to_first_value_without_resolving(reader, fused_state);
  advance_to_first_value_without_resolving(reader, ordinary_state);

  const State saved = fused_state;
  std::int64_t fused_value = 77;
  EXPECT_FALSE(
      reader.template try_read_const_number<"id">(fused_state, fused_value));
  expect_same_state(fused_state, saved);
  EXPECT_EQ(fused_value, 77);

  std::int64_t ordinary_value = 77;
  std::string_view fused_key;
  std::string_view ordinary_key;
  EXPECT_EQ(read_member_ordinary(reader, fused_state, fused_key, fused_value),
            read_member_ordinary(reader, ordinary_state, ordinary_key,
                                 ordinary_value));
  EXPECT_EQ(fused_key, ordinary_key);
  expect_same_number(fused_value, ordinary_value);
  expect_same_state(fused_state, ordinary_state);
}

TEST(ReaderMemberFusion, ResolvedPreviousValueSupportsCommaTransition) {
  const std::string input = R"({"skip":7, "id" : "-12", "tail":3})";
  constexpr std::size_t first_value_end = sizeof(R"({"skip":7)") - 1;
  Reader reader;
  for (std::size_t length = first_value_end; length <= input.size(); ++length) {
    SCOPED_TRACE(length);
    const std::string_view prefix(input.data(), length);
    State fused_state;
    State ordinary_state;
    reader.reset(fused_state, prefix);
    reader.reset(ordinary_state, prefix);

    auto advance = [&](State& state) {
      ASSERT_TRUE(reader.next_token(state));
      std::string_view key;
      std::int64_t value = 0;
      ASSERT_TRUE(read_member_ordinary(reader, state, key, value));
      EXPECT_EQ(key, "skip");
      EXPECT_EQ(value, 7);
    };
    advance(fused_state);
    advance(ordinary_state);

    std::int64_t ordinary_value = 77;
    std::string_view ordinary_key;
    const bool ordinary_ok = read_member_ordinary(
        reader, ordinary_state, ordinary_key, ordinary_value);
    const State saved = fused_state;
    std::int64_t fused_value = 77;
    const bool fused_ok =
        reader.template try_read_const_number<"id">(fused_state, fused_value);
    EXPECT_EQ(fused_ok, ordinary_ok && ordinary_key == "id");
    if (fused_ok) {
      EXPECT_EQ(fused_value, ordinary_value);
      expect_same_state(fused_state, ordinary_state);
    } else {
      expect_same_state(fused_state, saved);
      EXPECT_EQ(fused_value, 77);
      std::string_view fallback_key;
      EXPECT_EQ(read_member_ordinary(reader, fused_state, fallback_key,
                                     fused_value),
                ordinary_ok);
      EXPECT_EQ(fallback_key, ordinary_key);
      EXPECT_EQ(fused_value, ordinary_value);
      expect_same_state(fused_state, ordinary_state);
    }
  }
}

#if defined(__unix__) || defined(__APPLE__)
TEST(ReaderMemberFusion, BoundedPrefixesDoNotReadIntoGuardPage) {
  const auto page = static_cast<std::size_t>(sysconf(_SC_PAGESIZE));
  auto* memory = static_cast<char*>(mmap(nullptr, 2 * page,
      PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0));
  ASSERT_NE(memory, MAP_FAILED);
  ASSERT_EQ(mprotect(memory + page, page, PROT_NONE), 0);

  const std::string input = R"({ "id" : -9223372036854775808 })";
  for (std::size_t length = 0; length <= input.size(); ++length) {
    SCOPED_TRACE(length);
    char* begin = memory + page - length;
    std::memcpy(begin, input.data(), length);
    compare_probe_with_ordinary<std::int64_t>(
        std::string_view(begin, length));
  }
  EXPECT_EQ(munmap(memory, 2 * page), 0);
}
#endif

}  // namespace
