// #include "ser20/pipeline/decode.hh"
//
// #include <gtest/gtest.h>
//
// #include <string_view>
//
// #include "ser20/decoder.hh"
// #include "ser20/element.hh"
// #include "ser20/json/simple_reader.hh"
// #include "ser20/pipeline/match.hh"
// #include "ser20/pipeline/scan.hh"
// #include "ser20/pipeline/store.hh"
// #include "ser20/pipeline_util.hh"
// #include "ser20/string_literal.hh"
//
// using namespace std::string_view_literals;
// using namespace ser20;
//
// using Reader = ser20::json::SimpleReader;
//
// // ── Basic object decode ───────────────────────────────────────────────────────
//
// TEST(DecodeJson, StringAndIntFields) {
//   struct {
//     ser20::Entry<
//         ser20::Elem<ser20::scan::string | ser20::match::literal<"name"_literal>,
//                     ser20::noop_encoder>,
//         ser20::Elem<ser20::scan::string | ser20::decode::string_view |
//                         ser20::store::once,
//                     ser20::noop_encoder>>
//         name;
//     ser20::Entry<
//         ser20::Elem<ser20::scan::string | ser20::match::literal<"id"_literal>,
//                     ser20::noop_encoder>,
//         ser20::Elem<ser20::scan::literal | ser20::decode::number<int> |
//                         ser20::store::once,
//                     ser20::noop_encoder>>
//         id;
//   } json;
//
//   auto json_str = R"({"name": "John", "id": 123})"sv;
//   auto result = ser20::decode_into<Reader>(json, json_str);
//
//   ASSERT_TRUE(result.has_value());
//   EXPECT_EQ(json.name.value().value(), "John"sv);
//   EXPECT_EQ(json.id.value().value(), 123);
// }
//
// TEST(DecodeJson, FieldsInReverseOrder) {
//   struct {
//     ser20::Entry<
//         ser20::Elem<ser20::scan::string | ser20::match::literal<"name"_literal>,
//                     ser20::noop_encoder>,
//         ser20::Elem<ser20::scan::string | ser20::decode::string_view |
//                         ser20::store::once,
//                     ser20::noop_encoder>>
//         name;
//     ser20::Entry<
//         ser20::Elem<ser20::scan::string | ser20::match::literal<"age"_literal>,
//                     ser20::noop_encoder>,
//         ser20::Elem<ser20::scan::literal | ser20::decode::number<int> |
//                         ser20::store::once,
//                     ser20::noop_encoder>>
//         age;
//   } json;
//
//   auto json_str = R"({"age": 30, "name": "Alice"})"sv;
//   auto result = ser20::decode_into<Reader>(json, json_str);
//
//   ASSERT_TRUE(result.has_value());
//   EXPECT_EQ(json.name.value().value(), "Alice"sv);
//   EXPECT_EQ(json.age.value().value(), 30);
// }
//
// TEST(DecodeJson, RejectsUnknownFields) {
//   struct {
//     ser20::Entry<
//         ser20::Elem<ser20::scan::string | ser20::match::literal<"id"_literal>,
//                     ser20::noop_encoder>,
//         ser20::Elem<ser20::scan::literal | ser20::decode::number<int> |
//                         ser20::store::once,
//                     ser20::noop_encoder>>
//         id;
//   } json;
//
//   auto json_str = R"({"name": "ignored", "id": 42})"sv;
//   auto result = ser20::decode_into<Reader>(json, json_str);
//
//   ASSERT_FALSE(result.has_value());
//   EXPECT_EQ(result.error().code, ser20::ErrorCode::candidate_rejected);
// }
//
// TEST(DecodeJson, StoreOnceDuplicateKeyErrors) {
//   struct {
//     ser20::Entry<
//         ser20::Elem<ser20::scan::string | ser20::match::literal<"x"_literal>,
//                     ser20::noop_encoder>,
//         ser20::Elem<ser20::scan::literal | ser20::decode::number<int> |
//                         ser20::store::once,
//                     ser20::noop_encoder>>
//         x;
//   } json;
//
//   auto json_str = R"({"x": 1, "x": 2})"sv;
//   auto result = ser20::decode_into<Reader>(json, json_str);
//
//   ASSERT_FALSE(result.has_value());
//   EXPECT_EQ(result.error().code, ser20::ErrorCode::duplicate_member);
// }
//
// TEST(DecodeJson, WrongTopLevelTypeErrors) {
//   struct {
//     ser20::Entry<
//         ser20::Elem<ser20::scan::string | ser20::match::literal<"v"_literal>,
//                     ser20::noop_encoder>,
//         ser20::Elem<ser20::scan::literal | ser20::decode::number<int> |
//                         ser20::store::once,
//                     ser20::noop_encoder>>
//         v;
//   } json;
//
//   auto result = ser20::decode_into<Reader>(json, "[1,2,3]"sv);
//   ASSERT_FALSE(result.has_value());
//   EXPECT_EQ(result.error().code, ser20::ErrorCode::type_mismatch);
// }
//
// TEST(DecodeJson, SingleStringField) {
//   struct {
//     ser20::Entry<
//         ser20::Elem<ser20::scan::string | ser20::match::literal<"msg"_literal>,
//                     ser20::noop_encoder>,
//         ser20::Elem<ser20::scan::string | ser20::decode::string_view |
//                         ser20::store::once,
//                     ser20::noop_encoder>>
//         msg;
//   } json;
//
//   auto json_str = R"({"msg": "hello world"})"sv;
//   auto result = ser20::decode_into<Reader>(json, json_str);
//
//   ASSERT_TRUE(result.has_value());
//   EXPECT_EQ(json.msg.value().value(), "hello world"sv);
// }
//
// TEST(DecodeJson, FloatField) {
//   struct {
//     ser20::Entry<
//         ser20::Elem<ser20::scan::string | ser20::match::literal<"ratio"_literal>,
//                     ser20::noop_encoder>,
//         ser20::Elem<ser20::scan::literal | ser20::decode::number<double> |
//                         ser20::store::once,
//                     ser20::noop_encoder>>
//         ratio;
//   } json;
//
//   auto json_str = R"({"ratio": 3.14})"sv;
//   auto result = ser20::decode_into<Reader>(json, json_str);
//
//   ASSERT_TRUE(result.has_value());
//   EXPECT_DOUBLE_EQ(json.ratio.value().value(), 3.14);
// }
