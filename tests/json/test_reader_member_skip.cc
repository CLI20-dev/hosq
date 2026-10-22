#include <gtest/gtest.h>

#include <hosq/hosq.hh>
#include <hosq/json/fast_reader.hh>
#include <string>

namespace member_skip_test {
using Reader = hosq::json::FastReader<17, hosq::json::InputPadding::none,
                                      hosq::json::WhitespaceMode::Compact>;
inline int value_calls = 0;
}  // namespace member_skip_test
namespace hosq::ext {
template <>
struct DecodeSkipValue<member_skip_test::Reader> {
  template <class T, TryPhase P>
  void operator()(const ScalarView&,
                  DecodeContext<T, member_skip_test::Reader, P>&) const {}
  template <class T, TryPhase P>
  void operator()(DecodeContext<T, member_skip_test::Reader, P>& ctx) const {
    ++member_skip_test::value_calls;
    if constexpr (P == TryPhase::normal) {
      if (!ctx.reader().next_token(ctx.reader_state())) return;
    }
    (void)ctx.reader().resolve_skip(ctx.reader_state());
  }
};
}  // namespace hosq::ext

namespace {
using namespace hosq;
using Number =
    Elem<scan::scalar | decode::number<int> | store::assign, noop_encoder>;
using SkipKey = Elem<scan::scalar | decode::skip, noop_encoder>;
using SkipValue = Elem<decode::skip, noop_encoder>;
struct Fields {
  Try<Entry<Key<"id">, Number>> id;
  Try<Entry<SkipKey, SkipValue>> skip;
};
struct ObserveKey {
  static inline int calls = 0;
  template <class C>
  bool operator()(const ScalarView&, const C&) const {
    ++calls;
    return true;
  }
};
struct ObservedFields {
  Try<Entry<Key<"id">, Number>> id;
  Try<Entry<Elem<scan::scalar | step<ObserveKey>, noop_encoder>, SkipValue>>
      skip;
};

template <json::InputPadding P, json::WhitespaceMode M>
void examples() {
  using R = json::FastReader<128, P, M>;
  for (std::string input :
       {R"({"id":1,"a":0,"b":true,"c":"escaped\"text","id":7,"z":null})",
        R"({"a":1,"b":null,"id":7})",
        R"({"a":[1,{"b":2}],"x":false,"id":7,"tail":{}})",
        R"({"a":trueX,"b":1.2.3e+-,"id":7})",
        "{\n    \"a\" : 1,\t\"b\":\"x\\\\y\",\r\n    \"id\":7\n}"}) {
    const auto size = input.size();
    input.append(8, '\0');
    Fields f;
    EXPECT_TRUE(decode_into<R>(f, {input.data(), size}).is_ok());
    EXPECT_EQ(*f.id, 7);
  }
}
}  // namespace

TEST(ReaderMemberSkip, ScalarsContainersDuplicatesAndWhitespace) {
  examples<json::InputPadding::none, json::WhitespaceMode::Auto>();
  examples<json::InputPadding::none, json::WhitespaceMode::Pretty>();
  examples<json::InputPadding::none, json::WhitespaceMode::Compact>();
  examples<json::InputPadding::zero_padded_4, json::WhitespaceMode::Auto>();
  examples<json::InputPadding::zero_padded_4, json::WhitespaceMode::Pretty>();
  examples<json::InputPadding::zero_padded_4, json::WhitespaceMode::Compact>();
  examples<json::InputPadding::zero_padded_8, json::WhitespaceMode::Auto>();
  examples<json::InputPadding::zero_padded_8, json::WhitespaceMode::Pretty>();
  examples<json::InputPadding::zero_padded_8, json::WhitespaceMode::Compact>();
}

TEST(ReaderMemberSkip, CustomFallbackStillObservesEveryUnknownMember) {
  ObservedFields f;
  ObserveKey::calls = 0;
  EXPECT_TRUE(decode_into<json::FastReader<>>(
                  f, R"({"a":1,"b":true,"id":7,"c":null,"d":"text"})")
                  .is_ok());
  EXPECT_EQ(ObserveKey::calls, 4);
  EXPECT_EQ(*f.id, 7);
}

TEST(ReaderMemberSkip, KnownKeyHandlerIsReachedWithoutIntermediateScalarTokens) {
  using R = json::FastReader<>;
  R reader;
  R::state_type state;
  reader.reset(state, R"({"a":1,"b":true,"id":7})");
  ASSERT_TRUE(reader.next_token(state));
  ASSERT_TRUE(next_token<Expect::next_member_or_end>(reader, state));
  int calls = 0;
  auto handlers = std::tuple{meta::make_handler<"id">([&] {
    ++calls;
    EXPECT_EQ(state.current.length, 2u);
    EXPECT_EQ(state.pos - state.begin, 20);
  })};
  auto scan = [&]() -> std::string_view {
    ADD_FAILURE() << "standard skipped keys should not require scan callback";
    return {};
  };
  EXPECT_TRUE(reader.match_const_key(state, handlers, scan, std::true_type{}));
  EXPECT_EQ(calls, 1);
}

TEST(ReaderMemberSkip, CustomSkipAdapterIsNotBypassed) {
  Fields f;
  member_skip_test::value_calls = 0;
  EXPECT_TRUE(decode_into<member_skip_test::Reader>(
                  f, R"({"a":1,"b":true,"id":7,"c":null,"d":"text"})")
                  .is_ok());
  EXPECT_EQ(member_skip_test::value_calls, 4);
  EXPECT_EQ(*f.id, 7);
}
