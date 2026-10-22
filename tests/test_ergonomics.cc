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
  ASSERT_TRUE(hosq::decode_document_into<hosq::json::FastReader<>>(
                  user,
                  R"({"id":42,"ids":[1,2],"address":{"city":"Tokyo"},"nickname":"g"})")
                  .is_ok());

  EXPECT_EQ(*user->id, 42U);
  EXPECT_EQ(user->ids->size(), 2U);
  EXPECT_EQ(*user->address->city, "Tokyo");
  ASSERT_TRUE(user->nickname->has_value());
  EXPECT_EQ(**user->nickname, "g");

  // Equivalent to the verbose spelling.
  EXPECT_EQ(*user->id, user.value().id.value().value());
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
}  // namespace
