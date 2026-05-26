#include <gtest/gtest.h>

#include <hosq/hosq.hh>
#include <string_view>
#include <type_traits>
#include <vector>

#include "hosq/json/simple_reader.hh"

using namespace std::string_view_literals;
using namespace hosq;

namespace {

using Reader = json::SimpleReader;

using IntArrayValue =
    Elem<scan::array | decode::collect<scan::scalar | decode::number<int>> |
             store::assign,
         noop_encoder>;

using IntArrayEachValue =
    Elem<scan::array | decode::each<scan::scalar | decode::number_into<int>>,
         noop_encoder>;

using NumberObjectValue =
    Elem<scan::object |
             decode::collect<scan::scalar | decode::string_view,
                             scan::scalar | decode::number<int>> |
             store::assign,
         noop_encoder>;

struct EvenPushImpl {
  template <class Input>
  using storage_type = std::remove_cvref_t<Input>;

  template <class Input, class Context>
  auto operator()(Input value, Context& ctx) const -> bool {
    const auto is_even = value % 2 == 0;
    ctx.value() = std::move(value);
    return is_even;
  }
};

inline constexpr auto even_push = step<EvenPushImpl>;

template <auto Pipeline, class Storage>
auto invoke_pipeline(Storage& storage, std::string_view input) {
  auto reader = Reader{};
  auto state = Reader::state_type{};
  reader.reset(state, input);
  (void)reader.next_token(state);
  auto ctx =
      DecodeContext<Storage, Reader, TryPhase::normal>{storage, reader, state};
  return std::remove_cvref_t<decltype(Pipeline)>::invoke(ctx);
}

template <auto Pipeline>
auto invoke_pipeline(std::string_view input) {
  auto reader = Reader{};
  auto state = Reader::state_type{};
  reader.reset(state, input);
  (void)reader.next_token(state);
  auto scratch = int{};
  auto storage_ctx =
      DecodeContext<int, Reader, TryPhase::normal>{scratch, reader, state};
  auto ctx = DecodeContext<void, Reader, TryPhase::normal>{storage_ctx};
  return std::remove_cvref_t<decltype(Pipeline)>::invoke(ctx);
}

template <StringLiteral Name>
using FieldKey = Elem<scan::scalar | decode::string_view | match::literal<Name>,
                      noop_encoder>;

struct Document {
  Try<Entry<FieldKey<"ids">, IntArrayValue>> ids;
};

}  // namespace

TEST(DecodeArray, CollectsTopLevelArrayValues) {
  auto ids = IntArrayValue{};

  const auto result = decode_into<Reader>(ids, "[1,2,3]"sv);

  ASSERT_TRUE(result.is_ok());
  ASSERT_EQ(ids.value().size(), 3U);
  EXPECT_EQ(ids.value()[0], 1);
  EXPECT_EQ(ids.value()[1], 2);
  EXPECT_EQ(ids.value()[2], 3);
}

TEST(DecodeArray, EachStoresTopLevelArrayValues) {
  auto ids = IntArrayEachValue{};

  const auto result = decode_into<Reader>(ids, "[4,5,6]"sv);

  ASSERT_TRUE(result.is_ok());
  ASSERT_EQ(ids.value().size(), 3U);
  EXPECT_EQ(ids.value()[0], 4);
  EXPECT_EQ(ids.value()[1], 5);
  EXPECT_EQ(ids.value()[2], 6);
}

TEST(DecodeArray, DecodesObjectMemberArrayValues) {
  auto document = Document{};

  const auto result = decode_into<Reader>(document, R"({"ids":[4,5]})"sv);

  ASSERT_TRUE(result.is_ok());
  ASSERT_EQ(document.ids.value().value().size(), 2U);
  EXPECT_EQ(document.ids.value().value()[0], 4);
  EXPECT_EQ(document.ids.value().value()[1], 5);
}

TEST(DecodeArray, DecodesObjectMemberArrayValuesWithReorderedKeys) {
  struct WithArrayAndScalar {
    Try<Entry<FieldKey<"ids">, IntArrayValue>> ids;
    Try<Entry<
        FieldKey<"version">,
        Elem<scan::scalar | decode::number<int> | store::assign, noop_encoder>>>
        version;
  };

  auto document = WithArrayAndScalar{};

  const auto result =
      decode_into<Reader>(document, R"({"version":2,"ids":[7,8]})"sv);

  ASSERT_TRUE(result.is_ok());
  EXPECT_EQ(document.version.value().value(), 2);
  ASSERT_EQ(document.ids.value().value().size(), 2U);
  EXPECT_EQ(document.ids.value().value()[0], 7);
  EXPECT_EQ(document.ids.value().value()[1], 8);
}

TEST(DecodeArray, RequiresArrayBegin) {
  auto ids = IntArrayValue{};

  const auto result = decode_into<Reader>(ids, "1"sv);

  ASSERT_FALSE(result.is_ok());
  EXPECT_EQ(result.code, ErrorCode::type_mismatch);
}

TEST(DecodeArray, RejectsInvalidElement) {
  auto ids = IntArrayValue{};

  const auto result = decode_into<Reader>(ids, R"([1,"x"])"sv);

  ASSERT_FALSE(result.is_ok());
  EXPECT_EQ(result.code, ErrorCode::invalid_number);
}

TEST(DecodeArray, CollectsEmptyArrayValues) {
  auto ids = IntArrayValue{};

  const auto result = decode_into<Reader>(ids, "[]"sv);

  ASSERT_TRUE(result.is_ok());
  EXPECT_TRUE(ids.value().empty());
}

TEST(DecodeArray, EachAggregatesBoolAcrossElements) {
  constexpr auto pipeline =
      scan::array | decode::each<scan::scalar | decode::number<int> | even_push>;

  auto values = std::vector<int>{};
  const auto result = invoke_pipeline<pipeline>(values, "[2,4,5,8]"sv);

  EXPECT_FALSE(result);
  EXPECT_TRUE(values.empty());
}

TEST(DecodeArray, EachBoolReturnsTrueForEmptyArray) {
  constexpr auto pipeline =
      scan::array | decode::each<scan::scalar | decode::number<int> | even_push>;

  auto values = std::vector<int>{};
  const auto result = invoke_pipeline<pipeline>(values, "[]"sv);

  EXPECT_TRUE(result);
  EXPECT_TRUE(values.empty());
}

TEST(DecodeArray, CollectCanReturnBoolValues) {
  constexpr auto pipeline =
      scan::array |
      decode::collect<scan::scalar | decode::number<int> |
                      decode::custom<[](int value) constexpr -> bool {
                        return value % 2 == 0;
                      }>>;

  const auto result = invoke_pipeline<pipeline>("[2,3,4]"sv);

  ASSERT_EQ(result.size(), 3U);
  EXPECT_TRUE(result[0]);
  EXPECT_FALSE(result[1]);
  EXPECT_TRUE(result[2]);
}

TEST(DecodeObjectCollect, CollectsKeysAndValues) {
  auto object = NumberObjectValue{};

  const auto result = decode_into<Reader>(object, R"({"x":1,"y":2,"z":3})"sv);

  ASSERT_TRUE(result.is_ok());
  ASSERT_EQ(object.value().keys().size(), 3U);
  ASSERT_EQ(object.value().values().size(), 3U);
  EXPECT_EQ(object.value().keys()[0], "x"sv);
  EXPECT_EQ(object.value().keys()[1], "y"sv);
  EXPECT_EQ(object.value().keys()[2], "z"sv);
  EXPECT_EQ(object.value().values()[0], 1);
  EXPECT_EQ(object.value().values()[1], 2);
  EXPECT_EQ(object.value().values()[2], 3);
}

TEST(DecodeObjectCollect, CollectsEmptyObject) {
  auto object = NumberObjectValue{};

  const auto result = decode_into<Reader>(object, "{}"sv);

  ASSERT_TRUE(result.is_ok());
  EXPECT_TRUE(object.value().keys().empty());
  EXPECT_TRUE(object.value().values().empty());
}

TEST(DecodeObjectEach, AggregatesBoolAcrossMembers) {
  constexpr auto pipeline =
      scan::object |
      decode::each<scan::scalar | decode::string_view | store::assign,
                   scan::scalar | decode::number<int> |
                       decode::custom<[](int value) constexpr -> bool {
                         return value > 0;
                       }>>;

  auto object_state =
      Map<std::vector<std::string_view>, std::vector<detail::NoStorage>>{};
  const auto result =
      invoke_pipeline<pipeline>(object_state, R"({"x":1,"y":-2,"z":3})"sv);

  EXPECT_FALSE(result);
  EXPECT_TRUE(object_state.keys().empty());
  EXPECT_TRUE(object_state.values().empty());
}
