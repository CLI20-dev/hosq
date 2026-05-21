// #include "hosq/pipeline/decode.hh"
//
// #include <gtest/gtest.h>
//
// #include <string_view>
//
// #include "hosq/decoder.hh"
// #include "hosq/element.hh"
// #include "hosq/json/simple_reader.hh"
// #include "hosq/pipeline/match.hh"
// #include "hosq/pipeline/scan.hh"
// #include "hosq/pipeline/store.hh"
// #include "hosq/pipeline_util.hh"
// #include "hosq/string_literal.hh"
//
// using namespace std::string_view_literals;
// using namespace hosq;
//
// using Reader = hosq::json::SimpleReader;
//
// // ── Basic object decode ───────────────────────────────────────────────────────
//
// TEST(DecodeJson, StringAndIntFields) {
//   struct {
//     hosq::Entry<
//         hosq::Elem<hosq::scan::string | hosq::match::literal<"name"_literal>,
//                     hosq::noop_encoder>,
//         hosq::Elem<hosq::scan::string | hosq::decode::string_view |
//                         hosq::store::once,
//                     hosq::noop_encoder>>
//         name;
//     hosq::Entry<
//         hosq::Elem<hosq::scan::string | hosq::match::literal<"id"_literal>,
//                     hosq::noop_encoder>,
//         hosq::Elem<hosq::scan::literal | hosq::decode::number<int> |
//                         hosq::store::once,
//                     hosq::noop_encoder>>
//         id;
//   } json;
//
//   auto json_str = R"({"name": "John", "id": 123})"sv;
//   auto result = hosq::decode_into<Reader>(json, json_str);
//
//   ASSERT_TRUE(result.has_value());
//   EXPECT_EQ(json.name.value().value(), "John"sv);
//   EXPECT_EQ(json.id.value().value(), 123);
// }
//
// TEST(DecodeJson, FieldsInReverseOrder) {
//   struct {
//     hosq::Entry<
//         hosq::Elem<hosq::scan::string | hosq::match::literal<"name"_literal>,
//                     hosq::noop_encoder>,
//         hosq::Elem<hosq::scan::string | hosq::decode::string_view |
//                         hosq::store::once,
//                     hosq::noop_encoder>>
//         name;
//     hosq::Entry<
//         hosq::Elem<hosq::scan::string | hosq::match::literal<"age"_literal>,
//                     hosq::noop_encoder>,
//         hosq::Elem<hosq::scan::literal | hosq::decode::number<int> |
//                         hosq::store::once,
//                     hosq::noop_encoder>>
//         age;
//   } json;
//
//   auto json_str = R"({"age": 30, "name": "Alice"})"sv;
//   auto result = hosq::decode_into<Reader>(json, json_str);
//
//   ASSERT_TRUE(result.has_value());
//   EXPECT_EQ(json.name.value().value(), "Alice"sv);
//   EXPECT_EQ(json.age.value().value(), 30);
// }
//
// TEST(DecodeJson, RejectsUnknownFields) {
//   struct {
//     hosq::Entry<
//         hosq::Elem<hosq::scan::string | hosq::match::literal<"id"_literal>,
//                     hosq::noop_encoder>,
//         hosq::Elem<hosq::scan::literal | hosq::decode::number<int> |
//                         hosq::store::once,
//                     hosq::noop_encoder>>
//         id;
//   } json;
//
//   auto json_str = R"({"name": "ignored", "id": 42})"sv;
//   auto result = hosq::decode_into<Reader>(json, json_str);
//
//   ASSERT_FALSE(result.has_value());
//   EXPECT_EQ(result.error().code, hosq::ErrorCode::candidate_rejected);
// }
//
// TEST(DecodeJson, StoreOnceDuplicateKeyErrors) {
//   struct {
//     hosq::Entry<
//         hosq::Elem<hosq::scan::string | hosq::match::literal<"x"_literal>,
//                     hosq::noop_encoder>,
//         hosq::Elem<hosq::scan::literal | hosq::decode::number<int> |
//                         hosq::store::once,
//                     hosq::noop_encoder>>
//         x;
//   } json;
//
//   auto json_str = R"({"x": 1, "x": 2})"sv;
//   auto result = hosq::decode_into<Reader>(json, json_str);
//
//   ASSERT_FALSE(result.has_value());
//   EXPECT_EQ(result.error().code, hosq::ErrorCode::duplicate_member);
// }
//
// TEST(DecodeJson, WrongTopLevelTypeErrors) {
//   struct {
//     hosq::Entry<
//         hosq::Elem<hosq::scan::string | hosq::match::literal<"v"_literal>,
//                     hosq::noop_encoder>,
//         hosq::Elem<hosq::scan::literal | hosq::decode::number<int> |
//                         hosq::store::once,
//                     hosq::noop_encoder>>
//         v;
//   } json;
//
//   auto result = hosq::decode_into<Reader>(json, "[1,2,3]"sv);
//   ASSERT_FALSE(result.has_value());
//   EXPECT_EQ(result.error().code, hosq::ErrorCode::type_mismatch);
// }
//
// TEST(DecodeJson, SingleStringField) {
//   struct {
//     hosq::Entry<
//         hosq::Elem<hosq::scan::string | hosq::match::literal<"msg"_literal>,
//                     hosq::noop_encoder>,
//         hosq::Elem<hosq::scan::string | hosq::decode::string_view |
//                         hosq::store::once,
//                     hosq::noop_encoder>>
//         msg;
//   } json;
//
//   auto json_str = R"({"msg": "hello world"})"sv;
//   auto result = hosq::decode_into<Reader>(json, json_str);
//
//   ASSERT_TRUE(result.has_value());
//   EXPECT_EQ(json.msg.value().value(), "hello world"sv);
// }
//
// TEST(DecodeJson, FloatField) {
//   struct {
//     hosq::Entry<
//         hosq::Elem<hosq::scan::string | hosq::match::literal<"ratio"_literal>,
//                     hosq::noop_encoder>,
//         hosq::Elem<hosq::scan::literal | hosq::decode::number<double> |
//                         hosq::store::once,
//                     hosq::noop_encoder>>
//         ratio;
//   } json;
//
//   auto json_str = R"({"ratio": 3.14})"sv;
//   auto result = hosq::decode_into<Reader>(json, json_str);
//
//   ASSERT_TRUE(result.has_value());
//   EXPECT_DOUBLE_EQ(json.ratio.value().value(), 3.14);
// }
