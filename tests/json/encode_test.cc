#include "ser20/pipeline/encode.hh"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <variant>

#include "ser20/encoder.hh"
#include "ser20/json/compact_writer.hh"
#include "ser20/pipeline/decode.hh"
#include "ser20/pipeline/match.hh"
#include "ser20/pipeline/scan.hh"
#include "ser20/pipeline/store.hh"
#include "ser20/string_literal.hh"

using namespace std::string_view_literals;
using namespace ser20;

namespace {

using TT = ser20::TokenType;
using JTC = ser20::JsonTokenContext;

template <class W>
auto write_scalar(W& writer, std::string_view content, JTC context)
    -> ser20::Result<void, ser20::Error> {
  auto buffer = writer.write(ser20::WriteRequest{
      .length = content.size(),
      .type = TT::Scalar,
      .context = static_cast<std::uint8_t>(context),
  });
  if (!buffer) {
    return ser20::unexpected(buffer.error());
  }
  std::copy(content.begin(), content.end(), buffer->data());
  return writer.commit(content.size());
}

struct WriteJsonString {
  template <class Context>
  auto operator()(std::string_view value, Context& ctx) const
      -> ser20::Result<void, ser20::Error> {
    return write_scalar(ctx.writer.get(), value, JTC::string);
  }
};

struct WriteJsonLiteral {
  template <class Context>
  auto operator()(std::string_view value, Context& ctx) const
      -> ser20::Result<void, ser20::Error> {
    return write_scalar(ctx.writer.get(), value, JTC::literal);
  }

  template <class Context>
  auto operator()(const std::string& value, Context& ctx) const
      -> ser20::Result<void, ser20::Error> {
    return write_scalar(ctx.writer.get(), value, JTC::literal);
  }
};

template <ser20::StringLiteral Name>
struct WriteJsonKey {
  template <class Context>
  auto operator()(std::monostate, Context& ctx) const
      -> ser20::Result<void, ser20::Error> {
    return write_scalar(ctx.writer.get(), Name.view(), JTC::string);
  }
};

template <ser20::StringLiteral Name>
using Key = ser20::Elem<ser20::noop_decoder, ser20::step<WriteJsonKey<Name>>>;

using StringValue =
    ser20::Elem<ser20::scan::string | ser20::decode::string_view |
                    ser20::store::once,
                ser20::encode::string_view | ser20::step<WriteJsonString>>;

template <class T>
using NumberValue =
    ser20::Elem<ser20::scan::literal | ser20::decode::number<T> |
                    ser20::store::once,
                ser20::encode::number<T> | ser20::step<WriteJsonLiteral>>;

struct Address {
  ser20::Entry<Key<"city">, StringValue> city;
  ser20::Entry<Key<"zip">, NumberValue<int>> zip;
};

struct User {
  ser20::Entry<Key<"id">, NumberValue<int>> id;
  ser20::Entry<Key<"name">, StringValue> name;
  ser20::Entry<Key<"address">, Address> address;
};

}  // namespace

TEST(EncodeJson, EncodesTopLevelElement) {
  NumberValue<int> value;
  value.value() = 42;

  std::string output;
  ser20::json::CompactWriter writer{output};

  auto result = ser20::encode_into(value, writer);

  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(output, "42");
}

TEST(EncodeJson, EncodesObjectSchema) {
  User user;
  user.id.value().value() = 7;
  user.name.value().value() = "Alice"sv;
  user.address.value().city.value().value() = "Tokyo"sv;
  user.address.value().zip.value().value() = 100;

  std::string output;
  ser20::json::CompactWriter writer{output};

  auto result = ser20::encode_into(user, writer);

  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(output,
            R"({"id":7,"name":"Alice","address":{"city":"Tokyo","zip":100}})");
}

TEST(EncodeJson, CanEncodeIntoExistingWriterStream) {
  User user;
  user.id.value().value() = 1;
  user.name.value().value() = "Bob"sv;
  user.address.value().city.value().value() = "Osaka"sv;
  user.address.value().zip.value().value() = 150;

  std::string output;
  ser20::json::CompactWriter writer{output};

  ASSERT_TRUE(writer.write({.length = 0, .type = TT::ArrayBegin, .context = 0})
                  .has_value());
  ASSERT_TRUE(ser20::Encoder<ser20::json::CompactWriter<>>::encode_to_writer(
                  user, writer)
                  .has_value());
  ASSERT_TRUE(writer.write({.length = 0, .type = TT::ArrayEnd, .context = 0})
                  .has_value());
  ASSERT_TRUE(
      writer.write({.length = 0, .type = TT::End, .context = 0}).has_value());

  EXPECT_EQ(output,
            R"([{"id":1,"name":"Bob","address":{"city":"Osaka","zip":150}}])");
}
