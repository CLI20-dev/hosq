#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "hosq/bson/reader.hh"
#include "hosq/hosq.hh"
#include "hosq/json/fast_reader.hh"
#include "hosq/json/simple_reader.hh"
#include "hosq/msgpack/reader.hh"
#include "hosq/toml/reader.hh"

namespace prefailed_decode_test {
struct DispatchReader : hosq::json::SimpleReader {
  unsigned dispatch_calls = 0;
};
struct ResetErrorReader : hosq::json::SimpleReader {
  inline static unsigned reset_calls = 0;
  inline static unsigned next_calls = 0;
  inline static unsigned dispatch_calls = 0;
  void reset(state_type& state, input_type input) {
    ++reset_calls;
    hosq::json::SimpleReader::reset(state, input);
    report_error(state, {hosq::ErrorCode::invalid_utf8, 27,
                         "error reported by reader.reset"});
  }
  template <auto... Hints>
  auto next_token(state_type& state) -> bool {
    ++next_calls;
    return hosq::json::SimpleReader::next_token(state);
  }
};
}  // namespace prefailed_decode_test

namespace hosq::ext {
template <>
struct ReaderDispatch<prefailed_decode_test::DispatchReader> {
  template <class State, class Function>
  auto operator()(prefailed_decode_test::DispatchReader& reader, State&,
                  Function&& function) const -> decltype(auto) {
    ++reader.dispatch_calls;
    return std::forward<Function>(function)(&reader);
  }
};
template <>
struct ReaderDispatch<prefailed_decode_test::ResetErrorReader> {
  template <class State, class Function>
  auto operator()(prefailed_decode_test::ResetErrorReader& reader, State&,
                  Function&& function) const -> decltype(auto) {
    ++reader.dispatch_calls;
    return std::forward<Function>(function)(&reader);
  }
};
}  // namespace hosq::ext

namespace {
using namespace hosq;
namespace v = hosq::types::value;

constexpr Error original_error{ErrorCode::validation_failed, 73,
                               "original error before decode entry"};
unsigned callback_calls = 0;
struct ObserveValue {
  auto operator()(int value) const -> int {
    ++callback_calls;
    return value;
  }
};
using Observed = Elem<scan::scalar | decode::integer<int> | step<ObserveValue> |
                          store::assign,
                      noop_encoder>;
struct ArrayFields {
  Observed n;
};
struct ObjectFields {
  Entry<Key<"n">, Observed> n;
};
struct TryArrayFields {
  Try<Observed> n;
};
struct TryObjectFields {
  Try<Entry<Key<"n">, Observed>> n;
};

enum class Shape { scalar, array, object };
enum class Payload { integer, array, string };

auto octets(std::initializer_list<unsigned> bytes) -> std::string {
  std::string output;
  for (auto byte : bytes) output += static_cast<char>(byte);
  return output;
}
auto little(std::uint32_t value) -> std::string {
  std::string output;
  for (unsigned i = 0; i < 4; ++i) output += static_cast<char>(value >> (8 * i));
  return output;
}
auto bson_object(unsigned marker, std::string_view key, std::string payload)
    -> std::string {
  const auto body =
      static_cast<char>(marker) + std::string(key) + '\0' + payload;
  return little(static_cast<std::uint32_t>(body.size() + 5)) + body + '\0';
}

// Always use document roots accepted by every backend. Scalar and array
// decode_nested calls start at the member value within these documents.
template <class R>
auto input(Payload payload) -> std::string {
  if constexpr (ReaderTraits<R>::format == Format::bson) {
    if (payload == Payload::array)
      return bson_object(4, "n", bson_object(16, "0", little(7)));
    if (payload == Payload::string)
      return bson_object(2, "n", little(2) + std::string("x\0", 2));
    return bson_object(16, "n", little(7));
  } else if constexpr (ReaderTraits<R>::format == Format::msgpack) {
    if (payload == Payload::array) return octets({0x81, 0xa1, 'n', 0x91, 7});
    if (payload == Payload::string) return octets({0x81, 0xa1, 'n', 0xa1, 'x'});
    return octets({0x81, 0xa1, 'n', 7});
  } else if constexpr (ReaderTraits<R>::format == Format::toml) {
    if (payload == Payload::array) return "n = [7]";
    if (payload == Payload::string) return "n = 'x'";
    return "n = 7";
  } else {
    if (payload == Payload::array) return R"({"n":[7]})";
    if (payload == Payload::string) return R"({"n":"x"})";
    return R"({"n":7})";
  }
}

template <class R>
auto advance_to_value(R& reader, typename R::state_type& state, bool member)
    -> bool {
  if (!reader.next_token(state)) return false;
  return !member || (reader.next_token(state) && reader.next_token(state));
}

void expect_original_error(const Error& error) {
  EXPECT_EQ(error.code, original_error.code);
  EXPECT_EQ(error.offset, original_error.offset);
  EXPECT_EQ(error.detail, original_error.detail);
}
void expect_same_token(const Token& actual, const Token& expected) {
  EXPECT_EQ(actual.type, expected.type);
  EXPECT_EQ(actual.offset, expected.offset);
  EXPECT_EQ(actual.length, expected.length);
  EXPECT_EQ(actual.context, expected.context);
}

template <class R, class Value, class Storage>
void check_prefailed(Value& value, Shape shape, Storage storage) {
  R reader;
  typename R::state_type state;
  const auto valid_input =
      input<R>(shape == Shape::array ? Payload::array : Payload::integer);
  const auto scalar_input = input<R>(Payload::integer);
  for (bool wrong_token : {false, true}) {
    SCOPED_TRACE(wrong_token ? "wrong current token" : "matching current token");
    using Stored = std::remove_cvref_t<decltype(storage(value))>;
    if constexpr (std::same_as<Stored, std::vector<int>>)
      storage(value) = {91, 92};
    else
      storage(value) = 91;
    const auto before_storage = storage(value);
    callback_calls = 0;
    reader.reset(state, wrong_token ? scalar_input : valid_input);
    ASSERT_TRUE(reader.error(state).is_ok());
    const bool member =
        wrong_token ? shape != Shape::scalar : shape != Shape::object;
    ASSERT_TRUE(advance_to_value(reader, state, member));
    reader.report_error(state, original_error);
    const auto before_position = state.pos;
    const Token before_token = reader.current_token(state);

    expect_original_error(Decoder<R>::decode_nested(value, reader, state));
    expect_original_error(reader.error(state));
    EXPECT_EQ(storage(value), before_storage);
    EXPECT_EQ(callback_calls, 0u);
    EXPECT_EQ(state.pos, before_position);
    expect_same_token(reader.current_token(state), before_token);

    reader.reset(state, valid_input);
    ASSERT_TRUE(reader.error(state).is_ok());
    ASSERT_TRUE(advance_to_value(reader, state, shape != Shape::object));
    const auto result = Decoder<R>::decode_nested(value, reader, state);
    ASSERT_TRUE(result.is_ok()) << result.detail;
    EXPECT_TRUE(reader.error(state).is_ok());
    EXPECT_EQ(callback_calls, 1u);
    if constexpr (std::same_as<Stored, std::vector<int>>)
      EXPECT_EQ(storage(value), (std::vector<int>{7}));
    else
      EXPECT_EQ(storage(value), 7);
  }
}

using Readers = ::testing::Types<
    json::SimpleReader,
    json::FastReader<128, json::InputPadding::none, json::WhitespaceMode::Auto>,
    bson::Reader<>, msgpack::Reader<>, toml::Reader<>>;
template <class R>
class PrefailedDecode : public ::testing::Test {};
TYPED_TEST_SUITE(PrefailedDecode, Readers);

TYPED_TEST(PrefailedDecode, SingleElementPreservesErrorAndStorage) {
  Observed value;
  check_prefailed<TypeParam>(value, Shape::scalar,
                             [](auto& item) -> auto& { return item.value(); });
}
TYPED_TEST(PrefailedDecode, OrdinaryArrayAggregatePreservesErrorAndStorage) {
  ArrayFields value;
  check_prefailed<TypeParam>(value, Shape::array,
                             [](auto& item) -> auto& { return item.n.value(); });
}
TYPED_TEST(PrefailedDecode, EntryAggregatePreservesErrorAndStorage) {
  ObjectFields value;
  check_prefailed<TypeParam>(value, Shape::object, [](auto& item) -> auto& {
    return item.n.value().value();
  });
}
TYPED_TEST(PrefailedDecode, AllTryArrayPreservesErrorAndStorage) {
  TryArrayFields value;
  check_prefailed<TypeParam>(value, Shape::array,
                             [](auto& item) -> auto& { return item.n.value(); });
}
TYPED_TEST(PrefailedDecode, AllTryObjectPreservesErrorAndStorage) {
  TryObjectFields value;
  check_prefailed<TypeParam>(value, Shape::object, [](auto& item) -> auto& {
    return item.n.value().value();
  });
}
TYPED_TEST(PrefailedDecode, TypedArrayPreservesErrorAndStorage) {
  v::Array<Observed> value;
  check_prefailed<TypeParam>(value, Shape::array,
                             [](auto& item) -> auto& { return item.value(); });
}

TYPED_TEST(PrefailedDecode, NewInputResetsErrorForReusableDecoderEntrypoints) {
  Decoder<TypeParam> decoder;
  const auto bad_input = input<TypeParam>(Payload::string);
  const auto good_input = input<TypeParam>(Payload::integer);
  ObjectFields value;
  for (bool document : {false, true}) {
    SCOPED_TRACE(document ? "decode_document" : "decode");
    auto decode = [&](std::string_view source) {
      return document ? decoder.decode_document(value, source)
                      : decoder.decode(value, source);
    };
    EXPECT_EQ(decode(bad_input).code, ErrorCode::type_mismatch);
    value.n.value().value() = 91;
    callback_calls = 0;
    const auto result = decode(good_input);
    ASSERT_TRUE(result.is_ok()) << result.detail;
    EXPECT_EQ(value.n.value().value(), 7);
    EXPECT_EQ(callback_calls, 1u);
  }
}

struct ProduceSeven {
  auto operator()() const -> int {
    ++callback_calls;
    return 7;
  }
};
TEST(PrefailedDecodeDispatch, ReturnsBeforeCallingReaderDispatchHook) {
  using R = prefailed_decode_test::DispatchReader;
  Elem<step<ProduceSeven> | store::assign, noop_encoder> value;
  value.value() = 91;
  callback_calls = 0;
  R reader;
  R::state_type state;
  reader.reset(state, "7");
  ASSERT_TRUE(reader.next_token(state));
  reader.report_error(state, original_error);
  const auto before_position = state.pos;
  const Token before_token = reader.current_token(state);
  expect_original_error(Decoder<R>::decode_nested(value, reader, state));
  expect_original_error(reader.error(state));
  EXPECT_EQ(reader.dispatch_calls, 0u);
  EXPECT_EQ(callback_calls, 0u);
  EXPECT_EQ(value.value(), 91);
  EXPECT_EQ(state.pos, before_position);
  expect_same_token(reader.current_token(state), before_token);

  reader.reset(state, "7");
  ASSERT_TRUE(reader.error(state).is_ok());
  ASSERT_TRUE(reader.next_token(state));
  ASSERT_TRUE(Decoder<R>::decode_nested(value, reader, state).is_ok());
  EXPECT_EQ(reader.dispatch_calls, 1u);
  EXPECT_EQ(callback_calls, 1u);
  EXPECT_EQ(value.value(), 7);
}

TEST(PrefailedDecodeDispatch, ResetErrorStopsEveryNewInputEntrypoint) {
  using R = prefailed_decode_test::ResetErrorReader;
  Elem<step<ProduceSeven> | store::assign, noop_encoder> value;
  Decoder<R> decoder;
  auto check = [&](auto decode) {
    value.value() = 91;
    callback_calls = 0;
    R::reset_calls = R::next_calls = R::dispatch_calls = 0;
    const auto error = decode();
    EXPECT_EQ(error.code, ErrorCode::invalid_utf8);
    EXPECT_EQ(error.offset, 27u);
    EXPECT_EQ(error.detail, "error reported by reader.reset");
    EXPECT_EQ(R::reset_calls, 1u);
    EXPECT_EQ(R::next_calls, 0u);
    EXPECT_EQ(R::dispatch_calls, 0u);
    EXPECT_EQ(callback_calls, 0u);
    EXPECT_EQ(value.value(), 91);
  };
  check([&] { return decoder.decode(value, "7"); });
  check([&] { return decoder.decode_document(value, "7"); });
  check([&] { return decode_into<R>(value, "7"); });
  check([&] { return decode_document_into<R>(value, "7"); });
}
}  // namespace
