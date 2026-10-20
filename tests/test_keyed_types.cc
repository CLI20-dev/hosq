#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <tuple>
#include <type_traits>
#include <vector>

#include "hosq/bson/reader.hh"
#include "hosq/bson/writer.hh"
#include "hosq/hosq.hh"
#include "hosq/json/compact_writer.hh"
#include "hosq/json/fast_reader.hh"
#include "hosq/msgpack/reader.hh"
#include "hosq/msgpack/writer.hh"

namespace {
namespace t = hosq::types;
namespace v = hosq::types::value;

// Exact Entry identity is required by the existing schema traits; inheriting
// from Entry would silently break the low-level API's type recognition.
using Named =
    std::tuple<t::Int8<"k">, t::Int16<"k">, t::Int32<"k">, t::Int64<"k">,
               t::Uint8<"k">, t::Uint16<"k">, t::Uint32<"k">, t::Uint64<"k">,
               t::Float32<"k">, t::Float64<"k">, t::Bool<"k">, t::Null<"k">,
               t::String<"k">, t::Bytes<"k">, t::Instant<"k">,
               t::OffsetDateTime<"k">, t::LocalDateTime<"k">, t::LocalDate<"k">,
               t::LocalTime<"k">, t::Decimal128<"k">, t::ObjectId<"k">,
               t::BsonTimestamp<"k">, t::Extension<"k">>;
using Values =
    std::tuple<v::Int8, v::Int16, v::Int32, v::Int64, v::Uint8, v::Uint16,
               v::Uint32, v::Uint64, v::Float32, v::Float64, v::Bool, v::Null,
               v::String, v::Bytes, v::Instant, v::OffsetDateTime,
               v::LocalDateTime, v::LocalDate, v::LocalTime, v::Decimal128,
               v::ObjectId, v::BsonTimestamp, v::Extension>;
static_assert([]<std::size_t... I>(std::index_sequence<I...>) {
  return (std::same_as<
              std::tuple_element_t<I, Named>,
              hosq::Entry<hosq::Key<"k">, std::tuple_element_t<I, Values>>> &&
          ...);
}(std::make_index_sequence<std::tuple_size_v<Named>>{}));
static_assert(std::same_as<t::Int<"k">, t::Int64<"k">>);
static_assert(std::same_as<t::Uint<"k">, t::Uint64<"k">>);
static_assert(std::same_as<t::Float<"k">, t::Float64<"k">>);
static_assert(std::same_as<t::Int<"k", std::int16_t>, t::Int16<"k">>);
static_assert(std::same_as<t::Uint<"k", std::uint16_t>, t::Uint16<"k">>);
static_assert(std::same_as<t::Float<"k", float>, t::Float32<"k">>);
static_assert(std::same_as<t::Try<t::Uint64<"id">>,
                           hosq::Try<hosq::Entry<hosq::Key<"id">, v::Uint64>>>);
static_assert(!hosq::detail::ElemT<t::Try<v::Uint64>>);

template <class S>
using ObjectValue =
    hosq::Elem<hosq::scan::object | hosq::decode::to<S> | hosq::store::assign,
               hosq::encode::value>;

struct Address {
  t::String<"city"> city;
};
struct User {
  hosq::types::Uint64<"id"> id;
  t::Array<"ids", v::Uint64> ids;
  t::Field<"address", ObjectValue<Address>> address;
  t::Mapping<"scores", v::String, v::Int64> scores;
};
static_assert(std::same_as<t::Optional<"nickname", v::String>,
                           t::Field<"nickname", v::Optional<v::String>,
                                    t::presence::Optional>>);

static_assert(!hosq::detail::TryT<t::Optional<"nickname", v::String>>);
static_assert(hosq::detail::EntryT<t::Optional<"nickname", v::String>>);
static_assert(
    hosq::detail::IsOptionalField<t::Optional<"nickname", v::String>>::value);
static_assert(!hosq::detail::IsOptionalField<
              t::Field<"nickname", v::Optional<v::String>>>::value);

template <class Writer, class E>
std::string encode(const E& value) {
  std::string out;
  Writer writer(out);
  EXPECT_TRUE(hosq::encode_into(value, writer)) << writer.error().detail;
  return out;
}

TEST(KeyedTypes, NestedSchemaCrossesJsonMessagePackAndBson) {
  ObjectValue<User> user;
  ASSERT_TRUE(
      hosq::decode_document_into<hosq::json::FastReader<>>(
          user,
          R"({"id":42,"ids":[1,2],"address":{"city":"a\nb"},"scores":{"n":-7}})")
          .is_ok());
  EXPECT_EQ(user.value().address.value().value().city.value().value(), "a\nb");
  const auto msgpack = encode<hosq::msgpack::Writer<>>(user);
  ObjectValue<User> from_msgpack;
  ASSERT_TRUE(
      hosq::decode_document_into<hosq::msgpack::Reader<>>(from_msgpack, msgpack)
          .is_ok());
  const auto bson = encode<hosq::bson::Writer<>>(from_msgpack);
  ObjectValue<User> from_bson;
  ASSERT_TRUE(
      hosq::decode_document_into<hosq::bson::Reader<>>(from_bson, bson).is_ok());
  EXPECT_EQ(
      encode<hosq::json::CompactWriter<>>(from_bson),
      R"({"id":42,"ids":[1,2],"address":{"city":"a\nb"},"scores":{"n":-7}})");
}

TEST(KeyedTypes, EmptyKeyIsNamedAndRootArrayUsesValueTypes) {
  struct Schema {
    t::Uint64<""> empty;
  };
  v::Array<ObjectValue<Schema>> root;
  ASSERT_TRUE(hosq::decode_document_into<hosq::json::FastReader<>>(
                  root, R"([{"":7},{"":9}])")
                  .is_ok());
  EXPECT_EQ(encode<hosq::json::CompactWriter<>>(root), R"([{"":7},{"":9}])");
  v::Uint64 scalar;
  ASSERT_TRUE(hosq::decode_document_into<hosq::json::FastReader<>>(scalar, "42")
                  .is_ok());
  EXPECT_EQ(scalar.value(), 42U);
}

TEST(KeyedTypes, FieldKeepsCustomPipelineAndTryIdentity) {
  using Legacy = hosq::Elem<hosq::scan::scalar | hosq::decode::number<int> |
                                hosq::store::once,
                            hosq::encode::value>;
  using Before = hosq::Try<hosq::Entry<hosq::Key<"n">, Legacy>>;
  using After = t::Try<t::Field<"n", Legacy>>;
  static_assert(std::same_as<Before, After>);
  struct Schema {
    After n;
  } schema;
  ASSERT_TRUE(hosq::decode_document_into<hosq::msgpack::Reader<>>(
                  schema, std::string("\x81\xa1n\xcb\x40\x45\0\0\0\0\0\0", 12))
                  .is_ok());
  ASSERT_TRUE(schema.n.value().value());
  EXPECT_EQ(*schema.n.value().value(), 42);
  EXPECT_EQ(encode<hosq::json::CompactWriter<>>(schema), R"({"n":42})");
  EXPECT_EQ(
      hosq::decode_document_into<hosq::json::FastReader<>>(schema, R"({"n":1})")
          .code,
      hosq::ErrorCode::duplicate_member);
}
}  // namespace

TEST(FieldPresence, OptionalPolicyLeavesNullAcceptanceToValueCodec) {
  struct Schema {
    t::Field<"nil", v::Optional<v::Null>, t::presence::Optional> nil;
  };
  Schema value;
  ASSERT_TRUE(
      hosq::decode_document_into<hosq::json::FastReader<>>(value, "{}").is_ok());
  EXPECT_FALSE(value.nil.value().value().has_value());
  ASSERT_TRUE(hosq::decode_document_into<hosq::json::FastReader<>>(
                  value, R"({"nil":null})")
                  .is_ok());
  EXPECT_EQ(encode<hosq::json::CompactWriter<>>(value), R"({"nil":null})");
}

namespace {
struct NonOptionalStorage {
  auto has_value() const -> bool { return false; }
};
struct ProduceNonOptional {
  template <class Context>
  auto operator()(const Context&) const -> NonOptionalStorage {
    return {};
  }
};
struct EncodeNonOptional {
  auto operator()(const NonOptionalStorage&) const -> int { return 42; }
};
TEST(FieldPresence, LegacyTryDoesNotOmitNonOptionalCustomStorage) {
  using Custom = hosq::Elem<hosq::step<ProduceNonOptional> | hosq::store::assign,
                            hosq::step<EncodeNonOptional> | hosq::encode::value>;
  struct Schema {
    hosq::Try<t::Field<"custom", Custom>> custom;
  };
  Schema value;
  EXPECT_EQ(encode<hosq::json::CompactWriter<>>(value), R"({"custom":42})");
}
}  // namespace
