#include <gtest/gtest.h>

#include <string_view>
#include <type_traits>
#include <vector>

#include "hosq/context.hh"
#include "hosq/decoder.hh"
#include "hosq/entry.hh"
#include "hosq/error.hh"
#include "hosq/json/simple_reader.hh"
#include "hosq/map.hh"
#include "hosq/pipeline/decode.hh"
#include "hosq/pipeline/match.hh"
#include "hosq/pipeline/scan.hh"
#include "hosq/pipeline/store.hh"
#include "hosq/pipeline_util.hh"
#include "hosq/string_literal.hh"
#include "hosq/try.hh"

using namespace std::string_view_literals;

namespace {

using Reader = hosq::json::SimpleReader;

using IntArrayValue = hosq::Elem<
    hosq::scan::array |
        hosq::decode::collect<hosq::scan::literal | hosq::decode::number<int>> |
        hosq::store::assign,
    hosq::noop_encoder>;

using IntArrayEachValue =
    hosq::Elem<hosq::scan::array | hosq::decode::each<hosq::scan::literal |
                                                      hosq::decode::number<int> |
                                                      hosq::store::push>,
               hosq::noop_encoder>;

using NumberObjectValue = hosq::Elem<
    hosq::scan::object |
        hosq::decode::collect<hosq::scan::string,
                              hosq::scan::literal | hosq::decode::number<int>> |
        hosq::store::assign,
    hosq::noop_encoder>;

struct EvenPushImpl {
  template <class Input>
  using storage_type = std::vector<std::remove_cvref_t<Input>>;

  template <class Input, class Context>
  auto operator()(Input value, Context& ctx) const -> bool {
    const auto is_even = value % 2 == 0;
    ctx.elem().push_back(std::move(value));
    return is_even;
  }
};

inline constexpr auto even_push = hosq::step<EvenPushImpl>;

struct KeyPushImpl {
  template <class Input>
  using storage_type = std::vector<std::remove_cvref_t<Input>>;

  template <class Input, class Context>
  auto operator()(Input value, Context& ctx) const -> bool {
    ctx.elem().push_back(std::move(value));
    return true;
  }
};

inline constexpr auto key_push = hosq::step<KeyPushImpl>;

template <auto Pipeline, class Storage>
auto invoke_pipeline(Storage& storage, std::string_view input) {
  auto reader = Reader{};
  auto state = Reader::state_type{};
  reader.reset(state, input);
  (void)reader.next_token(state);
  auto ctx = hosq::DecodeContext<Storage, Reader, hosq::TryPhase::normal>{
      storage, reader, state};
  return std::remove_cvref_t<decltype(Pipeline)>::invoke(ctx);
}

template <auto Pipeline>
auto invoke_pipeline(std::string_view input) {
  auto reader = Reader{};
  auto state = Reader::state_type{};
  reader.reset(state, input);
  (void)reader.next_token(state);
  auto scratch = int{};
  auto storage_ctx = hosq::DecodeContext<int, Reader, hosq::TryPhase::normal>{
      scratch, reader, state};
  auto ctx =
      hosq::DecodeContext<void, Reader, hosq::TryPhase::normal>{storage_ctx};
  return std::remove_cvref_t<decltype(Pipeline)>::invoke(ctx);
}

template <hosq::StringLiteral Name>
using Key = hosq::Elem<hosq::scan::string | hosq::match::literal<Name>,
                       hosq::noop_encoder>;

struct Document {
  hosq::Try<hosq::Entry<Key<"ids">, IntArrayValue>> ids;
};

}  // namespace

TEST(DecodeArray, CollectsTopLevelArrayValues) {
  auto ids = IntArrayValue{};

  const auto result = hosq::decode_into<Reader>(ids, "[1,2,3]"sv);

  ASSERT_TRUE(result.is_ok());
  ASSERT_EQ(ids.value().size(), 3U);
  EXPECT_EQ(ids.value()[0], 1);
  EXPECT_EQ(ids.value()[1], 2);
  EXPECT_EQ(ids.value()[2], 3);
}

TEST(DecodeArray, EachStoresTopLevelArrayValues) {
  auto ids = IntArrayEachValue{};

  const auto result = hosq::decode_into<Reader>(ids, "[4,5,6]"sv);

  ASSERT_TRUE(result.is_ok());
  ASSERT_EQ(ids.value().size(), 3U);
  EXPECT_EQ(ids.value()[0], 4);
  EXPECT_EQ(ids.value()[1], 5);
  EXPECT_EQ(ids.value()[2], 6);
}

TEST(DecodeArray, DecodesObjectMemberArrayValues) {
  auto document = Document{};

  const auto result = hosq::decode_into<Reader>(document, R"({"ids":[4,5]})"sv);

  ASSERT_TRUE(result.is_ok());
  ASSERT_EQ(document.ids.value().value().size(), 2U);
  EXPECT_EQ(document.ids.value().value()[0], 4);
  EXPECT_EQ(document.ids.value().value()[1], 5);
}

TEST(DecodeArray, DecodesObjectMemberArrayValuesWithReorderedKeys) {
  struct WithArrayAndScalar {
    hosq::Try<hosq::Entry<Key<"ids">, IntArrayValue>> ids;
    hosq::Try<
        hosq::Entry<Key<"version">,
                    hosq::Elem<hosq::scan::literal | hosq::decode::number<int> |
                                   hosq::store::assign,
                               hosq::noop_encoder>>>
        version;
  };

  auto document = WithArrayAndScalar{};

  const auto result =
      hosq::decode_into<Reader>(document, R"({"version":2,"ids":[7,8]})"sv);

  ASSERT_TRUE(result.is_ok());
  EXPECT_EQ(document.version.value().value(), 2);
  ASSERT_EQ(document.ids.value().value().size(), 2U);
  EXPECT_EQ(document.ids.value().value()[0], 7);
  EXPECT_EQ(document.ids.value().value()[1], 8);
}

TEST(DecodeArray, RequiresArrayBegin) {
  auto ids = IntArrayValue{};

  const auto result = hosq::decode_into<Reader>(ids, "1"sv);

  ASSERT_FALSE(result.is_ok());
  EXPECT_EQ(result.code, hosq::ErrorCode::type_mismatch);
}

TEST(DecodeArray, RejectsInvalidElement) {
  auto ids = IntArrayValue{};

  const auto result = hosq::decode_into<Reader>(ids, R"([1,"x"])"sv);

  ASSERT_FALSE(result.is_ok());
  EXPECT_EQ(result.code, hosq::ErrorCode::type_mismatch);
}

TEST(DecodeArray, CollectsEmptyArrayValues) {
  auto ids = IntArrayValue{};

  const auto result = hosq::decode_into<Reader>(ids, "[]"sv);

  ASSERT_TRUE(result.is_ok());
  EXPECT_TRUE(ids.value().empty());
}

TEST(DecodeArray, EachAggregatesBoolAcrossElements) {
  constexpr auto pipeline =
      hosq::scan::array |
      hosq::decode::each<hosq::scan::literal | hosq::decode::number<int> |
                         even_push>;

  auto values = std::vector<int>{};
  const auto result = invoke_pipeline<pipeline>(values, "[2,4,5,8]"sv);

  EXPECT_FALSE(result);
  EXPECT_EQ(values, (std::vector<int>{2, 4, 5}));
}

TEST(DecodeArray, EachBoolReturnsTrueForEmptyArray) {
  constexpr auto pipeline =
      hosq::scan::array |
      hosq::decode::each<hosq::scan::literal | hosq::decode::number<int> |
                         even_push>;

  auto values = std::vector<int>{};
  const auto result = invoke_pipeline<pipeline>(values, "[]"sv);

  EXPECT_TRUE(result);
  EXPECT_TRUE(values.empty());
}

TEST(DecodeArray, CollectCanReturnBoolValues) {
  constexpr auto pipeline =
      hosq::scan::array |
      hosq::decode::collect<hosq::scan::literal | hosq::decode::number<int> |
                            hosq::decode::custom<[](int value) constexpr {
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

  const auto result =
      hosq::decode_into<Reader>(object, R"({"x":1,"y":2,"z":3})"sv);

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

  const auto result = hosq::decode_into<Reader>(object, "{}"sv);

  ASSERT_TRUE(result.is_ok());
  EXPECT_TRUE(object.value().keys().empty());
  EXPECT_TRUE(object.value().values().empty());
}

TEST(DecodeObjectEach, AggregatesBoolAcrossMembers) {
  constexpr auto pipeline =
      hosq::scan::object |
      hosq::decode::each<hosq::scan::string | key_push,
                         hosq::scan::literal | hosq::decode::number<int> |
                             hosq::decode::custom<[](int value) constexpr {
                               return value > 0;
                             }>>;

  auto object_state =
      hosq::Map<std::vector<std::string_view>, hosq::detail::NoStorage>{};
  const auto result =
      invoke_pipeline<pipeline>(object_state, R"({"x":1,"y":-2,"z":3})"sv);

  EXPECT_FALSE(result);
  EXPECT_EQ(object_state.keys(), (std::vector<std::string_view>{"x"sv, "y"sv}));
}
