#include <gtest/gtest.h>

#include <array>
#include <string>
#include <tuple>
#include <type_traits>

#include "hosq/bson/reader.hh"
#include "hosq/bson/writer.hh"
#include "hosq/hosq.hh"
#include "hosq/json/fast_reader.hh"
#include "hosq/json/simple_reader.hh"
#include "hosq/msgpack/reader.hh"
#include "hosq/msgpack/writer.hh"
#include "hosq/toml/reader.hh"
#include "hosq/types.hh"

namespace {
using namespace hosq;
namespace v = hosq::types::value;
using EC = ErrorCode;
using P = PathSegment;

static_assert(std::is_aggregate_v<Error>);
static_assert(Error::ok().is_ok());
static_assert(Error{EC::out_of_range, 7, "old initializer"}.offset == 7);

struct Leaf {
  Entry<Key<"count">, v::Int8> count;
};
struct Root {
  Entry<Key<"items">, v::Array<v::Mapping<v::Text, v::Record<Leaf>>>> items;
};
using Value = v::Record<Root>;

template <class R>
void check_nested() {
  Value value;
  Decoder<R> decoder;
  const auto ok = decoder.decode_document(
      value, R"({"items":[{"a":{"count":1}},{"a\u002fb~c":{"count":2}}]})");
  ASSERT_TRUE(ok.is_ok()) << ok.detail;
  EXPECT_TRUE(ok.path.empty());
  auto error = decoder.decode_document(
      value, R"({"items":[{"a":{"count":1}},{"a\u002fb~c":{"count":128}}]})");
  EXPECT_EQ(error.code, EC::out_of_range);
  EXPECT_EQ(error.detail, "integer out of range");
  EXPECT_EQ(error.path, (ErrorPath{P::field("items"), P::element(1),
                                   P::key("a/b~c"), P::field("count")}));
  // Failure metadata remains owned across input replacement and decoder reuse.
  std::string invalid = R"({"items":[{"owned":{"count":"SECRET_PAYLOAD"}}]})";
  error = decoder.decode(value, invalid);
  const auto copy = error;
  invalid.assign(invalid.size(), 'x');
  EXPECT_EQ(error.path, (ErrorPath{P::field("items"), P::element(0),
                                   P::key("owned"), P::field("count")}));
  ASSERT_TRUE(decoder.decode(value, R"({"items":[]})").is_ok());
  EXPECT_EQ(error.path, copy.path);
  for (const auto& part : error.path)
    EXPECT_EQ(part.name.find("SECRET_PAYLOAD"), std::string::npos);
  error.path.back().name = "changed";
  EXPECT_EQ(copy.path.back().name, "count");
  EXPECT_TRUE(decoder.decode(value, R"({"items":[]})").path.empty());
}

TEST(ErrorPath, NestedTypedSimpleAndFastReaders) {
  check_nested<json::SimpleReader>();
  check_nested<json::FastReader<>>();
}

// A reader implementing only the original ReaderConcept protocol. No path
// hooks, ReaderBase inheritance, or access to its private error are required.
class CustomReader {
 public:
  using input_type = std::string_view;
  using state_type = json::SimpleReader::state_type;
  void reset(state_type& state, input_type input) {
    reader_.reset(state, input);
  }
  auto next_token(state_type& state) -> bool {
    return reader_.next_token(state);
  }
  auto error(const state_type& state) const -> Error {
    return reader_.error(state);
  }
  auto current_token(const state_type& state) const -> const Token& {
    return reader_.current_token(state);
  }
  auto input(const state_type& state) const -> BytesView {
    return reader_.input(state);
  }
  void report_error(const state_type& state, Error error) const {
    reader_.report_error(state, std::move(error));
  }

 private:
  json::SimpleReader reader_;
};
static_assert(ReaderConcept<CustomReader>);
TEST(ErrorPath, OriginalCustomReaderProtocolNeedsNoChanges) {
  check_nested<CustomReader>();
}

template <class R>
void check_schema_failures() {
  Value value;
  auto error = decode_into<R>(value, R"({"items":[{"x":{}}]})");
  EXPECT_EQ(error.code, EC::missing_required);
  EXPECT_EQ(error.path, (ErrorPath{P::field("items"), P::element(0), P::key("x"),
                                   P::field("count")}));
  error =
      decode_into<R>(value, R"({"items":[{"x":{"count":1,"c\u006funt":2}}]})");
  EXPECT_EQ(error.code, EC::duplicate_member);
  EXPECT_EQ(error.path.back(), P::field("count"));
  EXPECT_EQ(error.path.size(), 4U);
  error = decode_into<R>(value, R"({"items":[{"x":{"unknown\/key":0}}]})");
  EXPECT_EQ(error.code, EC::unknown_member);
  EXPECT_EQ(error.path.back(), P::field("unknown/key"));
  error = decode_into<R>(
      value, R"({"items":[{"x":{"count":1},"\u0078":{"count":2}}]})");
  EXPECT_EQ(error.code, EC::duplicate_member);
  EXPECT_EQ(error.path,
            (ErrorPath{P::field("items"), P::element(0), P::key("x", 1)}));
}
TEST(ErrorPath, MissingDuplicateUnknownAndEscapedKeys) {
  check_schema_failures<json::SimpleReader>();
  check_schema_failures<json::FastReader<>>();
}

template <class R, class W>
void check_binary() {
  struct WideLeaf {
    Entry<Key<"count">, v::Int64> count;
  };
  struct WideRoot {
    Entry<Key<"items">, v::Array<v::Mapping<v::Text, v::Record<WideLeaf>>>>
        items;
  };
  v::Record<WideRoot> wide;
  wide.value().items.value().value().resize(1);
  auto& map = wide.value().items.value().value()[0];
  map.keys().push_back("binary");
  map.values().resize(1);
  map.values()[0].count.value().value() = 128;
  std::string bytes;
  W writer(bytes);
  ASSERT_TRUE(encode_into(wide, writer));
  Value value;
  const auto error = decode_into<R>(value, bytes);
  EXPECT_EQ(error.code, EC::out_of_range);
  EXPECT_EQ(error.path, (ErrorPath{P::field("items"), P::element(0),
                                   P::key("binary"), P::field("count")}));
}
TEST(ErrorPath, BinaryAndTomlReadersKeepLocations) {
  check_binary<msgpack::Reader<>, msgpack::Writer<>>();
  check_binary<bson::Reader<>, bson::Writer<>>();
  Value value;
  const auto error = decode_into<toml::Reader<>>(
      value, "items = [{\"a/b\" = { count = 128 }}]\n");
  EXPECT_EQ(error.code, EC::out_of_range);
  EXPECT_EQ(error.path, (ErrorPath{P::field("items"), P::element(0),
                                   P::key("a/b"), P::field("count")}));
}

TEST(ErrorPath, NonTextMappingKeysAreOrdinalsWithoutValues) {
  v::Mapping<v::Int64, v::Array<v::Int8>> value;
  // {123456: [128]}: numeric key payload must not be copied into diagnostics.
  const std::string bytes("\x81\xce\x00\x01\xe2\x40\x91\xcc\x80", 9);
  auto error = decode_into<msgpack::Reader<>>(value, bytes);
  EXPECT_EQ(error.code, EC::out_of_range);
  EXPECT_EQ(error.path, (ErrorPath{P::key_at(0), P::element(0)}));
}

struct HardFailure {
  template <class C>
  auto operator()(int value, C& ctx) const -> MatchResult<int> {
    ctx.reader().report_error(ctx.reader_state(),
                              {EC::validation_failed, 19, "sentinel detail"});
    return {false, value};
  }
};
struct SoftFailure {
  auto operator()(int value) const -> MatchResult<int> { return {false, value}; }
};
struct Observe {
  static inline int calls = 0;
  auto operator()(int value) const -> int {
    ++calls;
    return value;
  }
};
template <class Check>
using Checked =
    Elem<scan::scalar | decode::number<int> | step<Check> | store::assign,
         noop_encoder>;
using Observed =
    Elem<scan::scalar | decode::number<int> | step<Observe> | store::assign,
         noop_encoder>;
using AnyKey =
    Elem<scan::scalar | decode::string_view | store::assign, noop_encoder>;

template <class R>
void check_try() {
  struct Hard {
    Try<Entry<Key<"n">, Checked<HardFailure>>> first;
    Try<Entry<AnyKey, Observed>> fallback;
  } hard;
  Observe::calls = 0;
  auto error = decode_into<R>(hard, R"({"n":7})");
  EXPECT_EQ(error.code, EC::validation_failed);
  EXPECT_EQ(error.offset, 19U);
  EXPECT_EQ(error.detail, "sentinel detail");
  EXPECT_EQ(error.path, (ErrorPath{P::field("n")}));
  EXPECT_EQ(Observe::calls, 0);
  struct Soft {
    Try<Entry<Key<"n">, Checked<SoftFailure>>> first;
    Try<Entry<AnyKey, Observed>> fallback;
  } soft;
  error = decode_into<R>(soft, R"({"n":7})");
  EXPECT_TRUE(error.is_ok()) << error.detail;
  EXPECT_TRUE(error.path.empty());
  EXPECT_EQ(Observe::calls, 1);
}
TEST(ErrorPath, HardIdentityAndSoftTryFallbackArePreserved) {
  check_try<json::SimpleReader>();
  check_try<json::FastReader<>>();
}

template <class R>
void check_legacy() {
  struct Legacy {
    v::Int8 first;
    v::Array<v::Int8> second;
  } legacy;
  auto error = decode_into<R>(legacy, "[1,[2,128]]");
  EXPECT_EQ(error.path, (ErrorPath{P::element(1), P::element(1)}));
  struct Named {
    Entry<Key<"numbers">, v::Array<v::Int8>> numbers;
  } named;
  error = decode_into<R>(named, R"({"numbers":[128]})");
  EXPECT_EQ(error.path, (ErrorPath{P::field("numbers"), P::element(0)}));
  using ToLeaf =
      Elem<scan::object | decode::to<Leaf> | store::assign, noop_encoder>;
  v::Array<ToLeaf> to;
  error = decode_into<R>(to, R"([{"count":128}])");
  EXPECT_EQ(error.path, (ErrorPath{P::element(0), P::field("count")}));
}
TEST(ErrorPath, LegacyTuplesAndDecodeToPreserveNestedLocations) {
  check_legacy<json::SimpleReader>();
  check_legacy<json::FastReader<>>();
}

template <class R>
void check_pipelines() {
  constexpr auto number = scan::scalar | decode::number<int>;
  Elem<scan::array | decode::collect<number> | store::assign, noop_encoder>
      collect;
  auto error = decode_into<R>(collect, "[1,\"bad\"]");
  EXPECT_EQ(error.path, (ErrorPath{P::element(1)}));
  Elem<scan::array | decode::each<number | store::assign>, noop_encoder> each;
  error = decode_into<R>(each, "[1,\"bad\"]");
  EXPECT_EQ(error.path, (ErrorPath{P::element(1)}));
  Elem<scan::array | decode::collect<number, 2> | store::assign, noop_encoder>
      fixed;
  error = decode_into<R>(fixed, "[1,\"bad\"]");
  EXPECT_EQ(error.path, (ErrorPath{P::element(1)}));
  constexpr auto key = scan::scalar | decode::text;
  Elem<scan::object | decode::collect<key, number> | store::assign, noop_encoder>
      map;
  error = decode_into<R>(map, R"({"a\u002fb":"bad"})");
  EXPECT_EQ(error.path, (ErrorPath{P::key("a/b")}));
  Elem<scan::object | decode::each<key | store::assign, number | store::assign>,
       noop_encoder>
      each_map;
  error = decode_into<R>(each_map, R"({"a\u002fb":"bad"})");
  EXPECT_EQ(error.path, (ErrorPath{P::key("a/b")}));
  Elem<scan::array | decode::each<number | store::assign, 2>, noop_encoder>
      fixed_each;
  error = decode_into<R>(fixed_each, "[1,\"bad\"]");
  EXPECT_EQ(error.path, (ErrorPath{P::element(1)}));
  Elem<scan::object | decode::collect<key, number, 2> | store::assign,
       noop_encoder>
      fixed_map;
  error = decode_into<R>(fixed_map, R"({"ok":1,"next":"bad"})");
  EXPECT_EQ(error.path, (ErrorPath{P::key("next", 1)}));
  Elem<scan::object |
           decode::each<key | store::assign, number | store::assign, 2>,
       noop_encoder>
      fixed_each_map;
  error = decode_into<R>(fixed_each_map, R"({"ok":1,"next":"bad"})");
  EXPECT_EQ(error.path, (ErrorPath{P::key("next", 1)}));
  constexpr auto object = scan::object | decode::to<Leaf>;
  Elem<scan::array | decode::collect<object> | store::assign, noop_encoder>
      nested_collect;
  error = decode_into<R>(nested_collect, R"([{"count":128}])");
  EXPECT_EQ(error.path, (ErrorPath{P::element(0), P::field("count")}));
  Elem<scan::array | decode::each<object | store::assign>, noop_encoder>
      nested_each;
  error = decode_into<R>(nested_each, R"([{"count":128}])");
  EXPECT_EQ(error.path, (ErrorPath{P::element(0), P::field("count")}));
}
TEST(ErrorPath, ExistingEachAndCollectPipelinesKeepLocations) {
  check_pipelines<json::SimpleReader>();
  check_pipelines<json::FastReader<>>();
}

TEST(ErrorPath, EachInputOrdinalsDoNotDependOnExistingStorage) {
  constexpr auto number = scan::scalar | decode::number<int> | store::assign;
  Elem<scan::array | decode::each<number>, noop_encoder> each;
  ASSERT_TRUE(decode_into<json::FastReader<>>(each, "[7,8,9]").is_ok());
  auto error = decode_into<json::FastReader<>>(each, "[1,]");
  EXPECT_EQ(error.path, (ErrorPath{P::element(1)}));
  constexpr auto key = scan::scalar | decode::text | store::assign;
  Elem<scan::object | decode::each<key, number>, noop_encoder> object;
  ASSERT_TRUE(decode_into<json::FastReader<>>(object, R"({"old":1})").is_ok());
  error = decode_into<json::FastReader<>>(object, R"({"new":"bad"})");
  EXPECT_EQ(error.path, (ErrorPath{P::key("new", 0)}));
}

TEST(ErrorPath, EmptyTextKeyIsDistinctFromUnknownOrNonTextKey) {
  v::Mapping<v::Text, v::Int8> value;
  const auto error = decode_into<json::FastReader<>>(value, R"({"":128})");
  ASSERT_EQ(error.path.size(), 1U);
  EXPECT_EQ(error.path[0], P::key(""));
  EXPECT_TRUE(error.path[0].has_name);
  EXPECT_NE(error.path[0], P::key_at(0));
}

template <class R>
void check_unmatched() {
  struct Array {
    Try<Checked<SoftFailure>> rejected;
  } array;
  auto error = decode_into<R>(array, "[7]");
  EXPECT_EQ(error.code, EC::unknown_member);
  EXPECT_EQ(error.path, (ErrorPath{P::element(0)}));
  struct Object {
    Try<Entry<Key<"n">, v::Int8>> number;
  } object;
  error = decode_into<R>(object, R"({"n":1,"unknown":2})");
  EXPECT_EQ(error.code, EC::unknown_member);
  EXPECT_EQ(error.path, (ErrorPath{P::key_at(1)}));
  struct Dynamic {
    Try<Entry<AnyKey, v::Int8>> number;
  } dynamic;
  error = decode_into<R>(dynamic, R"({"a":1,"b":128})");
  EXPECT_EQ(error.code, EC::out_of_range);
  EXPECT_EQ(error.path, (ErrorPath{P::key_at(1)}));
}
TEST(ErrorPath, UnmatchedTryAndDynamicMembersUseAccurateOrdinals) {
  check_unmatched<json::SimpleReader>();
  check_unmatched<json::FastReader<>>();
}

template <class R, class E>
void check_array_syntax(E& value, std::string_view input) {
  // Compare with the same pipeline invoked through an original, untracked
  // context: attaching location metadata must not replace the reader failure.
  R reader;
  typename R::state_type state;
  reader.reset(state, input);
  ASSERT_TRUE(reader.next_token(state));
  DecodeContext<typename E::storage_type, R> context{value.storage(), reader,
                                                     state};
  value.decode(context);
  const auto original = reader.error(state);
  ASSERT_TRUE(original.has_error());
  const auto error = decode_into<R>(value, input);
  EXPECT_EQ(error.code, original.code);
  EXPECT_EQ(error.offset, original.offset);
  EXPECT_EQ(error.detail, original.detail);
  EXPECT_EQ(error.path, (ErrorPath{P::element(1)}));
}

template <class R>
void check_array_boundaries() {
  constexpr auto number = scan::scalar | decode::number<int>;
  v::Array<v::Int8> typed;
  Elem<scan::array | decode::each<number | store::assign>, noop_encoder> each;
  Elem<scan::array | decode::collect<number> | store::assign, noop_encoder>
      collect;
  for (const auto input : {"[1,]", "[1,}", "[1,"}) {
    check_array_syntax<R>(typed, input);
    check_array_syntax<R>(each, input);
    check_array_syntax<R>(collect, input);
    struct Candidates {
      Try<v::Int8> number;
    } candidates;
    const auto error = decode_into<R>(candidates, input);
    EXPECT_TRUE(error.has_error());
    EXPECT_EQ(error.path, (ErrorPath{P::element(1)}));
  }
  struct ArrayRecord {
    Entry<Key<"items">, v::Array<v::Int8>> items;
  };
  v::Record<ArrayRecord> record;
  for (const auto input : {R"({"items":[1,]})", R"({"items":[1,})",
                           R"({"items":[1,)", R"({"items":[1,@]})"}) {
    const auto error = decode_into<R>(record, input);
    EXPECT_TRUE(error.has_error());
    EXPECT_EQ(error.path, (ErrorPath{P::field("items"), P::element(1)}));
  }
}
TEST(ErrorPath, MalformedNextArraySlotKeepsIndexAndOriginalReaderFailure) {
  check_array_boundaries<json::SimpleReader>();
  check_array_boundaries<json::FastReader<>>();
}

TEST(ErrorPath, RootErrorsAndOldInitializersHaveEmptyPaths) {
  Error positional{EC::invalid_token, 4, "old"};
  Error designated{.code = EC::invalid_token, .offset = 4, .detail = "old"};
  EXPECT_TRUE(positional.path.empty());
  EXPECT_TRUE(designated.path.empty());
  Value value;
  EXPECT_TRUE(decode_into<json::SimpleReader>(value, "[]").path.empty());
  EXPECT_TRUE(
      decode_document_into<json::FastReader<>>(value, R"({"items":[]} false)")
          .path.empty());
}
}  // namespace
