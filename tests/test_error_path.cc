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

template <class R>
constexpr bool reader_error_types =
    std::is_same_v<decltype(detail::reader_error(
                       std::declval<const R&>(),
                       std::declval<const typename R::state_type&>())),
                   detail::ErrorState> &&
    std::is_same_v<decltype(std::declval<const R&>().error(
                       std::declval<const typename R::state_type&>())),
                   Error>;
static_assert(reader_error_types<json::SimpleReader>);
static_assert(reader_error_types<json::FastReader<>>);
static_assert(reader_error_types<msgpack::Reader<>>);
static_assert(reader_error_types<bson::Reader<>>);
static_assert(reader_error_types<toml::Reader<>>);

template <class S>
using Object = Elem<scan::object | decode::to<S> | store::assign, encode::value>;

struct Leaf {
  Entry<Key<"count">, v::Int8> count;
};
struct Root {
  Entry<Key<"items">, v::Array<v::Mapping<v::String, Object<Leaf>>>> items;
};
using Value = Root;

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
  EXPECT_EQ(error.detail, "numeric conversion would lose range or precision");
  EXPECT_EQ(error.path, (ErrorPath{P::key("items"), P::element(1),
                                   P::key("a/b~c"), P::key("count")}));
  // Failure metadata remains owned across input replacement and decoder reuse.
  std::string invalid = R"({"items":[{"owned":{"count":"SECRET_PAYLOAD"}}]})";
  error = decoder.decode(value, invalid);
  const auto copy = error;
  invalid.assign(invalid.size(), 'x');
  EXPECT_EQ(error.path, (ErrorPath{P::key("items"), P::element(0),
                                   P::key("owned"), P::key("count")}));
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
  static inline int resets = 0;
  static inline int advances = 0;
  using input_type = std::string_view;
  using state_type = json::SimpleReader::state_type;
  void reset(state_type& state, input_type input) {
    ++resets;
    reader_.reset(state, input);
  }
  auto next_token(state_type& state) -> bool {
    ++advances;
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
  struct CustomLeaf {
    Entry<Key<"count">,
          Elem<scan::scalar | decode::number<std::int8_t> | store::assign,
               noop_encoder>>
        count;
  };
  struct CustomRoot {
    Entry<Key<"items">, v::Array<Object<CustomLeaf>>> items;
  } value;
  const auto error =
      decode_into<CustomReader>(value, R"({"items":[{"count":128}]})");
  EXPECT_EQ(error.code, EC::out_of_range);
  EXPECT_EQ(error.path, (ErrorPath{P::key_at(0), P::element(0), P::key_at(0)}));
}

// Inherited native storage must not bypass a custom public protocol hook.
template <class Base>
struct ReaderWithErrorOverride : Base {
  mutable int error_calls = 0;
  auto error(const typename Base::state_type&) const -> Error {
    ++error_calls;
    return {EC::validation_failed, 23, "custom reader error"};
  }
};

template <class Base>
struct ReaderWithReportOverride : Base {
  mutable int report_calls = 0;
  mutable Error reported = Error::ok();
  void report_error(const typename Base::state_type&, Error error) const {
    ++report_calls;
    reported = std::move(error);
  }
};

template <class Base>
void check_inherited_reader_hooks() {
  ReaderWithErrorOverride<Base> getter;
  typename Base::state_type state;
  getter.reset(state, {});
  EXPECT_TRUE(static_cast<const Base&>(getter).error(state).is_ok());
  EXPECT_EQ(detail::reader_error(getter, state).code, EC::validation_failed);
  EXPECT_EQ(getter.error_calls, 1);

  ReaderWithReportOverride<Base> reporter;
  reporter.reset(state, {});
  detail::report_reader_error(
      reporter, state, {EC::validation_failed, 29, "custom reader report"});
  EXPECT_EQ(reporter.report_calls, 1);
  EXPECT_EQ(reporter.reported.code, EC::validation_failed);
  EXPECT_EQ(reporter.reported.offset, 29U);
  EXPECT_EQ(reporter.reported.detail, "custom reader report");
  EXPECT_TRUE(static_cast<const Base&>(reporter).error(state).is_ok());
}

TEST(ErrorPath, InheritedReadersHonorIndividualPublicErrorOverrides) {
  check_inherited_reader_hooks<msgpack::Reader<>>();
  check_inherited_reader_hooks<bson::Reader<>>();
}

struct WriterWithErrorOverride : msgpack::Writer<> {
  using msgpack::Writer<>::Writer;
  mutable int error_calls = 0;
  auto error() const -> Error {
    ++error_calls;
    return {EC::validation_failed, 31, "custom writer error"};
  }
};

struct WriterWithReportOverride : msgpack::Writer<> {
  using msgpack::Writer<>::Writer;
  int report_calls = 0;
  Error reported = Error::ok();
  void report_error(Error error) {
    ++report_calls;
    reported = std::move(error);
  }
};

TEST(ErrorPath, InheritedWritersHonorIndividualPublicErrorOverrides) {
  std::string output;
  WriterWithErrorOverride getter(output);
  EXPECT_TRUE(static_cast<const msgpack::Writer<>&>(getter).error().is_ok());
  EXPECT_EQ(detail::writer_error(getter).code, EC::validation_failed);
  EXPECT_EQ(getter.error_calls, 1);

  WriterWithReportOverride reporter(output);
  detail::report_writer_error(
      reporter, {EC::validation_failed, 37, "custom writer report"});
  EXPECT_EQ(reporter.report_calls, 1);
  EXPECT_EQ(reporter.reported.code, EC::validation_failed);
  EXPECT_EQ(reporter.reported.offset, 37U);
  EXPECT_EQ(reporter.reported.detail, "custom writer report");
  EXPECT_TRUE(static_cast<const msgpack::Writer<>&>(reporter).error().is_ok());
}

template <class R>
void check_mapping_duplicate() {
  Value value;
  const auto error = decode_into<R>(
      value, R"({"items":[{"x":{"count":1},"\u0078":{"count":2}}]})");
  EXPECT_EQ(error.code, EC::duplicate_member);
  EXPECT_EQ(error.path,
            (ErrorPath{P::key("items"), P::element(0), P::key("x", 1)}));
}
TEST(ErrorPath, DuplicateEscapedMappingKeys) {
  check_mapping_duplicate<json::SimpleReader>();
  check_mapping_duplicate<json::FastReader<>>();
}

template <class R, class W>
void check_binary() {
  struct WideLeaf {
    Entry<Key<"count">, v::Int64> count;
  };
  struct WideRoot {
    Entry<Key<"items">, v::Array<v::Mapping<v::String, Object<WideLeaf>>>> items;
  };
  WideRoot wide;
  wide.items->resize(1);
  auto& map = (*wide.items)[0];
  map.entries().emplace_back("binary", WideLeaf{});
  *map.entries()[0].second.count = 128;
  std::string bytes;
  W writer(bytes);
  ASSERT_TRUE(encode_into(wide, writer));
  Value value;
  const auto error = decode_into<R>(value, bytes);
  EXPECT_EQ(error.code, EC::out_of_range);
  EXPECT_EQ(error.path, (ErrorPath{P::key("items"), P::element(0),
                                   P::key("binary"), P::key("count")}));
}
TEST(ErrorPath, BinaryAndTomlReadersKeepLocations) {
  check_binary<msgpack::Reader<>, msgpack::Writer<>>();
  check_binary<bson::Reader<>, bson::Writer<>>();
  Value value;
  const auto error = decode_into<toml::Reader<>>(
      value, "items = [{\"a/b\" = { count = 128 }}]\n");
  EXPECT_EQ(error.code, EC::out_of_range);
  EXPECT_EQ(error.path, (ErrorPath{P::key("items"), P::element(0), P::key("a/b"),
                                   P::key("count")}));
}

TEST(ErrorPath, NonTextMappingKeysAreOrdinalsWithoutValues) {
  v::Mapping<v::Int64, v::Array<v::Int8>> value;
  // {123456: [128]}: numeric key payload must not be copied into diagnostics.
  const std::string bytes("\x81\xce\x00\x01\xe2\x40\x91\xcc\x80", 9);
  auto error = decode_into<msgpack::Reader<>>(value, bytes);
  EXPECT_EQ(error.code, EC::out_of_range);
  EXPECT_EQ(error.path, (ErrorPath{P::key_at(0), P::element(0)}));
}

TEST(ErrorPath, CompoundMessagePackKeysUseInputOrdinals) {
  v::Mapping<v::Array<v::Int64>, v::Array<v::Int8>> value;
  // {[1, 2]: [128]}: the key is a container, not a scalar token.
  const std::string bytes("\x81\x92\x01\x02\x91\xcc\x80", 7);
  const auto error = decode_into<msgpack::Reader<>>(value, bytes);
  EXPECT_EQ(error.code, EC::out_of_range);
  EXPECT_EQ(error.path, (ErrorPath{P::key_at(0), P::element(0)}));
}

TEST(ErrorPath, TomlTraversalDoesNotRequireIncreasingSourceOffsets) {
  struct More {
    Entry<Key<"value">, v::Int8> value;
  };
  struct A {
    Entry<Key<"first">, v::Int8> first;
    Entry<Key<"more">, More> more;
  };
  struct B {
    Entry<Key<"count">, v::Int8> count;
  };
  struct Record {
    Entry<Key<"a">, A> a;
    Entry<Key<"b">, B> b;
  } value;
  // Traversal finishes a.more, whose source is after b.count, before visiting b.
  const auto error = decode_into<toml::Reader<>>(
      value, "[a]\nfirst=1\n[b]\ncount=128\n[a.more]\nvalue=7\n");
  EXPECT_EQ(error.code, EC::out_of_range);
  EXPECT_EQ(error.path, (ErrorPath{P::key("b", 1), P::key("count", 0)}));
}

struct HardFailure {
  template <class C>
  auto operator()(int, C& ctx) const -> bool {
    ctx.reader().report_error(ctx.reader_state(),
                              {EC::validation_failed, 19, "sentinel detail"});
    return false;
  }
};
struct SoftFailure {
  auto operator()(int) const -> bool { return false; }
};
struct Observe {
  static inline int calls = 0;
  auto operator()(int value) const -> int {
    ++calls;
    return value;
  }
};
template <class Check>
using Checked = Elem<scan::scalar | decode::number<int> |
                         match::predicate<Check{}> | store::assign,
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
  EXPECT_TRUE(error.path.empty());
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
  EXPECT_EQ(error.path, (ErrorPath{P::key("numbers"), P::element(0)}));
  using ToLeaf =
      Elem<scan::object | decode::to<Leaf> | store::assign, noop_encoder>;
  v::Array<ToLeaf> to;
  error = decode_into<R>(to, R"([{"count":128}])");
  EXPECT_EQ(error.path, (ErrorPath{P::element(0), P::key("count")}));
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
  constexpr auto key = scan::scalar | decode::string_source |
                       construct<std::string> | decode::json_unescape |
                       decode::toml_unescape | validate::utf8;
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
  EXPECT_EQ(error.path, (ErrorPath{P::element(0), P::key("count")}));
  Elem<scan::array | decode::each<object | store::assign>, noop_encoder>
      nested_each;
  error = decode_into<R>(nested_each, R"([{"count":128}])");
  EXPECT_EQ(error.path, (ErrorPath{P::element(0), P::key("count")}));
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
  constexpr auto key = scan::scalar | decode::string_source |
                       construct<std::string> | decode::json_unescape |
                       decode::toml_unescape | validate::utf8 | store::assign;
  Elem<scan::object | decode::each<key, number>, noop_encoder> object;
  ASSERT_TRUE(decode_into<json::FastReader<>>(object, R"({"old":1})").is_ok());
  error = decode_into<json::FastReader<>>(object, R"({"new":"bad"})");
  EXPECT_EQ(error.path, (ErrorPath{P::key("new", 0)}));
}

TEST(ErrorPath, EmptyTextKeyIsDistinctFromUnknownOrNonTextKey) {
  v::Mapping<v::String, v::Int8> value;
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
  EXPECT_EQ(error.path, (ErrorPath{P::key("unknown", 1)}));
  struct Dynamic {
    Try<Entry<AnyKey, v::Int8>> number;
  } dynamic;
  error = decode_into<R>(dynamic, R"({"a":1,"b":128})");
  EXPECT_EQ(error.code, EC::out_of_range);
  EXPECT_EQ(error.path, (ErrorPath{P::key("b", 1)}));
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
  ArrayRecord record;
  for (const auto input : {R"({"items":[1,]})", R"({"items":[1,})",
                           R"({"items":[1,)", R"({"items":[1,@]})"}) {
    const auto error = decode_into<R>(record, input);
    EXPECT_TRUE(error.has_error());
    EXPECT_EQ(error.path, (ErrorPath{P::key("items"), P::element(1)}));
  }
}
TEST(ErrorPath, MalformedNextArraySlotKeepsIndexAndOriginalReaderFailure) {
  check_array_boundaries<json::SimpleReader>();
  check_array_boundaries<json::FastReader<>>();
}

template <class R, class E>
void check_matching_container(E&) {
  struct Choices {
    Try<E> candidate;
    Try<E> fallback;
  } choices;
  const auto error = decode_into<R>(choices, R"([[1,"bad"]])");
  EXPECT_EQ(error.code, EC::invalid_number);
  EXPECT_EQ(error.path, (ErrorPath{P::element(0), P::element(1)}));
}

template <class R>
void check_matching_containers() {
  constexpr auto number = scan::scalar | decode::number<int> |
                          match::predicate<[](int) { return true; }>;
  Elem<scan::array | decode::collect<number> | store::assign, noop_encoder> a;
  Elem<scan::array | decode::collect<number, 2> | store::assign, noop_encoder> b;
  Elem<scan::array | decode::each<number | store::assign>, noop_encoder> c;
  Elem<scan::array | decode::each<number | store::assign, 2>, noop_encoder> d;
  check_matching_container<R>(a);
  check_matching_container<R>(b);
  check_matching_container<R>(c);
  check_matching_container<R>(d);
  constexpr auto key = scan::scalar | decode::string_source |
                       construct<std::string> | decode::json_unescape |
                       decode::toml_unescape | validate::utf8;
  auto object = [&]<class E>(E&) {
    struct Choices {
      Try<E> candidate;
      Try<E> fallback;
    } choices;
    const auto error = decode_into<R>(choices, R"([{"ok":1,"next":"bad"}])");
    EXPECT_EQ(error.code, EC::invalid_number);
    EXPECT_EQ(error.path, (ErrorPath{P::element(0), P::key("next", 1)}));
  };
  Elem<scan::object | decode::collect<key, number> | store::assign, noop_encoder>
      e;
  Elem<scan::object | decode::collect<key, number, 2> | store::assign,
       noop_encoder>
      f;
  Elem<scan::object | decode::each<key | store::assign, number | store::assign>,
       noop_encoder>
      g;
  Elem<scan::object |
           decode::each<key | store::assign, number | store::assign, 2>,
       noop_encoder>
      h;
  object(e);
  object(f);
  object(g);
  object(h);
  // A rejected candidate consumed a nested value before fallback. Its path
  // must be discarded before a later sibling produces a hard error.
  using Rejected = Elem<scan::array |
                            decode::collect<scan::scalar | decode::number<int> |
                                            match::literal<1>> |
                            store::assign,
                        noop_encoder>;
  using Accepted =
      Elem<scan::array | decode::collect<scan::scalar | decode::number<int>> |
               store::assign,
           noop_encoder>;
  struct Siblings {
    Try<Rejected> first;
    Try<Accepted> fallback;
  } siblings;
  const auto error = decode_into<R>(siblings, R"([[2],["bad"]])");
  EXPECT_EQ(error.code, EC::invalid_number);
  EXPECT_EQ(error.path, (ErrorPath{P::element(1), P::element(0)}));
}
TEST(ErrorPath, MatchingEachAndCollectKeepHardFailuresAndDiscardSoftPaths) {
  check_matching_containers<json::SimpleReader>();
  check_matching_containers<json::FastReader<>>();
}

template <class R>
void check_prefailed_reader() {
  Root value;
  R reader;
  typename R::state_type state;
  reader.reset(state, R"({"items":[]})");
  ASSERT_TRUE(reader.next_token(state));
  Error original{EC::validation_failed, 17, "already failed"};
  reader.report_error(state, original);
  const auto position = state.pos;
  const auto token = reader.current_token(state);
  const auto error = Decoder<R>::decode_nested(value, reader, state);
  EXPECT_EQ(error.code, original.code);
  EXPECT_EQ(error.offset, original.offset);
  EXPECT_EQ(error.detail, original.detail);
  EXPECT_TRUE(error.path.empty());
  EXPECT_TRUE(reader.error(state).path.empty());
  EXPECT_EQ(state.pos, position);
  EXPECT_EQ(reader.current_token(state).type, token.type);
  EXPECT_EQ(reader.current_token(state).offset, token.offset);
  EXPECT_TRUE(value.items->empty());
}
TEST(ErrorPath, PrefailedReaderPreservesMetadataAndPosition) {
  check_prefailed_reader<json::SimpleReader>();
  check_prefailed_reader<json::FastReader<>>();
}

struct SourceFailure {
  static inline int calls = 0;
  static inline bool seed_path = false;
  template <class C>
  auto operator()(int, C& ctx) const -> int {
    ++calls;
    Error error{EC::validation_failed,
                ctx.reader().current_token(ctx.reader_state()).offset,
                "source callback detail"};
    if (seed_path) error.path = {P::key("user supplied"), P::element(9)};
    ctx.reader().report_error(ctx.reader_state(), std::move(error));
    return 0;
  }
};

template <class R>
void check_callback_and_cursor() {
  struct Record {
    Entry<Key<"items">, v::Array<Elem<scan::scalar | decode::number<int> |
                                          step<SourceFailure> | store::assign,
                                      noop_encoder>>>
        items;
  } value;
  constexpr auto input = R"({"items":[7,8]})";
  R reader;
  typename R::state_type state;
  reader.reset(state, input);
  ASSERT_TRUE(reader.next_token(state));
  SourceFailure::calls = 0;
  SourceFailure::seed_path = false;
  const auto error = Decoder<R>::decode_nested(value, reader, state);
  EXPECT_EQ(SourceFailure::calls, 1);
  EXPECT_EQ(error.code, EC::validation_failed);
  EXPECT_EQ(error.offset, 10U);
  EXPECT_EQ(error.detail, "source callback detail");
  EXPECT_EQ(error.path, (ErrorPath{P::key("items"), P::element(0)}));
  const auto position = state.pos;
  const auto token = reader.current_token(state);

  Record baseline;
  R baseline_reader;
  typename R::state_type baseline_state;
  baseline_reader.reset(baseline_state, input);
  ASSERT_TRUE(baseline_reader.next_token(baseline_state));
  SourceFailure::calls = 0;
  const auto plain =
      Decoder<R>::decode_nested(baseline, baseline_reader, baseline_state,
                                DecodeOptions{.error_path = false});
  EXPECT_EQ(SourceFailure::calls, 1);
  EXPECT_TRUE(plain.path.empty());
  EXPECT_EQ(plain.code, error.code);
  EXPECT_EQ(plain.offset, error.offset);
  EXPECT_EQ(plain.detail, error.detail);
  EXPECT_EQ(baseline_state.pos, position);
  EXPECT_EQ(baseline_reader.current_token(baseline_state).type, token.type);
  EXPECT_EQ(baseline_reader.current_token(baseline_state).offset, token.offset);

  SourceFailure::calls = 0;
  SourceFailure::seed_path = true;
  const auto seeded = decode_into<R>(value, input);
  EXPECT_EQ(SourceFailure::calls, 1);
  // Public paths are constructed from the failing input at the API boundary.
  // report_error retains only code, offset, and detail in a built-in reader.
  EXPECT_EQ(seeded.path, error.path);
  EXPECT_EQ(seeded.code, error.code);
  EXPECT_EQ(seeded.offset, error.offset);
  EXPECT_EQ(seeded.detail, error.detail);
  SourceFailure::seed_path = false;
  Decoder<R> decoder;
  auto check_public = [&](auto invoke) {
    SourceFailure::calls = 0;
    const auto wrapped = invoke();
    static_assert(std::is_same_v<std::remove_cv_t<decltype(wrapped)>, Error>);
    EXPECT_EQ(SourceFailure::calls, 1);
    EXPECT_EQ(wrapped.code, error.code);
    EXPECT_EQ(wrapped.offset, error.offset);
    EXPECT_EQ(wrapped.detail, error.detail);
    EXPECT_EQ(wrapped.path, error.path);
  };
  check_public([&] { return decoder.decode(value, input); });
  check_public([&] { return decoder.decode_document(value, input); });
  check_public([&] { return decode_document_into<R>(value, input); });
}
TEST(ErrorPath, SourceCallbacksRunOnceAndReaderCursorIsUnchanged) {
  check_callback_and_cursor<json::SimpleReader>();
  check_callback_and_cursor<json::FastReader<>>();
}

template <class R>
void check_opt_out() {
  Root value;
  constexpr auto input = R"({"items":[{"abc":{"count":128}}]})";
  const DecodeOptions options{.error_path = false};
  Decoder<R> decoder;
  const auto original = decoder.decode(value, input);
  ASSERT_TRUE(original.has_error());
  for (const auto error : {decoder.decode(value, input, options),
                           decoder.decode_document(value, input, options),
                           decode_into<R>(value, input, options),
                           decode_document_into<R>(value, input, options)}) {
    EXPECT_TRUE(error.path.empty());
    EXPECT_EQ(error.code, original.code);
    EXPECT_EQ(error.offset, original.offset);
    EXPECT_EQ(error.detail, original.detail);
  }
}
TEST(ErrorPath, OptOutPreservesFailureAcrossPublicEntryPoints) {
  check_opt_out<json::SimpleReader>();
  check_opt_out<json::FastReader<>>();
}

struct UppercaseKey {
  auto operator()(std::string key) const -> std::string {
    for (auto& c : key)
      if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    return key;
  }
};
template <class R>
void check_source_keys() {
  struct Record {
    Entry<Key<"abc">, v::Int8> abc;
  } record;
  v::Mapping<v::String, v::Int8> mapping;
  using ConvertedKey =
      Elem<scan::scalar | decode::string_source | construct<std::string> |
               decode::json_unescape | step<UppercaseKey> | store::assign,
           noop_encoder>;
  v::Mapping<ConvertedKey, v::Int8> converted;
  constexpr auto input = R"({"a\u0062c":128})";
  const auto expected = ErrorPath{P::key("abc")};
  EXPECT_EQ(decode_into<R>(record, input).path, expected);
  EXPECT_EQ(decode_into<R>(mapping, input).path, expected);
  EXPECT_EQ(decode_into<R>(converted, input).path, expected);
}
TEST(ErrorPath, InputKeysDoNotDependOnSchemaOrKeyTransformations) {
  check_source_keys<json::SimpleReader>();
  check_source_keys<json::FastReader<>>();
}

TEST(ErrorPath, TupleFailureStopsBeforeLaterSibling) {
  struct Record {
    v::Int8 first;
    Observed later;
  } value;
  Observe::calls = 0;
  const auto error = decode_into<json::SimpleReader>(value, "[128,7]");
  EXPECT_EQ(error.code, EC::out_of_range);
  EXPECT_EQ(error.path, (ErrorPath{P::element(0)}));
  EXPECT_EQ(Observe::calls, 0);
}

TEST(ErrorPath, ReplayOccursOnlyForEnabledFailures) {
  Root value;
  CustomReader::resets = 0;
  CustomReader::advances = 0;
  ASSERT_TRUE(decode_into<CustomReader>(value, R"({"items":[]})").is_ok());
  EXPECT_EQ(CustomReader::resets, 1);
  EXPECT_GT(CustomReader::advances, 0);

  constexpr auto input = R"({"items":[{"abc":{"count":128}}]})";
  CustomReader::resets = 0;
  CustomReader::advances = 0;
  const auto plain = decode_into<CustomReader>(
      value, input, DecodeOptions{.error_path = false});
  ASSERT_TRUE(plain.has_error());
  EXPECT_EQ(CustomReader::resets, 1);
  const auto ordinary_advances = CustomReader::advances;

  CustomReader::resets = 0;
  CustomReader::advances = 0;
  const auto located = decode_into<CustomReader>(value, input);
  EXPECT_EQ(located.code, plain.code);
  EXPECT_EQ(located.offset, plain.offset);
  EXPECT_EQ(located.detail, plain.detail);
  EXPECT_EQ(CustomReader::resets, 2);
  EXPECT_GT(CustomReader::advances, ordinary_advances);
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
