#include "hosq/pipeline/encode.hh"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <variant>

#include "hosq/encoder.hh"
#include "hosq/json/compact_writer.hh"
#include "hosq/pipeline/decode.hh"
#include "hosq/pipeline/match.hh"
#include "hosq/pipeline/scan.hh"
#include "hosq/pipeline/store.hh"
#include "hosq/string_literal.hh"

using namespace std::string_view_literals;
using namespace hosq;

namespace {

using TT = hosq::TokenType;
using JTC = hosq::JsonTokenContext;

template <class W>
auto write_scalar(W& writer, std::string_view content, JTC context)
    -> hosq::Result<void, hosq::Error> {
  auto buffer = writer.write(hosq::WriteRequest{
      .length = content.size(),
      .type = TT::Scalar,
      .context = static_cast<std::uint8_t>(context),
  });
  if (!buffer) {
    return hosq::unexpected(buffer.error());
  }
  std::copy(content.begin(), content.end(), buffer->data());
  return writer.commit(content.size());
}

struct WriteJsonString {
  template <class Context>
  auto operator()(std::string_view value, Context& ctx) const
      -> hosq::Result<void, hosq::Error> {
    return write_scalar(ctx.writer.get(), value, JTC::string);
  }
};

struct WriteJsonLiteral {
  template <class Context>
  auto operator()(std::string_view value, Context& ctx) const
      -> hosq::Result<void, hosq::Error> {
    return write_scalar(ctx.writer.get(), value, JTC::literal);
  }

  template <class Context>
  auto operator()(const std::string& value, Context& ctx) const
      -> hosq::Result<void, hosq::Error> {
    return write_scalar(ctx.writer.get(), value, JTC::literal);
  }
};

template <hosq::StringLiteral Name>
struct WriteJsonKey {
  template <class Context>
  auto operator()(std::monostate, Context& ctx) const
      -> hosq::Result<void, hosq::Error> {
    return write_scalar(ctx.writer.get(), Name.view(), JTC::string);
  }
};

template <hosq::StringLiteral Name>
using Key = hosq::Elem<hosq::noop_decoder, hosq::step<WriteJsonKey<Name>>>;

using StringValue =
    hosq::Elem<hosq::scan::string | hosq::decode::string_view |
                    hosq::store::once,
                hosq::encode::string_view | hosq::step<WriteJsonString>>;

template <class T>
using NumberValue =
    hosq::Elem<hosq::scan::literal | hosq::decode::number<T> |
                    hosq::store::once,
                hosq::encode::number<T> | hosq::step<WriteJsonLiteral>>;

struct Address {
  hosq::Entry<Key<"city">, StringValue> city;
  hosq::Entry<Key<"zip">, NumberValue<int>> zip;
};

struct User {
  hosq::Entry<Key<"id">, NumberValue<int>> id;
  hosq::Entry<Key<"name">, StringValue> name;
  hosq::Entry<Key<"address">, Address> address;
};

}  // namespace

TEST(EncodeJson, EncodesTopLevelElement) {
  NumberValue<int> value;
  value.value() = 42;

  std::string output;
  hosq::json::CompactWriter writer{output};

  auto result = hosq::encode_into(value, writer);

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
  hosq::json::CompactWriter writer{output};

  auto result = hosq::encode_into(user, writer);

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
  hosq::json::CompactWriter writer{output};

  ASSERT_TRUE(writer.write({.length = 0, .type = TT::ArrayBegin, .context = 0})
                  .has_value());
  ASSERT_TRUE(hosq::Encoder<hosq::json::CompactWriter<>>::encode_to_writer(
                  user, writer)
                  .has_value());
  ASSERT_TRUE(writer.write({.length = 0, .type = TT::ArrayEnd, .context = 0})
                  .has_value());
  ASSERT_TRUE(
      writer.write({.length = 0, .type = TT::End, .context = 0}).has_value());

  EXPECT_EQ(output,
            R"([{"id":1,"name":"Bob","address":{"city":"Osaka","zip":150}}])");
}
