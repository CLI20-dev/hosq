#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <variant>

#include "hosq/hosq.hh"
#include "hosq/json/compact_writer.hh"

using namespace std::string_view_literals;
using namespace hosq;

namespace {

using TT = TokenType;
using JTC = JsonTokenContext;

template <class W>
auto write_scalar(W& writer, std::string_view content, JTC context) -> bool {
  auto buffer = writer.write(WriteRequest{
      .length = content.size(),
      .type = TT::Scalar,
      .context = static_cast<std::uint8_t>(context),
  });
  if (writer.error().has_error()) return false;
  std::copy(content.begin(), content.end(), buffer.data());
  return writer.commit(content.size());
}

struct WriteJsonString {
  template <class Context>
  auto operator()(std::string_view value, Context& ctx) const -> void {
    write_scalar(ctx.writer(), value, JTC::string);
  }
};

struct WriteJsonLiteral {
  template <class Context>
  auto operator()(std::string_view value, Context& ctx) const -> void {
    write_scalar(ctx.writer(), value, JTC::literal);
  }

  template <class Context>
  auto operator()(const std::string& value, Context& ctx) const -> void {
    write_scalar(ctx.writer(), value, JTC::literal);
  }
};

template <StringLiteral Name>
struct WriteJsonKey {
  template <class Context>
  auto operator()(std::monostate, Context& ctx) const -> void {
    write_scalar(ctx.writer(), Name.view(), JTC::string);
  }
};

template <StringLiteral Name>
using Key = Elem<noop_decoder, step<WriteJsonKey<Name>>>;

using StringValue = Elem<scan::scalar | decode::string_view | store::once,
                         encode::string_view | step<WriteJsonString>>;

template <class T>
using NumberValue = Elem<scan::scalar | decode::number<T> | store::once,
                         encode::number<T> | step<WriteJsonLiteral>>;

struct User {
  Entry<Key<"id">, NumberValue<int>> id;
  Entry<Key<"name">, StringValue> name;
  Entry<Key<"age">, NumberValue<int>> age;
};

}  // namespace

TEST(EncodeJson, EncodesTopLevelElement) {
  NumberValue<int> value;
  value.value() = 42;

  std::string output;
  json::CompactWriter writer{output};

  ASSERT_TRUE(encode_into(value, writer));
  EXPECT_EQ(output, "42");
}

TEST(EncodeJson, EncodesObjectSchema) {
  User user;
  user.id.value().value() = 7;
  user.name.value().value() = "Alice"sv;
  user.age.value().value() = 30;

  std::string output;
  json::CompactWriter writer{output};

  ASSERT_TRUE(encode_into(user, writer));
  EXPECT_EQ(output, R"({"id":7,"name":"Alice","age":30})");
}

TEST(EncodeJson, CanEncodeIntoExistingWriterStream) {
  User user;
  user.id.value().value() = 1;
  user.name.value().value() = "Bob"sv;
  user.age.value().value() = 41;

  std::string output;
  json::CompactWriter writer{output};

  (void)writer.write({.length = 0, .type = TT::ArrayBegin, .context = 0});
  ASSERT_FALSE(writer.error().has_error());
  ASSERT_TRUE(Encoder<json::CompactWriter<>>::encode_to_writer(user, writer));
  (void)writer.write({.length = 0, .type = TT::ArrayEnd, .context = 0});
  ASSERT_FALSE(writer.error().has_error());
  (void)writer.write({.length = 0, .type = TT::End, .context = 0});
  ASSERT_FALSE(writer.error().has_error());

  EXPECT_EQ(output, R"([{"id":1,"name":"Bob","age":41}])");
}
