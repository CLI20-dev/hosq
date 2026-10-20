#include <gtest/gtest.h>

#include <array>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "hosq/hosq.hh"
#include "hosq/json/fast_reader.hh"
#include "hosq/json/simple_reader.hh"

namespace {
using namespace hosq;
constexpr auto integer = scan::scalar | decode::integer<int>;
constexpr auto one = integer | match::literal<1>;
constexpr auto text = scan::scalar | decode::string_view;
constexpr auto key_a =
    text | match::predicate<[](std::string_view key) { return key == "a"; }>;
using Plain =
    Elem<scan::array | decode::collect<integer> | store::assign, noop_encoder>;
using PlainObject =
    Elem<scan::object | decode::collect<text, integer> | store::assign,
         noop_encoder>;

template <class E, class F>
struct Choices {
  Try<E> candidate;
  Try<F> fallback;
};

template <class R>
void check_fixed_each() {
  using Fixed =
      Elem<scan::array | decode::each<one | store::assign, 2>, noop_encoder>;
  Choices<Fixed, Plain> values;
  ASSERT_TRUE(decode_document_into<R>(values, "[[1,1],[1,2]]").is_ok());
  EXPECT_EQ(values.candidate.value(), (std::array<int, 2>{1, 1}));
  EXPECT_EQ(values.fallback.value(), (std::vector<int>{1, 2}));
  for (auto input : {"[[1]]", "[[1,1,1]]", "[[1,\"x\"]]"}) {
    Choices<Fixed, Plain> invalid;
    EXPECT_FALSE(decode_document_into<R>(invalid, input).is_ok());
    EXPECT_TRUE(invalid.fallback.value().empty());
  }
  using Empty =
      Elem<scan::array | decode::each<one | store::assign, 0>, noop_encoder>;
  Choices<Empty, Plain> empty;
  EXPECT_TRUE(decode_document_into<R>(empty, "[[]]").is_ok());
  EXPECT_EQ(decode_document_into<R>(empty, "[[1]]").code,
            ErrorCode::invalid_length);
  EXPECT_TRUE(empty.fallback.value().empty());

  using DynamicObject =
      Elem<scan::object |
               decode::each<key_a | store::assign, one | store::assign>,
           noop_encoder>;
  using FixedObject =
      Elem<scan::object |
               decode::each<key_a | store::assign, one | store::assign, 1>,
           noop_encoder>;
  auto check_object = []<class E>() {
    Choices<E, PlainObject> object;
    ASSERT_TRUE(decode_document_into<R>(object, R"([{"a":1}])").is_ok());
    ASSERT_EQ(object.candidate.value().keys().size(), 1U);
    ASSERT_EQ(object.candidate.value().values().size(), 1U);
    EXPECT_EQ(object.candidate.value().keys()[0], "a");
    EXPECT_EQ(object.candidate.value().values()[0], 1);
    Choices<E, PlainObject> rejected;
    ASSERT_TRUE(decode_document_into<R>(rejected, R"([{"b":2}])").is_ok());
    ASSERT_EQ(rejected.fallback.value().keys().size(), 1U);
    ASSERT_EQ(rejected.fallback.value().values().size(), 1U);
    EXPECT_EQ(rejected.fallback.value().keys()[0], "b");
    EXPECT_EQ(rejected.fallback.value().values()[0], 2);
  };
  check_object.template operator()<DynamicObject>();
  check_object.template operator()<FixedObject>();
}

TEST(MatchCoreAudit, EachMatchingReachesTryWithoutBoolTerminals) {
  check_fixed_each<json::SimpleReader>();
  check_fixed_each<json::FastReader<>>();
}

struct ElementContextTerminal {
  template <class C>
  auto operator()(C& ctx) const {
    if constexpr (requires { ctx.value(); }) {
      using Storage = std::remove_cvref_t<decltype(ctx.value())>;
      if constexpr (std::same_as<Storage, std::array<int, 1>>) return false;
    }
  }
};

TEST(MatchCoreAudit, FixedEachAnalyzesTheElementContextItInvokes) {
  constexpr auto inner =
      scan::scalar | decode::number_into<int> | step<ElementContextTerminal>;
  using E = Elem<scan::array | decode::each<inner, 1>, noop_encoder>;
  E value;
  ASSERT_TRUE(decode_document_into<json::SimpleReader>(value, "[7]").is_ok());
  EXPECT_EQ(value.value()[0], 7);
  ASSERT_TRUE(decode_document_into<json::FastReader<>>(value, "[8]").is_ok());
  EXPECT_EQ(value.value()[0], 8);
}

struct ConstMatchingTerminal {
  template <class>
  using storage_type = std::monostate;
  template <class C>
    requires(!std::is_const_v<C>)
  void operator()(C&) const {}
  template <class C>
  auto operator()(const C& ctx) const {
    return match::predicate<[] { return false; }>.invoke(ctx);
  }
};

TEST(MatchCoreAudit, SelectedConstOverloadPreservesItsMatchingTerminal) {
  using E = Elem<step<ConstMatchingTerminal>, noop_encoder>;
  E value;
  json::SimpleReader reader;
  json::SimpleReader::state_type state;
  DecodeContext<E::storage_type, json::SimpleReader> ctx{value.storage(), reader,
                                                         state};
  static_assert(std::same_as<decltype(value.decode(ctx)), MatchResult<void>>);
  EXPECT_FALSE(value.decode(ctx).matched());
  EXPECT_FALSE(value.decode(std::as_const(ctx)).matched());

  Choices<E, types::value::Int64> choices;
  ASSERT_TRUE(decode_document_into<json::SimpleReader>(choices, "[7]").is_ok());
  EXPECT_EQ(choices.fallback.value(), 7);
}

TEST(MatchCoreAudit, CoreInvocationAndAnalysisAgreeForFalseDataAndNoInputMatch) {
  struct Context {
    int calls = 0;
  } ctx;
  const Context const_ctx;
  constexpr auto yes = match::predicate<[] { return true; }>;
  constexpr auto no = match::predicate<[] { return false; }>;
  constexpr auto produce_false = action<void, bool, [] { return false; }>;
  constexpr auto accept_false = match::predicate<[](bool x) { return !x; }>;
  constexpr auto consume = mutable_action<bool, void, [](bool x, Context& c) {
    EXPECT_FALSE(x);
    ++c.calls;
  }>;
  constexpr auto pipeline = produce_false | accept_false | consume;
  using P = decltype(pipeline);
  static_assert(std::same_as<P::final_output_type<void, Context>,
                             decltype(pipeline.invoke(ctx))>);
  static_assert(std::same_as<decltype(pipeline.invoke(ctx)), MatchResult<void>>);
  EXPECT_TRUE(pipeline.invoke(ctx).matched());
  EXPECT_EQ(ctx.calls, 1);
  EXPECT_FALSE((no | produce_false | consume).invoke(ctx).matched());
  EXPECT_EQ(ctx.calls, 1);
  const auto false_payload = (yes | produce_false).invoke(const_ctx);
  static_assert(std::same_as<std::remove_cvref_t<decltype(false_payload)>,
                             MatchResult<bool>>);
  EXPECT_TRUE(false_payload.matched());
  EXPECT_FALSE(false_payload.value());
  const auto transformed = (match::literal<false> |
                            action<bool, int, [](bool x) { return x ? 1 : 2; }>)
                               .invoke(false, const_ctx);
  EXPECT_TRUE(transformed.matched());
  EXPECT_EQ(transformed.value(), 2);
}
}  // namespace
