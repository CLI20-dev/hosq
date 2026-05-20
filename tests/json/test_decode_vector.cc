// #include <gtest/gtest.h>
//
// #include <array>
// #include <string_view>
// #include <vector>
//
// #include "ser20/decoder.hh"
// #include "ser20/element.hh"
// #include "ser20/json/simple_reader.hh"
// #include "ser20/pipeline/decode.hh"
// #include "ser20/pipeline/match.hh"
// #include "ser20/pipeline/scan.hh"
// #include "ser20/pipeline/store.hh"
// #include "ser20/string_literal.hh"
//
// using namespace std::string_view_literals;
// using namespace ser20;
//
// using Reader = ser20::json::SimpleReader;
//
// namespace {
//
// template <ser20::StringLiteral Name>
// using Key = ser20::Elem<ser20::scan::string | ser20::match::literal<Name>,
//                         ser20::noop_encoder>;
//
// template <class T>
// using NumberValue = ser20::Elem<ser20::scan::literal | ser20::decode::number<T> |
//                                     ser20::store::once,
//                                 ser20::noop_encoder>;
//
// using StringValue =
//     ser20::Elem<ser20::scan::string | ser20::decode::string_view |
//                     ser20::store::once,
//                 ser20::noop_encoder>;
//
// using IntElement =
//     ser20::Elem<ser20::scan::literal | ser20::decode::number<int> |
//                     ser20::store::once,
//                 ser20::noop_encoder>;
//
// struct Point {
//   ser20::Entry<Key<"x"_literal>, NumberValue<int>> x;
//   ser20::Entry<Key<"y"_literal>, NumberValue<int>> y;
// };
//
// struct Document {
//   ser20::Entry<
//       Key<"ids"_literal>,
//       ser20::Elem<ser20::decode::vector<IntElement> | ser20::store::once,
//                   ser20::noop_encoder>>
//       ids;
//   ser20::Entry<Key<"points"_literal>,
//                ser20::Elem<ser20::decode::vector<Point> | ser20::store::once,
//                            ser20::noop_encoder>>
//       points;
// };
//
// }  // namespace
//
// TEST(DecodeVector, DecodesTopLevelArrayOfElemValues) {
//   ser20::Elem<ser20::decode::vector<IntElement> | ser20::store::once,
//               ser20::noop_encoder>
//       ids;
//
//   auto result = ser20::decode_into<Reader>(ids, "[1,2,3]"sv);
//
//   ASSERT_TRUE(result.has_value());
//   ASSERT_EQ(ids.value().size(), 3U);
//   EXPECT_EQ(ids.value()[0], 1);
//   EXPECT_EQ(ids.value()[1], 2);
//   EXPECT_EQ(ids.value()[2], 3);
// }
//
// TEST(DecodeVector, DecodesObjectMemberArrayOfElemValues) {
//   Document document;
//
//   auto result =
//       ser20::decode_into<Reader>(document, R"({"ids":[4,5],"points":[]})"sv);
//
//   ASSERT_TRUE(result.has_value());
//   ASSERT_EQ(document.ids.value().value().size(), 2U);
//   EXPECT_EQ(document.ids.value().value()[0], 4);
//   EXPECT_EQ(document.ids.value().value()[1], 5);
//   EXPECT_TRUE(document.points.value().value().empty());
// }
//
// TEST(DecodeVector, DecodesObjectMemberArrayOfSchemas) {
//   Document document;
//
//   auto result = ser20::decode_into<Reader>(
//       document, R"({"ids":[],"points":[{"x":1,"y":2},{"y":4,"x":3}]})"sv);
//
//   ASSERT_TRUE(result.has_value());
//   EXPECT_TRUE(document.ids.value().value().empty());
//   ASSERT_EQ(document.points.value().value().size(), 2U);
//   EXPECT_EQ(document.points.value().value()[0].x.value().value(), 1);
//   EXPECT_EQ(document.points.value().value()[0].y.value().value(), 2);
//   EXPECT_EQ(document.points.value().value()[1].x.value().value(), 3);
//   EXPECT_EQ(document.points.value().value()[1].y.value().value(), 4);
// }
//
// TEST(DecodeVector, RequiresArrayBegin) {
//   ser20::Elem<ser20::decode::vector<IntElement> | ser20::store::once,
//               ser20::noop_encoder>
//       ids;
//
//   auto result = ser20::decode_into<Reader>(ids, "1"sv);
//
//   ASSERT_FALSE(result.has_value());
//   EXPECT_EQ(result.error().code, ser20::ErrorCode::type_mismatch);
// }
//
// TEST(DecodeVector, RejectsInvalidElement) {
//   ser20::Elem<ser20::decode::vector<IntElement> | ser20::store::once,
//               ser20::noop_encoder>
//       ids;
//
//   auto result = ser20::decode_into<Reader>(ids, "[1,\"x\"]"sv);
//
//   ASSERT_FALSE(result.has_value());
//   EXPECT_EQ(result.error().code, ser20::ErrorCode::type_mismatch);
// }
//
// TEST(DecodeFixedArray, DecodesTopLevelArrayOfElemValues) {
//   ser20::Elem<ser20::decode::fixed_array<IntElement, 3> | ser20::store::once,
//               ser20::noop_encoder>
//       ids;
//
//   auto result = ser20::decode_into<Reader>(ids, "[1,2,3]"sv);
//
//   ASSERT_TRUE(result.has_value());
//   EXPECT_EQ(ids.value()[0], 1);
//   EXPECT_EQ(ids.value()[1], 2);
//   EXPECT_EQ(ids.value()[2], 3);
// }
//
// TEST(DecodeFixedArray, DecodesTopLevelArrayOfSchemas) {
//   ser20::Elem<ser20::decode::fixed_array<Point, 2> | ser20::store::once,
//               ser20::noop_encoder>
//       points;
//
//   auto result =
//       ser20::decode_into<Reader>(points, R"([{"x":1,"y":2},{"x":3,"y":4}])"sv);
//
//   ASSERT_TRUE(result.has_value());
//   EXPECT_EQ(points.value()[0].x.value().value(), 1);
//   EXPECT_EQ(points.value()[0].y.value().value(), 2);
//   EXPECT_EQ(points.value()[1].x.value().value(), 3);
//   EXPECT_EQ(points.value()[1].y.value().value(), 4);
// }
//
// TEST(DecodeFixedArray, RejectsTooFewElements) {
//   ser20::Elem<ser20::decode::fixed_array<IntElement, 3> | ser20::store::once,
//               ser20::noop_encoder>
//       ids;
//
//   auto result = ser20::decode_into<Reader>(ids, "[1,2]"sv);
//
//   ASSERT_FALSE(result.has_value());
//   EXPECT_EQ(result.error().code, ser20::ErrorCode::size_mismatch);
// }
//
// TEST(DecodeFixedArray, RejectsTooManyElements) {
//   ser20::Elem<ser20::decode::fixed_array<IntElement, 2> | ser20::store::once,
//               ser20::noop_encoder>
//       ids;
//
//   auto result = ser20::decode_into<Reader>(ids, "[1,2,3]"sv);
//
//   ASSERT_FALSE(result.has_value());
//   EXPECT_EQ(result.error().code, ser20::ErrorCode::size_mismatch);
// }
