#include <gtest/gtest.h>
#include <hosq/hosq.hh>
#include <hosq/json/fast_reader.hh>
#include <string>

TEST(LazyKeyMatch, TerminatorAndCommitBeforeHandler) {
  std::size_t committed = 99;
  int selected = 0;
  auto handlers = std::tuple{
      hosq::meta::make_handler<"id">([&] { EXPECT_EQ(committed, 2u); selected = 1; }),
      hosq::meta::make_handler<"identity">([&] { EXPECT_EQ(committed, 8u); selected = 2; }),
      hosq::meta::make_handler<"">([&] { EXPECT_EQ(committed, 0u); selected = 3; })};
  auto commit = [&](std::size_t n) { committed = n; };
  EXPECT_TRUE(hosq::meta::match_terminated<'"'>("id\":123", handlers, commit));
  EXPECT_EQ(selected, 1);
  EXPECT_TRUE(hosq::meta::match_terminated<'"'>("identity\":456", handlers, commit));
  EXPECT_EQ(selected, 2);
  EXPECT_TRUE(hosq::meta::match_terminated<'"'>("\":0", handlers, commit));
  EXPECT_EQ(selected, 3);
  for (std::string key : {"", "i", "id", "identity", "idX\":0", "id\\\"x\":0"}) {
    selected = 0;
    committed = 99;
    EXPECT_FALSE(hosq::meta::match_terminated<'"'>(key, handlers, commit));
    EXPECT_EQ(selected, 0);
    EXPECT_EQ(committed, 99u);
  }
  // The original exact-view API still requires an exact key.
  committed = 2;
  EXPECT_TRUE(hosq::meta::match("id", handlers));
  EXPECT_FALSE(hosq::meta::match("id\":123", handlers));
}

namespace {
template <hosq::json::InputPadding Padding>
void check_reader_laziness() {
  using R = hosq::json::FastReader<128, Padding>;
  for (bool known : {true, false}) {
    std::string input = known ? "{\"id\":1}" : "{\"other\":1}";
    const auto size = input.size();
    input.append(8, '\0');
    for (bool deferred : {true, false}) {
      R reader;
      typename R::state_type state;
      reader.reset(state, {input.data(), size});
      ASSERT_TRUE(reader.next_token(state));
      ASSERT_TRUE(hosq::next_token<hosq::Expect::next_member_or_end>(reader, state));
      const char* before = state.pos;
      int scans = 0;
      bool called = false;
      auto handlers = std::tuple{hosq::meta::make_handler<"id">([&] {
        called = true;
        EXPECT_EQ(state.current.length, 2u);
        EXPECT_EQ(state.pos - state.begin, 5);
      })};
      hosq::DecodeContext<void, R, hosq::TryPhase::first> ctx{reader, state};
      auto scan = [&] {
        ++scans;
        return (hosq::scan::scalar | hosq::decode::string_view).invoke(ctx);
      };
      bool matched = deferred
          ? reader.match_const_key(state, handlers, scan, std::true_type{})
          : reader.match_const_key(state, handlers, scan, std::false_type{});
      EXPECT_EQ(matched, known);
      EXPECT_EQ(called, known);
      EXPECT_EQ(scans, !known && !deferred ? 1 : 0);
      if (!known && deferred) EXPECT_EQ(state.pos, before);
    }
  }
}
}
TEST(LazyKeyMatch, NoPrescanOnHitAndControlledMissDeferral) {
  check_reader_laziness<hosq::json::InputPadding::none>();
  check_reader_laziness<hosq::json::InputPadding::zero_padded_4>();
  check_reader_laziness<hosq::json::InputPadding::zero_padded_8>();
}
