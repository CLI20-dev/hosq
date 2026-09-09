#include <gtest/gtest.h>

#include <cstring>
#include <hosq/hosq.hh>
#include <hosq/json/fast_reader.hh>
#include <string>
#if defined(__unix__) || defined(__APPLE__)
#include <sys/mman.h>
#include <unistd.h>
#endif

namespace {
template <hosq::json::InputPadding Padding, std::size_t Depth = 128>
void check_skip(std::string_view input, hosq::ErrorCode code,
                std::size_t consumed, std::size_t error_offset = 0) {
  using R = hosq::json::FastReader<Depth, Padding>;
  R reader;
  typename R::state_type state;
  reader.reset(state, input);
  ASSERT_TRUE(reader.next_token(state));
  hosq::DecodeContext<void, R, hosq::TryPhase::first> ctx{reader, state};
  hosq::ext::DecodeSkipValue<R>{}(ctx);
  EXPECT_EQ(state.last_error.code, code);
  if (code == hosq::ErrorCode::ok) {
    EXPECT_EQ(static_cast<std::size_t>(state.pos - state.begin), consumed);
    EXPECT_EQ(state.ctx_size, 0u);
  } else
    EXPECT_EQ(state.last_error.offset, error_offset);
}
template <hosq::json::InputPadding Padding>
void examples() {
  for (std::string value :
       {R"({"a":[true,false,null,-1,2.5e+3],"b":{"x":"a\"b","y":[]}})",
        "{\n    \"a\": [true, false, null],\n    \"b\": {}\n}",
        // Skip deliberately preserves its structural, permissive contract.
        R"([trueX,falseX,nullX,1.2.3e+-])", R"({"x" [1]})",
        R"(["a\\b",{},[],[null]])"}) {
    auto size = value.size();
    value += " trailing";
    value.append(8, '\0');
    check_skip<Padding>({value.data(), size + 9}, hosq::ErrorCode::ok, size);
  }
  for (std::string value :
       {"[true", "[false", "[null", "{\"x\":", "[\"a\\", "[   "}) {
    auto size = value.size();
    value.append(8, '\0');
    check_skip<Padding>({value.data(), size}, hosq::ErrorCode::unexpected_eof, 0,
                        size);
  }
  std::string mismatch = "[true}";
  mismatch.append(8, '\0');
  check_skip<Padding>({mismatch.data(), 6}, hosq::ErrorCode::unexpected_token, 0,
                      5);
  std::string depth = "[[[]]]";
  depth.append(8, '\0');
  check_skip<Padding, 2>({depth.data(), 6},
                         hosq::ErrorCode::depth_limit_exceeded, 0, 2);
}
}  // namespace
TEST(ReaderSkipTransitions, BoundedAndPaddedContracts) {
  examples<hosq::json::InputPadding::none>();
  examples<hosq::json::InputPadding::zero_padded_4>();
  examples<hosq::json::InputPadding::zero_padded_8>();
}
#if defined(__unix__) || defined(__APPLE__)
TEST(ReaderSkipTransitions, TruncatedLiteralsAtGuardPage) {
  auto page = static_cast<std::size_t>(sysconf(_SC_PAGESIZE));
  void* mem = mmap(nullptr, page * 2, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANON, -1, 0);
  ASSERT_NE(mem, MAP_FAILED);
  char* edge = static_cast<char*>(mem) + page;
  ASSERT_EQ(mprotect(edge, page, PROT_NONE), 0);
  for (std::string value :
       {"[true", "[false", "[null", "[    ", "{\"a\":false"}) {
    for (std::size_t n = 1; n <= value.size(); ++n) {
      for (std::size_t padding : {0u, 4u, 8u}) {
        char* start = edge - padding - n;
        std::memcpy(start, value.data(), n);
        std::memset(start + n, 0, padding);
        std::string_view input(start, n);
        if (padding == 0)
          check_skip<hosq::json::InputPadding::none>(
              input, hosq::ErrorCode::unexpected_eof, 0, n);
        if (padding == 4)
          check_skip<hosq::json::InputPadding::zero_padded_4>(
              input, hosq::ErrorCode::unexpected_eof, 0, n);
        if (padding == 8)
          check_skip<hosq::json::InputPadding::zero_padded_8>(
              input, hosq::ErrorCode::unexpected_eof, 0, n);
      }
    }
  }
  EXPECT_EQ(munmap(mem, page * 2), 0);
}
#endif

TEST(ReaderSkipTransitions, StringEscapesAcrossBlockBoundaries) {
  using R = hosq::json::FastReader<>;
  for (char fill : {'a', '\0', static_cast<char>(0x80)}) {
    for (std::size_t prefix = 0; prefix < 64; ++prefix) {
      for (std::size_t slashes = 0; slashes < 7; ++slashes) {
        std::string input(prefix, fill);
        input.append(slashes, '\\');
        input += "\"tail\"";
        const auto first_quote = prefix + slashes;
        const auto expected = slashes % 2 ? input.size() - 1 : first_quote;
        EXPECT_EQ(R::string_end(input.data(), input.data() + input.size()),
                  input.data() + expected);
        input.pop_back();
        const auto truncated = slashes % 2 ? input.size() : first_quote;
        EXPECT_EQ(R::string_end(input.data(), input.data() + input.size()),
                  input.data() + truncated);
      }
    }
  }
}

TEST(ReaderSkipTransitions, SeparatorsWhitespaceAndTruncation) {
  for (std::string gap : {"", " ", "  ", "\n", "\r\n", "\t"}) {
    std::string input =
        "{\"x\": " + gap + "[true,\n {\"y\": null}],\n\"z\": []}";
    const auto size = input.size();
    input.append(8, '\0');
    check_skip<hosq::json::InputPadding::none>({input.data(), size},
                                               hosq::ErrorCode::ok, size);
    check_skip<hosq::json::InputPadding::zero_padded_4>(
        {input.data(), size}, hosq::ErrorCode::ok, size);
    check_skip<hosq::json::InputPadding::zero_padded_8>(
        {input.data(), size}, hosq::ErrorCode::ok, size);
  }
  for (std::string input : {"{\"x\"", "{\"x\":", "{\"x\": ", "{\"x\":  ",
                            "[true,\n", "{\"x\": null,\n"}) {
    const auto size = input.size();
    input.append(8, '\0');
    check_skip<hosq::json::InputPadding::none>(
        {input.data(), size}, hosq::ErrorCode::unexpected_eof, 0, size);
    check_skip<hosq::json::InputPadding::zero_padded_4>(
        {input.data(), size}, hosq::ErrorCode::unexpected_eof, 0, size);
    check_skip<hosq::json::InputPadding::zero_padded_8>(
        {input.data(), size}, hosq::ErrorCode::unexpected_eof, 0, size);
  }
}
