#include <gtest/gtest.h>

#include <array>
#include <string>
#include <string_view>

#include "hosq/bson/reader.hh"
#include "hosq/bson/writer.hh"
#include "hosq/hosq.hh"
#include "hosq/json/fast_reader.hh"
#include "hosq/json/simple_reader.hh"
#include "hosq/msgpack/reader.hh"
#include "hosq/toml/reader.hh"

namespace {
using namespace hosq;

using BorrowedStringSource =
    Elem<scan::scalar | decode::string_source | store::assign, noop_encoder>;
using OwnedStringSource = Elem<scan::scalar | decode::string_source |
                                   construct<std::string> | store::assign,
                               noop_encoder>;
using JsonConvertedString =
    Elem<scan::scalar | decode::string_source | construct<std::string> |
             decode::json_unescape | store::assign,
         noop_encoder>;
using TomlConvertedString =
    Elem<scan::scalar | decode::string_source | construct<std::string> |
             decode::toml_unescape | store::assign,
         noop_encoder>;

template <class Value>
struct StringDocument {
  Entry<Key<"value">, Value> value;
};

template <class Reader>
void check_json_source_and_conversion() {
  const std::string input = R"("a\n\u0062")";
  BorrowedStringSource borrowed;
  ASSERT_TRUE(decode_into<Reader>(borrowed, input).is_ok());
  EXPECT_EQ(borrowed.value(), R"(a\n\u0062)");
  EXPECT_EQ(borrowed.value().data(), input.data() + 1);

  OwnedStringSource owned;
  ASSERT_TRUE(decode_into<Reader>(owned, input).is_ok());
  EXPECT_EQ(owned.value(), R"(a\n\u0062)");
  EXPECT_NE(owned.value().data(), borrowed.value().data());

  JsonConvertedString converted;
  ASSERT_TRUE(decode_into<Reader>(converted, input).is_ok());
  EXPECT_EQ(converted.value(), "a\nb");

  for (const auto invalid_category : {"42", "true", "null"}) {
    EXPECT_EQ(decode_into<Reader>(borrowed, invalid_category).code,
              ErrorCode::type_mismatch);
  }
}

TEST(StringPipeline, JsonSourceOwnershipAndConversionAreSeparate) {
  check_json_source_and_conversion<json::SimpleReader>();
  check_json_source_and_conversion<json::FastReader<>>();
}

TEST(StringPipeline, TomlSourceRetainsTheSyntaxNeededForConversion) {
  for (const std::string& literal :
       std::array<std::string, 4>{R"("a\nb")", R"('a\nb')",
                                  "\"\"\"\n\na\\nb\"\"\"", "'''\n\na\\nb'''"}) {
    const std::string input = "value = " + literal;
    StringDocument<BorrowedStringSource> borrowed;
    ASSERT_TRUE(decode_into<toml::Reader<>>(borrowed, input).is_ok());
    EXPECT_EQ(borrowed.value.value().value(), literal);
    EXPECT_EQ(borrowed.value.value().value().data(), input.data() + 8);
  }

  StringDocument<TomlConvertedString> basic;
  ASSERT_TRUE(decode_into<toml::Reader<>>(basic, R"(value = "a\nb")").is_ok());
  EXPECT_EQ(basic.value.value().value(), "a\nb");
  StringDocument<TomlConvertedString> literal;
  ASSERT_TRUE(decode_into<toml::Reader<>>(literal, R"(value = 'a\nb')").is_ok());
  EXPECT_EQ(literal.value.value().value(), R"(a\nb)");
}

TEST(StringPipeline, TomlConversionPreservesTheSecondInitialMultilineNewline) {
  StringDocument<TomlConvertedString> converted;
  ASSERT_TRUE(
      decode_into<toml::Reader<>>(converted, "value = \"\"\"\n\na\\nb\"\"\"")
          .is_ok());
  EXPECT_EQ(converted.value.value().value(), "\na\nb");
}

TEST(StringPipeline, JsonConversionIsANoopForMessagePackAndBson) {
  const std::string bytes = std::string(1, static_cast<char>(0xa2)) + R"(\q)";
  JsonConvertedString msgpack;
  ASSERT_TRUE(decode_into<msgpack::Reader<>>(msgpack, bytes).is_ok());
  EXPECT_EQ(msgpack.value(), R"(\q)");

  StringDocument<types::value::String> original;
  original.value.value().value() = R"(\q\u0061)";
  std::string bson_wire;
  bson::Writer writer(bson_wire);
  ASSERT_TRUE(encode_into(original, writer));
  StringDocument<JsonConvertedString> decoded;
  ASSERT_TRUE(decode_into<bson::Reader<>>(decoded, bson_wire).is_ok());
  EXPECT_EQ(decoded.value.value().value(), R"(\q\u0061)");
}

TEST(StringPipeline, FormatConversionsAreIndependent) {
  StringDocument<JsonConvertedString> toml_value;
  ASSERT_TRUE(
      decode_into<toml::Reader<>>(toml_value, R"(value = '\q')").is_ok());
  EXPECT_EQ(toml_value.value.value().value(), R"('\q')");

  TomlConvertedString json_value;
  ASSERT_TRUE(decode_into<json::SimpleReader>(json_value, R"("\n")").is_ok());
  EXPECT_EQ(json_value.value(), R"(\n)");
}

struct ReplaceJsonSource {
  auto operator()(std::string) const -> std::string { return R"(\u0062)"; }
};
struct ReplaceTomlSource {
  auto operator()(std::string) const -> std::string { return R"("changed\n")"; }
};
struct InvalidJsonSource {
  auto operator()(std::string) const -> std::string { return R"(\q)"; }
};

TEST(StringPipeline, ConversionsConsumeTheirPipelineInput) {
  Elem<scan::scalar | decode::string_source | construct<std::string> |
           step<ReplaceJsonSource> | decode::json_unescape | store::assign,
       noop_encoder>
      json_value;
  ASSERT_TRUE(
      decode_into<json::SimpleReader>(json_value, R"("original")").is_ok());
  EXPECT_EQ(json_value.value(), "b");

  using ReplacedToml =
      Elem<scan::scalar | decode::string_source | construct<std::string> |
               step<ReplaceTomlSource> | decode::toml_unescape | store::assign,
           noop_encoder>;
  StringDocument<ReplacedToml> toml_value;
  ASSERT_TRUE(
      decode_into<toml::Reader<>>(toml_value, R"(value = 'original')").is_ok());
  EXPECT_EQ(toml_value.value.value().value(), "changed\n");
}

TEST(StringPipeline, ConversionFailuresAreReportedAsReaderErrors) {
  Elem<scan::scalar | decode::string_source | construct<std::string> |
           step<InvalidJsonSource> | decode::json_unescape | store::assign,
       noop_encoder>
      value;
  EXPECT_EQ(decode_into<json::SimpleReader>(value, R"("valid")").code,
            ErrorCode::invalid_escape);
  EXPECT_EQ(decode_into<json::FastReader<>>(value, R"("valid")").code,
            ErrorCode::invalid_escape);
}

TEST(StringPipeline, Utf8ValidationIsAnExplicitStep) {
  const std::string invalid = std::string(1, static_cast<char>(0xa1)) +
                              std::string(1, static_cast<char>(0xc0));
  BorrowedStringSource borrowed;
  ASSERT_TRUE(decode_into<msgpack::Reader<>>(borrowed, invalid).is_ok());
  ASSERT_EQ(borrowed.value().size(), 1);
  EXPECT_EQ(static_cast<unsigned char>(borrowed.value()[0]), 0xc0);

  using Validated =
      Elem<scan::scalar | decode::string_source | validate::utf8 | store::assign,
           noop_encoder>;
  Validated checked;
  const auto error = decode_into<msgpack::Reader<>>(checked, invalid);
  EXPECT_EQ(error.code, ErrorCode::invalid_utf8);
  EXPECT_EQ(error.offset, 1);
}
}  // namespace
