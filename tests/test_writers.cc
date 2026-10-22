#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <limits>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "hosq/bson/reader.hh"
#include "hosq/bson/writer.hh"
#include "hosq/hosq.hh"
#include "hosq/json/compact_writer.hh"
#include "hosq/json/simple_reader.hh"
#include "hosq/msgpack/reader.hh"
#include "hosq/msgpack/writer.hh"
#include "hosq/toml/reader.hh"
#include "hosq/toml/writer.hh"

namespace hosq::ext {
template <>
struct EncodeValue<msgpack::Writer<7, 4096>> {
  template <class T>
  auto operator()(const T&, msgpack::Writer<7, 4096>&) const -> bool {
    return false;
  }
};
}  // namespace hosq::ext

namespace {
using namespace hosq;
using EC = ErrorCode;
using TT = TokenType;
std::string octets(std::initializer_list<unsigned> list) {
  std::string out;
  for (auto v : list) out += static_cast<char>(v);
  return out;
}
std::string little(std::uint64_t value, unsigned n = 4) {
  std::string out;
  for (unsigned i = 0; i < n; ++i) out += static_cast<char>(value >> (8 * i));
  return out;
}
std::string document(std::string body) {
  return little(body.size() + 5) + body + '\0';
}
std::string member(unsigned type, std::string key, std::string payload) {
  return static_cast<char>(type) + key + '\0' + payload;
}
template <class W>
bool token(W& writer, TT type) {
  (void)writer.write({0, type, 0});
  return writer.error().is_ok();
}
template <class R>
Error valid(std::string_view bytes) {
  R r;
  typename R::state_type s;
  r.reset(s, bytes);
  while (r.next_token(s))
    if (s.token.type == TT::End) break;
  return r.error(s);
}
template <class T>
struct Slot {
  template <class Input>
  using storage_type = T;
  template <class Context>
  void operator()(Context&) const {}
};
template <class T>
using Leaf = Elem<step<Slot<T>>, encode::value>;
template <class T>
using Number =
    Elem<scan::scalar | decode::number<T> | store::assign, encode::value>;
struct ReadOwned {
  template <class Context>
  auto operator()(const ScalarView& view, const Context& ctx) const
      -> std::string {
    std::string text;
    if constexpr (requires {
                    ctx.reader().read_string(ctx.reader_state(), text);
                  })
      (void)ctx.reader().read_string(ctx.reader_state(), text);
    else {
      using R = std::remove_cvref_t<decltype(ctx.reader())>;
      text = ext::DecodeStringView<R>{}(view, ctx);
    }
    return text;
  }
};
using Text = Elem<scan::scalar | step<ReadOwned> | store::assign, encode::value>;
using Boolean =
    Elem<scan::scalar | decode::boolean | store::assign, encode::value>;
struct Child {
  Try<Entry<Key<"n">, Number<int>>> n;
};
using ChildValue =
    Elem<scan::object | decode::to<Child> | store::assign, encode::value>;
using Children =
    Elem<scan::array | decode::collect<scan::object | decode::to<Child>> |
             store::assign,
         encode::value>;
struct Record {
  Try<Entry<Key<"n">, Number<int>>> n;
  Try<Entry<Key<"text">, Text>> text;
  Try<Entry<Key<"ok">, Boolean>> ok;
  Try<Entry<Key<"items">, Children>> items;
  Try<Entry<Key<"meta">, ChildValue>> meta;
};
Record sample() {
  Record r;
  *r.n = 42;
  *r.text = std::string("a\n\"\\😀\0z", 11);
  *r.ok = true;
  r.items->resize(2);
  *(*r.items)[0].n = 1;
  *(*r.items)[1].n = 2;
  *r.meta->n = -7;
  return r;
}
template <class W, class R>
void roundtrip(const Record& original = sample()) {
  std::string output;
  W writer(output);
  ASSERT_TRUE(encode_into(original, writer)) << writer.error().detail;
  Record decoded;
  ASSERT_TRUE(decode_into<R>(decoded, output).is_ok());
  EXPECT_EQ(*decoded.n, 42);
  EXPECT_EQ(*decoded.text, *original.text);
  EXPECT_TRUE(*decoded.ok);
  ASSERT_EQ(decoded.items->size(), 2u);
  EXPECT_EQ(*(*decoded.items)[0].n, 1);
  EXPECT_EQ(*(*decoded.items)[1].n, 2);
  EXPECT_EQ(*decoded.meta->n, -7);
}
TEST(EncodeFormats, SameSchemaRoundTripsAllNewWriters) {
  roundtrip<msgpack::Writer<>, msgpack::Reader<>>();
  roundtrip<bson::Writer<>, bson::Reader<>>();
  roundtrip<toml::Writer<>, toml::Reader<>>();
}
TEST(EncodeFormats, JsonSchemaRoundTrip) {
  auto original = sample();
  *original.text = "日本語";
  roundtrip<json::CompactWriter<>, json::SimpleReader>(original);
}
TEST(EncodeFormats, SemanticPipelineWorksWithExistingJsonWriter) {
  Child c;
  *c.n = 42;
  std::string out;
  json::CompactWriter writer(out);
  ASSERT_TRUE(encode_into(c, writer));
  EXPECT_EQ(out, "{\"n\":42}");
  Leaf<std::string> text;
  text.value() = "a\n\"\\";
  std::string strings;
  json::CompactWriter sw(strings);
  ASSERT_TRUE(encode_into(text, sw));
  EXPECT_EQ(strings, "\"a\\n\\\"\\\\\"");
}
TEST(EncodeFormats, ElemTupleAndTryElemTupleUseExistingArraySchema) {
  struct Pair {
    Number<int> a;
    Number<int> b;
  };
  Pair pair;
  pair.a.value() = 1;
  pair.b.value() = 2;
  std::string out;
  msgpack::Writer writer(out);
  ASSERT_TRUE(encode_into(pair, writer));
  EXPECT_EQ(out, octets({0x92, 1, 2}));
  struct Tried {
    Try<Number<int>> a;
    Try<Number<int>> b;
  };
  Tried tried;
  tried.a.value() = 3;
  tried.b.value() = 4;
  std::string second;
  msgpack::Writer tw(second);
  ASSERT_TRUE(encode_into(tried, tw));
  EXPECT_EQ(second, octets({0x92, 3, 4}));
}
TEST(MessagePackWriter, IntegerWidthTransitionsMatchSpecification) {
  const std::vector<std::pair<std::uint64_t, std::string>> unsigned_cases{
      {0, octets({0})},
      {127, octets({127})},
      {128, octets({0xcc, 128})},
      {255, octets({0xcc, 255})},
      {256, octets({0xcd, 1, 0})},
      {65535, octets({0xcd, 255, 255})},
      {65536, octets({0xce, 0, 1, 0, 0})},
      {0xffffffffULL, octets({0xce, 255, 255, 255, 255})},
      {0x100000000ULL, octets({0xcf, 0, 0, 0, 1, 0, 0, 0, 0})},
      {std::numeric_limits<std::uint64_t>::max(),
       octets({0xcf, 255, 255, 255, 255, 255, 255, 255, 255})}};
  for (const auto& [value, expected] : unsigned_cases) {
    std::string out;
    msgpack::Writer w(out);
    ASSERT_TRUE(w.write_number(value));
    ASSERT_TRUE(token(w, TT::End));
    EXPECT_EQ(out, expected);
  }
  const std::vector<std::pair<std::int64_t, std::string>> signed_cases{
      {-1, octets({255})},
      {-32, octets({0xe0})},
      {-33, octets({0xd0, 0xdf})},
      {-128, octets({0xd0, 0x80})},
      {-129, octets({0xd1, 255, 0x7f})},
      {-32768, octets({0xd1, 0x80, 0})},
      {-32769, octets({0xd2, 255, 255, 0x7f, 255})},
      {std::numeric_limits<std::int32_t>::min(), octets({0xd2, 0x80, 0, 0, 0})},
      {-2147483649LL, octets({0xd3, 255, 255, 255, 255, 0x7f, 255, 255, 255})},
      {std::numeric_limits<std::int64_t>::min(),
       octets({0xd3, 0x80, 0, 0, 0, 0, 0, 0, 0})}};
  for (const auto& [value, expected] : signed_cases) {
    std::string out;
    msgpack::Writer w(out);
    ASSERT_TRUE(w.write_number(value));
    ASSERT_TRUE(token(w, TT::End));
    EXPECT_EQ(out, expected);
  }
}
TEST(MessagePackWriter, FloatBooleanNilAndBinaryHaveDistinctWireTypes) {
  std::string out;
  msgpack::Writer w(out);
  ASSERT_TRUE(token(w, TT::ArrayBegin));
  ASSERT_TRUE(w.write_number(1.5f));
  ASSERT_TRUE(w.write_number(1.5));
  ASSERT_TRUE(w.write_boolean(true));
  ASSERT_TRUE(w.write_boolean(false));
  ASSERT_TRUE(w.write_null());
  const auto binary = octets({0, 255});
  ASSERT_TRUE(w.write_binary(BytesView::from_string_view(binary)));
  ASSERT_TRUE(token(w, TT::ArrayEnd));
  ASSERT_TRUE(token(w, TT::End));
  EXPECT_EQ(out, octets({0x96, 0xca, 0x3f, 0xc0, 0, 0,  0xcb, 0x3f,
                         0xf8, 0,    0,    0,    0, 0,  0,    0xc3,
                         0xc2, 0xc0, 0xc4, 2,    0, 255}));
  EXPECT_TRUE(valid<msgpack::Reader<>>(out).is_ok());
}
TEST(MessagePackWriter, StringLengthTransitionsMatchSpecification) {
  for (auto n : {0u, 31u, 32u, 255u, 256u, 65535u, 65536u}) {
    std::string out;
    msgpack::Writer w(out);
    ASSERT_TRUE(w.write_string(std::string(n, 'a')));
    ASSERT_TRUE(token(w, TT::End));
    std::string header;
    if (n <= 31)
      header = octets({0xa0 + n});
    else if (n <= 255)
      header = octets({0xd9, n});
    else if (n <= 65535)
      header = octets({0xda, n >> 8, n & 255});
    else
      header = octets({0xdb, 0, 1, 0, 0});
    EXPECT_EQ(out, header + std::string(n, 'a'));
    EXPECT_TRUE(valid<msgpack::Reader<>>(out).is_ok());
  }
}
TEST(MessagePackWriter, ArrayCountTransitionsAndNestedBackpatching) {
  for (auto n : {0u, 15u, 16u, 65535u, 65536u}) {
    std::string out;
    msgpack::Writer w(out);
    ASSERT_TRUE(token(w, TT::ArrayBegin));
    for (unsigned i = 0; i < n; ++i) ASSERT_TRUE(w.write_number(0));
    ASSERT_TRUE(token(w, TT::ArrayEnd));
    ASSERT_TRUE(token(w, TT::End));
    const auto header = n <= 15      ? octets({0x90 + n})
                        : n <= 65535 ? octets({0xdc, n >> 8, n & 255})
                                     : octets({0xdd, 0, 1, 0, 0});
    EXPECT_EQ(out, header + std::string(n, 0));
  }
  std::string out;
  msgpack::Writer w(out);
  ASSERT_TRUE(token(w, TT::ArrayBegin));
  ASSERT_TRUE(token(w, TT::ObjectBegin));
  ASSERT_TRUE(w.write_string("k"));
  ASSERT_TRUE(token(w, TT::ArrayBegin));
  for (int i = 0; i < 16; ++i) ASSERT_TRUE(w.write_number(i));
  ASSERT_TRUE(token(w, TT::ArrayEnd));
  ASSERT_TRUE(token(w, TT::ObjectEnd));
  ASSERT_TRUE(w.write_number(42));
  ASSERT_TRUE(token(w, TT::ArrayEnd));
  ASSERT_TRUE(token(w, TT::End));
  EXPECT_EQ(out, octets({0x92, 0x81, 0xa1}) + "k" +
                     octets({0xdc, 0, 16, 0,  1,  2,  3,  4,  5,  6,
                             7,    8, 9,  10, 11, 12, 13, 14, 15, 42}));
}
TEST(MessagePackWriter, MapCountsContainerKeysAndExtensions) {
  for (auto n : {0u, 15u, 16u}) {
    std::string out;
    msgpack::Writer w(out);
    ASSERT_TRUE(token(w, TT::ObjectBegin));
    for (unsigned i = 0; i < n; ++i) {
      ASSERT_TRUE(w.write_number(i));
      ASSERT_TRUE(w.write_null());
    }
    ASSERT_TRUE(token(w, TT::ObjectEnd));
    ASSERT_TRUE(token(w, TT::End));
    EXPECT_EQ(static_cast<unsigned char>(out[0]), n <= 15 ? 0x80 + n : 0xde);
    EXPECT_TRUE(valid<msgpack::Reader<>>(out).is_ok());
  }
  std::string out;
  msgpack::Writer w(out);
  ASSERT_TRUE(token(w, TT::ObjectBegin));
  ASSERT_TRUE(token(w, TT::ArrayBegin));
  ASSERT_TRUE(w.write_number(1));
  ASSERT_TRUE(token(w, TT::ArrayEnd));
  ASSERT_TRUE(w.write_boolean(true));
  ASSERT_TRUE(token(w, TT::ObjectEnd));
  ASSERT_TRUE(token(w, TT::End));
  EXPECT_EQ(out, octets({0x81, 0x91, 1, 0xc3}));
  for (auto n : {0u, 1u, 2u, 3u, 4u, 8u, 16u, 256u}) {
    const std::string payload(n, 'x');
    std::string encoded;
    msgpack::Writer ew(encoded);
    ASSERT_TRUE(ew.write_extension(7, BytesView::from_string_view(payload)));
    ASSERT_TRUE(token(ew, TT::End));
    EXPECT_TRUE(valid<msgpack::Reader<>>(encoded).is_ok());
    if (n == 3) EXPECT_EQ(encoded, octets({0xc7, 3, 7}) + payload);
    if (n == 4) EXPECT_EQ(encoded, octets({0xd6, 7}) + payload);
  }
}
TEST(BsonWriter, KnownHelloWorldEncodingAndIntegerTypes) {
  std::string out;
  bson::Writer w(out);
  ASSERT_TRUE(token(w, TT::ObjectBegin));
  ASSERT_TRUE(w.write_string("hello"));
  ASSERT_TRUE(w.write_string("world"));
  ASSERT_TRUE(token(w, TT::ObjectEnd));
  ASSERT_TRUE(token(w, TT::End));
  EXPECT_EQ(out, octets({22, 0, 0, 0, 2}) + "hello" + '\0' + little(6) +
                     "world" + octets({0, 0}));
  std::string numbers;
  bson::Writer n(numbers);
  ASSERT_TRUE(token(n, TT::ObjectBegin));
  ASSERT_TRUE(n.write_string("i"));
  ASSERT_TRUE(n.write_number(42));
  ASSERT_TRUE(n.write_string("l"));
  ASSERT_TRUE(n.write_number(std::int64_t{42}));
  ASSERT_TRUE(n.write_string("u"));
  ASSERT_TRUE(n.write_number(std::uint32_t{0xffffffff}));
  ASSERT_TRUE(token(n, TT::ObjectEnd));
  ASSERT_TRUE(token(n, TT::End));
  EXPECT_EQ(numbers, document(member(16, "i", little(42)) +
                              member(18, "l", little(42, 8)) +
                              member(18, "u", little(0xffffffff, 8))));
}
TEST(BsonWriter, NestedArrayIndexesAndScalarPayloadSizes) {
  std::string out;
  bson::Writer w(out);
  ASSERT_TRUE(token(w, TT::ObjectBegin));
  ASSERT_TRUE(w.write_string("items"));
  ASSERT_TRUE(token(w, TT::ArrayBegin));
  ASSERT_TRUE(w.write_number(1));
  ASSERT_TRUE(w.write_number(2));
  ASSERT_TRUE(token(w, TT::ArrayEnd));
  ASSERT_TRUE(w.write_string("s"));
  ASSERT_TRUE(w.write_string(std::string("a\0b", 3)));
  ASSERT_TRUE(w.write_string("b"));
  ASSERT_TRUE(w.write_boolean(true));
  ASSERT_TRUE(w.write_string("z"));
  ASSERT_TRUE(w.write_null());
  const auto bytes = octets({0, 255});
  ASSERT_TRUE(w.write_string("bin"));
  ASSERT_TRUE(w.write_binary(BytesView::from_string_view(bytes)));
  ASSERT_TRUE(token(w, TT::ObjectEnd));
  ASSERT_TRUE(token(w, TT::End));
  EXPECT_EQ(out, document(member(4, "items",
                                 document(member(16, "0", little(1)) +
                                          member(16, "1", little(2)))) +
                          member(2, "s", little(4) + std::string("a\0b\0", 4)) +
                          member(8, "b", octets({1})) + member(10, "z", "") +
                          member(5, "bin", little(2) + octets({0, 0, 255}))));
  EXPECT_TRUE(valid<bson::Reader<>>(out).is_ok());
}
TEST(BsonWriter, RootKeyAndUint64ConstraintsAreErrors) {
  std::string out;
  bson::Writer w(out);
  EXPECT_FALSE(w.write_number(42));
  EXPECT_EQ(w.error().code, EC::unsupported_type);
  std::string array;
  bson::Writer aw(array);
  EXPECT_FALSE(token(aw, TT::ArrayBegin));
  EXPECT_EQ(aw.error().code, EC::unsupported_type);
  std::string keys;
  bson::Writer kw(keys);
  ASSERT_TRUE(token(kw, TT::ObjectBegin));
  EXPECT_FALSE(kw.write_string(std::string("a\0b", 3)));
  EXPECT_EQ(kw.error().code, EC::invalid_token);
  std::string range;
  bson::Writer rw(range);
  ASSERT_TRUE(token(rw, TT::ObjectBegin));
  ASSERT_TRUE(rw.write_string("x"));
  EXPECT_FALSE(rw.write_number(std::numeric_limits<std::uint64_t>::max()));
  EXPECT_EQ(rw.error().code, EC::out_of_range);
}
TEST(TomlWriter, QuotesKeysStringsAndPreservesFloatType) {
  std::string out;
  toml::Writer w(out);
  ASSERT_TRUE(token(w, TT::ObjectBegin));
  ASSERT_TRUE(w.write_string("a.b\n\""));
  ASSERT_TRUE(w.write_string("x\n\"\\"));
  ASSERT_TRUE(w.write_string("f"));
  ASSERT_TRUE(w.write_number(1.0));
  ASSERT_TRUE(w.write_string("z"));
  ASSERT_TRUE(w.write_number(-0.0));
  ASSERT_TRUE(token(w, TT::ObjectEnd));
  ASSERT_TRUE(token(w, TT::End));
  EXPECT_EQ(out,
            "\"a.b\\n\\\"\" = \"x\\n\\\"\\\\\"\n\"f\" = 1.0\n\"z\" = -0.0\n");
  EXPECT_TRUE(valid<toml::Reader<>>(out).is_ok());
}
TEST(TomlWriter, InlineTablesAndArraysOfTablesAreEquivalentValues) {
  Record data = sample();
  *data.text = "plain";
  data.items->clear();
  std::string out;
  toml::Writer writer(out);
  ASSERT_TRUE(encode_into(data, writer));
  EXPECT_EQ(out,
            "\"n\" = 42\n\"text\" = \"plain\"\n\"ok\" = true\n\"items\" = "
            "[]\n\"meta\" = {\"n\" = -7}\n");
  data.items->resize(2);
  *(*data.items)[0].n = 1;
  *(*data.items)[1].n = 2;
  std::string second;
  toml::Writer tw(second);
  ASSERT_TRUE(encode_into(data, tw));
  EXPECT_NE(second.find("\"items\" = [{\"n\" = 1}, {\"n\" = 2}]"),
            std::string::npos);
}
TEST(TomlWriter, DuplicateKeysNullBinaryAndRootArraysAreRejected) {
  std::string out;
  toml::Writer w(out);
  ASSERT_TRUE(token(w, TT::ObjectBegin));
  ASSERT_TRUE(w.write_string("x"));
  ASSERT_TRUE(w.write_number(1));
  EXPECT_FALSE(w.write_string("x"));
  EXPECT_EQ(w.error().code, EC::duplicate_member);
  std::string null;
  toml::Writer nw(null);
  EXPECT_FALSE(nw.write_null());
  EXPECT_EQ(nw.error().code, EC::unsupported_type);
  std::string binary;
  toml::Writer bw(binary);
  EXPECT_FALSE(bw.write_binary(BytesView::from_string_view("")));
  EXPECT_EQ(bw.error().code, EC::unsupported_type);
  std::string array;
  toml::Writer aw(array);
  EXPECT_FALSE(token(aw, TT::ArrayBegin));
  EXPECT_EQ(aw.error().code, EC::unsupported_type);
  std::string range;
  toml::Writer rw(range);
  ASSERT_TRUE(token(rw, TT::ObjectBegin));
  ASSERT_TRUE(rw.write_string("x"));
  EXPECT_FALSE(rw.write_number(std::numeric_limits<std::uint64_t>::max()));
  EXPECT_EQ(rw.error().code, EC::out_of_range);
}
TEST(EncodeFormats, NonFiniteAndExtremeFloatRoundTrips) {
  for (double value : {0.1, -0.0, std::numeric_limits<double>::denorm_min(),
                       std::numeric_limits<double>::max(),
                       std::numeric_limits<double>::infinity(),
                       -std::numeric_limits<double>::infinity(),
                       std::numeric_limits<double>::quiet_NaN()}) {
    struct FloatDoc {
      Try<Entry<Key<"x">, Number<double>>> x;
    };
    FloatDoc doc;
    *doc.x = value;
    auto verify = [&]<class W, class R>() {
      std::string out;
      W writer(out);
      ASSERT_TRUE(encode_into(doc, writer));
      FloatDoc decoded;
      ASSERT_TRUE(decode_into<R>(decoded, out).is_ok());
      if (std::isnan(value))
        EXPECT_TRUE(std::isnan(*decoded.x));
      else {
        EXPECT_EQ(*decoded.x, value);
        EXPECT_EQ(std::signbit(*decoded.x), std::signbit(value));
      }
    };
    verify.template operator()<msgpack::Writer<>, msgpack::Reader<>>();
    verify.template operator()<bson::Writer<>, bson::Reader<>>();
    verify.template operator()<toml::Writer<>, toml::Reader<>>();
  }
}
TEST(EncodeFormats, OptionalVariantAndMapStorageAreExplicit) {
  Leaf<std::optional<int>> value;
  std::string out;
  msgpack::Writer w(out);
  EXPECT_FALSE(encode_into(value, w));
  EXPECT_EQ(w.error().code, EC::invalid_object_state);
  value.value() = 42;
  std::string second;
  msgpack::Writer sw(second);
  ASSERT_TRUE(encode_into(value, sw));
  EXPECT_EQ(second, octets({42}));
  Leaf<std::variant<int, std::string>> choice;
  choice.value() = std::string("a");
  std::string variant;
  msgpack::Writer vw(variant);
  ASSERT_TRUE(encode_into(choice, vw));
  EXPECT_EQ(variant, octets({0xa1}) + "a");
  Map<std::vector<std::string>, std::vector<int>> map;
  map.keys() = {"x"};
  map.values() = {1, 2};
  std::string mismatch;
  msgpack::Writer mw(mismatch);
  EXPECT_FALSE(ext::EncodeValue<decltype(mw)>{}(map, mw));
  EXPECT_EQ(mw.error().code, EC::size_mismatch);
  map.keys() = {"x", "y"};
  std::string valid_map;
  msgpack::Writer vm(valid_map);
  ASSERT_TRUE(ext::EncodeValue<decltype(vm)>{}(map, vm));
  ASSERT_TRUE(token(vm, TT::End));
  EXPECT_EQ(valid_map,
            octets({0x82, 0xa1}) + "x" + octets({1, 0xa1}) + "y" + octets({2}));
  Map<int, int> scratch;
  std::string unsupported;
  msgpack::Writer uw(unsupported);
  EXPECT_FALSE(ext::EncodeValue<decltype(uw)>{}(scratch, uw));
  EXPECT_EQ(uw.error().code, EC::unsupported_type);
}
template <class W>
void state_checks() {
  std::string out;
  W writer(out);
  ASSERT_TRUE(token(writer, TT::ObjectBegin));
  auto buffer = writer.write({1, TT::Scalar, 1});
  ASSERT_EQ(buffer.size(), 1u);
  buffer[0] = 'x';
  EXPECT_FALSE(token(writer, TT::ObjectEnd));
  EXPECT_EQ(writer.error().code, EC::invalid_state);
  const auto first = writer.error();
  EXPECT_FALSE(writer.commit(1));
  EXPECT_EQ(writer.error().code, first.code);
  std::string wrong;
  W ww(wrong);
  ASSERT_TRUE(token(ww, TT::ObjectBegin));
  EXPECT_FALSE(token(ww, TT::ArrayEnd));
  EXPECT_EQ(ww.error().code, EC::invalid_state);
  std::string finished;
  W fw(finished);
  ASSERT_TRUE(token(fw, TT::ObjectBegin));
  ASSERT_TRUE(token(fw, TT::ObjectEnd));
  ASSERT_TRUE(token(fw, TT::End));
  EXPECT_FALSE(fw.write_string("later"));
  EXPECT_EQ(fw.error().code, EC::invalid_state);
}
TEST(EncodeFormats, WriterStateAndFirstErrorAreRetained) {
  state_checks<msgpack::Writer<>>();
  state_checks<bson::Writer<>>();
  state_checks<toml::Writer<>>();
}
TEST(EncodeFormats, StringReservationLengthAndCommitAreChecked) {
  std::string out;
  msgpack::Writer w(out);
  auto buffer = w.write({3, TT::Scalar, 1});
  ASSERT_EQ(buffer.size(), 3u);
  buffer[0] = 'a';
  buffer[1] = 'b';
  ASSERT_TRUE(w.commit(2));
  ASSERT_TRUE(token(w, TT::End));
  EXPECT_EQ(out, octets({0xa2}) + "ab");
  std::string bad;
  msgpack::Writer bw(bad);
  (void)bw.write({2, TT::Scalar, 1});
  EXPECT_FALSE(bw.commit(3));
  EXPECT_EQ(bw.error().code, EC::invalid_state);
  std::string number;
  msgpack::Writer nw(number);
  (void)nw.write({2, TT::Scalar, 2});
  EXPECT_EQ(nw.error().code, EC::invalid_token);
  std::string huge;
  bson::Writer hw(huge);
  (void)hw.write({0x7fffffffULL, TT::Scalar, 1});
  EXPECT_EQ(hw.error().code, EC::output_too_large);
  std::string larger;
  msgpack::Writer lw(larger);
  (void)lw.write({0x100000000ULL, TT::Scalar, 1});
  EXPECT_EQ(lw.error().code, EC::output_too_large);
}
TEST(EncodeFormats, DepthOutputAndUtf8LimitsAreChecked) {
  std::string out;
  msgpack::Writer<1> w(out);
  ASSERT_TRUE(token(w, TT::ArrayBegin));
  EXPECT_FALSE(token(w, TT::ObjectBegin));
  EXPECT_EQ(w.error().code, EC::depth_limit_exceeded);
  std::string bson;
  bson::Writer<1> bw(bson);
  ASSERT_TRUE(token(bw, TT::ObjectBegin));
  ASSERT_TRUE(bw.write_string("x"));
  EXPECT_FALSE(token(bw, TT::ObjectBegin));
  EXPECT_EQ(bw.error().code, EC::depth_limit_exceeded);
  std::string toml;
  toml::Writer<1> tw(toml);
  ASSERT_TRUE(token(tw, TT::ObjectBegin));
  ASSERT_TRUE(tw.write_string("x"));
  EXPECT_FALSE(token(tw, TT::ArrayBegin));
  EXPECT_EQ(tw.error().code, EC::depth_limit_exceeded);
  std::string minimal;
  msgpack::Writer<1, 1> one(minimal);
  ASSERT_TRUE(token(one, TT::ArrayBegin));
  ASSERT_TRUE(token(one, TT::ArrayEnd));
  ASSERT_TRUE(token(one, TT::End));
  EXPECT_EQ(minimal, octets({0x90}));
  std::string limited;
  bson::Writer<64, 4> limit(limited);
  ASSERT_TRUE(token(limit, TT::ObjectBegin));
  EXPECT_FALSE(token(limit, TT::ObjectEnd));
  EXPECT_EQ(limit.error().code, EC::output_too_large);
  std::string text;
  toml::Writer<64, 4> textlimit(text);
  ASSERT_TRUE(token(textlimit, TT::ObjectBegin));
  EXPECT_FALSE(textlimit.write_string("long key"));
  EXPECT_EQ(textlimit.error().code, EC::output_too_large);
  const auto invalid = octets({0xc0, 0xaf});
  auto check = [&]<class W>() {
    std::string bytes;
    W writer(bytes);
    ASSERT_TRUE(token(writer, TT::ObjectBegin));
    EXPECT_FALSE(writer.write_string(invalid));
    EXPECT_EQ(writer.error().code, EC::invalid_utf8);
  };
  check.template operator()<msgpack::Writer<>>();
  check.template operator()<bson::Writer<>>();
  check.template operator()<toml::Writer<>>();
}
TEST(EncodeFormats, AliasedStringInputsRemainSafeDuringOutputGrowth) {
  std::string out = std::string(100, 'a');
  msgpack::Writer w(out);
  ASSERT_TRUE(w.write_string(w.output()));
  ASSERT_TRUE(token(w, TT::End));
  EXPECT_EQ(out.substr(100), octets({0xd9, 100}) + std::string(100, 'a'));
  std::string body;
  bson::Writer bw(body);
  ASSERT_TRUE(token(bw, TT::ObjectBegin));
  ASSERT_TRUE(bw.write_string("x"));
  const auto old = std::string(bw.output());
  ASSERT_TRUE(bw.write_string(bw.output()));
  ASSERT_TRUE(token(bw, TT::ObjectEnd));
  ASSERT_TRUE(token(bw, TT::End));
  EXPECT_EQ(body, document(member(2, "x", little(old.size() + 1) + old + '\0')));
}
TEST(EncodeFormats, UnsupportedValuesAndPrecisionLossAreErrors) {
  struct Unknown {
    int n = 1;
  };
  Leaf<Unknown> value;
  std::string out;
  msgpack::Writer w(out);
  EXPECT_FALSE(encode_into(value, w));
  EXPECT_EQ(w.error().code, EC::unsupported_type);
  Leaf<const char*> string;
  string.value() = nullptr;
  std::string null;
  msgpack::Writer nw(null);
  EXPECT_FALSE(encode_into(string, nw));
  EXPECT_EQ(nw.error().code, EC::invalid_object_state);
  if constexpr (std::numeric_limits<long double>::digits >
                std::numeric_limits<double>::digits) {
    std::string loss;
    msgpack::Writer lw(loss);
    EXPECT_FALSE(
        lw.write_number(1.0L + std::numeric_limits<long double>::epsilon()));
    EXPECT_EQ(lw.error().code, EC::out_of_range);
  }
}
TEST(EncodeFormats, ReaderOnlyNoopFieldsCannotSilentlyShiftMemberValues) {
  using ReaderOnly =
      Elem<scan::scalar | decode::number<int> | store::assign, noop_encoder>;
  struct Only {
    Try<Entry<Key<"a">, ReaderOnly>> a;
    Try<Entry<Key<"b">, ReaderOnly>> b;
  };
  Only value;
  auto verify = [&]<class W>() {
    std::string out;
    W writer(out);
    EXPECT_FALSE(encode_into(value, writer));
    EXPECT_EQ(writer.error().code, EC::unsupported_type);
  };
  verify.template operator()<msgpack::Writer<>>();
  verify.template operator()<bson::Writer<>>();
  verify.template operator()<toml::Writer<>>();
}
TEST(EncodeFormats, EmptyBinaryAndInitialOutputLimitsAreChecked) {
  std::string out;
  msgpack::Writer writer(out);
  ASSERT_TRUE(writer.write_binary(BytesView{nullptr, 0}));
  ASSERT_TRUE(token(writer, TT::End));
  EXPECT_EQ(out, octets({0xc4, 0}));
  std::string existing = "too long";
  toml::Writer<64, 1> limited(existing);
  EXPECT_EQ(limited.error().code, EC::output_too_large);
  std::string data;
  bson::Writer bw(data);
  (void)bw.write({0x7ffffffeULL, TT::Scalar, 1});
  EXPECT_EQ(bw.error().code, EC::unsupported_type);
}
TEST(EncodeFormats, FailedWriterExtensionCannotReportEncodeSuccess) {
  Leaf<int> value;
  value.value() = 42;
  std::string output;
  msgpack::Writer<7, 4096> writer(output);
  EXPECT_FALSE(encode_into(value, writer));
  EXPECT_EQ(writer.error().code, EC::write_failed);
  EXPECT_TRUE(output.empty());
}
}  // namespace

namespace {
std::string fixture(std::string_view name) {
  std::ifstream input(std::string(HOSQ_FIXTURE_DIR) + "/" + std::string(name),
                      std::ios::binary);
  if (!input)
    throw std::runtime_error("cannot open fixture: " + std::string(name));
  return {std::istreambuf_iterator<char>(input), {}};
}
void expect_fixture(const Record& value) {
  EXPECT_EQ(*value.n, 42);
  EXPECT_EQ(*value.text, "日本語");
  EXPECT_TRUE(*value.ok);
  ASSERT_EQ(value.items->size(), 2u);
  EXPECT_EQ(*(*value.items)[0].n, 1);
  EXPECT_EQ(*(*value.items)[1].n, 2);
  EXPECT_EQ(*value.meta->n, -7);
}
template <class R, class W>
void fixture_roundtrip(std::string_view name, std::string_view invalid) {
  const auto input = fixture(name);
  Record first;
  ASSERT_TRUE(decode_into<R>(first, input).is_ok());
  expect_fixture(first);
  std::string output;
  W writer(output);
  ASSERT_TRUE(encode_into(first, writer)) << writer.error().detail;
  Record second;
  ASSERT_TRUE(decode_into<R>(second, output).is_ok());
  expect_fixture(second);
  Record rejected;
  const auto bad = fixture(invalid);
  EXPECT_FALSE(decode_into<R>(rejected, bad).is_ok());
}
TEST(FileSmoke, JsonDecodeEncodeDecode) {
  fixture_roundtrip<json::SimpleReader, json::CompactWriter<>>("record.json",
                                                               "invalid.json");
}
TEST(FileSmoke, MessagePackDecodeEncodeDecode) {
  fixture_roundtrip<msgpack::Reader<>, msgpack::Writer<>>("record.msgpack",
                                                          "invalid.msgpack");
}
TEST(FileSmoke, BsonDecodeEncodeDecode) {
  fixture_roundtrip<bson::Reader<>, bson::Writer<>>("record.bson",
                                                    "invalid.bson");
}
TEST(FileSmoke, TomlDecodeEncodeDecode) {
  fixture_roundtrip<toml::Reader<>, toml::Writer<>>("record.toml",
                                                    "invalid.toml");
}
}  // namespace
