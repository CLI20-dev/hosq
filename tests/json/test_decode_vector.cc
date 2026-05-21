// #include <gtest/gtest.h>
//
// #include <array>
// #include <string_view>
// #include <vector>
//
// #include "hosq/decoder.hh"
// #include "hosq/element.hh"
// #include "hosq/json/simple_reader.hh"
// #include "hosq/pipeline/decode.hh"
// #include "hosq/pipeline/match.hh"
// #include "hosq/pipeline/scan.hh"
// #include "hosq/pipeline/store.hh"
// #include "hosq/string_literal.hh"
//
// using namespace std::string_view_literals;
// using namespace hosq;
//
// using Reader = hosq::json::SimpleReader;
//
// namespace {
//
// template <hosq::StringLiteral Name>
// using Key = hosq::Elem<hosq::scan::string | hosq::match::literal<Name>,
//                         hosq::noop_encoder>;
//
// template <class T>
// using NumberValue = hosq::Elem<hosq::scan::literal | hosq::decode::number<T> |
//                                     hosq::store::once,
//                                 hosq::noop_encoder>;
//
// using StringValue =
//     hosq::Elem<hosq::scan::string | hosq::decode::string_view |
//                     hosq::store::once,
//                 hosq::noop_encoder>;
//
// using IntElement =
//     hosq::Elem<hosq::scan::literal | hosq::decode::number<int> |
//                     hosq::store::once,
//                 hosq::noop_encoder>;
//
// struct Point {
//   hosq::Entry<Key<"x"_literal>, NumberValue<int>> x;
//   hosq::Entry<Key<"y"_literal>, NumberValue<int>> y;
// };
//
// struct Document {
//   hosq::Entry<
//       Key<"ids"_literal>,
//       hosq::Elem<hosq::decode::vector<IntElement> | hosq::store::once,
//                   hosq::noop_encoder>>
//       ids;
//   hosq::Entry<Key<"points"_literal>,
//                hosq::Elem<hosq::decode::vector<Point> | hosq::store::once,
//                            hosq::noop_encoder>>
//       points;
// };
//
// }  // namespace
//
// TEST(DecodeVector, DecodesTopLevelArrayOfElemValues) {
//   hosq::Elem<hosq::decode::vector<IntElement> | hosq::store::once,
//               hosq::noop_encoder>
//       ids;
//
//   auto result = hosq::decode_into<Reader>(ids, "[1,2,3]"sv);
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
//       hosq::decode_into<Reader>(document, R"({"ids":[4,5],"points":[]})"sv);
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
//   auto result = hosq::decode_into<Reader>(
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
//   hosq::Elem<hosq::decode::vector<IntElement> | hosq::store::once,
//               hosq::noop_encoder>
//       ids;
//
//   auto result = hosq::decode_into<Reader>(ids, "1"sv);
//
//   ASSERT_FALSE(result.has_value());
//   EXPECT_EQ(result.error().code, hosq::ErrorCode::type_mismatch);
// }
//
// TEST(DecodeVector, RejectsInvalidElement) {
//   hosq::Elem<hosq::decode::vector<IntElement> | hosq::store::once,
//               hosq::noop_encoder>
//       ids;
//
//   auto result = hosq::decode_into<Reader>(ids, "[1,\"x\"]"sv);
//
//   ASSERT_FALSE(result.has_value());
//   EXPECT_EQ(result.error().code, hosq::ErrorCode::type_mismatch);
// }
//
// TEST(DecodeFixedArray, DecodesTopLevelArrayOfElemValues) {
//   hosq::Elem<hosq::decode::fixed_array<IntElement, 3> | hosq::store::once,
//               hosq::noop_encoder>
//       ids;
//
//   auto result = hosq::decode_into<Reader>(ids, "[1,2,3]"sv);
//
//   ASSERT_TRUE(result.has_value());
//   EXPECT_EQ(ids.value()[0], 1);
//   EXPECT_EQ(ids.value()[1], 2);
//   EXPECT_EQ(ids.value()[2], 3);
// }
//
// TEST(DecodeFixedArray, DecodesTopLevelArrayOfSchemas) {
//   hosq::Elem<hosq::decode::fixed_array<Point, 2> | hosq::store::once,
//               hosq::noop_encoder>
//       points;
//
//   auto result =
//       hosq::decode_into<Reader>(points, R"([{"x":1,"y":2},{"x":3,"y":4}])"sv);
//
//   ASSERT_TRUE(result.has_value());
//   EXPECT_EQ(points.value()[0].x.value().value(), 1);
//   EXPECT_EQ(points.value()[0].y.value().value(), 2);
//   EXPECT_EQ(points.value()[1].x.value().value(), 3);
//   EXPECT_EQ(points.value()[1].y.value().value(), 4);
// }
//
// TEST(DecodeFixedArray, RejectsTooFewElements) {
//   hosq::Elem<hosq::decode::fixed_array<IntElement, 3> | hosq::store::once,
//               hosq::noop_encoder>
//       ids;
//
//   auto result = hosq::decode_into<Reader>(ids, "[1,2]"sv);
//
//   ASSERT_FALSE(result.has_value());
//   EXPECT_EQ(result.error().code, hosq::ErrorCode::size_mismatch);
// }
//
// TEST(DecodeFixedArray, RejectsTooManyElements) {
//   hosq::Elem<hosq::decode::fixed_array<IntElement, 2> | hosq::store::once,
//               hosq::noop_encoder>
//       ids;
//
//   auto result = hosq::decode_into<Reader>(ids, "[1,2,3]"sv);
//
//   ASSERT_FALSE(result.has_value());
//   EXPECT_EQ(result.error().code, hosq::ErrorCode::size_mismatch);
// }
