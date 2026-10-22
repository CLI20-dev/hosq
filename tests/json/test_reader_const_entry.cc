#include <gtest/gtest.h>

#include <cstring>
#if defined(__unix__) || defined(__APPLE__)
#include <sys/mman.h>
#include <unistd.h>
#endif
#include "hosq/hosq.hh"
#include "reader_test_types.hh"

namespace {
using namespace hosq;
using Number =
    Elem<scan::scalar | decode::number<int> | store::assign, noop_encoder>;
struct Ordered {
  Entry<hosq::Key<"id">, Number> id;
  Entry<hosq::Key<"count">, Number> count;
};
template <class R>
class ConstEntryTest : public test::RuntimeReaderTest<R> {};
TYPED_TEST_SUITE(ConstEntryTest, test::RuntimeReaderTypes);
TYPED_TEST(ConstEntryTest, OrderedConstantsAndQuotedNumbers) {
  for (auto input :
       {R"({"id":12,"count":34})", R"({ "id" : "12", "count" : 34 })"}) {
    Ordered out;
    ASSERT_TRUE(decode_into<TypeParam>(out, input).is_ok());
    EXPECT_EQ(*out.id, 12);
    EXPECT_EQ(*out.count, 34);
  }
}
TYPED_TEST(ConstEntryTest, RejectsWrongKeysMissingValuesAndTruncation) {
  for (auto input : {R"({"idx":12,"count":34})", R"({"count":12,"id":34})",
                     R"({"id":12})", R"({"id":12,"count":})",
                     R"({"id" 12,"count":34})", R"({"id":12 "count":34})"}) {
    Ordered out;
    EXPECT_FALSE(decode_into<TypeParam>(out, input).is_ok()) << input;
  }
  const std::string input = R"({"id":12,"count":34})";
  for (std::size_t n = 0; n < input.size(); ++n) {
    Ordered out;
    EXPECT_FALSE(
        decode_into<TypeParam>(out, std::string_view(input.data(), n)).is_ok())
        << n;
  }
}
TEST(ConstEntryProbe, SuccessMatchesOrdinaryTransitionAndMissDoesNotMutate) {
  using R = json::FastReader<>;
  for (auto value :
       {"123", "\"hi\"", "true", "false", "null", "[]", "{}", "", ":"}) {
    const std::string input = std::string("{ \"id\" : ") + value + "}";
    for (std::size_t n = 1; n <= input.size(); ++n) {
      R r;
      R::state_type a, b;
      r.reset(a, std::string_view(input.data(), n));
      r.reset(b, std::string_view(input.data(), n));
      ASSERT_EQ(r.next_token(a), r.next_token(b));
      const auto saved = a;
      if (r.template try_read_const_member<"id">(a)) {
        ASSERT_TRUE((next_token<Expect::next_member_or_end>(r, b)));
        DecodeContext<void, R> ctx{r, b};
        EXPECT_EQ((scan::scalar | decode::string_view).invoke(ctx), "id");
        ASSERT_TRUE((next_token<ReadPosition::member_value>(r, b)));
        EXPECT_EQ(a.pos, b.pos);
        EXPECT_EQ(a.current.offset, b.current.offset);
        EXPECT_EQ(a.current.length, b.current.length);
        EXPECT_EQ(a.current.type, b.current.type);
        EXPECT_EQ(a.current.context, b.current.context);
        EXPECT_EQ(a.expect, b.expect);
        EXPECT_EQ(a.ctx_size, b.ctx_size);
        EXPECT_EQ(a.ctx_stack, b.ctx_stack);
        EXPECT_EQ(r.error(a).code, r.error(b).code);
      } else {
        EXPECT_EQ(a.pos, saved.pos);
        EXPECT_EQ(a.current.offset, saved.current.offset);
        EXPECT_EQ(a.current.length, saved.current.length);
        EXPECT_EQ(a.current.type, saved.current.type);
        EXPECT_EQ(a.expect, saved.expect);
        EXPECT_EQ(a.ctx_size, saved.ctx_size);
        EXPECT_EQ(a.ctx_stack, saved.ctx_stack);
        EXPECT_EQ(a.last_error.code, saved.last_error.code);
      }
    }
  }
}
#if defined(__unix__) || defined(__APPLE__)
TEST(ConstEntryProbe, BoundedInputAtGuardPage) {
  const auto page = static_cast<std::size_t>(sysconf(_SC_PAGESIZE));
  auto* memory = static_cast<char*>(mmap(
      nullptr, 2 * page, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0));
  ASSERT_NE(memory, MAP_FAILED);
  ASSERT_EQ(mprotect(memory + page, page, PROT_NONE), 0);
  const std::string input = R"({"id":12,"count":34})";
  for (std::size_t n = 0; n <= input.size(); ++n) {
    char* begin = memory + page - n;
    std::memcpy(begin, input.data(), n);
    Ordered out;
    EXPECT_EQ(
        decode_into<json::FastReader<>>(out, std::string_view(begin, n)).is_ok(),
        n == input.size());
  }
  EXPECT_EQ(munmap(memory, 2 * page), 0);
}
#endif
}  // namespace
