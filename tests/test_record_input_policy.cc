#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <tuple>
#include <type_traits>

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
using namespace hosq;
namespace t = hosq::types;
namespace v = t::value;
using EC = ErrorCode;

template <class R = json::SimpleReader, class E>
auto read(E& value, std::string_view input) -> Error {
  Decoder<R> decoder;
  return decoder.decode_document(value, input);
}
template <class W = json::CompactWriter<>, class E>
auto write(const E& value, Error* error = nullptr) -> std::string {
  std::string output;
  W writer(output);
  const bool ok = encode_into(value, writer);
  if (error)
    *error = writer.error();
  else
    EXPECT_TRUE(ok) << writer.error().detail;
  return output;
}

using Name = t::Field<"name", v::Text, t::InputAliases<"old_name", "legacy">>;
using Count = t::Field<"count", v::Uint8, t::Default<[] { return 7; }>>;
struct Schema {
  Name name;
  Count count;
  t::Try<t::Field<"note", v::Optional<v::Text>, t::InputAliases<"old_note">>>
      note;
};
using Record = v::Record<Schema>;
using OpenRecord = v::Record<Schema, t::UnknownMembers::ignore>;
static_assert(std::same_as<t::Field<"x", v::Int32>, Entry<Key<"x">, v::Int32>>);
static_assert(detail::ConstKeyEntryT<Name> && detail::EntryT<Name>);
static_assert(detail::TryT<t::Try<Name>>);
static_assert(std::same_as<v::Record<Schema>, v::Object<Schema>>);
static_assert(std::same_as<t::Record<"x", Schema, t::UnknownMembers::ignore>,
                           t::Object<"x", Schema, t::UnknownMembers::ignore>>);
static_assert(Name::key().view() == "name");
static_assert(!std::is_aggregate_v<Name>);

template <detail::ElemT E>
auto deduced_field(const t::Field<"x", E>&) -> std::type_identity<E>;
static_assert(std::same_as<decltype(deduced_field(
                               std::declval<const t::Field<"x", v::Int32>&>())),
                           std::type_identity<v::Int32>>);

using SameField = std::tuple<t::Field<"x", v::Text, t::InputAliases<"x">>>;
using RepeatedAlias =
    std::tuple<t::Field<"x", v::Text, t::InputAliases<"old", "old">>>;
using AliasCanonical = std::tuple<Name, t::Field<"old_name", v::Text>>;
using AliasAlias =
    std::tuple<Name, t::Field<"other", v::Text, t::InputAliases<"legacy">>>;
static_assert(!detail::unique_record_input_names<SameField>());
static_assert(!detail::unique_record_input_names<RepeatedAlias>());
static_assert(!detail::unique_record_input_names<AliasCanonical>());
static_assert(!detail::unique_record_input_names<AliasAlias>());
static_assert(detail::unique_record_input_names<std::tuple<Name, Count>>());

TEST(RecordInputPolicy, MissingDefaultAndLegacyInputEncodeCanonicalKeys) {
  Record value;
  ASSERT_TRUE(read(value, R"({"legacy":"Ada","old_note":"hi"})").is_ok());
  EXPECT_EQ(value.value().name.value().value(), "Ada");
  EXPECT_EQ(value.value().count.value().value(), 7);
  ASSERT_TRUE(value.value().note.value().value());
  EXPECT_EQ(*value.value().note.value().value(), "hi");
  EXPECT_EQ(write(value), R"({"name":"Ada","count":7,"note":"hi"})");
  ASSERT_TRUE(read(value, R"({"name":"","count":0})").is_ok());
  EXPECT_EQ(value.value().count.value().value(), 0);
  EXPECT_FALSE(value.value().note.value().value());
  EXPECT_EQ(write(value), R"({"name":"","count":0})");
  ASSERT_TRUE(read(value, R"({"old_name":"x","count":255})").is_ok());
  EXPECT_EQ(value.value().count.value().value(), 255);
}

TEST(RecordInputPolicy, ExplicitDefaultOverridesFreshCppInitializerWhenMissing) {
  struct S {
    Count count = [] {
      Count field;
      field.value().value() = 99;
      return field;
    }();
  };
  v::Record<S> value;
  ASSERT_TRUE(read(value, "{}").is_ok());
  EXPECT_EQ(value.value().count.value().value(), 7);
  ASSERT_TRUE(read(value, R"({"count":3})").is_ok());
  EXPECT_EQ(value.value().count.value().value(), 3);
}

TEST(RecordInputPolicy, PresentNullAndInvalidValuesNeverUseDefault) {
  Record value;
  for (auto input :
       {R"({"name":"x","count":null})", R"({"name":"x","count":"7"})",
        R"({"name":"x","count":false})", R"({"name":"x","count":[]})",
        R"({"name":"x","count":{}})"})
    EXPECT_EQ(read(value, input).code, EC::type_mismatch) << input;
  for (auto input : {R"({"name":"x","count":256})", R"({"name":"x","count":-1})",
                     R"({"name":"x","count":1.5})"})
    EXPECT_FALSE(read(value, input).is_ok()) << input;
  for (auto input : {R"({"name":"x","count":})", R"({"name":"x","count":)",
                     R"({"name":"x",)", R"({"name":"x" bad})"})
    EXPECT_FALSE(read(value, input).is_ok()) << input;
  EXPECT_EQ(read(value, "{}").code, EC::missing_required);
  EXPECT_EQ(read(value, R"({"name":null})").code, EC::type_mismatch);
  EXPECT_EQ(read(value, R"({"name":"x","old_note":null})").code,
            EC::type_mismatch);
}

int default_calls = 0;
using Factory = t::Default<[] {
  ++default_calls;
  return std::string("fresh");
}>;
TEST(RecordInputPolicy, FactoryRunsOnlyForMissingAndDecoderReuseStartsFresh) {
  struct S {
    t::Try<t::Field<"x", v::Text, Factory, t::InputAliases<"old">>> x;
  };
  v::Record<S> value;
  Decoder<json::SimpleReader> decoder;
  default_calls = 0;
  ASSERT_TRUE(decoder.decode_document(value, R"({"old":"present"})").is_ok());
  EXPECT_EQ(default_calls, 0);
  EXPECT_EQ(value.value().x.value().value(), "present");
  ASSERT_TRUE(decoder.decode_document(value, "{}").is_ok());
  EXPECT_EQ(default_calls, 1);
  EXPECT_EQ(value.value().x.value().value(), "fresh");
  EXPECT_FALSE(decoder.decode_document(value, R"({"x":null})").is_ok());
  EXPECT_EQ(default_calls, 1);
  ASSERT_TRUE(decoder.decode_document(value, "{}").is_ok());
  EXPECT_EQ(default_calls, 2);
  EXPECT_EQ(write(value), R"({"x":"fresh"})");
}

TEST(RecordInputPolicy, CppInitializersStillRequirePresenceWithoutTryOrDefault) {
  struct S {
    t::Int32<"x"> x = [] {
      t::Int32<"x"> field;
      field.value().value() = 42;
      return field;
    }();
  };
  v::Record<S> required;
  EXPECT_EQ(read(required, "{}").code, EC::missing_required);
  struct O {
    t::Try<t::Int32<"x">> x = [] {
      t::Try<t::Int32<"x">> field;
      field.value().value() = 42;
      return field;
    }();
  };
  v::Record<O> optional;
  ASSERT_TRUE(read(optional, "{}").is_ok());
  EXPECT_EQ(optional.value().x.value().value(), 42);
  EXPECT_EQ(write(optional), R"({"x":42})");
}

template <class R>
void duplicate_names() {
  Record value;
  for (auto input :
       {R"({"name":"a","old_name":"b"})", R"({"old_name":"a","name":"b"})",
        R"({"legacy":"a","old_name":"b"})", R"({"name":"a","name":"b"})",
        R"({"old_name":"a","old_name":"b"})",
        R"({"name":"a","old_\u006eame":"b"})",
        R"({"old_name":"a","\u006cegacy":"b"})",
        R"({"name":"a","note":"n","old_note":"m"})"}) {
    EXPECT_EQ(read<R>(value, input).code, EC::duplicate_member) << input;
  }
}
TEST(RecordInputPolicy, DuplicateLogicalFieldsAndEscapedAliases) {
  duplicate_names<json::SimpleReader>();
  duplicate_names<json::FastReader<>>();
}

TEST(RecordInputPolicy, StrictDefaultAndExplicitUnknownIgnore) {
  Record strict;
  EXPECT_EQ(read(strict, R"({"name":"a","extra":1})").code, EC::unknown_member);
  OpenRecord open;
  for (auto input :
       {R"({"unknown":null,"old_name":"a"})",
        R"({"name":"a","unknown":[{"deep":[true,{},[1]]}],"count":8})",
        R"({"unknown":{"nested":[{},{}]},"name":"a","other":false})"}) {
    ASSERT_TRUE(read(open, input).is_ok()) << input;
    EXPECT_EQ(open.value().name.value().value(), "a");
    EXPECT_EQ(write(open).find("unknown"), std::string::npos);
  }
  EXPECT_EQ(read(open, R"({"unknown":true})").code, EC::missing_required);
  EXPECT_EQ(read(open, R"({"name":"a","old_name":"b","extra":1})").code,
            EC::duplicate_member);
  for (auto input : {R"({"name":"a","unknown":[})", R"({"name":"a","unknown":})",
                     R"({"name":"a","unknown":{"x":)"})
    EXPECT_FALSE(read(open, input).is_ok()) << input;
  v::Record<std::tuple<>, t::UnknownMembers::ignore> empty;
  ASSERT_TRUE(read(empty, R"({"anything":[{"x":1}]})").is_ok());
  EXPECT_EQ(write(empty), "{}");
}

TEST(RecordInputPolicy, FastReaderSkipsNestedUnknownValues) {
  OpenRecord value;
  ASSERT_TRUE(
      read<json::FastReader<>>(
          value,
          R"({"unknown":{"x":[{},[true,null]]},"old_name":"a","tail":[]})")
          .is_ok());
  EXPECT_EQ(write(value), R"({"name":"a","count":7})");
}

template <class R>
void skip_errors() {
  struct S {
    t::Field<"x", v::Text, Factory> x;
  };
  v::Record<S, t::UnknownMembers::ignore> value;
  Decoder<R> decoder;
  default_calls = 0;
  for (auto input : {R"({"unknown":[)", R"({"unknown":{"x":[1])",
                     R"({"unknown":{"x":{"y":[]})"}) {
    EXPECT_FALSE(decoder.decode_document(value, input).is_ok()) << input;
    EXPECT_EQ(default_calls, 0);
  }
  ASSERT_TRUE(decoder.decode_document(value, "{}").is_ok());
  EXPECT_EQ(default_calls, 1);
  EXPECT_EQ(value.value().x.value().value(), "fresh");
}
TEST(RecordInputPolicy, StructuralSkipErrorsPropagateBeforeMissingDefaults) {
  skip_errors<json::SimpleReader>();
  skip_errors<json::FastReader<>>();
}

TEST(RecordInputPolicy, UnknownIgnoreIsLocalToEachRecord) {
  struct Outer {
    t::Record<"child", Schema> child;
  };
  v::Record<Outer, t::UnknownMembers::ignore> value;
  ASSERT_TRUE(read(value, R"({"outer":[],"child":{"old_name":"a"}})").is_ok());
  EXPECT_EQ(read(value, R"({"child":{"name":"a","inner":1}})").code,
            EC::unknown_member);
}

TEST(RecordInputPolicy, OptionalStorageRetainsPresenceRules) {
  using RequiredOptional =
      t::Field<"x", v::Optional<v::Text>, t::InputAliases<"old">>;
  v::Record<std::tuple<RequiredOptional>> required;
  EXPECT_EQ(read(required, "{}").code, EC::missing_required);
  EXPECT_EQ(read(required, R"({"old":null})").code, EC::type_mismatch);
  ASSERT_TRUE(read(required, R"({"old":""})").is_ok());
  EXPECT_EQ(write(required), R"({"x":""})");
  using MissingOptional =
      t::Field<"x", v::Optional<v::Text>,
               t::Default<[] { return std::optional<std::string>{}; }>>;
  v::Record<std::tuple<MissingOptional>> empty_required;
  ASSERT_TRUE(read(empty_required, "{}").is_ok());
  Error error;
  (void)write<msgpack::Writer<>>(empty_required, &error);
  EXPECT_EQ(error.code, EC::invalid_object_state);
  v::Record<std::tuple<t::Try<MissingOptional>>> empty_optional;
  ASSERT_TRUE(read(empty_optional, "{}").is_ok());
  EXPECT_EQ(write(empty_optional), "{}");
  using PresentOptional =
      t::Field<"x", v::Optional<v::Text>, t::Default<[] {
                 return std::optional<std::string>{"fallback"};
               }>>;
  v::Record<std::tuple<t::Try<PresentOptional>>> present;
  ASSERT_TRUE(read(present, "{}").is_ok());
  EXPECT_EQ(write(present), R"({"x":"fallback"})");
}

TEST(RecordInputPolicy, ExplicitNullRemainsPresentWhenElementAcceptsNull) {
  using Field =
      t::Field<"x", v::Optional<v::Null>,
               t::Default<[] { return std::optional<std::nullptr_t>{}; }>>;
  v::Record<std::tuple<t::Try<Field>>> value;
  ASSERT_TRUE(read(value, "{}").is_ok());
  EXPECT_FALSE(std::get<0>(value.value()).value().value());
  EXPECT_EQ(write(value), "{}");
  ASSERT_TRUE(read(value, R"({"x":null})").is_ok());
  EXPECT_TRUE(std::get<0>(value.value()).value().value());
  EXPECT_EQ(write(value), R"({"x":null})");
}

TEST(RecordInputPolicy, CandidateFailureNeverUsesMissingDefault) {
  using One = Elem<scan::scalar | decode::integer<int> | match::literal<1> |
                       store::assign,
                   encode::value>;
  using F =
      t::Field<"x", One, t::InputAliases<"old">, t::Default<[] { return 1; }>>;
  v::Record<std::tuple<F>> value;
  ASSERT_TRUE(read(value, "{}").is_ok());
  EXPECT_EQ(read(value, R"({"old":2})").code, EC::candidate_rejected);
}

TEST(RecordInputPolicy, EmptyCanonicalAndAliasNamesAreValid) {
  using F = t::Field<"", v::Text, t::InputAliases<"old">>;
  v::Record<std::tuple<F>> canonical_empty;
  ASSERT_TRUE(read(canonical_empty, R"({"old":"x"})").is_ok());
  EXPECT_EQ(write(canonical_empty), R"({"":"x"})");
  using G = t::Field<"x", v::Text, t::InputAliases<"">>;
  v::Record<std::tuple<G>> alias_empty;
  ASSERT_TRUE(read(alias_empty, R"({"":"v"})").is_ok());
  EXPECT_EQ(write(alias_empty), R"({"x":"v"})");
  EXPECT_EQ(read(alias_empty, R"({"":"v","x":"v"})").code, EC::duplicate_member);
}

template <class W, class R>
void codec_roundtrip(const Record& source) {
  const auto bytes = write<W>(source);
  Record decoded;
  ASSERT_TRUE(read<R>(decoded, bytes).is_ok());
  EXPECT_EQ(write(decoded), R"({"name":"Ada","count":7,"note":"hi"})");
  OpenRecord open;
  ASSERT_TRUE(read<R>(open, bytes).is_ok());
  EXPECT_EQ(write(open), write(decoded));
}
TEST(RecordInputPolicy, SameAndCrossCodecRoundtripUsesCanonicalOutput) {
  Record source;
  ASSERT_TRUE(read(source, R"({"legacy":"Ada","old_note":"hi"})").is_ok());
  codec_roundtrip<json::CompactWriter<>, json::SimpleReader>(source);
  codec_roundtrip<bson::Writer<>, bson::Reader<>>(source);
  codec_roundtrip<msgpack::Writer<>, msgpack::Reader<>>(source);
  codec_roundtrip<toml::Writer<>, toml::Reader<>>(source);
}

template <class W, class R>
void read_legacy_codec() {
  struct Legacy {
    t::Text<"legacy"> name;
    t::Int32<"unknown"> extra;
  };
  v::Record<Legacy> old;
  old.value().name.value().value() = "Ada";
  old.value().extra.value().value() = 5;
  const auto bytes = write<W>(old);
  OpenRecord value;
  ASSERT_TRUE(read<R>(value, bytes).is_ok());
  EXPECT_EQ(write(value), R"({"name":"Ada","count":7})");
  Record strict;
  EXPECT_EQ(read<R>(strict, bytes).code, EC::unknown_member);
}
TEST(RecordInputPolicy, LegacyAliasesDefaultsAndUnknownIgnoreAcrossCodecs) {
  read_legacy_codec<json::CompactWriter<>, json::SimpleReader>();
  read_legacy_codec<bson::Writer<>, bson::Reader<>>();
  read_legacy_codec<msgpack::Writer<>, msgpack::Reader<>>();
  read_legacy_codec<toml::Writer<>, toml::Reader<>>();
}

template <class W, class R>
void binary_duplicates() {
  struct S {
    t::Text<"old_name"> first;
    t::Text<"legacy"> second;
  };
  v::Record<S> source;
  source.value().first.value().value() = "a";
  source.value().second.value().value() = "b";
  Record value;
  EXPECT_EQ(read<R>(value, write<W>(source)).code, EC::duplicate_member);
}
TEST(RecordInputPolicy, BinaryCodecsRejectTwoAliasesForOneField) {
  binary_duplicates<bson::Writer<>, bson::Reader<>>();
  binary_duplicates<msgpack::Writer<>, msgpack::Reader<>>();
}

TEST(RecordInputPolicy, TomlQuotedAliasAndDuplicateLogicalField) {
  Record value;
  ASSERT_TRUE(read<toml::Reader<>>(value, "\"old_\\u006eame\"='Ada'\n").is_ok());
  EXPECT_EQ(write(value), R"({"name":"Ada","count":7})");
  EXPECT_EQ(read<toml::Reader<>>(value, "name='a'\nlegacy='b'\n").code,
            EC::duplicate_member);
}

TEST(RecordInputPolicy, FieldsKeepLegacyAccessAndTryCompatibility) {
  struct Legacy {
    Name name;
  };
  Legacy value;
  ASSERT_TRUE(read(value, R"({"name":"Ada"})").is_ok());
  EXPECT_EQ(value.name.value().value(), "Ada");
  EXPECT_EQ(write(value), R"({"name":"Ada"})");
}
}  // namespace
