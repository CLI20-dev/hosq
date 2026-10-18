#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "hosq/bson/reader.hh"
#include "hosq/bson/writer.hh"
#include "hosq/hosq.hh"
#include "hosq/json/compact_writer.hh"
#include "hosq/json/fast_reader.hh"
#include "hosq/json/simple_reader.hh"
#include "hosq/msgpack/reader.hh"
#include "hosq/msgpack/writer.hh"
#include "hosq/toml/reader.hh"
#include "hosq/toml/writer.hh"

namespace {
namespace t = hosq::types;
namespace v = hosq::types::value;
enum class Mode : std::int8_t {
  stopped = -128,
  running = 127,
  running_alias = running,
  escaped = 42,
  empty = 0,
};
using Stopped = hosq::EnumName<Mode::stopped, "stopped", "old-stop">;
using Running = hosq::EnumName<Mode::running, "running", "old-run", "RUN">;
using Escaped = hosq::EnumName<Mode::escaped, "line\n\"\\日", "old\n\"\\日">;
using Empty = hosq::EnumName<Mode::empty, "", "empty">;
using Modes = hosq::EnumMapping<Stopped, Running, Escaped, Empty>;

template <class... Names>
concept ValidMapping = requires { typename hosq::EnumMapping<Names...>; };
template <auto Value>
concept ValidName = requires { typename hosq::EnumName<Value, "value">; };
template <class Mapping>
concept EnumValue = requires { typename v::Enum<Mapping>; };
enum class Other : std::uint64_t { maximum = UINT64_MAX };
using OtherName = hosq::EnumName<Other::maximum, "max">;
struct FakeMapping : Modes {};

static_assert(ValidMapping<Stopped, Running>);
static_assert(!ValidMapping<>);
static_assert(!ValidMapping<int>);
static_assert(!ValidMapping<Stopped, OtherName>);
static_assert(!ValidMapping<Running, Running>);
static_assert(!ValidMapping<
              Running, hosq::EnumName<Mode::running_alias, "second-output">>);
static_assert(!ValidMapping<Stopped, hosq::EnumName<Mode::running, "stopped">>);
static_assert(!ValidMapping<
              Stopped, hosq::EnumName<Mode::running, "running", "old-stop">>);
static_assert(!ValidMapping<hosq::EnumName<Mode::running, "run", "run">>);
static_assert(!ValidMapping<hosq::EnumName<Mode::running, "run", "a", "a">>);
static_assert(!ValidMapping<Empty, hosq::EnumName<Mode::running, "">>);
static_assert(!ValidName<1>);
static_assert(ValidName<Mode::running>);
static_assert(!EnumValue<int>);
static_assert(!EnumValue<FakeMapping>);
static_assert(EnumValue<Modes>);
static_assert(std::same_as<v::Enum<Modes>::storage_type, Mode>);
static_assert(std::same_as<t::Enum<"mode", Modes>,
                           hosq::Entry<hosq::Key<"mode">, v::Enum<Modes>>>);
static_assert(std::same_as<v::Enum<Modes>,
                           hosq::Elem<hosq::scan::scalar | hosq::decode::text |
                                          hosq::decode::enum_value<Modes> |
                                          hosq::store::assign,
                                      hosq::encode::enum_value<Modes>>>);
static_assert(Modes::from_string("old-run") == Mode::running);
static_assert(Modes::to_string(Mode::running) == "running");
static_assert(!Modes::from_string("unknown"));
static_assert(!Modes::to_string(static_cast<Mode>(1)));

struct State {
  t::Enum<"mode", Modes> mode;
  t::Int32<"tail"> tail;
};
struct TextState {
  t::Text<"mode"> mode;
  t::Int32<"tail"> tail;
};

template <class Writer, class Value>
auto encode(const Value& value) -> std::string {
  std::string output;
  Writer writer(output);
  EXPECT_TRUE(hosq::encode_into(value, writer)) << writer.error().detail;
  return output;
}

template <class Reader, class Writer>
void round_trip() {
  hosq::Decoder<Reader> decoder;
  v::Record<State> value;
  for (const auto& [name, expected] :
       {std::pair<std::string_view, Mode>{"old-stop", Mode::stopped},
        {"old-run", Mode::running},
        {"RUN", Mode::running},
        {"old\n\"\\日", Mode::escaped},
        {"", Mode::empty}}) {
    v::Record<TextState> source;
    source.value().mode.value().value() = name;
    source.value().tail.value().value() = 72;
    std::string input = encode<Writer>(source);
    ASSERT_TRUE(decoder.decode_document(value, input).is_ok());
    EXPECT_EQ(value.value().mode.value().value(), expected);
    EXPECT_EQ(value.value().tail.value().value(), 72);
    input.assign(input.size(), 'x');
    const auto output = encode<Writer>(value);
    v::Record<TextState> canonical;
    ASSERT_TRUE(hosq::decode_document_into<Reader>(canonical, output).is_ok());
    EXPECT_EQ(canonical.value().mode.value().value(),
              *Modes::to_string(expected));
    EXPECT_EQ(canonical.value().tail.value().value(), 72);
  }
}

TEST(EnumMapping, CanonicalOutputAndLegacyInputAcrossFourFormats) {
  round_trip<hosq::json::SimpleReader, hosq::json::CompactWriter<>>();
  round_trip<hosq::json::FastReader<>, hosq::json::CompactWriter<>>();
  round_trip<hosq::bson::Reader<>, hosq::bson::Writer<>>();
  round_trip<hosq::msgpack::Reader<>, hosq::msgpack::Writer<>>();
  round_trip<hosq::toml::Reader<>, hosq::toml::Writer<>>();
}

TEST(EnumMapping, JsonAndTomlResolveEscapesBeforeExactMatching) {
  v::Enum<Modes> value;
  ASSERT_TRUE(hosq::decode_document_into<hosq::json::SimpleReader>(
                  value, R"("old\u002drun")")
                  .is_ok());
  EXPECT_EQ(value.value(), Mode::running);
  ASSERT_TRUE(hosq::decode_document_into<hosq::json::FastReader<>>(
                  value, R"("old\n\"\\\u65e5")")
                  .is_ok());
  EXPECT_EQ(value.value(), Mode::escaped);
  EXPECT_EQ(encode<hosq::json::CompactWriter<>>(value), "\"line\\n\\\"\\\\日\"");
  v::Record<State> document;
  ASSERT_TRUE(hosq::decode_document_into<hosq::toml::Reader<>>(
                  document, "mode = \"old\\u002drun\"\ntail = 1\n")
                  .is_ok());
  EXPECT_EQ(document.value().mode.value().value(), Mode::running);
}

template <class Reader, class Writer>
void unknown_name_and_reuse() {
  v::Record<TextState> source;
  source.value().tail.value().value() = 9;
  hosq::Decoder<Reader> decoder;
  v::Record<State> value;
  for (std::string_view name :
       {"RUNNING", "run", "running-extra", " running", "running ", "unknown"}) {
    source.value().mode.value().value() = name;
    const auto input = encode<Writer>(source);
    const auto error = decoder.decode_document(value, input);
    EXPECT_EQ(error.code, hosq::ErrorCode::invalid_choice);
    EXPECT_LT(error.offset, input.size());
    source.value().mode.value().value() = "old-run";
    ASSERT_TRUE(decoder.decode_document(value, encode<Writer>(source)).is_ok());
    EXPECT_EQ(value.value().mode.value().value(), Mode::running);
    EXPECT_EQ(value.value().tail.value().value(), 9);
  }
}

TEST(EnumMapping, UnknownNamesRejectAndDecoderCanBeReused) {
  unknown_name_and_reuse<hosq::json::SimpleReader,
                         hosq::json::CompactWriter<>>();
  unknown_name_and_reuse<hosq::json::FastReader<>,
                         hosq::json::CompactWriter<>>();
  unknown_name_and_reuse<hosq::bson::Reader<>, hosq::bson::Writer<>>();
  unknown_name_and_reuse<hosq::msgpack::Reader<>, hosq::msgpack::Writer<>>();
  unknown_name_and_reuse<hosq::toml::Reader<>, hosq::toml::Writer<>>();
}

template <class Reader, class Writer, class Scalar>
void wrong_kind(typename Scalar::storage_type wire_value) {
  struct Wrong {
    t::Field<"mode", Scalar> mode;
    t::Int32<"tail"> tail;
  };
  v::Record<Wrong> source;
  source.value().mode.value().value() = wire_value;
  v::Record<State> value;
  EXPECT_EQ(
      hosq::decode_document_into<Reader>(value, encode<Writer>(source)).code,
      hosq::ErrorCode::type_mismatch);
}

template <class Reader, class Writer>
void wrong_kinds() {
  wrong_kind<Reader, Writer, v::Int32>(127);
  wrong_kind<Reader, Writer, v::Bool>(true);
  wrong_kind<Reader, Writer, v::Array<v::Text>>({});
  wrong_kind<Reader, Writer, v::Mapping<v::Text, v::Text>>({});
}

TEST(EnumMapping, WrongWireKindsDoNotBecomeNames) {
  wrong_kinds<hosq::json::SimpleReader, hosq::json::CompactWriter<>>();
  wrong_kinds<hosq::json::FastReader<>, hosq::json::CompactWriter<>>();
  wrong_kinds<hosq::bson::Reader<>, hosq::bson::Writer<>>();
  wrong_kinds<hosq::msgpack::Reader<>, hosq::msgpack::Writer<>>();
  wrong_kinds<hosq::toml::Reader<>, hosq::toml::Writer<>>();
  wrong_kind<hosq::json::SimpleReader, hosq::json::CompactWriter<>, v::Null>(
      nullptr);
  wrong_kind<hosq::bson::Reader<>, hosq::bson::Writer<>, v::Null>(nullptr);
  wrong_kind<hosq::msgpack::Reader<>, hosq::msgpack::Writer<>, v::Null>(nullptr);
  wrong_kind<hosq::bson::Reader<>, hosq::bson::Writer<>, v::Bytes>({});
  wrong_kind<hosq::msgpack::Reader<>, hosq::msgpack::Writer<>, v::Bytes>({});
  v::Record<State> value;
  EXPECT_EQ(hosq::decode_document_into<hosq::toml::Reader<>>(
                value, "mode = 1979-05-27\ntail = 1\n")
                .code,
            hosq::ErrorCode::type_mismatch);
}

TEST(EnumMapping, InvalidMutableEnumCannotEncode) {
  v::Enum<Modes> value;
  value.value() = static_cast<Mode>(1);
  auto check_scalar = [&]<class Writer>() {
    std::string output;
    Writer writer(output);
    EXPECT_FALSE(hosq::encode_into(value, writer));
    EXPECT_TRUE(writer.error().has_error());
    EXPECT_TRUE(output.empty());
  };
  check_scalar.template operator()<hosq::json::CompactWriter<>>();
  check_scalar.template operator()<hosq::msgpack::Writer<>>();
  auto check_document = [&]<class Writer>() {
    v::Record<State> document;
    document.value().mode.value().value() = static_cast<Mode>(1);
    std::string output;
    Writer writer(output);
    EXPECT_FALSE(hosq::encode_into(document, writer));
    EXPECT_EQ(writer.error().code, hosq::ErrorCode::invalid_object_state);
  };
  check_document.template operator()<hosq::bson::Writer<>>();
  check_document.template operator()<hosq::msgpack::Writer<>>();
  check_document.template operator()<hosq::toml::Writer<>>();
}

TEST(EnumMapping, FailedScalarDecodeKeepsPriorEnumAndFreshInputWorks) {
  hosq::Decoder<hosq::json::SimpleReader> decoder;
  v::Enum<Modes> value;
  value.value() = Mode::running;
  for (std::string_view input : {R"("missing")", "127", "null", "[]", "{}",
                                 R"("bad\q")", R"("running\u0000")"}) {
    EXPECT_TRUE(decoder.decode_document(value, input).has_error());
    EXPECT_EQ(value.value(), Mode::running);
  }
  ASSERT_TRUE(decoder.decode_document(value, R"("old-stop")").is_ok());
  EXPECT_EQ(value.value(), Mode::stopped);
  EXPECT_EQ(decoder.decode_document(value, R"("running" "stopped")").code,
            hosq::ErrorCode::trailing_data);
  ASSERT_TRUE(decoder.decode_document(value, R"("old-stop")").is_ok());
}

TEST(EnumMapping, ExplicitTextNamesSupportNullBytesAndUnsignedEnumBoundary) {
  using BinaryNames = hosq::EnumMapping<
      hosq::EnumName<Other::maximum, "max\0value", "old\0max">>;
  static_assert(std::same_as<v::Enum<BinaryNames>::storage_type, Other>);
  static_assert(BinaryNames::from_string(std::string_view("old\0max", 7)) ==
                Other::maximum);
  struct Unsigned {
    t::Enum<"mode", BinaryNames> mode;
  };
  v::Record<Unsigned> value;
  value.value().mode.value().value() = Other::maximum;
  auto check = [&]<class Reader, class Writer>() {
    const auto output = encode<Writer>(value);
    v::Record<Unsigned> decoded;
    ASSERT_TRUE(hosq::decode_document_into<Reader>(decoded, output).is_ok());
    EXPECT_EQ(decoded.value().mode.value().value(), Other::maximum);
  };
  check.template
  operator()<hosq::json::SimpleReader, hosq::json::CompactWriter<>>();
  check.template operator()<hosq::bson::Reader<>, hosq::bson::Writer<>>();
  check.template operator()<hosq::msgpack::Reader<>, hosq::msgpack::Writer<>>();
  check.template operator()<hosq::toml::Reader<>, hosq::toml::Writer<>>();
}

TEST(EnumMapping, ExistingContainersAndStorePipelinesReuseEnumStorage) {
  v::Array<v::Enum<Modes>> values;
  ASSERT_TRUE(hosq::decode_document_into<hosq::json::SimpleReader>(
                  values, R"(["old-run","old-stop",""])")
                  .is_ok());
  EXPECT_EQ(values.value(),
            (std::vector<Mode>{Mode::running, Mode::stopped, Mode::empty}));
  EXPECT_EQ(encode<hosq::msgpack::Writer<>>(values),
            std::string("\x93\xa7running\xa7stopped\xa0", 18));
  using Once =
      hosq::Elem<hosq::scan::scalar | hosq::decode::text |
                     hosq::decode::enum_value<Modes> | hosq::store::once,
                 hosq::encode::identity | hosq::encode::value>;
  static_assert(std::same_as<Once::storage_type, std::optional<Mode>>);
  Once once;
  EXPECT_TRUE(
      hosq::decode_into<hosq::json::SimpleReader>(once, R"("old-run")").is_ok());
  ASSERT_TRUE(once.value());
  EXPECT_EQ(*once.value(), Mode::running);
  EXPECT_EQ(
      hosq::decode_into<hosq::json::SimpleReader>(once, R"("stopped")").code,
      hosq::ErrorCode::duplicate_member);

  struct OptionalMode {
    t::Optional<"mode", v::Enum<Modes>> mode;
  };
  v::Record<OptionalMode> optional;
  ASSERT_TRUE(hosq::decode_document_into<hosq::json::SimpleReader>(
                  optional, R"({"mode":"old-run"})")
                  .is_ok());
  ASSERT_TRUE(optional.value().mode.value().value());
  EXPECT_EQ(*optional.value().mode.value().value(), Mode::running);
  EXPECT_EQ(encode<hosq::json::CompactWriter<>>(optional),
            R"({"mode":"running"})");
  ASSERT_TRUE(
      hosq::decode_document_into<hosq::json::SimpleReader>(optional, "{}")
          .is_ok());
  EXPECT_FALSE(optional.value().mode.value().value());
  EXPECT_EQ(encode<hosq::json::CompactWriter<>>(optional), "{}");
}
}  // namespace
