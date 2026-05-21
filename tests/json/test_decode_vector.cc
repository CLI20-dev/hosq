#include <gtest/gtest.h>

#include <string_view>

#include "hosq/decoder.hh"
#include "hosq/entry.hh"
#include "hosq/json/simple_reader.hh"
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

using IntArrayValue =
    hosq::Elem<hosq::scan::array |
                   hosq::decode::collect<hosq::scan::literal |
                                         hosq::decode::number<int>> |
                   hosq::store::assign,
               hosq::noop_encoder>;

using IntArrayEachValue =
    hosq::Elem<hosq::scan::array |
                   hosq::decode::each<hosq::scan::literal |
                                      hosq::decode::number<int> |
                                      hosq::store::push>,
               hosq::noop_encoder>;

using NumberObjectValue =
    hosq::Elem<hosq::scan::object |
                   hosq::decode::collect<hosq::scan::string,
                                         hosq::scan::literal |
                                             hosq::decode::number<int>> |
                   hosq::store::assign,
               hosq::noop_encoder>;

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

  ASSERT_TRUE(result.has_value());
  ASSERT_EQ(ids.value().size(), 3U);
  EXPECT_EQ(ids.value()[0], 1);
  EXPECT_EQ(ids.value()[1], 2);
  EXPECT_EQ(ids.value()[2], 3);
}

TEST(DecodeArray, EachStoresTopLevelArrayValues) {
  auto ids = IntArrayEachValue{};

  const auto result = hosq::decode_into<Reader>(ids, "[4,5,6]"sv);

  ASSERT_TRUE(result.has_value());
  ASSERT_EQ(ids.value().size(), 3U);
  EXPECT_EQ(ids.value()[0], 4);
  EXPECT_EQ(ids.value()[1], 5);
  EXPECT_EQ(ids.value()[2], 6);
}

TEST(DecodeArray, DecodesObjectMemberArrayValues) {
  auto document = Document{};

  const auto result =
      hosq::decode_into<Reader>(document, R"({"ids":[4,5]})"sv);

  ASSERT_TRUE(result.has_value());
  ASSERT_EQ(document.ids.value().value().size(), 2U);
  EXPECT_EQ(document.ids.value().value()[0], 4);
  EXPECT_EQ(document.ids.value().value()[1], 5);
}

TEST(DecodeArray, DecodesObjectMemberArrayValuesWithReorderedKeys) {
  struct WithArrayAndScalar {
    hosq::Try<hosq::Entry<Key<"ids">, IntArrayValue>> ids;
    hosq::Try<hosq::Entry<Key<"version">,
                          hosq::Elem<hosq::scan::literal |
                                         hosq::decode::number<int> |
                                         hosq::store::assign,
                                     hosq::noop_encoder>>>
        version;
  };

  auto document = WithArrayAndScalar{};

  const auto result =
      hosq::decode_into<Reader>(document, R"({"version":2,"ids":[7,8]})"sv);

  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(document.version.value().value(), 2);
  ASSERT_EQ(document.ids.value().value().size(), 2U);
  EXPECT_EQ(document.ids.value().value()[0], 7);
  EXPECT_EQ(document.ids.value().value()[1], 8);
}

TEST(DecodeArray, RequiresArrayBegin) {
  auto ids = IntArrayValue{};

  const auto result = hosq::decode_into<Reader>(ids, "1"sv);

  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code, hosq::ErrorCode::type_mismatch);
}

TEST(DecodeArray, RejectsInvalidElement) {
  auto ids = IntArrayValue{};

  const auto result = hosq::decode_into<Reader>(ids, R"([1,"x"])"sv);

  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code, hosq::ErrorCode::type_mismatch);
}

TEST(DecodeObjectCollect, CollectsKeysAndValues) {
  auto object = NumberObjectValue{};

  const auto result =
      hosq::decode_into<Reader>(object, R"({"x":1,"y":2,"z":3})"sv);

  ASSERT_TRUE(result.has_value());
  ASSERT_EQ(object.value().keys.size(), 3U);
  ASSERT_EQ(object.value().values.size(), 3U);
  EXPECT_EQ(object.value().keys[0], "x"sv);
  EXPECT_EQ(object.value().keys[1], "y"sv);
  EXPECT_EQ(object.value().keys[2], "z"sv);
  EXPECT_EQ(object.value().values[0], 1);
  EXPECT_EQ(object.value().values[1], 2);
  EXPECT_EQ(object.value().values[2], 3);
}
