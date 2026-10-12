#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <string_view>

#include "hosq/hosq.hh"
#include "hosq/json/fast_reader.hh"
#include "hosq/json/simple_reader.hh"

namespace construct_test {
struct Bytes {};
struct DispatchContext {
  mutable int calls = 0;
  mutable int output = -1;
  auto value() const -> int& { return output; }
};
struct PriorityInput {
  auto operator()(int input, DispatchContext& ctx) const
      -> hosq::MatchResult<int> {
    ++ctx.calls;
    return {input > 0, input + 1};
  }
  auto operator()(int input, const DispatchContext& ctx) const
      -> hosq::MatchResult<int> {
    ++ctx.calls;
    return {input > 0, input + 2};
  }
  auto operator()(int) const -> int { return -10; }
};
struct PrioritySource {
  auto operator()(const DispatchContext& ctx) const -> hosq::MatchResult<int> {
    ++ctx.calls;
    return {true, 10};
  }
  auto operator()() const -> int { return -20; }
};
inline unsigned adaptations = 0;
struct OrdinaryConstruct {
  auto operator()(std::string_view input) const -> std::string {
    return std::string(input);
  }
};
struct DerivedConstruct : hosq::detail::ConstructFn<std::string> {
  auto operator()(std::string_view input) const -> std::string {
    return std::string(input) + "!";
  }
};
struct DerivedStore : hosq::store::detail::OnceImpl {
  template <class Input, class Context>
  auto operator()(Input&& input, Context& ctx) const -> void {
    ctx.value() = std::forward<Input>(input) + "?";
  }
};
struct ObserveString {
  auto operator()(std::string input) const -> std::string { return input + "!"; }
};
struct ObservedValue {
  std::string text;
  explicit ObservedValue(std::string_view input) : text(input) {}
  ObservedValue(ObservedValue&& other) : text(other.text + "m") {}
  auto operator=(ObservedValue&& other) -> ObservedValue& {
    text = other.text + "m";
    return *this;
  }
};
struct CustomContext {
  std::optional<std::string> output;
  unsigned value_calls = 0;
  hosq::json::FastReader<> reader_value;
  hosq::json::FastReader<>::state_type state;
  auto value() -> std::optional<std::string>& {
    ++value_calls;
    return output;
  }
  auto reader() -> hosq::json::FastReader<>& { return reader_value; }
  auto reader_state() -> hosq::json::FastReader<>::state_type& { return state; }
};
}  // namespace construct_test

namespace hosq::ext {
template <>
struct InputView<construct_test::Bytes> {
  template <class Step, class Context>
  using type = std::string_view;
  template <class Step, class Context>
  static auto get(construct_test::Bytes&, Context&) -> std::string_view {
    ++construct_test::adaptations;
    return "adapted";
  }
};
}  // namespace hosq::ext

namespace {
using namespace hosq;
using Reader = json::FastReader<>;
using Context = DecodeContext<std::optional<std::string>, Reader>;
constexpr auto owned = construct<std::string> | store::once;
template <class Input, class Ctx>
constexpr bool direct = requires(Input& input, Ctx& ctx) {
  store::detail::OnceImpl::template try_construct<
      store::detail::OnceImpl, detail::ConstructFn<std::string>{}>(input, ctx);
};
static_assert(direct<std::string_view, Context>);
static_assert(!direct<construct_test::Bytes, Context>);
static_assert(!direct<std::string, Context>);
static_assert(!direct<std::string_view, construct_test::CustomContext>);
static_assert(std::same_as<detail::pipeline_storage_t<
                               decltype(owned)::info<std::string_view, Context>>,
                           std::optional<std::string>>);

TEST(Construct, OrdinaryStepOwnsItsResultAndComposesWithUnknownSteps) {
  std::string source = "abc";
  Reader reader;
  Reader::state_type state;
  std::optional<std::string> output;
  Context ctx{output, reader, state};
  auto result = construct<std::string>.invoke(std::string_view{source}, ctx);
  source[0] = 'x';
  EXPECT_EQ(result, "abc");
  (construct<std::string> | step<construct_test::ObserveString> | store::once)
      .invoke(std::string_view{"abc"}, ctx);
  EXPECT_EQ(output, "abc!");

  int integer = 0;
  DecodeContext<int, Reader> integer_ctx{integer, reader, state};
  (construct<int> | store::assign).invoke(short{42}, integer_ctx);
  EXPECT_EQ(integer, 42);

  std::optional<construct_test::ObservedValue> tracked;
  DecodeContext<decltype(tracked), Reader> tracked_ctx{tracked, reader, state};
  (construct<construct_test::ObservedValue> | store::once)
      .invoke(std::string_view{"abc"}, tracked_ctx);
  ASSERT_TRUE(tracked.has_value());
  EXPECT_EQ(tracked->text, "abcmm");
}

TEST(Construct, UnknownAdapterContextAndDerivedStepUseOrdinaryExecution) {
  Reader reader;
  Reader::state_type state;
  std::optional<std::string> output;
  const Context ctx{output, reader, state};
  construct_test::adaptations = 0;
  owned.invoke(construct_test::Bytes{}, ctx);
  EXPECT_EQ(output, "adapted");
  EXPECT_EQ(construct_test::adaptations, 1u);
  output.reset();
  (step<construct_test::DerivedConstruct> | store::once)
      .invoke(std::string_view{"abc"}, ctx);
  EXPECT_EQ(output, "abc!");

  output.reset();
  (construct<std::string> | step<construct_test::DerivedStore>)
      .invoke(std::string_view{"abc"}, ctx);
  EXPECT_EQ(output, "abc?");

  construct_test::CustomContext custom;
  owned.invoke(std::string_view{"abc"}, custom);
  EXPECT_EQ(custom.output, "abc");
  EXPECT_EQ(custom.value_calls, 2u);
}

TEST(Construct, OccupiedOutputKeepsDuplicateErrorAndAliasedInput) {
  Reader reader;
  Reader::state_type state;
  reader.reset(state, "\"unused\"");
  ASSERT_TRUE(reader.next_token(state));
  std::optional<std::string> output{std::string(128, 'x')};
  Context ctx{output, reader, state};
  auto bytes = std::string_view{*output}.substr(32);
  owned.invoke(bytes, ctx);
  EXPECT_EQ(output, std::string(128, 'x'));
  EXPECT_EQ(reader.error(state).code, ErrorCode::duplicate_member);
}

TEST(Construct, JsonStringsRetainRawEscapeSemanticsAndOwnedStorage) {
  constexpr auto optimized =
      scan::scalar | decode::string_view | construct<std::string> | store::once;
  constexpr auto baseline = scan::scalar | decode::string_view |
                            step<construct_test::OrdinaryConstruct> |
                            store::once;
  for (auto text : {std::string("\"short\""),
                    std::string("\"") + std::string(64, 'a') + "\"",
                    std::string("\"") + std::string(128, 'f') + "\"",
                    std::string(R"("a\n\u0061\"\\z")")}) {
    auto compare = [&]<class R>() {
      Elem<optimized, noop_encoder> actual;
      Elem<baseline, noop_encoder> expected;
      const auto a = decode_into<R>(actual, text);
      const auto b = decode_into<R>(expected, text);
      ASSERT_TRUE(a.is_ok());
      EXPECT_EQ(a.code, b.code);
      EXPECT_EQ(actual.value(), expected.value());
      EXPECT_EQ(actual.value()->size(), text.size() - 2);
      EXPECT_EQ(*actual.value(), text.substr(1, text.size() - 2));
    };
    compare.template operator()<json::FastReader<>>();
    compare.template operator()<json::SimpleReader>();
  }
}
TEST(Pipeline, SharedContinuationKeepsContextPriorityAndMismatchShortCircuit) {
  constexpr auto input = step<construct_test::PriorityInput> | store::assign;
  construct_test::DispatchContext ctx;
  EXPECT_FALSE(input.invoke(0, ctx));
  EXPECT_EQ(ctx.output, -1);
  EXPECT_EQ(ctx.calls, 1);
  EXPECT_TRUE(input.invoke(3, ctx));
  EXPECT_EQ(ctx.output, 4);
  EXPECT_TRUE(input.invoke(3, std::as_const(ctx)));
  EXPECT_EQ(ctx.output, 5);
  EXPECT_EQ(ctx.calls, 3);

  constexpr auto source = step<construct_test::PrioritySource> | store::assign;
  EXPECT_TRUE(source.invoke(std::as_const(ctx)));
  EXPECT_EQ(ctx.output, 10);
  EXPECT_EQ(ctx.calls, 4);
  constexpr auto reset =
      step<decltype([](const construct_test::DispatchContext& c) {
        c.output = -1;
      })>;
  EXPECT_TRUE((reset | source).invoke(ctx));
  EXPECT_EQ(ctx.output, 10);
  EXPECT_EQ(ctx.calls, 5);

  // No-context MatchResult and plain/void terminals use the same continuation.
  constexpr auto match = step<decltype([](int value) {
    return MatchResult<int>{value > 0, value};
  })>;
  EXPECT_FALSE(match.invoke(0, ctx));
  EXPECT_TRUE((match | store::assign).invoke(7, ctx));
  EXPECT_EQ(ctx.output, 7);
  constexpr auto number = step<decltype([] { return 42; })>;
  EXPECT_EQ(number.invoke(ctx), 42);
  reset.invoke(ctx);
  EXPECT_EQ(ctx.output, -1);
}
}  // namespace
