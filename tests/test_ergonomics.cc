#include <gtest/gtest.h>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

#include "hosq/hosq.hh"
#include "hosq/json/fast_reader.hh"

namespace {
namespace t = hosq::types;
namespace v = hosq::types::value;

template <class S>
using ObjectValue =
    hosq::Elem<hosq::scan::object | hosq::decode::to<S> | hosq::store::assign,
               hosq::encode::value>;

struct Address {
  t::String<"city"> city;
};
struct User {
  t::Uint64<"id"> id;
  t::Array<"ids", v::Uint64> ids;
  t::Field<"address", ObjectValue<Address>> address;
};

constexpr auto kJson = R"({"id":42,"ids":[1,2],"address":{"city":"Tokyo"}})";

TEST(Ergonomics, DereferenceAndArrowReachStorage) {
  ObjectValue<User> user;
  ASSERT_TRUE(
      hosq::decode_document_into<hosq::json::FastReader<>>(user, kJson).is_ok());

  EXPECT_EQ(*user->id, 42U);
  EXPECT_EQ(user->ids->size(), 2U);
  EXPECT_EQ(*user->address->city, "Tokyo");
}

struct WithNickname {
  t::Optional<"nickname", v::String> nickname;
};

TEST(Ergonomics, OptionalFieldDereferencesToOptional) {
  WithNickname value;
  EXPECT_FALSE(*value.nickname);
  *value.nickname = "g";
  ASSERT_TRUE(value.nickname->has_value());
  EXPECT_EQ(**value.nickname, "g");
}

TEST(Ergonomics, MutationAndConstAccess) {
  ObjectValue<User> user;
  *user->id = 7;
  user->ids->push_back(3);
  *user->address->city = "Osaka";
  const auto& cuser = user;
  EXPECT_EQ(*cuser->id, 7U);
  EXPECT_EQ(cuser->ids->front(), 3U);
  EXPECT_EQ(*cuser->address->city, "Osaka");
  static_assert(std::is_const_v<std::remove_reference_t<decltype(*cuser->id)>>);
}

TEST(Ergonomics, ScalarElemArrow) {
  v::String text;
  ASSERT_TRUE(
      hosq::decode_document_into<hosq::json::FastReader<>>(text, R"("abc")")
          .is_ok());
  EXPECT_EQ(text->size(), 3U);
}

TEST(Ergonomics, FieldLookupByKey) {
  ObjectValue<User> user;
  ASSERT_TRUE(
      hosq::decode_document_into<hosq::json::FastReader<>>(user, kJson).is_ok());
  EXPECT_EQ(*hosq::field<"id">(user), 42U);
  EXPECT_EQ(*hosq::field<"city">(*hosq::field<"address">(user)), "Tokyo");
  EXPECT_EQ(*hosq::field<"city">(hosq::field<"address">(user)), "Tokyo");

  *hosq::field<"id">(user) = 10;
  const auto& cuser = user;
  EXPECT_EQ(*hosq::field<"id">(cuser), 10U);
  static_assert(
      std::same_as<decltype(hosq::field<"id">(user)), t::Uint64<"id">&>);
  static_assert(std::is_const_v<
                std::remove_reference_t<decltype(hosq::field<"id">(cuser))>>);

  std::tuple<t::Int32<"a">, t::String<"b">> tup;
  *hosq::field<"a">(tup) = 5;
  EXPECT_EQ(*std::get<0>(tup), 5);
}

TEST(Ergonomics, KeyIsAString) {
  t::Uint64<"id"> id;
  const std::string_view key = id.key();
  EXPECT_EQ(key, "id");
  static_assert(t::Uint64<"id">::key() == "id");
  static_assert(std::same_as<decltype(id.key()), std::string_view>);
}

TEST(Ergonomics, DynamicKeyIsReadableAndWritable) {
  hosq::Entry<v::String, v::Int64> entry;
  entry.key() = "k";
  *entry = 3;
  const auto& centry = entry;
  EXPECT_EQ(centry.key(), "k");
  EXPECT_EQ(*centry, 3);
}
}  // namespace
