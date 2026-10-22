#include <gtest/gtest.h>

#include <array>
#include <bit>
#include <cmath>
#include <string>
#include <string_view>
#include <type_traits>
#if defined(__unix__) || defined(__APPLE__)
#include <sys/mman.h>
#include <unistd.h>
#endif
#include "hosq/hosq.hh"
#include "hosq/json/fast_reader.hh"

namespace view_specialization_test {
using namespace hosq;
template <json::InputPadding P = json::InputPadding::none>
struct OrdinaryReader : json::FastReader<128, P> {};
struct CustomNumberReader : OrdinaryReader<> {};
}  // namespace view_specialization_test
namespace hosq::ext {
template <json::InputPadding P>
struct ScanScalar<view_specialization_test::OrdinaryReader<P>> {
  template <class S, TryPhase Phase>
  auto operator()(
      const DecodeContext<S, view_specialization_test::OrdinaryReader<P>, Phase>&
          ctx) const -> ScalarView {
    using Base = json::FastReader<128, P>;
    auto& r = const_cast<Base&>(static_cast<const Base&>(ctx.reader()));
    auto& s = const_cast<typename Base::state_type&>(ctx.reader_state());
    const DecodeContext<void, Base> base{r, s};
    return ScanScalar<Base>::resolved(base);
  }
};
template <>
struct ScanScalar<view_specialization_test::CustomNumberReader> {
  template <class S, TryPhase Phase>
  auto operator()(
      const DecodeContext<S, view_specialization_test::CustomNumberReader,
                          Phase>& ctx) const -> ScalarView {
    using Base = json::FastReader<>;
    auto& r = const_cast<Base&>(static_cast<const Base&>(ctx.reader()));
    auto& s = const_cast<typename Base::state_type&>(ctx.reader_state());
    const DecodeContext<void, Base> base{r, s};
    return ScanScalar<Base>::resolved(base);
  }
};
template <>
struct DecodeNumber<view_specialization_test::CustomNumberReader> {
  template <class T, class Ctx>
  T operator()(const ScalarView&, const Ctx&) const {
    return 77;
  }
};
}  // namespace hosq::ext
namespace {
using namespace hosq;
using namespace view_specialization_test;

template <class T, class U>
void same_value(const T& a, const U& b) {
  if constexpr (std::is_floating_point_v<T>) {
    if (std::isnan(a) && std::isnan(b)) return;
    EXPECT_EQ((std::bit_cast<std::array<std::byte, sizeof(T)>>(a)),
              (std::bit_cast<std::array<std::byte, sizeof(T)>>(b)));
  } else
    EXPECT_EQ(a, b);
}
template <class S>
void same_state(const S& a, const S& b) {
  EXPECT_EQ(a.pos, b.pos);
  EXPECT_EQ(a.begin, b.begin);
  EXPECT_EQ(a.end, b.end);
  EXPECT_EQ(a.current.offset, b.current.offset);
  EXPECT_EQ(a.current.length, b.current.length);
  EXPECT_EQ(a.current.type, b.current.type);
  EXPECT_EQ(a.current.context, b.current.context);
  EXPECT_EQ(a.ctx_size, b.ctx_size);
  EXPECT_EQ(a.ctx_stack, b.ctx_stack);
  EXPECT_EQ(a.expect, b.expect);
  EXPECT_EQ(a.error_flag, b.error_flag);
  EXPECT_EQ(a.last_error.code, b.last_error.code);
  EXPECT_EQ(a.last_error.offset, b.last_error.offset);
  EXPECT_EQ(a.last_error.detail, b.last_error.detail);
  EXPECT_EQ(a.error_token.type, b.error_token.type);
  EXPECT_EQ(a.error_token.offset, b.error_token.offset);
  EXPECT_EQ(a.error_token.length, b.error_token.length);
  EXPECT_EQ(a.error_token.context, b.error_token.context);
}
template <class V>
struct Record {
  Entry<Key<"id">, V> id;
};

template <auto Value, json::InputPadding P>
void compare_member(std::string_view text) {
  using V = Elem<Value | store::assign, noop_encoder>;
  using E = Record<V>;
  using Fast = json::FastReader<128, P>;
  Fast fast;
  OrdinaryReader<P> plain;
  typename Fast::state_type a, b;
  fast.reset(a, text);
  plain.reset(b, text);
  E x{}, y{};
  bool started = fast.next_token(a);
  ASSERT_EQ(started, plain.next_token(b));
  if (started) {
    auto ae = Decoder<Fast>::decode_nested(x, fast, a);
    auto be = Decoder<OrdinaryReader<P>>::decode_nested(y, plain, b);
    EXPECT_EQ(ae.code, be.code);
    EXPECT_EQ(ae.offset, be.offset);
    EXPECT_EQ(ae.detail, be.detail);
  }
  same_value(*x.id, *y.id);
  same_state(a, b);
}
template <auto Value, json::InputPadding P>
void prefixes(std::string_view value) {
  std::string text = "{\"id\":" + std::string(value) + ",\"extra\":0}";
  for (std::size_t n = 0; n <= text.size(); ++n) {
    SCOPED_TRACE(text.substr(0, n));
    std::string storage = text.substr(0, n);
    storage.resize(n + 8, '\0');
    compare_member<Value, P>({storage.data(), n});
  }
}
template <auto Value>
void all_padding(std::string_view value) {
  prefixes<Value, json::InputPadding::none>(value);
  prefixes<Value, json::InputPadding::zero_padded_4>(value);
  prefixes<Value, json::InputPadding::zero_padded_8>(value);
}
TEST(ReaderViewSpecialization, MemberValuesErrorsAndStateAtEveryPrefix) {
  for (auto v : {"0", "-0", "1.25", "1e309", "1e-999", "18446744073709551615",
                 "-9223372036854775809", "1xyz", "null", "true", "false",
                 "\"123\"", "\"1\\u0032\"", "\"a\\\"b\"", "\"\"", "[]", "{}"}) {
    all_padding<scan::scalar | decode::number<double>>(v);
    all_padding<scan::scalar | decode::number<std::int64_t>>(v);
    all_padding<scan::scalar | decode::number<std::uint64_t>>(v);
    all_padding<scan::scalar | decode::boolean>(v);
    all_padding<scan::scalar | decode::string_view>(v);
  }
}
struct Observer {
  template <class Input, class Ctx>
  int operator()(const Input&, Ctx& ctx) const {
    static_assert(std::same_as<Input, ScalarView>);
    EXPECT_EQ(ctx.reader_state().current.length, 3u);
    EXPECT_EQ(ctx.reader_state().pos - ctx.reader_state().begin, 3);
    return 19;
  }
};
TEST(ReaderViewSpecialization, GenericUserStepSeesOrdinaryTypeAndResolvedState) {
  using E = Elem<scan::scalar | step<Observer> | store::assign, noop_encoder>;
  E e;
  EXPECT_TRUE(decode_into<json::FastReader<>>(e, "123").is_ok());
  EXPECT_EQ(e.value(), 19);
}
struct OverloadedStore {
  template <class Input>
  using storage_type = Input;
  template <class Ctx>
  void operator()(std::string_view v, const Ctx& ctx) const {
    ctx.value() = v;
    ++const_calls;
  }
  template <class Ctx>
  void operator()(std::string_view v, Ctx& ctx) const {
    ctx.value() = v;
    ++mutable_calls;
  }
  static inline int const_calls = 0, mutable_calls = 0;
};
TEST(ReaderViewSpecialization, MemberStringPreservesConstOverloadSelection) {
  using V = Elem<scan::scalar | decode::string_view | step<OverloadedStore>,
                 noop_encoder>;
  Record<V> e;
  OverloadedStore::const_calls = OverloadedStore::mutable_calls = 0;
  EXPECT_TRUE(decode_into<json::FastReader<>>(e, R"({"id":"text"})").is_ok());
  EXPECT_EQ(*e.id, "text");
  EXPECT_EQ(OverloadedStore::const_calls, 1);
  EXPECT_EQ(OverloadedStore::mutable_calls, 0);
}
struct CustomNumber : decode::detail::NumberImpl<double> {
  template <class Ctx>
  double operator()(const ScalarView&, Ctx& ctx) const {
    EXPECT_NE(ctx.reader_state().current.length, unresolved_length);
    ++calls;
    return 17;
  }
  static inline int calls = 0;
};
TEST(ReaderViewSpecialization, DerivedUserStepIsNotMistakenForBuiltinNumber) {
  using V =
      Elem<scan::array | decode::collect<scan::scalar | step<CustomNumber>, 2> |
               store::assign,
           noop_encoder>;
  V v;
  CustomNumber::calls = 0;
  EXPECT_TRUE(decode_into<json::FastReader<>>(v, "[1,2]").is_ok());
  EXPECT_EQ(v.value(), (std::array<double, 2>{17, 17}));
  EXPECT_EQ(CustomNumber::calls, 2);
  using M =
      Elem<scan::scalar | step<CustomNumber> | store::assign, noop_encoder>;
  Record<M> member;
  EXPECT_TRUE(decode_into<json::FastReader<>>(member, "{\"id\":123}").is_ok());
  EXPECT_EQ(*member.id, 17);
  EXPECT_EQ(CustomNumber::calls, 3);
}
TEST(ReaderViewSpecialization, CustomReaderNumberExtensionStillRuns) {
  using V =
      Elem<scan::scalar | decode::number<int> | store::assign, noop_encoder>;
  V v;
  EXPECT_TRUE(decode_into<CustomNumberReader>(v, "12").is_ok());
  EXPECT_EQ(v.value(), 77);
  Record<V> member;
  EXPECT_TRUE(decode_into<CustomNumberReader>(member, "{\"id\":12}").is_ok());
  EXPECT_EQ(*member.id, 77);
}
}  // namespace

#if defined(__unix__) || defined(__APPLE__)
namespace {
template <json::InputPadding P>
void guarded_members() {
  const auto page = static_cast<std::size_t>(sysconf(_SC_PAGESIZE));
  auto* memory = static_cast<char*>(mmap(
      nullptr, page * 2, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0));
  ASSERT_NE(memory, MAP_FAILED);
  ASSERT_EQ(mprotect(memory + page, page, PROT_NONE), 0);
  const std::size_t padding = static_cast<std::size_t>(P);
  for (std::string text :
       {R"({"id":true})", R"({"id":false})", R"({"id":trueX})",
        R"({"id":falseX})", R"({"id":"escaped\"text"})",
        R"({"id":18446744073709551615})"}) {
    for (std::size_t n = 0; n <= text.size(); ++n) {
      SCOPED_TRACE(text.substr(0, n));
      auto* begin = memory + page - padding - n;
      std::memcpy(begin, text.data(), n);
      std::memset(begin + n, 0, padding);
      std::string_view input(begin, n);
      compare_member<scan::scalar | decode::boolean, P>(input);
      compare_member<scan::scalar | decode::string_view, P>(input);
      compare_member<scan::scalar | decode::number<double>, P>(input);
    }
  }
  EXPECT_EQ(munmap(memory, page * 2), 0);
}
TEST(ReaderViewSpecialization, NativeViewsRespectExactGuardedInputBounds) {
  guarded_members<json::InputPadding::none>();
  guarded_members<json::InputPadding::zero_padded_4>();
  guarded_members<json::InputPadding::zero_padded_8>();
}
}  // namespace
#endif

namespace {
TEST(ReaderViewSpecialization, TerminalScanKeepsOrdinaryViewAndResolvedState) {
  using R = json::FastReader<>;
  R reader;
  R::state_type state;
  reader.reset(state, "123");
  ASSERT_TRUE(reader.next_token(state));
  DecodeContext<void, R> ctx{reader, state};
  auto view = scan::scalar.invoke(ctx);
  static_assert(std::same_as<decltype(view), ScalarView>);
  EXPECT_EQ(state.current.length, 3u);
  EXPECT_EQ(state.pos - state.begin, 3);
  auto direct = scan::detail::ScanScalar{}(ctx);
  static_assert(!std::same_as<decltype(direct), ScalarView>);
  (void)direct;
}
}  // namespace

namespace {
TEST(ReaderViewSpecialization, CallabilityStillRejectsWrongInputTypes) {
  using Ctx = DecodeContext<int, json::FastReader<>>;
  using Number = std::remove_cvref_t<decltype(decode::number<int>)>;
  static_assert(
      Number::template callable_with_mutable_context<Ctx, ScalarView>());
  static_assert(
      !Number::template callable_with_mutable_context<Ctx, std::string_view>());
  static_assert(
      !Number::template callable_with_mutable_context<Ctx, ArrayView>());
  static_assert(
      !Number::template callable_with_const_context<Ctx, ScalarView>());
}
}  // namespace

static_assert(
    std::is_empty_v<hosq::json::detail::ScalarInput<hosq::json::FastReader<>>>);
static_assert(
    std::is_empty_v<hosq::json::detail::ArrayInput<hosq::json::FastReader<>>>);
static_assert(std::is_trivially_copyable_v<
              hosq::json::detail::ScalarInput<hosq::json::FastReader<>>>);
static_assert(std::is_trivially_copyable_v<
              hosq::json::detail::ArrayInput<hosq::json::FastReader<>>>);

static_assert(std::is_empty_v<
              hosq::json::detail::MemberInput<hosq::json::FastReader<>, "id">>);
static_assert(
    std::is_empty_v<
        hosq::json::detail::MemberInput<hosq::json::FastReader<>, "id", true>>);
static_assert(
    std::is_trivially_copyable_v<
        hosq::json::detail::MemberInput<hosq::json::FastReader<>, "id", true>>);
static_assert(!std::is_default_constructible_v<
              hosq::json::detail::MemberInput<hosq::json::FastReader<>, "id">>);
static_assert(
    !std::is_default_constructible_v<
        hosq::json::detail::MemberInput<hosq::json::FastReader<>, "id", true>>);

namespace {
TEST(ReaderViewSpecialization, MemberPrefixFallbacksPreserveState) {
  for (std::string_view text :
       {"{\"other\":1}", "{\"i\\u0064\":1}", "{ \"id\" : 1 }", "{\"id\" 1}",
        "{\"id\":}", "{\"id\":xyz}", "{\"id\":\"true\"}", "{\"id\":falsex}",
        "{\"id\":1e9999}", "{\"id\":\"unterminated}"}) {
    compare_member<scan::scalar | decode::number<double>,
                   json::InputPadding::none>(text);
    compare_member<scan::scalar | decode::boolean, json::InputPadding::none>(
        text);
    compare_member<scan::scalar | decode::string_view, json::InputPadding::none>(
        text);
  }
}
struct CountMemberTail {
  inline static int calls = 0;
  template <class Input>
  using storage_type = Input;
  template <class T, class Ctx>
  void operator()(T value, Ctx& ctx) const {
    ++calls;
    ctx.value() = value;
  }
};
TEST(ReaderViewSpecialization, FailedKeyNeverExecutesValueTail) {
  using V = Elem<scan::scalar | decode::number<int> | step<CountMemberTail>,
                 noop_encoder>;
  Record<V> r;
  CountMemberTail::calls = 0;
  EXPECT_FALSE(decode_into<json::FastReader<>>(r, "{\"other\":1}").is_ok());
  EXPECT_EQ(CountMemberTail::calls, 0);
  EXPECT_TRUE(decode_into<json::FastReader<>>(r, "{\"id\":1}").is_ok());
  EXPECT_EQ(CountMemberTail::calls, 1);
}
}  // namespace

namespace {
TEST(ReaderViewSpecialization, MemberNumberIntoPreservesOutputOnFailure) {
  using V = Elem<scan::scalar | decode::number_into<double>, noop_encoder>;
  using Fast = json::FastReader<>;
  for (std::string_view input :
       {"{\"id\":1.25}", "{\"id\":1e9999}", "{\"id\":null}", "{\"id\":\"12\"}",
        "{\"id\":\"1xyz\"}", "{\"id\":[]}", "{\"other\":1}"}) {
    Fast fast;
    OrdinaryReader<> plain;
    Fast::state_type a, b;
    Record<V> x, y;
    *x.id = *y.id = 77;
    fast.reset(a, input);
    plain.reset(b, input);
    ASSERT_TRUE(fast.next_token(a));
    ASSERT_TRUE(plain.next_token(b));
    auto ae = Decoder<Fast>::decode_nested(x, fast, a);
    auto be = Decoder<OrdinaryReader<>>::decode_nested(y, plain, b);
    EXPECT_EQ(ae.code, be.code);
    same_value(*x.id, *y.id);
    same_state(a, b);
  }
}
}  // namespace
