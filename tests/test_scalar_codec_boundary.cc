#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <variant>

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

// This reader's state is private and deliberately unrelated to NativeState.
// Its scalar context is also unrelated to JSON's literal/string contexts.
class OpaqueReader {
 public:
  using input_type = std::string_view;
  class state_type {
    friend class OpaqueReader;
    std::string_view payload_;
    Token cursor_{};
    bool emitted_ = false;
    mutable Error failure_ = Error::ok();

   public:
    struct Checkpoint {
      Token cursor;
      bool emitted;
    };
    auto checkpoint() const -> Checkpoint { return {cursor_, emitted_}; }
    void rollback(Checkpoint value) {
      cursor_ = value.cursor;
      emitted_ = value.emitted;
    }
  };
  void reset(state_type& state, input_type input) {
    state = {};
    state.payload_ = input;
  }
  auto next_token(state_type& state) -> bool {
    state.cursor_ = {0, state.payload_.size(),
                     state.emitted_ ? TokenType::End : TokenType::Scalar, 217};
    state.emitted_ = true;
    return true;
  }
  auto current_token(const state_type& state) const -> const Token& {
    return state.cursor_;
  }
  auto input(const state_type& state) const -> BytesView {
    return BytesView::from_string_view(state.payload_);
  }
  auto error(const state_type& state) const -> Error { return state.failure_; }
  void report_error(const state_type& state, Error error) const {
    if (state.failure_.is_ok()) state.failure_ = error;
  }
  auto read_semantic(const state_type& state, SemanticNumber& value) const
      -> bool {
    if (state.payload_ == "wide")
      value.value = std::numeric_limits<std::uint64_t>::max();
    else if (state.payload_ == "fraction")
      value.value = 0.1;
    else if (state.payload_ == "minus zero")
      value.value = -0.0;
    else
      value.value = std::int64_t{42};
    return true;
  }
  auto read_semantic(const state_type&, bool& value) const -> bool {
    value = true;
    return true;
  }
  auto read_semantic(const state_type&, std::nullptr_t&) const -> bool {
    return true;
  }
  auto read_string_source(const state_type& state, std::string_view& value) const
      -> bool {
    if (state.cursor_.type != TokenType::Scalar ||
        state.cursor_.context != 217) {
      report_error(state, {ErrorCode::type_mismatch, state.cursor_.offset,
                           "opaque reader expected a string source"});
      return false;
    }
    value = state.payload_;
    return true;
  }
  auto read_semantic(const state_type&, BytesValue& value) const -> bool {
    value.value = {std::byte{0}, std::byte{255}};
    return true;
  }
};
static_assert(ReaderConcept<OpaqueReader>);

TEST(ScalarCodecBoundary, PrivateReaderStateProvidesAllCommonScalars) {
  types::value::Int64 integer;
  ASSERT_TRUE(decode_into<OpaqueReader>(integer, "answer").is_ok());
  EXPECT_EQ(integer.value(), 42);
  types::value::Bool boolean;
  ASSERT_TRUE(decode_into<OpaqueReader>(boolean, "yes").is_ok());
  EXPECT_TRUE(boolean.value());
  types::value::Null null;
  EXPECT_TRUE(decode_into<OpaqueReader>(null, "nothing").is_ok());
  types::value::String text;
  const std::string source = "opaque\n日";
  ASSERT_TRUE(decode_into<OpaqueReader>(text, source).is_ok());
  EXPECT_EQ(text.value(), source);
  types::value::Bytes bytes;
  ASSERT_TRUE(decode_into<OpaqueReader>(bytes, "bytes").is_ok());
  EXPECT_EQ(bytes.value().value,
            (std::vector<std::byte>{std::byte{0}, std::byte{255}}));
}

TEST(ScalarCodecBoundary, CommonChecksEnforceCategoryRangeAndPrecision) {
  types::value::Int64 integer;
  EXPECT_EQ(decode_into<OpaqueReader>(integer, "fraction").code,
            ErrorCode::type_mismatch);
  EXPECT_EQ(decode_into<OpaqueReader>(integer, "wide").code,
            ErrorCode::out_of_range);
  types::value::Uint64 unsigned_integer;
  ASSERT_TRUE(decode_into<OpaqueReader>(unsigned_integer, "wide").is_ok());
  EXPECT_EQ(unsigned_integer.value(), std::numeric_limits<std::uint64_t>::max());
  types::value::Float<float> floating;
  EXPECT_EQ(decode_into<OpaqueReader>(floating, "answer").code,
            ErrorCode::type_mismatch);
  EXPECT_EQ(decode_into<OpaqueReader>(floating, "fraction").code,
            ErrorCode::out_of_range);
  ASSERT_TRUE(decode_into<OpaqueReader>(floating, "minus zero").is_ok());
  EXPECT_TRUE(std::signbit(floating.value()));
}

class CodecLessReader : public OpaqueReader {
 private:
  using OpaqueReader::read_semantic;
};
TEST(ScalarCodecBoundary, MissingCodecReportsUnsupportedWithoutGuessingSyntax) {
  types::value::Int64 integer;
  EXPECT_EQ(decode_into<CodecLessReader>(integer, "42").code,
            ErrorCode::unsupported_type);
}

TEST(ScalarCodecBoundary, LegacyNumberStillAcceptsNativeIntegralFloats) {
  const std::string input("\xcb\x40\x45\x00\x00\x00\x00\x00\x00", 9);
  Elem<scan::scalar | decode::number<int> | store::assign, noop_encoder> legacy;
  ASSERT_TRUE(decode_into<msgpack::Reader<>>(legacy, input).is_ok());
  EXPECT_EQ(legacy.value(), 42);
  types::value::Int64 strict;
  EXPECT_EQ(decode_into<msgpack::Reader<>>(strict, input).code,
            ErrorCode::type_mismatch);
}

template <class Value>
struct TextDocument {
  Entry<Key<"value">, Value> value;
};

template <class SourceReader, class SourceWriter>
void check_text_round_trips() {
  TextDocument<types::value::String> original;
  original.value.value().value() = "a\n日\\z";
  std::string source;
  SourceWriter source_writer(source);
  ASSERT_TRUE(encode_into(original, source_writer));
  TextDocument<types::value::String> text;
  ASSERT_TRUE(decode_into<SourceReader>(text, source).is_ok());
  EXPECT_EQ(text.value.value().value(), original.value.value().value());
  auto destination = [&]<class Reader, class Writer>() {
    std::string output;
    Writer writer(output);
    ASSERT_TRUE(encode_into(text, writer)) << writer.error().detail;
    TextDocument<types::value::String> decoded;
    ASSERT_TRUE(decode_into<Reader>(decoded, output).is_ok());
    EXPECT_EQ(decoded.value.value().value(), original.value.value().value());
  };
  destination.template operator()<json::SimpleReader, json::CompactWriter<>>();
  destination.template operator()<toml::Reader<>, toml::Writer<>>();
  destination.template operator()<bson::Reader<>, bson::Writer<>>();
  destination.template operator()<msgpack::Reader<>, msgpack::Writer<>>();
}

TEST(ScalarCodecBoundary, TextResolvesSameAndCrossFormats) {
  check_text_round_trips<json::SimpleReader, json::CompactWriter<>>();
  check_text_round_trips<json::FastReader<>, json::CompactWriter<>>();
  check_text_round_trips<toml::Reader<>, toml::Writer<>>();
  check_text_round_trips<bson::Reader<>, bson::Writer<>>();
  check_text_round_trips<msgpack::Reader<>, msgpack::Writer<>>();
}

TEST(ScalarCodecBoundary, StrictUnsignedJsonRetainsNegativeIntegerZero) {
  types::value::Uint64 value;
  EXPECT_EQ(decode_into<json::SimpleReader>(value, "-0").code,
            ErrorCode::out_of_range);
  EXPECT_EQ(decode_into<json::FastReader<>>(value, "-0").code,
            ErrorCode::out_of_range);
}
}  // namespace
