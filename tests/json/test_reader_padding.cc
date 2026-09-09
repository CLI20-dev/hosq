#include <gtest/gtest.h>

#include <cstring>
#include <hosq/hosq.hh>
#include <hosq/json/fast_reader.hh>
#include <random>
#include <string>
#include <type_traits>
#if defined(__unix__) || defined(__APPLE__)
#include <sys/mman.h>
#include <unistd.h>
#endif

namespace {
using B = hosq::json::FastReader<>;
template <class Padding>
class ReaderPadding : public ::testing::Test {
 protected:
  using P = hosq::json::FastReader<128, Padding::value>;
  static constexpr auto padding = P::padding_bytes;
  static_assert(hosq::ReaderConcept<P>);
  static_assert(sizeof(B::state_type) == sizeof(typename P::state_type));
  static void compare(std::string_view text) {
    B b;
    P p;
    B::state_type bs;
    typename P::state_type ps;
    b.reset(bs, text);
    p.reset(ps, text);
    for (size_t i = 0; i < text.size() * 2 + 8; ++i) {
      bool a = b.next_token(bs), c = p.next_token(ps);
      ASSERT_EQ(a, c);
      EXPECT_EQ(bs.pos - bs.begin, ps.pos - ps.begin);
      EXPECT_EQ(bs.current.offset, ps.current.offset);
      EXPECT_EQ(bs.current.length, ps.current.length);
      EXPECT_EQ(bs.current.type, ps.current.type);
      EXPECT_EQ(b.error(bs).code, p.error(ps).code);
      EXPECT_EQ(b.error(bs).offset, p.error(ps).offset);
      if (!a || bs.current.type == hosq::TokenType::End) return;
    }
    FAIL() << "token loop did not finish";
  }
};
using PaddingWidths = ::testing::Types<
    std::integral_constant<hosq::json::InputPadding,
                           hosq::json::InputPadding::zero_padded_4>,
    std::integral_constant<hosq::json::InputPadding,
                           hosq::json::InputPadding::zero_padded_8>>;
TYPED_TEST_SUITE(ReaderPadding, PaddingWidths);
TYPED_TEST(ReaderPadding, TokenAndErrorEquivalence) {
  std::mt19937 random(20260910);
  for (int i = 0; i < 5000; ++i) {
    const auto length = random() % 128;
    std::string text(length, ' ');
    for (auto& c : text) c = char(random() % 256);
    text.append(TestFixture::padding, '\0');
    TestFixture::compare(std::string_view(text.data(), length));
  }
  for (std::string text : {"", "    ", "[1,2,3]", "{\"a\":true,\"b\":null}",
                           "[\"abc\\\"def\",-123.5e20]", "[123",
                           "{\"a\":", "[1,]", "[\"unfinished"}) {
    auto length = text.size();
    text.append(TestFixture::padding, '\0');
    TestFixture::compare(std::string_view(text.data(), length));
  }
}
#if defined(__unix__) || defined(__APPLE__)
TYPED_TEST(ReaderPadding, ExactPaddingBeforeGuardPage) {
  using P = typename TestFixture::P;
  auto page = static_cast<size_t>(sysconf(_SC_PAGESIZE));
  void* mem = mmap(nullptr, page * 2, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANON, -1, 0);
  ASSERT_NE(mem, MAP_FAILED);
  auto* edge = static_cast<char*>(mem) + page;
  ASSERT_EQ(mprotect(edge, page, PROT_NONE), 0);
  std::memset(edge - TestFixture::padding, 0, TestFixture::padding);
  for (int n = 0; n < 128; ++n) {
    char* start = edge - TestFixture::padding - n;
    std::memset(start, 'a', n);
    EXPECT_EQ(P::string_end(start, edge - TestFixture::padding),
              edge - TestFixture::padding);
    if (n) {
      start[n - 1] = '"';
      EXPECT_EQ(P::string_end(start, edge - TestFixture::padding),
                edge - TestFixture::padding - 1);
      start[n - 1] = '\\';
      EXPECT_EQ(P::string_end(start, edge - TestFixture::padding),
                edge - TestFixture::padding);
    }
    for (char c : {' ', '1', 'a'}) {
      std::memset(start, c, n);
      TestFixture::compare(std::string_view(start, n));
      uint64_t x = 77, y = 77;
      auto a = B::parse_number(start, edge - TestFixture::padding, x),
           b = P::parse_number(start, edge - TestFixture::padding, y);
      EXPECT_EQ(a.ec, b.ec);
      EXPECT_EQ(a.ptr, b.ptr);
      EXPECT_EQ(x, y);
    }
  }
  EXPECT_EQ(munmap(mem, page * 2), 0);
}
#endif
TYPED_TEST(ReaderPadding, PipelineAndScalarResolution) {
  using P = typename TestFixture::P;
  using namespace hosq;
  for (std::string text :
       {"123", "\"123\"", "123x", "-1", "1e9999", "\"x\\\"y\""}) {
    auto n = text.size();
    text.append(TestFixture::padding, '\0');
    auto input = std::string_view(text.data(), n);
    Elem<scan::scalar | decode::number<double> | store::assign, noop_encoder> a,
        b;
    auto ea = decode_into<B>(a, input), eb = decode_into<P>(b, input);
    EXPECT_EQ(ea.code, eb.code);
    EXPECT_EQ(ea.offset, eb.offset);
    EXPECT_EQ(a.value(), b.value());
    Elem<scan::scalar | decode::string_view | store::assign, noop_encoder> sa,
        sb;
    ea = decode_into<B>(sa, input);
    eb = decode_into<P>(sb, input);
    EXPECT_EQ(ea.code, eb.code);
    EXPECT_EQ(sa.value(), sb.value());
  }
}
TYPED_TEST(ReaderPadding, IntegerBoundaryAndOverflowEquivalence) {
  using P = typename TestFixture::P;
  std::mt19937_64 rng(20260911);
  for (int i = 0; i < 5000; ++i) {
    std::string text = (i % 2 ? "-" : "") + std::to_string(rng());
    if (i % 3 == 0) text += std::to_string(rng());
    if (i % 5 == 0) text += ".25";
    auto n = text.size();
    text.append(TestFixture::padding, '\0');
    int64_t a = 77, b = 77;
    auto ra = B::parse_number(text.data(), text.data() + n, a);
    auto rb = P::parse_number(text.data(), text.data() + n, b);
    EXPECT_EQ(ra.ptr, rb.ptr);
    EXPECT_EQ(ra.ec, rb.ec);
    EXPECT_EQ(a, b);
  }
}
}  // namespace
