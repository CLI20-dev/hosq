#include <gtest/gtest.h>

#include <string_view>

#include "common.hh"
#include "ser20/pipeline/decode.hh"
#include "ser20/pipeline/store.hh"
#include "ser20/pipeline/validate.hh"

using namespace std::string_view_literals;

TEST(PipelineChain, TwoSteps) {
  auto p = ser20::decode::number<int> | ser20::validate::range<0, 100>;
  using P = decltype(p);

  auto ok = pinvoke<P>("42"sv);
  ASSERT_TRUE(ok.has_value());
  EXPECT_EQ(*ok, 42);
}

TEST(PipelineChain, ThreeSteps) {
  auto p = ser20::decode::number<int> | ser20::validate::range<0, 100> |
           ser20::validate::match<42>;
  using P = decltype(p);

  EXPECT_TRUE(pinvoke<P>("42"sv).has_value());
  EXPECT_FALSE(pinvoke<P>("99"sv).has_value());
}

TEST(PipelineChain, CustomAtEnd) {
  constexpr auto doubled = [](int v) -> int { return v * 2; };
  auto p = ser20::decode::number<int> | ser20::decode::custom<doubled>;
  using P = decltype(p);

  auto r = pinvoke<P>("21"sv);
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(*r, 42);
}

TEST(PipelineChain, NumberToAssign) {
  ElemCtx<int> ctx;
  auto p = ser20::decode::number<int> | ser20::validate::range<0, 100> |
           ser20::store::assign;
  using P = decltype(p);
  auto r = pinvoke_with<P>("55"sv, ctx);
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(ctx.storage, 55);
}

TEST(PipelineChain, ErrorDoesNotStore) {
  ElemCtx<int> ctx;
  auto p = ser20::decode::number<int> | ser20::validate::range<0, 100> |
           ser20::store::assign;
  using P = decltype(p);
  auto r = pinvoke_with<P>("999"sv, ctx);
  ASSERT_FALSE(r.has_value());
  EXPECT_EQ(ctx.storage, 0);
}

TEST(PipelineErrorPropagation, ShortCircuitsAtFirstError) {
  auto p = ser20::validate::range<0, 10> | ser20::validate::match<5>;
  using P = decltype(p);

  auto r = pinvoke<P>(99);
  ASSERT_FALSE(r.has_value());
  EXPECT_EQ(r.error().code, ser20::ErrorCode::out_of_range);
}

TEST(PipelineErrorPropagation, NumberParseErrorPropagates) {
  auto p = ser20::decode::number<int> | ser20::validate::range<0, 100>;
  using P = decltype(p);

  auto r = pinvoke<P>("not_a_number"sv);
  ASSERT_FALSE(r.has_value());
  EXPECT_EQ(r.error().code, ser20::ErrorCode::invalid_number);
}

struct DispatchTrackCtx {
  int mutable_path_calls = 0;
};

struct DualDispatchStep {
  auto operator()(int input, DispatchTrackCtx& ctx) const -> int {
    ctx.mutable_path_calls++;
    return input;
  }

  auto operator()(int input, const DispatchTrackCtx&) const -> int {
    return input + 1000;
  }
};

TEST(PipelineDispatch, MutableContextUsesContextAwareOverload) {
  using P = ser20::Pipeline<DualDispatchStep{}>;
  DispatchTrackCtx ctx;
  auto r = P::invoke(42, ctx);
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(*r, 42);
  EXPECT_EQ(ctx.mutable_path_calls, 1);
}

TEST(PipelineDispatch, ConstContextFallsToNoContextOverload) {
  using P = ser20::Pipeline<DualDispatchStep{}>;
  const DispatchTrackCtx ctx{};
  auto r = P::invoke(42, ctx);
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(*r, 1042);
}

TEST(PipelineDispatch, MutableContextChainAllStepsSeeMutableCtx) {
  using P = ser20::Pipeline<DualDispatchStep{}, DualDispatchStep{}>;
  DispatchTrackCtx ctx;
  auto r = P::invoke(3, ctx);
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(*r, 3);
  EXPECT_EQ(ctx.mutable_path_calls, 2);
}

TEST(PipelineDispatch, ConstContextChainAllStepsFallToNoCtx) {
  using P = ser20::Pipeline<DualDispatchStep{}, DualDispatchStep{}>;
  const DispatchTrackCtx ctx{};
  auto r = P::invoke(3, ctx);
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(*r, 2003);
}

struct EarlyReturnCtx {
  int a_calls = 0;
  int b_calls = 0;
};

struct CountingStepA {
  auto operator()(int input, EarlyReturnCtx& ctx) const -> int {
    ctx.a_calls++;
    return input;
  }
};

struct CountingStepB {
  auto operator()(int input, EarlyReturnCtx& ctx) const -> int {
    ctx.b_calls++;
    return input;
  }
};

struct ErrorStep {
  auto operator()(int) const -> ser20::Result<int, ser20::Error> {
    return ser20::unexpected(
        ser20::Error{.code = ser20::ErrorCode::validation_failed});
  }
};

TEST(PipelineEarlyReturn, StepAfterErrorIsNotInvoked) {
  using P = ser20::Pipeline<ErrorStep{}, CountingStepB{}>;
  EarlyReturnCtx ctx;
  auto r = P::invoke(42, ctx);
  ASSERT_FALSE(r.has_value());
  EXPECT_EQ(r.error().code, ser20::ErrorCode::validation_failed);
  EXPECT_EQ(ctx.b_calls, 0);
}

TEST(PipelineEarlyReturn, StepsBeforeErrorAreInvoked) {
  using P = ser20::Pipeline<CountingStepA{}, ErrorStep{}, CountingStepB{}>;
  EarlyReturnCtx ctx;
  auto r = P::invoke(42, ctx);
  ASSERT_FALSE(r.has_value());
  EXPECT_EQ(ctx.a_calls, 1);
  EXPECT_EQ(ctx.b_calls, 0);
}

TEST(PipelineEarlyReturn, AllStepsCalledOnSuccess) {
  using P = ser20::Pipeline<CountingStepA{}, CountingStepB{}>;
  EarlyReturnCtx ctx;
  auto r = P::invoke(42, ctx);
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(ctx.a_calls, 1);
  EXPECT_EQ(ctx.b_calls, 1);
}

TEST(PipelineEarlyReturn, OriginalErrorCodePreservedThroughChain) {
  struct ErrorStepB {
    auto operator()(int) const -> ser20::Result<int, ser20::Error> {
      return ser20::unexpected(
          ser20::Error{.code = ser20::ErrorCode::invalid_choice});
    }
  };
  using P = ser20::Pipeline<ErrorStep{}, ErrorStepB{}>;
  EarlyReturnCtx ctx;
  auto r = P::invoke(42, ctx);
  ASSERT_FALSE(r.has_value());
  EXPECT_EQ(r.error().code, ser20::ErrorCode::validation_failed);
}
