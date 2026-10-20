#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

#include "hosq/hosq.hh"
#include "hosq/msgpack/reader.hh"
#include "hosq/msgpack/writer.hh"
#include "hosq/types.hh"

namespace {
using namespace hosq;
using EC = ErrorCode;

auto msgpack_octets(std::initializer_list<unsigned> bytes) -> std::string {
  std::string result;
  for (unsigned byte : bytes) result += static_cast<char>(byte);
  return result;
}

template <class Value>
auto read_msgpack_semantic(std::string_view input, Value& value) -> Error {
  msgpack::Reader<> reader;
  msgpack::Reader<>::state_type state;
  reader.reset(state, input);
  if (!reader.next_token(state) || !reader.read_semantic(state, value) ||
      !reader.next_token(state))
    return reader.error(state);
  return reader.error(state);
}

auto extension_payload(std::int8_t tag, std::string_view bytes)
    -> ExtensionValue {
  ExtensionValue result{tag, {}};
  for (unsigned char byte : bytes)
    result.payload.push_back(static_cast<std::byte>(byte));
  return result;
}

TEST(MessagePackSemantic, TimestampChoosesSmallestExactWireFormat) {
  struct Case {
    InstantValue value;
    std::string wire;
  };
  const std::vector<Case> cases{
      {{0, 0}, msgpack_octets({0xd6, 0xff, 0, 0, 0, 0})},
      {{0xffffffffLL, 0}, msgpack_octets({0xd6, 0xff, 0xff, 0xff, 0xff, 0xff})},
      {{0x100000000LL, 0}, msgpack_octets({0xd7, 0xff, 0, 0, 0, 1, 0, 0, 0, 0})},
      {{0, 1}, msgpack_octets({0xd7, 0xff, 0, 0, 0, 4, 0, 0, 0, 0})},
      {{0x3ffffffffLL, 999999999},
       msgpack_octets(
           {0xd7, 0xff, 0xee, 0x6b, 0x27, 0xff, 0xff, 0xff, 0xff, 0xff})},
      {{0x400000000LL, 0},
       msgpack_octets({0xc7, 12, 0xff, 0, 0, 0, 0, 0, 0, 0, 4, 0, 0, 0, 0})},
      {{-1, 999999999},
       msgpack_octets({0xc7, 12, 0xff, 0x3b, 0x9a, 0xc9, 0xff, 0xff, 0xff, 0xff,
                       0xff, 0xff, 0xff, 0xff, 0xff})},
      {{std::numeric_limits<std::int64_t>::min(), 0},
       msgpack_octets({0xc7, 12, 0xff, 0, 0, 0, 0, 0x80, 0, 0, 0, 0, 0, 0, 0})},
      {{std::numeric_limits<std::int64_t>::max(), 999999999},
       msgpack_octets({0xc7, 12, 0xff, 0x3b, 0x9a, 0xc9, 0xff, 0x7f, 0xff, 0xff,
                       0xff, 0xff, 0xff, 0xff, 0xff})},
  };
  for (const auto& test : cases) {
    std::string output;
    msgpack::Writer<> writer(output);
    ASSERT_TRUE(writer.write_semantic(test.value));
    EXPECT_EQ(output, test.wire);
    InstantValue decoded;
    ASSERT_TRUE(read_msgpack_semantic(test.wire, decoded).is_ok());
    EXPECT_EQ(decoded, test.value);
    ExtensionValue opaque;
    ASSERT_TRUE(read_msgpack_semantic(test.wire, opaque).is_ok());
    EXPECT_EQ(opaque.type, -1);
    std::string preserved;
    msgpack::Writer<> opaque_writer(preserved);
    ASSERT_TRUE(opaque_writer.write_semantic(opaque));
    EXPECT_EQ(preserved, test.wire);
  }
}

TEST(MessagePackSemantic, TimestampRoundTripsAllBoundarySubseconds) {
  for (auto seconds : std::array<std::int64_t, 10>{
           std::numeric_limits<std::int64_t>::min(), -2, -1, 0, 1, 0xffffffffLL,
           0x100000000LL, 0x3ffffffffLL, 0x400000000LL,
           std::numeric_limits<std::int64_t>::max()}) {
    for (auto nanos : {0U, 1U, 999999999U}) {
      const InstantValue original{seconds, nanos};
      std::string wire;
      msgpack::Writer<> writer(wire);
      ASSERT_TRUE(writer.write_semantic(original));
      InstantValue decoded;
      ASSERT_TRUE(read_msgpack_semantic(wire, decoded).is_ok());
      EXPECT_EQ(decoded, original);
    }
  }
}

TEST(MessagePackSemantic, TimestampRejectsInvalidTagLengthAndNanoseconds) {
  const auto nanos64 =
      msgpack_octets({0xd7, 0xff, 0xee, 0x6b, 0x28, 0, 0, 0, 0, 0});
  const auto nanos96 = msgpack_octets(
      {0xc7, 12, 0xff, 0x3b, 0x9a, 0xca, 0, 0, 0, 0, 0, 0, 0, 0, 0});
  for (const auto& wire : {nanos64, nanos96}) {
    InstantValue instant{123, 456};
    ExtensionValue opaque;
    EXPECT_EQ(read_msgpack_semantic(wire, instant).code, EC::out_of_range);
    EXPECT_EQ(instant, InstantValue{});
    EXPECT_EQ(read_msgpack_semantic(wire, opaque).code, EC::out_of_range);
  }
  for (auto size : {0, 1, 2, 3, 5, 7, 9, 11, 13, 16}) {
    const auto wire = msgpack_octets({0xc7, static_cast<unsigned>(size), 0xff}) +
                      std::string(size, '\0');
    InstantValue instant;
    ExtensionValue opaque;
    EXPECT_EQ(read_msgpack_semantic(wire, instant).code, EC::invalid_length);
    EXPECT_EQ(read_msgpack_semantic(wire, opaque).code, EC::invalid_length);
  }
  InstantValue instant;
  EXPECT_EQ(
      read_msgpack_semantic(msgpack_octets({0xd6, 0xfe, 0, 0, 0, 0}), instant)
          .code,
      EC::type_mismatch);
  EXPECT_EQ(read_msgpack_semantic(msgpack_octets({42}), instant).code,
            EC::type_mismatch);
}

TEST(MessagePackSemantic, TimestampAndExtensionRejectEveryTruncation) {
  for (const auto& wire :
       {msgpack_octets({0xd6, 0xff, 0, 0, 0, 0}),
        msgpack_octets({0xd7, 0xff, 0, 0, 0, 4, 0, 0, 0, 0}),
        msgpack_octets({0xc7, 12, 0xff, 0, 0, 0, 0, 0xff, 0xff, 0xff, 0xff, 0xff,
                        0xff, 0xff, 0xff})}) {
    for (std::size_t length = 0; length < wire.size(); ++length) {
      InstantValue instant;
      ExtensionValue opaque;
      const auto truncated = std::string_view(wire).substr(0, length);
      EXPECT_EQ(read_msgpack_semantic(truncated, instant).code,
                EC::unexpected_eof);
      EXPECT_EQ(read_msgpack_semantic(truncated, opaque).code,
                EC::unexpected_eof);
    }
  }
}

TEST(MessagePackSemantic, InvalidTypedTimestampWritesNoBytes) {
  for (auto nanos : {1000000000U, 1073741823U, 0xffffffffU}) {
    std::string output;
    msgpack::Writer<> writer(output);
    EXPECT_FALSE(writer.write_semantic(InstantValue{0, nanos}));
    EXPECT_EQ(writer.error().code, EC::out_of_range);
    EXPECT_TRUE(output.empty());
  }
  for (const auto& wire :
       {std::string(3, '\0'), msgpack_octets({0xee, 0x6b, 0x28, 0, 0, 0, 0, 0}),
        msgpack_octets({0x3b, 0x9a, 0xca, 0, 0, 0, 0, 0, 0, 0, 0, 0})}) {
    std::string output;
    msgpack::Writer<> writer(output);
    EXPECT_FALSE(writer.write_semantic(extension_payload(-1, wire)));
    EXPECT_TRUE(output.empty());
  }
}

TEST(MessagePackSemantic, OpaqueExtensionPreservesSignedTagsAndPayloads) {
  for (auto length :
       {0U, 1U, 2U, 3U, 4U, 8U, 16U, 17U, 255U, 256U, 65535U, 65536U}) {
    std::string payload(length, '\0');
    for (std::size_t i = 0; i < payload.size(); ++i)
      payload[i] = static_cast<char>(i);
    for (std::int8_t tag : {-128, -2, 0, 127}) {
      const auto original = extension_payload(tag, payload);
      std::string wire;
      msgpack::Writer<> writer(wire);
      ASSERT_TRUE(writer.write_semantic(original));
      ExtensionValue decoded;
      ASSERT_TRUE(read_msgpack_semantic(wire, decoded).is_ok());
      EXPECT_EQ(decoded, original);
      std::string second;
      msgpack::Writer<> second_writer(second);
      ASSERT_TRUE(second_writer.write_semantic(decoded));
      EXPECT_EQ(second, wire);
      const unsigned marker = static_cast<unsigned char>(wire.front());
      if (length == 1 || length == 2 || length == 4 || length == 8 ||
          length == 16)
        EXPECT_GE(marker, 0xd4U);
      else
        EXPECT_EQ(marker, length <= 255     ? 0xc7U
                          : length <= 65535 ? 0xc8U
                                            : 0xc9U);
    }
  }
}

TEST(MessagePackSemantic, OpaqueTimestampPreservesValidNonminimalPayload) {
  const auto payload = msgpack_octets({0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1});
  const auto wire = msgpack_octets({0xc7, 12, 0xff}) + payload;
  ExtensionValue opaque;
  ASSERT_TRUE(read_msgpack_semantic(wire, opaque).is_ok());
  std::string output;
  msgpack::Writer<> writer(output);
  ASSERT_TRUE(writer.write_semantic(opaque));
  EXPECT_EQ(output, wire);
  InstantValue instant;
  ASSERT_TRUE(read_msgpack_semantic(wire, instant).is_ok());
  EXPECT_EQ(instant, (InstantValue{1, 0}));
}

TEST(MessagePackSemantic, TypedWritesRespectWriterRootAndOutputLimits) {
  std::string output;
  msgpack::Writer<> writer(output);
  ASSERT_TRUE(writer.write_semantic(InstantValue{0, 0}));
  const auto first = output;
  EXPECT_FALSE(writer.write_semantic(extension_payload(0, "x")));
  EXPECT_EQ(writer.error().code, EC::invalid_state);
  EXPECT_EQ(output, first);

  std::string limited;
  msgpack::Writer<64, 5> small(limited);
  EXPECT_FALSE(small.write_semantic(InstantValue{0, 0}));
  EXPECT_EQ(small.error().code, EC::output_too_large);

  InstantValue value;
  EXPECT_EQ(read_msgpack_semantic(first + msgpack_octets({0}), value).code,
            EC::trailing_data);
}

TEST(MessagePackSemantic, PublicAliasesUseSemanticCodecs) {
  const auto timestamp =
      msgpack_octets({0xd7, 0xff, 0xee, 0x6b, 0x27, 0xfc, 0, 0, 0, 42});
  types::value::Instant instant;
  ASSERT_TRUE(decode_into<msgpack::Reader<>>(instant, timestamp).is_ok());
  EXPECT_EQ(instant.value(), (InstantValue{42, 999999999}));
  std::string timestamp_copy;
  msgpack::Writer<> timestamp_writer(timestamp_copy);
  ASSERT_TRUE(encode_into(instant, timestamp_writer));
  EXPECT_EQ(timestamp_copy, timestamp);

  const auto opaque_wire = msgpack_octets({0xd6, 0x80, 0, 0xff, 0xc1, 42});
  types::value::Extension extension;
  ASSERT_TRUE(decode_into<msgpack::Reader<>>(extension, opaque_wire).is_ok());
  EXPECT_EQ(extension.value(),
            extension_payload(-128, msgpack_octets({0, 0xff, 0xc1, 42})));
  std::string opaque_copy;
  msgpack::Writer<> opaque_writer(opaque_copy);
  ASSERT_TRUE(encode_into(extension, opaque_writer));
  EXPECT_EQ(opaque_copy, opaque_wire);

  EXPECT_EQ(decode_into<msgpack::Reader<>>(instant, opaque_wire).code,
            EC::type_mismatch);
  EXPECT_EQ(
      decode_into<msgpack::Reader<>>(extension, msgpack_octets({0xc4, 1, 0}))
          .code,
      EC::type_mismatch);
  types::value::Bytes bytes;
  EXPECT_EQ(decode_into<msgpack::Reader<>>(bytes, opaque_wire).code,
            EC::type_mismatch);
}

TEST(MessagePackSemantic, ForgedExtensionTokensRejectInvalidPayloadBounds) {
  const std::string input = msgpack_octets({0xff, 0, 0, 0, 0});
  struct Case {
    std::string_view input;
    std::size_t offset;
    std::size_t length;
  };
  for (const auto& test : std::array<Case, 6>{{
           {input, input.size() + 1, 1},
           {input, 0, 0},
           {input, input.size(), 1},
           {input, 1, input.size()},
           {input, 1, std::numeric_limits<std::size_t>::max()},
           {{}, 0, 1},
       }}) {
    msgpack::Reader<> reader;
    msgpack::Reader<>::state_type state;
    auto prepare_state = [&] {
      reader.reset(state, test.input);
      state.token = {
          test.offset, test.length, TokenType::Scalar,
          static_cast<std::uint8_t>(detail::NativeContext::extension)};
    };
    prepare_state();
    InstantValue instant{123, 456};
    EXPECT_FALSE(reader.read_semantic(state, instant));
    EXPECT_EQ(reader.error(state).code, EC::invalid_length);
    EXPECT_EQ(instant, InstantValue{});

    prepare_state();
    auto extension = extension_payload(5, "payload");
    EXPECT_FALSE(reader.read_semantic(state, extension));
    EXPECT_EQ(reader.error(state).code, EC::invalid_length);
    EXPECT_EQ(extension, ExtensionValue{});
  }
}
}  // namespace
