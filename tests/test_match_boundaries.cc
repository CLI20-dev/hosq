#include <gtest/gtest.h>

#include <concepts>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "hosq/hosq.hh"
#include "hosq/json/fast_reader.hh"
#include "hosq/json/simple_reader.hh"

namespace {
using namespace hosq;

using Candidate =
    Elem<scan::scalar | decode::integer<int> | match::literal<1> | store::assign,
         encode::value>;
using Validated =
    Elem<scan::scalar | decode::integer<int> | validate::min<1> | store::assign,
         encode::value>;

static_assert(detail::MatchResultT<Candidate::decoder_terminal>);
static_assert(!detail::RequiredDecoderAcceptableT<Candidate>);
static_assert(detail::RequiredDecoderAcceptableT<Validated>);
static_assert(!std::is_constructible_v<MatchResult<int>, bool, int>);
static_assert(!std::is_default_constructible_v<MatchResult<int>>);
static_assert(!std::is_constructible_v<MatchResult<void>, bool>);
static_assert(!std::is_convertible_v<MatchResult<int>, bool>);
static_assert(!std::is_convertible_v<MatchResult<void>, bool>);

struct Fixed {
  Candidate value;
};
struct Named {
  Entry<Key<"n">, Candidate> value;
};
struct Choices {
  Try<Candidate> first;
  Try<types::value::Int64> fallback;
};
struct FalsePayload {
  auto operator()(int) const -> bool { return false; }
};
static_assert(!detail::RequiredDecoderAcceptableT<Fixed>);
static_assert(!detail::RequiredDecoderAcceptableT<Named>);
static_assert(detail::RequiredDecoderAcceptableT<Choices>);

template <class Reader>
void check_required_and_candidate_boundaries() {
  Choices choices;
  ASSERT_TRUE(decode_document_into<Reader>(choices, "[1,2]").is_ok());
  EXPECT_EQ(choices.first.value(), 1);
  EXPECT_EQ(choices.fallback.value(), 2);

  Validated value;
  ASSERT_TRUE(decode_document_into<Reader>(value, "2").is_ok());
  EXPECT_EQ(value.value(), 2);
  EXPECT_EQ(decode_document_into<Reader>(value, "0").code,
            ErrorCode::out_of_range);

  using Bools =
      Elem<scan::array | decode::collect<scan::scalar | decode::typed_boolean> |
               store::assign,
           encode::value>;
  Bools booleans;
  ASSERT_TRUE(
      decode_document_into<Reader>(booleans, "[false,true,false]").is_ok());
  EXPECT_EQ(booleans.value(), (std::vector<bool>{false, true, false}));
}

TEST(MatchBoundaries, RequiredDecodeTryAndBoolDataRemainDistinct) {
  check_required_and_candidate_boundaries<json::SimpleReader>();
  check_required_and_candidate_boundaries<json::FastReader<>>();
}

TEST(MatchBoundaries, FalseBoolPayloadRetainsSuccessfulMatcherStatus) {
  struct Context {
  } ctx;
  constexpr auto pipeline = match::literal<1> | step<FalsePayload>;
  const auto accepted = decltype(pipeline)::invoke(1, ctx);
  static_assert(
      std::same_as<std::remove_cvref_t<decltype(accepted)>, MatchResult<bool>>);
  EXPECT_TRUE(accepted.matched());
  EXPECT_FALSE(accepted.value());
  const auto rejected = decltype(pipeline)::invoke(2, ctx);
  EXPECT_FALSE(rejected.matched());
}

TEST(MatchBoundaries, StoreKeepsMatchingEffectAndSkipsRejectedAssignment) {
  struct Context {
    int stored = 99;
    auto value() -> int& { return stored; }
  } ctx;
  constexpr auto pipeline = match::literal<1> | store::assign;
  const auto accepted = decltype(pipeline)::invoke(1, ctx);
  static_assert(
      std::same_as<std::remove_cvref_t<decltype(accepted)>, MatchResult<void>>);
  EXPECT_TRUE(accepted.matched());
  EXPECT_EQ(ctx.stored, 1);
  ctx.stored = 99;
  const auto rejected = decltype(pipeline)::invoke(2, ctx);
  EXPECT_FALSE(rejected.matched());
  EXPECT_EQ(ctx.stored, 99);
}

TEST(MatchBoundaries, MultipleMatchersFlattenTheirControlResult) {
  struct Context {
  } ctx;
  constexpr auto pipeline = match::literal<1> |
                            match::predicate<[](int n) { return n > 0; }> |
                            step<FalsePayload>;
  const auto accepted = decltype(pipeline)::invoke(1, ctx);
  static_assert(
      std::same_as<std::remove_cvref_t<decltype(accepted)>, MatchResult<bool>>);
  EXPECT_TRUE(accepted.matched());
  EXPECT_FALSE(accepted.value());
  EXPECT_FALSE(decltype(pipeline)::invoke(2, ctx).matched());
}

struct MoveOnlyPayload {
  explicit MoveOnlyPayload(int n) : value(n) {}
  MoveOnlyPayload(const MoveOnlyPayload&) = delete;
  MoveOnlyPayload(MoveOnlyPayload&&) = default;
  auto operator=(const MoveOnlyPayload&) -> MoveOnlyPayload& = delete;
  auto operator=(MoveOnlyPayload&&) -> MoveOnlyPayload& = default;
  int value;
};

TEST(MatchBoundaries, MismatchNeedsNoDefaultOrCopyablePayload) {
  static_assert(!std::is_default_constructible_v<MoveOnlyPayload>);
  static_assert(!std::is_copy_constructible_v<MoveOnlyPayload>);
  struct Context {
  } ctx;
  constexpr auto pipeline = match::predicate<[](const MoveOnlyPayload& input) {
    return input.value > 0;
  }>;
  auto accepted = decltype(pipeline)::invoke(MoveOnlyPayload{7}, ctx);
  static_assert(std::same_as<std::remove_cvref_t<decltype(accepted)>,
                             MatchResult<MoveOnlyPayload>>);
  ASSERT_TRUE(accepted.matched());
  auto transferred = std::move(accepted).value();
  EXPECT_EQ(transferred.value, 7);
  const auto rejected = decltype(pipeline)::invoke(MoveOnlyPayload{-1}, ctx);
  EXPECT_FALSE(rejected.matched());
}
}  // namespace
