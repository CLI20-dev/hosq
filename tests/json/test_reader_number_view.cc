#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <random>
#include <string>

#if defined(__unix__) || defined(__APPLE__)
#include <sys/mman.h>
#include <unistd.h>
#endif

#include "hosq/hosq.hh"
#include "hosq/json/fast_reader.hh"
#include "hosq/json/simple_reader.hh"

namespace {
using R = hosq::json::FastReader<>;
using namespace hosq;

// A custom reader keeps its own extension despite exposing a number operation.
struct CustomReader : json::SimpleReader {
  template <class T, bool Into = false>
  bool read_number(state_type&, T& value) const { value = 999; return true; }
};
}  // namespace
namespace hosq::ext {
template <> struct DecodeNumber<CustomReader> {
  template <class T, class Ctx>
  T operator()(const ScalarView&, const Ctx&) const { return 77; }
};
}  // namespace hosq::ext
namespace {

template <class T> void same_number(T a, T b) {
  if constexpr (std::is_floating_point_v<T>) {
    if (std::isnan(a)) { EXPECT_TRUE(std::isnan(b)); return; }
    if (a == 0 && b == 0) EXPECT_EQ(std::signbit(a), std::signbit(b));
  }
  EXPECT_EQ(a, b);
}

template <class T, bool Into>
void compare_number(std::string_view input, bool resolve_first) {
  SCOPED_TRACE(std::string(input));
  R reader;
  R::state_type a, b;
  reader.reset(a, input); reader.reset(b, input);
  const bool start = reader.next_token(a);
  ASSERT_EQ(start, reader.next_token(b));
  if (!start) return;
  T x = 77, y = 77;
  DecodeContext<T, R> cx{x, reader, a}, cy{y, reader, b};
  if (resolve_first) {
    (void)ext::ScanScalar<R>::resolved(cx);
    (void)ext::ScanScalar<R>::resolved(cy);
  }
  if constexpr (Into) {
    (scan::scalar | decode::number_into<T>).invoke(cx);
    ScalarView view = ext::ScanScalar<R>::resolved(cy);
    decode::detail::NumberIntoImpl<T>{}(view, cy);
  } else {
    x = (scan::scalar | decode::number<T>).invoke(cx);
    ScalarView view = ext::ScanScalar<R>::resolved(cy);
    y = decode::detail::NumberImpl<T>{}(view, cy);
  }
  same_number(x, y);
  EXPECT_EQ(reader.error(a).code, reader.error(b).code);
  EXPECT_EQ(reader.error(a).offset, reader.error(b).offset);
  EXPECT_EQ(reader.error(a).detail, reader.error(b).detail);
  EXPECT_EQ(a.current.length, b.current.length);
  EXPECT_EQ(a.pos - a.begin, b.pos - b.begin);
  EXPECT_EQ(reader.next_token(a), reader.next_token(b));
  EXPECT_EQ(reader.current_token(a).type, reader.current_token(b).type);
  EXPECT_EQ(reader.error(a).code, reader.error(b).code);
}

TEST(ReaderNumberView, PreservesValuesErrorsCacheAndPartialWrites) {
  for (std::string_view input : {
           "0", "-0", "42", "-42", "01", "1.25", "-1.25e-3", "1e309",
           "1e-999", "18446744073709551615", "18446744073709551616",
           "-9223372036854775808", "-9223372036854775809", "1e+", "1xyz",
           "-", "null", "true", "nan", "-inf", "{}", "[]",
           "\"123\"", "\"-0\"", "\"1.25\"", "\"\"", "\"123",
           "\"123\\\"\"", "\"1\\u0032\"", "123,", "123:", "123 "}) {
    for (bool resolved : {false, true}) {
      compare_number<int64_t, false>(input, resolved);
      compare_number<uint64_t, false>(input, resolved);
      compare_number<double, false>(input, resolved);
      compare_number<int64_t, true>(input, resolved);
      compare_number<uint64_t, true>(input, resolved);
      compare_number<double, true>(input, resolved);
    }
  }
}

TEST(ReaderNumberView, KeepsCustomNumberExtension) {
  using CustomCtx = DecodeContext<int, CustomReader>;
  CustomReader reader;
  CustomReader::state_type state;
  reader.reset(state, "123"); ASSERT_TRUE(reader.next_token(state));
  int value = 0;
  CustomCtx ctx{value, reader, state};
  EXPECT_EQ((scan::scalar | decode::number<int>).invoke(ctx), 77);
}

TEST(ReaderNumberView, SpecializedExecutionRetainsFollowingMismatchAndStorageSteps) {
  using Value = Elem<scan::scalar | decode::number<int> |
      step<decltype([](int x) { return MatchResult<int>{true, x}; })> | store::assign,
      noop_encoder>;
  Value value;
  EXPECT_TRUE(decode_into<R>(value, "123").is_ok());
  EXPECT_EQ(value.value(), 123);
}

const char* scalar_string_end(const char* p, const char* end) {
  while (p < end) {
    if (*p == '"') return p;
    if (*p == '\\') {
      if (end - p < 2) return end;
      p += 2;
    } else ++p;
  }
  return end;
}

TEST(ReaderNumberView, BlockStringScanMatchesScalarAtEveryBoundary) {
  std::mt19937 random(20260907);
  for (int offset = 0; offset < 32; ++offset) {
    for (int length = 0; length < 129; ++length) {
      std::string buffer(offset, 'x');
      for (int i = 0; i < length; ++i) {
        unsigned n = random() % 16;
        buffer += n == 0 ? '"' : n == 1 ? '\\' : char(random() % 256);
      }
      const char* begin = buffer.data() + offset;
      const char* end = buffer.data() + buffer.size();
      EXPECT_EQ(R::string_end(begin, end), scalar_string_end(begin, end));
    }
  }
  for (int length = 0; length < 256; ++length) {
    std::string text(length, 'a');
    text += "\\\"tail\"";
    EXPECT_EQ(R::string_end(text.data(), text.data() + text.size()),
              text.data() + text.size() - 1);
  }
}
template <class T>
void compare_integer_parser(std::string_view input) {
  T actual = 77, expected = 77;
  auto a = R::parse_number(input.data(), input.data() + input.size(), actual);
  auto b = std::from_chars(input.data(), input.data() + input.size(), expected);
  EXPECT_EQ(a.ec, b.ec);
  EXPECT_EQ(a.ptr - input.data(), b.ptr - input.data());
  EXPECT_EQ(actual, expected);
}

TEST(ReaderNumberView, GroupedIntegerParserMatchesFromChars) {
  std::mt19937_64 random(20260908);
  for (int i = 0; i < 10000; ++i) {
    std::string number = std::to_string(random());
    if (i % 2) number = "-" + number;
    if (i % 3 == 0) number += std::to_string(random());
    if (i % 5 == 0) number += ".123";
    compare_integer_parser<int64_t>(number);
    compare_integer_parser<uint64_t>(number);
    compare_integer_parser<int32_t>(number);
    compare_integer_parser<uint32_t>(number);
    compare_integer_parser<int16_t>(number);
    compare_integer_parser<uint8_t>(number);
  }
  for (int position = 0; position < 8; ++position) {
    for (int byte = 0; byte < 256; ++byte) {
      std::string number = "12345678";
      number[position] = char(byte);
      compare_integer_parser<uint64_t>(number);
      compare_integer_parser<int32_t>(number);
    }
  }
}

#if defined(__unix__) || defined(__APPLE__)
TEST(ReaderNumberView, ScannersDoNotReadIntoGuardPage) {
  const auto page = static_cast<std::size_t>(sysconf(_SC_PAGESIZE));
  void* memory = mmap(nullptr, page * 2, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANON, -1, 0);
  ASSERT_NE(memory, MAP_FAILED);
  auto* edge = static_cast<char*>(memory) + page;
  ASSERT_EQ(mprotect(edge, page, PROT_NONE), 0);
  for (int length = 0; length < 129; ++length) {
    char* start = edge - length;
    std::memset(start, 'a', length);
    EXPECT_EQ(R::string_end(start, edge), edge);
    if (length) {
      edge[-1] = '"';
      EXPECT_EQ(R::string_end(start, edge), edge - 1);
      edge[-1] = '\\';
      EXPECT_EQ(R::string_end(start, edge), edge);
    }
    std::memset(start, '1', length);
    compare_integer_parser<uint64_t>(std::string_view(start, length));
  }
  EXPECT_EQ(munmap(memory, page * 2), 0);
}
#endif

TEST(ReaderNumberView, SkippingTruncatedStringsIsBounded) {
  using Value = Elem<scan::object | decode::skip, noop_encoder>;
  for (std::string_view input : {"{\"a\":\"", "{\"a\":\"abc", "{\"a\":\"abc\\"}) {
    Value value;
    EXPECT_EQ(decode_into<R>(value, input).code, ErrorCode::unexpected_eof);
  }
}
TEST(ReaderNumberView, ProvenPositionsRetainSyntaxErrors) {
  R reader;
  for (std::string_view input : {"", "1", "[]", "{}", "{\"a\":1}",
       "{\"a\":[]}", "{\"a\":\"text\"}", "{\"a\" 1}", "{\"a\":", "{\"a\""}) {
    SCOPED_TRACE(std::string(input));
    R::state_type a, b;
    reader.reset(a, input); reader.reset(b, input);
    bool ok = reader.next_token<ReadPosition::value>(a);
    EXPECT_EQ(ok, reader.next_token(b));
    EXPECT_EQ(reader.error(a).code, reader.error(b).code);
    EXPECT_EQ(reader.error(a).offset, reader.error(b).offset);
    if (!ok || a.current.type != TokenType::ObjectBegin) continue;
    ok = reader.next_token(a);
    EXPECT_EQ(ok, reader.next_token(b));
    if (!ok || a.current.type != TokenType::Scalar) continue;
    EXPECT_EQ(reader.next_token<ReadPosition::member_value>(a), reader.next_token(b));
    EXPECT_EQ(reader.error(a).code, reader.error(b).code);
    EXPECT_EQ(reader.error(a).offset, reader.error(b).offset);
    EXPECT_EQ(reader.error(a).detail, reader.error(b).detail);
    EXPECT_EQ(a.current.type, b.current.type);
    EXPECT_EQ(a.pos - a.begin, b.pos - b.begin);
  }
}

TEST(ReaderNumberView, FollowingMismatchLeavesStorageUntouched) {
  constexpr auto pipeline = scan::scalar | decode::number<int> |
      step<decltype([](int x) { return MatchResult<int>{x == 123, x}; })> |
      store::assign;
  R reader;
  R::state_type state;
  int value = 77;
  DecodeContext<int, R> ctx{value, reader, state};
  reader.reset(state, "124"); ASSERT_TRUE(reader.next_token(state));
  EXPECT_FALSE(pipeline.invoke(ctx));
  EXPECT_EQ(value, 77);
  reader.reset(state, "123"); ASSERT_TRUE(reader.next_token(state));
  EXPECT_TRUE(pipeline.invoke(ctx));
  EXPECT_EQ(value, 123);
}
TEST(ReaderNumberView, SkippedScalarCachesLengthForNextToken) {
  for (std::string_view input : {"[12345,7]", "[\"abc\\\"def\",7]", "[\"\",7]"}) {
    R reader;
    R::state_type state;
    reader.reset(state, input);
    ASSERT_TRUE(reader.next_token(state));
    ASSERT_TRUE(reader.next_token(state));
    ASSERT_TRUE(reader.resolve_skip(state));
    EXPECT_NE(state.current.length, unresolved_length);
    auto position = state.pos;
    ASSERT_TRUE(reader.resolve_skip(state));
    EXPECT_EQ(position, state.pos);
    ASSERT_TRUE(reader.next_token(state));
    EXPECT_EQ(input[state.current.offset], '7');
  }
}
}  // namespace
