#include <gtest/gtest.h>

#include <string_view>

#include "hosq/hosq.hh"
#include "reader_test_types.hh"

using namespace std::string_view_literals;
using namespace hosq;

namespace {

template <StringLiteral Name>
using Key = Elem<scan::scalar | decode::string_view | match::literal<Name>,
                 noop_encoder>;

using StringValue =
    Elem<scan::scalar | decode::string_view | store::assign, noop_encoder>;

template <class T>
using NumberValue =
    Elem<scan::scalar | decode::number<T> | store::assign, noop_encoder>;

struct Address {
  Try<Entry<Key<"city">, StringValue>> city;
  Try<Entry<Key<"zip">, NumberValue<int>>> zip;
};

struct Profile {
  Try<Entry<Key<"display_name">, StringValue>> display_name;
};

using AddressValue =
    Elem<scan::object | decode::to<Address> | store::assign, noop_encoder>;

using ProfileValue =
    Elem<scan::object | decode::to<Profile> | store::assign, noop_encoder>;

struct User {
  Try<Entry<Key<"id">, NumberValue<int>>> id;
  Try<Entry<Key<"name">, StringValue>> name;
  Try<Entry<Key<"address">, AddressValue>> address;
  Try<Entry<Key<"profile">, ProfileValue>> profile;
};

using UserValue =
    Elem<scan::object | decode::to<User> | store::assign, noop_encoder>;

struct Company {
  Try<Entry<Key<"name">, StringValue>> name;
  Try<Entry<Key<"owner">, UserValue>> owner;
};

struct AddressEnvelope {
  Try<Entry<Key<"address">, AddressValue>> address;
};

struct UserWithSkip {
  Try<Entry<Key<"id">, NumberValue<int>>> id;
  Try<Entry<Key<"name">, StringValue>> name;
  Try<Entry<Elem<scan::scalar | decode::skip, noop_encoder>,
            Elem<decode::skip, noop_encoder>>>
      skip;
};

}  // namespace

template <class Reader>
class DecodeNestedJsonTest : public test::RuntimeReaderTest<Reader> {};

TYPED_TEST_SUITE(DecodeNestedJsonTest, test::RuntimeReaderTypes);

TYPED_TEST(DecodeNestedJsonTest, DecodesNestedObjectValue) {
  auto user = User{};

  const auto result = decode_into<TypeParam>(
      user,
      R"({"id":7,"name":"Alice","address":{"city":"Tokyo","zip":100},"profile":{"display_name":"alice"}})"sv);

  ASSERT_TRUE(result.is_ok());
  EXPECT_EQ(*user.id, 7);
  EXPECT_EQ(*user.name, "Alice"sv);
  EXPECT_EQ(*user.address->city, "Tokyo"sv);
  EXPECT_EQ(*user.address->zip, 100);
  EXPECT_EQ(*user.profile->display_name, "alice"sv);
}

TYPED_TEST(DecodeNestedJsonTest, DecodesNestedFieldsInReverseOrder) {
  auto user = User{};

  const auto result = decode_into<TypeParam>(
      user,
      R"({"profile":{"display_name":"bob"},"address":{"zip":150,"city":"Osaka"},"name":"Bob","id":8})"sv);

  ASSERT_TRUE(result.is_ok());
  EXPECT_EQ(*user.id, 8);
  EXPECT_EQ(*user.name, "Bob"sv);
  EXPECT_EQ(*user.address->city, "Osaka"sv);
  EXPECT_EQ(*user.address->zip, 150);
  EXPECT_EQ(*user.profile->display_name, "bob"sv);
}

TYPED_TEST(DecodeNestedJsonTest, DecodesMultipleNestedObjectLevels) {
  auto company = Company{};

  const auto result = decode_into<TypeParam>(
      company,
      R"({"name":"Acme","owner":{"id":1,"name":"Carol","address":{"city":"Kyoto","zip":600},"profile":{"display_name":"ceo"}}})"sv);

  ASSERT_TRUE(result.is_ok());
  EXPECT_EQ(*company.name, "Acme"sv);
  EXPECT_EQ(*company.owner->id, 1);
  EXPECT_EQ(*company.owner->name, "Carol"sv);
  EXPECT_EQ(*company.owner->address->city, "Kyoto"sv);
  EXPECT_EQ(*company.owner->address->zip, 600);
  EXPECT_EQ(*company.owner->profile->display_name, "ceo"sv);
}

TYPED_TEST(DecodeNestedJsonTest, RejectsUnknownNestedField) {
  auto envelope = AddressEnvelope{};

  const auto result = decode_into<TypeParam>(
      envelope, R"({"address":{"city":"Tokyo","extra":1,"zip":100}})"sv);

  ASSERT_FALSE(result.is_ok());
  EXPECT_EQ(result.code, ErrorCode::unknown_member);
}

TYPED_TEST(DecodeNestedJsonTest, NestedObjectTypeMismatch) {
  auto user = User{};

  const auto result = decode_into<TypeParam>(
      user,
      R"({"id":7,"name":"Alice","address":true,"profile":{"display_name":"alice"}})"sv);

  ASSERT_FALSE(result.is_ok());
  EXPECT_EQ(result.code, ErrorCode::type_mismatch);
}

TYPED_TEST(DecodeNestedJsonTest, SkipIgnoresUnknownScalarObjectAndArrayValues) {
  auto user = UserWithSkip{};

  const auto result = decode_into<TypeParam>(
      user,
      R"({"ignored_scalar":1,"name":"Alice","ignored_object":{"a":[1,2,3]},"id":7,"ignored_array":[{"x":1},2,3]})"sv);

  ASSERT_TRUE(result.is_ok());
  EXPECT_EQ(*user.id, 7);
  EXPECT_EQ(*user.name, "Alice"sv);
}
