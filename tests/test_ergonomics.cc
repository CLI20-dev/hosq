#include <gtest/gtest.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "hosq/hosq.hh"
#include "hosq/json/fast_reader.hh"

namespace {
namespace t = hosq::types;
namespace v = hosq::types::value;

struct Address {
  t::Text<"city"> city;
};
struct User {
  t::Uint64<"id"> id;
  t::Array<"ids", v::Uint64> ids;
  t::Record<"address", Address> address;
  t::Optional<"nickname", v::Text> nickname;
};

TEST(Ergonomics, DereferenceAndArrowReachStorage) {
  v::Record<User> user;
  ASSERT_TRUE(
      hosq::decode_document_into<hosq::json::FastReader<>>(
          user,
          R"({"id":42,"ids":[1,2],"address":{"city":"Tokyo"},"nickname":"g"})")
          .is_ok());

  EXPECT_EQ(*user->id, 42U);
  EXPECT_EQ(user->ids->size(), 2U);
  EXPECT_EQ(*user->address->city, "Tokyo");
  ASSERT_TRUE(user->nickname->has_value());
  EXPECT_EQ(**user->nickname, "g");

  // Equivalent to the verbose spelling.
  EXPECT_EQ(*user->id, *user.value().id);
}

TEST(Ergonomics, MutationAndConstAccess) {
  v::Record<User> user;
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
  v::Text text;
  ASSERT_TRUE(
      hosq::decode_document_into<hosq::json::FastReader<>>(text, R"("abc")")
          .is_ok());
  EXPECT_EQ(text->size(), 3U);
}

TEST(Ergonomics, FieldLookupByKey) {
  v::Record<User> user;
  ASSERT_TRUE(hosq::decode_document_into<hosq::json::FastReader<>>(
                  user, R"({"id":9,"ids":[],"address":{"city":"Kyoto"}})")
                  .is_ok());
  EXPECT_EQ(*hosq::field<"id">(user), 9U);
  EXPECT_EQ(*hosq::field<"city">(*hosq::field<"address">(user)), "Kyoto");
  EXPECT_FALSE(hosq::field<"nickname">(user)->has_value());

  *hosq::field<"id">(user) = 10;
  const auto& cuser = user;
  EXPECT_EQ(*hosq::field<"id">(cuser), 10U);
  static_assert(
      std::same_as<decltype(hosq::field<"id">(user)), t::Uint64<"id">&>);
  static_assert(std::is_const_v<
                std::remove_reference_t<decltype(hosq::field<"id">(cuser))>>);

  std::tuple<t::Int32<"a">, t::Text<"b">> tup;
  *hosq::field<"a">(tup) = 5;
  EXPECT_EQ(*std::get<0>(tup), 5);
}

TEST(Ergonomics, ConstKeyIsAStringView) {
  t::Uint64<"id"> id;
  const std::string_view key = id.key();
  EXPECT_EQ(key, "id");
  static_assert(t::Uint64<"id">::key() == "id");
  static_assert(std::same_as<decltype(id.key()), std::string_view>);
}
}  // namespace
