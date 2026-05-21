// #include <gtest/gtest.h>
//
// #include <string_view>
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
// using StringValue =
//     hosq::Elem<hosq::scan::string | hosq::decode::string_view |
//                     hosq::store::once,
//                 hosq::noop_encoder>;
//
// template <class T>
// using NumberValue = hosq::Elem<hosq::scan::literal | hosq::decode::number<T> |
//                                     hosq::store::once,
//                                 hosq::noop_encoder>;
//
// struct Address {
//   hosq::Entry<Key<"city"_literal>, StringValue> city;
//   hosq::Entry<Key<"zip"_literal>, NumberValue<int>> zip;
// };
//
// struct Profile {
//   hosq::Entry<Key<"display_name"_literal>, StringValue> display_name;
// };
//
// struct User {
//   hosq::Entry<Key<"id"_literal>, NumberValue<int>> id;
//   hosq::Entry<Key<"name"_literal>, StringValue> name;
//   hosq::Entry<Key<"address"_literal>, Address> address;
//   hosq::Entry<Key<"profile"_literal>, Profile> profile;
// };
//
// struct Company {
//   hosq::Entry<Key<"name"_literal>, StringValue> name;
//   hosq::Entry<Key<"owner"_literal>, User> owner;
// };
//
// struct AddressEnvelope {
//   hosq::Entry<Key<"address"_literal>, Address> address;
// };
//
// }  // namespace
//
// TEST(DecodeNestedJson, DecodesNestedObjectValue) {
//   User user;
//
//   auto result = hosq::decode_into<Reader>(
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
//   auto result = hosq::decode_into<Reader>(
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
//   auto result = hosq::decode_into<Reader>(
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
//   auto result = hosq::decode_into<Reader>(
//       envelope, R"({"address":{"city":"Tokyo","extra":1,"zip":100}})"sv);
//
//   ASSERT_FALSE(result.has_value());
//   EXPECT_EQ(result.error().code, hosq::ErrorCode::candidate_rejected);
// }
//
// TEST(DecodeNestedJson, NestedObjectTypeMismatch) {
//   User user;
//
//   auto result = hosq::decode_into<Reader>(
//       user,
//       R"({"id":7,"name":"Alice","address":true,"profile":{"display_name":"alice"}})"sv);
//
//   ASSERT_FALSE(result.has_value());
//   EXPECT_EQ(result.error().code, hosq::ErrorCode::type_mismatch);
// }
