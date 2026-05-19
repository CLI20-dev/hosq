#include <gtest/gtest.h>

#include <string_view>

#include "ser20/pipeline/decode.hh"
#include "ser20/pipeline/store.hh"
#include "ser20/pipeline/unknown.hh"
#include "ser20/pipeline/validate.hh"

using namespace std::string_view_literals;

// ── Context stubs ─────────────────────────────────────────────────────────────

// Minimal context for steps that don't access the context at all.
struct NoCtx {};

// Context stub with elem() for store:: steps.
template <class T>
struct ElemCtx {
  T storage{};
  auto elem() -> T& { return storage; }
};

// ── Invoke helpers ────────────────────────────────────────────────────────────

// Invoke a Pipeline<Fns...> with a NoCtx.
template <class P, class Input>
auto pinvoke(Input input) {
  NoCtx ctx;
  return P::invoke(std::move(input), ctx);
}

// Invoke a Pipeline<Fns...> with a given context.
template <class P, class Input, class Ctx>
auto pinvoke_with(Input input, Ctx& ctx) {
  return P::invoke(std::move(input), ctx);
}

// ── decode::boolean ───────────────────────────────────────────────────────────

TEST(DecodeBoolean, True) {
  using P = decltype(ser20::decode::boolean);
  auto r = pinvoke<P>("true"sv);
  ASSERT_TRUE(r.has_value());
  EXPECT_TRUE(r.value());
}

TEST(DecodeBoolean, False) {
  using P = decltype(ser20::decode::boolean);
  auto r = pinvoke<P>("false"sv);
  ASSERT_TRUE(r.has_value());
  EXPECT_FALSE(r.value());
}

TEST(DecodeBoolean, InvalidReturnsTypeMismatch) {
  using P = decltype(ser20::decode::boolean);
  auto r = pinvoke<P>("yes"sv);
  ASSERT_FALSE(r.has_value());
  EXPECT_EQ(r.error().code, ser20::ErrorCode::type_mismatch);
}

// ── decode::string_view ───────────────────────────────────────────────────────

TEST(DecodeStringView, PassesThrough) {
  using P = decltype(ser20::decode::string_view);
  auto r = pinvoke<P>("hello"sv);
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(*r, "hello"sv);
}

TEST(DecodeStringView, Empty) {
  using P = decltype(ser20::decode::string_view);
  auto r = pinvoke<P>(""sv);
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(*r, ""sv);
}

// ── decode::number<T> ─────────────────────────────────────────────────────────

TEST(DecodeNumber, Integer) {
  using P = decltype(ser20::decode::number<int>);
  auto r = pinvoke<P>("42"sv);
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(*r, 42);
}

TEST(DecodeNumber, NegativeInteger) {
  using P = decltype(ser20::decode::number<int>);
  auto r = pinvoke<P>("-7"sv);
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(*r, -7);
}

TEST(DecodeNumber, Float) {
  using P = decltype(ser20::decode::number<double>);
  auto r = pinvoke<P>("3.14"sv);
  ASSERT_TRUE(r.has_value());
  EXPECT_DOUBLE_EQ(*r, 3.14);
}

TEST(DecodeNumber, OutOfRange) {
  using P = decltype(ser20::decode::number<int8_t>);
  auto r = pinvoke<P>("999"sv);
  ASSERT_FALSE(r.has_value());
  EXPECT_EQ(r.error().code, ser20::ErrorCode::out_of_range);
}

TEST(DecodeNumber, InvalidString) {
  using P = decltype(ser20::decode::number<int>);
  auto r = pinvoke<P>("abc"sv);
  ASSERT_FALSE(r.has_value());
  EXPECT_EQ(r.error().code, ser20::ErrorCode::invalid_number);
}

TEST(DecodeNumber, TrailingChars) {
  using P = decltype(ser20::decode::number<int>);
  auto r = pinvoke<P>("42abc"sv);
  ASSERT_FALSE(r.has_value());
  EXPECT_EQ(r.error().code, ser20::ErrorCode::invalid_number);
}

// ── decode::identity ──────────────────────────────────────────────────────────

TEST(DecodeIdentity, Int) {
  using P = decltype(ser20::decode::identity);
  auto r = pinvoke<P>(99);
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(*r, 99);
}

TEST(DecodeIdentity, StringView) {
  using P = decltype(ser20::decode::identity);
  auto r = pinvoke<P>("foo"sv);
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(*r, "foo"sv);
}

// ── decode::custom<F> ─────────────────────────────────────────────────────────

TEST(DecodeCustom, PlainReturn) {
  constexpr auto to_len = [](std::string_view sv) -> int {
    return (int)sv.size();
  };
  using P = decltype(ser20::decode::custom<to_len>);
  auto r = pinvoke<P>("hello"sv);
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(*r, 5);
}

TEST(DecodeCustom, ResultReturn) {
  constexpr auto checked = [](int v) -> ser20::Result<int, ser20::Error> {
    if (v < 0)
      return ser20::unexpected(
          ser20::Error{.code = ser20::ErrorCode::out_of_range});
    return v * 2;
  };
  using P = decltype(ser20::decode::custom<checked>);
  auto ok = pinvoke<P>(3);
  ASSERT_TRUE(ok.has_value());
  EXPECT_EQ(*ok, 6);

  auto err = pinvoke<P>(-1);
  ASSERT_FALSE(err.has_value());
  EXPECT_EQ(err.error().code, ser20::ErrorCode::out_of_range);
}

// ── validate::match<V> ────────────────────────────────────────────────────────

TEST(ValidateMatch, Equal) {
  using P = decltype(ser20::validate::match<42>);
  auto r = pinvoke<P>(42);
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(*r, 42);
}

TEST(ValidateMatch, NotEqual) {
  using P = decltype(ser20::validate::match<42>);
  auto r = pinvoke<P>(99);
  ASSERT_FALSE(r.has_value());
  EXPECT_EQ(r.error().code, ser20::ErrorCode::invalid_choice);
}

// ── validate::one_of<Vs...> ───────────────────────────────────────────────────

TEST(ValidateOneOf, Matches) {
  using P = decltype(ser20::validate::one_of<1, 2, 3>);
  EXPECT_TRUE(pinvoke<P>(1).has_value());
  EXPECT_TRUE(pinvoke<P>(2).has_value());
  EXPECT_TRUE(pinvoke<P>(3).has_value());
}

TEST(ValidateOneOf, NoMatch) {
  using P = decltype(ser20::validate::one_of<1, 2, 3>);
  auto r = pinvoke<P>(4);
  ASSERT_FALSE(r.has_value());
  EXPECT_EQ(r.error().code, ser20::ErrorCode::invalid_choice);
}

// ── validate::range<Min, Max> ─────────────────────────────────────────────────

TEST(ValidateRange, InRange) {
  using P = decltype(ser20::validate::range<0, 10>);
  EXPECT_TRUE(pinvoke<P>(0).has_value());
  EXPECT_TRUE(pinvoke<P>(5).has_value());
  EXPECT_TRUE(pinvoke<P>(10).has_value());
}

TEST(ValidateRange, OutOfRange) {
  using P = decltype(ser20::validate::range<0, 10>);
  auto lo = pinvoke<P>(-1);
  ASSERT_FALSE(lo.has_value());
  EXPECT_EQ(lo.error().code, ser20::ErrorCode::out_of_range);

  auto hi = pinvoke<P>(11);
  ASSERT_FALSE(hi.has_value());
  EXPECT_EQ(hi.error().code, ser20::ErrorCode::out_of_range);
}

// ── validate::min<Min> / validate::max<Max> ───────────────────────────────────

TEST(ValidateMin, AboveOrAt) {
  using P = decltype(ser20::validate::min<5>);
  EXPECT_TRUE(pinvoke<P>(5).has_value());
  EXPECT_TRUE(pinvoke<P>(100).has_value());
}

TEST(ValidateMin, Below) {
  using P = decltype(ser20::validate::min<5>);
  auto r = pinvoke<P>(4);
  ASSERT_FALSE(r.has_value());
  EXPECT_EQ(r.error().code, ser20::ErrorCode::out_of_range);
}

TEST(ValidateMax, BelowOrAt) {
  using P = decltype(ser20::validate::max<5>);
  EXPECT_TRUE(pinvoke<P>(5).has_value());
  EXPECT_TRUE(pinvoke<P>(-100).has_value());
}

TEST(ValidateMax, Above) {
  using P = decltype(ser20::validate::max<5>);
  auto r = pinvoke<P>(6);
  ASSERT_FALSE(r.has_value());
  EXPECT_EQ(r.error().code, ser20::ErrorCode::out_of_range);
}

// ── validate::non_empty ───────────────────────────────────────────────────────

TEST(ValidateNonEmpty, NonEmpty) {
  using P = decltype(ser20::validate::non_empty);
  auto r = pinvoke<P>("hello"sv);
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(*r, "hello"sv);
}

TEST(ValidateNonEmpty, Empty) {
  using P = decltype(ser20::validate::non_empty);
  auto r = pinvoke<P>(""sv);
  ASSERT_FALSE(r.has_value());
  EXPECT_EQ(r.error().code, ser20::ErrorCode::validation_failed);
}

// ── validate::size<N> ─────────────────────────────────────────────────────────

TEST(ValidateSize, Exact) {
  using P = decltype(ser20::validate::size<3>);
  EXPECT_TRUE(pinvoke<P>("abc"sv).has_value());
}

TEST(ValidateSize, Mismatch) {
  using P = decltype(ser20::validate::size<3>);
  auto r = pinvoke<P>("ab"sv);
  ASSERT_FALSE(r.has_value());
  EXPECT_EQ(r.error().code, ser20::ErrorCode::size_mismatch);
}

// ── validate::size_range<Min, Max> ────────────────────────────────────────────

TEST(ValidateSizeRange, InRange) {
  using P = decltype(ser20::validate::size_range<2, 5>);
  EXPECT_TRUE(pinvoke<P>("ab"sv).has_value());
  EXPECT_TRUE(pinvoke<P>("abcde"sv).has_value());
}

TEST(ValidateSizeRange, OutOfRange) {
  using P = decltype(ser20::validate::size_range<2, 5>);
  EXPECT_FALSE(pinvoke<P>("a"sv).has_value());
  EXPECT_FALSE(pinvoke<P>("abcdef"sv).has_value());
}

// ── validate::predicate<F> ────────────────────────────────────────────────────

TEST(ValidatePredicate, Passes) {
  constexpr auto is_even = [](int v) -> bool { return v % 2 == 0; };
  using P = decltype(ser20::validate::predicate<is_even>);
  EXPECT_TRUE(pinvoke<P>(4).has_value());
}

TEST(ValidatePredicate, Fails) {
  constexpr auto is_even = [](int v) -> bool { return v % 2 == 0; };
  using P = decltype(ser20::validate::predicate<is_even>);
  auto r = pinvoke<P>(3);
  ASSERT_FALSE(r.has_value());
  EXPECT_EQ(r.error().code, ser20::ErrorCode::validation_failed);
}

// ── validate::custom<F> ───────────────────────────────────────────────────────

TEST(ValidateCustom, BoolReturn) {
  constexpr auto positive = [](int v) -> bool { return v > 0; };
  using P = decltype(ser20::validate::custom<positive>);
  EXPECT_TRUE(pinvoke<P>(1).has_value());
  EXPECT_FALSE(pinvoke<P>(-1).has_value());
}

TEST(ValidateCustom, ResultReturn) {
  constexpr auto check = [](int v) -> ser20::Result<int, ser20::Error> {
    if (v == 0)
      return ser20::unexpected(
          ser20::Error{.code = ser20::ErrorCode::invalid_choice});
    return v;
  };
  using P = decltype(ser20::validate::custom<check>);
  EXPECT_TRUE(pinvoke<P>(1).has_value());
  auto r = pinvoke<P>(0);
  ASSERT_FALSE(r.has_value());
  EXPECT_EQ(r.error().code, ser20::ErrorCode::invalid_choice);
}

// ── validate::identity ────────────────────────────────────────────────────────

TEST(ValidateIdentity, PassesThrough) {
  using P = decltype(ser20::validate::identity);
  auto r = pinvoke<P>(7);
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(*r, 7);
}

// ── unknown::identity ─────────────────────────────────────────────────────────

TEST(UnknownIdentity, PassesThrough) {
  using P = decltype(ser20::unknown::identity);
  auto r = pinvoke<P>(42);
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(*r, 42);
}

// ── Pipeline: operator| composition ──────────────────────────────────────────

TEST(PipelineChain, TwoSteps) {
  // number<int> → range<0,100>  →  Result<int, Error>
  auto p = ser20::decode::number<int> | ser20::validate::range<0, 100>;
  using P = decltype(p);

  auto ok = pinvoke<P>("42"sv);
  ASSERT_TRUE(ok.has_value());
  EXPECT_EQ(*ok, 42);
}

TEST(PipelineChain, ThreeSteps) {
  // number<int> → range<0,100> → match<42>
  auto p = ser20::decode::number<int> | ser20::validate::range<0, 100> |
           ser20::validate::match<42>;
  using P = decltype(p);

  EXPECT_TRUE(pinvoke<P>("42"sv).has_value());
  EXPECT_FALSE(pinvoke<P>("99"sv).has_value());  // fails match<42>
}

TEST(PipelineChain, CustomAtEnd) {
  constexpr auto doubled = [](int v) -> int { return v * 2; };
  auto p = ser20::decode::number<int> | ser20::decode::custom<doubled>;
  using P = decltype(p);

  auto r = pinvoke<P>("21"sv);
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(*r, 42);
}

// ── Pipeline: error propagation ───────────────────────────────────────────────

TEST(PipelineErrorPropagation, ShortCircuitsAtFirstError) {
  // range<0,10> fails → custom must not be called
  bool custom_called = false;
  // Note: lambda must be stateless for NTTP; use a flag via side effect through
  // a global to verify no call occurs.
  auto p = ser20::validate::range<0, 10> | ser20::validate::match<5>;
  using P = decltype(p);

  // 99 is outside [0,10], so match<5> should never run; error is out_of_range
  auto r = pinvoke<P>(99);
  ASSERT_FALSE(r.has_value());
  EXPECT_EQ(r.error().code, ser20::ErrorCode::out_of_range);
  (void)custom_called;
}

TEST(PipelineErrorPropagation, NumberParseErrorPropagates) {
  auto p = ser20::decode::number<int> | ser20::validate::range<0, 100>;
  using P = decltype(p);

  auto r = pinvoke<P>("not_a_number"sv);
  ASSERT_FALSE(r.has_value());
  EXPECT_EQ(r.error().code, ser20::ErrorCode::invalid_number);
}

// ── store:: steps ─────────────────────────────────────────────────────────────

TEST(StoreAssign, WritesToElem) {
  ElemCtx<int> ctx;
  using P = decltype(ser20::store::assign);
  auto r = pinvoke_with<P>(42, ctx);
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(ctx.storage, 42);
}

TEST(StorePush, AppendsToVector) {
  ElemCtx<std::vector<int>> ctx;
  using P = decltype(ser20::store::push);
  ASSERT_TRUE(pinvoke_with<P>(1, ctx).has_value());
  ASSERT_TRUE(pinvoke_with<P>(2, ctx).has_value());
  ASSERT_EQ(ctx.storage.size(), 2u);
  EXPECT_EQ(ctx.storage[0], 1);
  EXPECT_EQ(ctx.storage[1], 2);
}

TEST(StoreEmplace, AppendsToVector) {
  ElemCtx<std::vector<int>> ctx;
  using P = decltype(ser20::store::emplace);
  ASSERT_TRUE(pinvoke_with<P>(10, ctx).has_value());
  ASSERT_TRUE(pinvoke_with<P>(20, ctx).has_value());
  ASSERT_EQ(ctx.storage.size(), 2u);
  EXPECT_EQ(ctx.storage[0], 10);
  EXPECT_EQ(ctx.storage[1], 20);
}

TEST(StoreIgnore, DiscardsValue) {
  using P = decltype(ser20::decode::number<int> | ser20::store::ignore);
  auto r = pinvoke<P>("123"sv);
  EXPECT_TRUE(r.has_value());
}

// ── Pipeline: decode → store full chain ──────────────────────────────────────

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
  EXPECT_EQ(ctx.storage, 0);  // unchanged
}

// ── Mutable vs const context dispatch ────────────────────────────────────────
//
// A step with operator()(int, Ctx&) is matched by the mutable-context invoke
// overload (case 1–3).  When invoked with a const context the requirement
// `Fn(move(i), const_c)` fails for a non-const-ref parameter, so dispatch
// falls through to the no-context form (case 4–6).
//
// We observe which branch was taken via the return value:
//   mutable path → returns input unchanged
//   no-ctx  path → returns input + 1000 (sentinel)

struct DispatchTrackCtx {
  int mutable_path_calls = 0;
};

struct DualDispatchStep {
  // Mutable-context form — preferred by mutable invoke; increments counter.
  auto operator()(int input, DispatchTrackCtx& ctx) const -> int {
    ctx.mutable_path_calls++;
    return input;
  }
  // Const-context form — selected by const invoke; returns sentinel value.
  auto operator()(int input, const DispatchTrackCtx& /*ctx*/) const -> int {
    return input + 1000;
  }
};

TEST(PipelineDispatch, MutableContextUsesContextAwareOverload) {
  using P = ser20::Pipeline<DualDispatchStep{}>;
  DispatchTrackCtx ctx;
  auto r = P::invoke(42, ctx);
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(*r, 42);  // mutable path: no sentinel
  EXPECT_EQ(ctx.mutable_path_calls, 1);
}

TEST(PipelineDispatch, ConstContextFallsToNoContextOverload) {
  using P = ser20::Pipeline<DualDispatchStep{}>;
  const DispatchTrackCtx ctx{};
  auto r = P::invoke(42, ctx);
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(*r, 1042);  // no-ctx path: input + 1000 sentinel
  // ctx.mutable_path_calls is still 0 — never touched
}

TEST(PipelineDispatch, MutableContextChainAllStepsSeeMutableCtx) {
  // Two DualDispatchSteps chained: both should take the mutable path.
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
  // First step: 3 + 1000 = 1003; second step: 1003 + 1000 = 2003
  auto r = P::invoke(3, ctx);
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(*r, 2003);
}

// ── Early return: explicit invocation counting ────────────────────────────────
//
// Use a mutable context to count how many times each step is called.
// Steps after an error must not be invoked.

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
  EXPECT_EQ(ctx.b_calls, 0);  // B must not have been called
}

TEST(PipelineEarlyReturn, StepsBeforeErrorAreInvoked) {
  using P = ser20::Pipeline<CountingStepA{}, ErrorStep{}, CountingStepB{}>;
  EarlyReturnCtx ctx;
  auto r = P::invoke(42, ctx);
  ASSERT_FALSE(r.has_value());
  EXPECT_EQ(ctx.a_calls, 1);  // A was called before the error
  EXPECT_EQ(ctx.b_calls, 0);  // B was not called after the error
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
  // Step A errors with validation_failed.
  // Step B would error with invalid_choice if reached.
  // The final error must be validation_failed (A's), not B's.
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
