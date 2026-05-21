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
#include "hosq/try.hh"

using namespace std::string_view_literals;

namespace {

using Reader = hosq::json::SimpleReader;

template <hosq::StringLiteral Name>
using Key = hosq::Elem<hosq::scan::string | hosq::match::literal<Name>,
                       hosq::noop_encoder>;

using StringValue =
    hosq::Elem<hosq::scan::string | hosq::decode::string_view |
                   hosq::store::assign,
               hosq::noop_encoder>;

template <class T>
using NumberValue =
    hosq::Elem<hosq::scan::literal | hosq::decode::number<T> |
                   hosq::store::assign,
               hosq::noop_encoder>;

struct Address {
  hosq::Try<hosq::Entry<Key<"city">, StringValue>> city;
  hosq::Try<hosq::Entry<Key<"zip">, NumberValue<int>>> zip;
};

struct Profile {
  hosq::Try<hosq::Entry<Key<"display_name">, StringValue>> display_name;
};

using AddressValue =
    hosq::Elem<hosq::scan::object | hosq::decode::to<Address> |
                   hosq::store::assign,
               hosq::noop_encoder>;

using ProfileValue =
    hosq::Elem<hosq::scan::object | hosq::decode::to<Profile> |
                   hosq::store::assign,
               hosq::noop_encoder>;

struct User {
  hosq::Try<hosq::Entry<Key<"id">, NumberValue<int>>> id;
  hosq::Try<hosq::Entry<Key<"name">, StringValue>> name;
  hosq::Try<hosq::Entry<Key<"address">, AddressValue>> address;
  hosq::Try<hosq::Entry<Key<"profile">, ProfileValue>> profile;
};

using UserValue =
    hosq::Elem<hosq::scan::object | hosq::decode::to<User> |
                   hosq::store::assign,
               hosq::noop_encoder>;

struct Company {
  hosq::Try<hosq::Entry<Key<"name">, StringValue>> name;
  hosq::Try<hosq::Entry<Key<"owner">, UserValue>> owner;
};

struct AddressEnvelope {
  hosq::Try<hosq::Entry<Key<"address">, AddressValue>> address;
};

}  // namespace

TEST(DecodeNestedJson, DecodesNestedObjectValue) {
  auto user = User{};

  const auto result = hosq::decode_into<Reader>(
      user,
      R"({"id":7,"name":"Alice","address":{"city":"Tokyo","zip":100},"profile":{"display_name":"alice"}})"sv);

  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(user.id.value().value(), 7);
  EXPECT_EQ(user.name.value().value(), "Alice"sv);
  EXPECT_EQ(user.address.value().value().city.value().value(), "Tokyo"sv);
  EXPECT_EQ(user.address.value().value().zip.value().value(), 100);
  EXPECT_EQ(user.profile.value().value().display_name.value().value(),
            "alice"sv);
}

TEST(DecodeNestedJson, DecodesNestedFieldsInReverseOrder) {
  auto user = User{};

  const auto result = hosq::decode_into<Reader>(
      user,
      R"({"profile":{"display_name":"bob"},"address":{"zip":150,"city":"Osaka"},"name":"Bob","id":8})"sv);

  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(user.id.value().value(), 8);
  EXPECT_EQ(user.name.value().value(), "Bob"sv);
  EXPECT_EQ(user.address.value().value().city.value().value(), "Osaka"sv);
  EXPECT_EQ(user.address.value().value().zip.value().value(), 150);
  EXPECT_EQ(user.profile.value().value().display_name.value().value(), "bob"sv);
}

TEST(DecodeNestedJson, DecodesMultipleNestedObjectLevels) {
  auto company = Company{};

  const auto result = hosq::decode_into<Reader>(
      company,
      R"({"name":"Acme","owner":{"id":1,"name":"Carol","address":{"city":"Kyoto","zip":600},"profile":{"display_name":"ceo"}}})"sv);

  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(company.name.value().value(), "Acme"sv);
  EXPECT_EQ(company.owner.value().value().id.value().value(), 1);
  EXPECT_EQ(company.owner.value().value().name.value().value(), "Carol"sv);
  EXPECT_EQ(company.owner.value().value().address.value().value().city.value().value(),
            "Kyoto"sv);
  EXPECT_EQ(company.owner.value().value().address.value().value().zip.value().value(),
            600);
  EXPECT_EQ(company.owner.value().value().profile.value().value().display_name.value().value(),
            "ceo"sv);
}

TEST(DecodeNestedJson, RejectsUnknownNestedField) {
  auto envelope = AddressEnvelope{};

  const auto result = hosq::decode_into<Reader>(
      envelope, R"({"address":{"city":"Tokyo","extra":1,"zip":100}})"sv);

  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code, hosq::ErrorCode::unknown_member);
}

TEST(DecodeNestedJson, NestedObjectTypeMismatch) {
  auto user = User{};

  const auto result = hosq::decode_into<Reader>(
      user,
      R"({"id":7,"name":"Alice","address":true,"profile":{"display_name":"alice"}})"sv);

  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code, hosq::ErrorCode::type_mismatch);
}
