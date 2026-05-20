// #include <gtest/gtest.h>
//
// #include <string_view>
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
// using StringValue =
//     ser20::Elem<ser20::scan::string | ser20::decode::string_view |
//                     ser20::store::once,
//                 ser20::noop_encoder>;
//
// template <class T>
// using NumberValue = ser20::Elem<ser20::scan::literal | ser20::decode::number<T> |
//                                     ser20::store::once,
//                                 ser20::noop_encoder>;
//
// struct Address {
//   ser20::Entry<Key<"city"_literal>, StringValue> city;
//   ser20::Entry<Key<"zip"_literal>, NumberValue<int>> zip;
// };
//
// struct Profile {
//   ser20::Entry<Key<"display_name"_literal>, StringValue> display_name;
// };
//
// struct User {
//   ser20::Entry<Key<"id"_literal>, NumberValue<int>> id;
//   ser20::Entry<Key<"name"_literal>, StringValue> name;
//   ser20::Entry<Key<"address"_literal>, Address> address;
//   ser20::Entry<Key<"profile"_literal>, Profile> profile;
// };
//
// struct Company {
//   ser20::Entry<Key<"name"_literal>, StringValue> name;
//   ser20::Entry<Key<"owner"_literal>, User> owner;
// };
//
// struct AddressEnvelope {
//   ser20::Entry<Key<"address"_literal>, Address> address;
// };
//
// }  // namespace
//
// TEST(DecodeNestedJson, DecodesNestedObjectValue) {
//   User user;
//
//   auto result = ser20::decode_into<Reader>(
//       user,
//       R"({"id":7,"name":"Alice","address":{"city":"Tokyo","zip":100},"profile":{"display_name":"alice"}})"sv);
//
//   ASSERT_TRUE(result.has_value());
//   EXPECT_EQ(user.id.value().value(), 7);
//   EXPECT_EQ(user.name.value().value(), "Alice"sv);
//   EXPECT_EQ(user.address.value().city.value().value(), "Tokyo"sv);
//   EXPECT_EQ(user.address.value().zip.value().value(), 100);
//   EXPECT_EQ(user.profile.value().display_name.value().value(), "alice"sv);
// }
//
// TEST(DecodeNestedJson, DecodesNestedFieldsInReverseOrder) {
//   User user;
//
//   auto result = ser20::decode_into<Reader>(
//       user,
//       R"({"profile":{"display_name":"bob"},"address":{"zip":150,"city":"Osaka"},"name":"Bob","id":8})"sv);
//
//   ASSERT_TRUE(result.has_value());
//   EXPECT_EQ(user.id.value().value(), 8);
//   EXPECT_EQ(user.name.value().value(), "Bob"sv);
//   EXPECT_EQ(user.address.value().city.value().value(), "Osaka"sv);
//   EXPECT_EQ(user.address.value().zip.value().value(), 150);
//   EXPECT_EQ(user.profile.value().display_name.value().value(), "bob"sv);
// }
//
// TEST(DecodeNestedJson, DecodesMultipleNestedObjectLevels) {
//   Company company;
//
//   auto result = ser20::decode_into<Reader>(
//       company,
//       R"({"name":"Acme","owner":{"id":1,"name":"Carol","address":{"city":"Kyoto","zip":600},"profile":{"display_name":"ceo"}}})"sv);
//
//   ASSERT_TRUE(result.has_value());
//   EXPECT_EQ(company.name.value().value(), "Acme"sv);
//   EXPECT_EQ(company.owner.value().id.value().value(), 1);
//   EXPECT_EQ(company.owner.value().name.value().value(), "Carol"sv);
//   EXPECT_EQ(company.owner.value().address.value().city.value().value(),
//             "Kyoto"sv);
//   EXPECT_EQ(company.owner.value().address.value().zip.value().value(), 600);
//   EXPECT_EQ(company.owner.value().profile.value().display_name.value().value(),
//             "ceo"sv);
// }
//
// TEST(DecodeNestedJson, RejectsUnknownNestedField) {
//   AddressEnvelope envelope;
//
//   auto result = ser20::decode_into<Reader>(
//       envelope, R"({"address":{"city":"Tokyo","extra":1,"zip":100}})"sv);
//
//   ASSERT_FALSE(result.has_value());
//   EXPECT_EQ(result.error().code, ser20::ErrorCode::candidate_rejected);
// }
//
// TEST(DecodeNestedJson, NestedObjectTypeMismatch) {
//   User user;
//
//   auto result = ser20::decode_into<Reader>(
//       user,
//       R"({"id":7,"name":"Alice","address":true,"profile":{"display_name":"alice"}})"sv);
//
//   ASSERT_FALSE(result.has_value());
//   EXPECT_EQ(result.error().code, ser20::ErrorCode::type_mismatch);
// }
