#include <gtest/gtest.h>

#include <bit>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include "hosq/bson/reader.hh"
#include "hosq/hosq.hh"
#include "hosq/msgpack/reader.hh"
#include "hosq/toml/reader.hh"

namespace {
using namespace hosq;
using TT = TokenType;
using EC = ErrorCode;
std::string bytes(std::initializer_list<unsigned> values) {
  std::string out;
  for (auto v : values) out += static_cast<char>(v);
  return out;
}
std::string le(std::uint64_t n, unsigned width = 4) {
  std::string out;
  for (unsigned i = 0; i < width; ++i) out += static_cast<char>(n >> (8 * i));
  return out;
}
std::string doc(std::string elements) {
  return le(elements.size() + 5) + elements + '\0';
}
std::string element(unsigned type, std::string key, std::string value) {
  return static_cast<char>(type) + key + '\0' + value;
}
template <class R>
auto read(std::string_view input, std::vector<TT>* tokens = nullptr) -> Error {
  R r;
  typename R::state_type s;
  r.reset(s, input);
  for (std::size_t i = 0; i < input.size() * 3 + 100; ++i) {
    if (!r.next_token(s)) return r.error(s);
    if (tokens) tokens->push_back(r.current_token(s).type);
    if (r.current_token(s).type == TT::End) return r.error(s);
  }
  ADD_FAILURE() << "reader did not terminate";
  return {EC::invalid_state, 0, {}};
}
template <class R, class T>
auto scalar(std::string_view input, T& output) -> Error {
  Elem<scan::scalar | decode::number<T> | store::assign, noop_encoder> value;
  auto result = decode_into<R>(value, input);
  output = value.value();
  return result;
}
template <class T>
using Number =
    Elem<scan::scalar | decode::number<T> | store::assign, noop_encoder>;
using String = Elem<scan::scalar | decode::string_view | construct<std::string> |
                        store::once,
                    noop_encoder>;
using Boolean =
    Elem<scan::scalar | decode::boolean | store::assign, noop_encoder>;
struct Record {
  Try<Entry<Key<"n">, Number<int>>> n;
  Try<Entry<Key<"name">, String>> name;
  Try<Entry<Key<"ok">, Boolean>> ok;
};
template <class R>
void record(std::string_view input) {
  Record value;
  const auto result = decode_into<R>(value, input);
  ASSERT_TRUE(result.is_ok())
      << static_cast<int>(result.code) << ' ' << result.detail;
  EXPECT_EQ(*value.n, 42);
  EXPECT_EQ(*value.name, "hello");
  EXPECT_TRUE(*value.ok);
}
TEST(Formats, ExistingSchemaDecodesAllReaders) {
  record<msgpack::Reader<>>(bytes({0x83, 0xa2}) + "ok" + bytes({0xc3, 0xa1}) +
                            "n" + bytes({42, 0xa4}) + "name" + bytes({0xa5}) +
                            "hello");
  record<bson::Reader<>>(doc(element(8, "ok", bytes({1})) +
                             element(16, "n", le(42)) +
                             element(2, "name", le(6) + "hello" + '\0')));
  record<toml::Reader<>>("ok = true\nn = 42\nname = 'hello'\n");
}
TEST(MessagePack, ScalarsWidthsBoundariesAndTypeChecks) {
  std::uint64_t u = 0;
  EXPECT_TRUE((scalar<msgpack::Reader<>>(
                   bytes({0xcf, 255, 255, 255, 255, 255, 255, 255, 255}), u)
                   .is_ok()));
  EXPECT_EQ(u, std::numeric_limits<std::uint64_t>::max());
  std::int64_t i = 0;
  EXPECT_TRUE(
      (scalar<msgpack::Reader<>>(bytes({0xd3, 128, 0, 0, 0, 0, 0, 0, 0}), i)
           .is_ok()));
  EXPECT_EQ(i, std::numeric_limits<std::int64_t>::min());
  for (auto encoding :
       {bytes({0xcc, 42}), bytes({0xcd, 0, 42}), bytes({0xce, 0, 0, 0, 42}),
        bytes({0xd0, 42}), bytes({0xd1, 0, 42}), bytes({0xd2, 0, 0, 0, 42})}) {
    EXPECT_TRUE((scalar<msgpack::Reader<>>(encoding, i).is_ok()));
    EXPECT_EQ(i, 42);
  }
  double f = 0;
  EXPECT_TRUE(
      (scalar<msgpack::Reader<>>(bytes({0xca, 0x3f, 0xc0, 0, 0}), f).is_ok()));
  EXPECT_EQ(f, 1.5);
  EXPECT_EQ((scalar<msgpack::Reader<>>(bytes({0xca, 0x3f, 0xc0, 0, 0}), i).code),
            EC::out_of_range);
  EXPECT_EQ((scalar<msgpack::Reader<>>(bytes({0xff}), u).code),
            EC::out_of_range);
  EXPECT_EQ((scalar<msgpack::Reader<>>(bytes({0xa2}) + "42", i).code),
            EC::type_mismatch);
  EXPECT_EQ((scalar<msgpack::Reader<>>(bytes({0xc3}), i).code),
            EC::type_mismatch);
}
TEST(MessagePack, NestingOpaqueValuesAndInvalidInput) {
  std::vector<TT> seq;
  EXPECT_TRUE(read<msgpack::Reader<>>(
                  bytes({0x92, 0x81, 0xa1}) + "x" + bytes({0x90, 0xc0}), &seq)
                  .is_ok());
  EXPECT_EQ(seq, (std::vector<TT>{TT::ArrayBegin, TT::ObjectBegin, TT::Scalar,
                                  TT::ArrayBegin, TT::ArrayEnd, TT::ObjectEnd,
                                  TT::Scalar, TT::ArrayEnd, TT::End}));
  for (auto in : {bytes({0xc4, 2, 0, 255}), bytes({0xc7, 1, 255, 7}),
                  bytes({0xd4, 255, 0}), bytes({0xd9, 0}), bytes({0xda, 0, 0}),
                  bytes({0xdb, 0, 0, 0, 0}), bytes({0xdc, 0, 0}),
                  bytes({0xdd, 0, 0, 0, 0}), bytes({0xde, 0, 0}),
                  bytes({0xdf, 0, 0, 0, 0})})
    EXPECT_TRUE(read<msgpack::Reader<>>(in).is_ok());
  EXPECT_EQ(read<msgpack::Reader<>>(bytes({0xc1})).code,
            EC::invalid_type_marker);
  EXPECT_TRUE(read<msgpack::Reader<>>(bytes({0x81, 1, 2})).is_ok());
  EXPECT_EQ(read<msgpack::Reader<>>(bytes({1, 2})).code, EC::trailing_data);
  EXPECT_EQ(read<msgpack::Reader<1>>(bytes({0x91, 0x90})).code,
            EC::depth_limit_exceeded);
  for (auto in : {bytes({0xcf, 1, 2}), bytes({0x91}), bytes({0xa3, 1}),
                  bytes({0xc7, 2, 1}), bytes({0xdf, 255, 255, 255, 255})})
    EXPECT_EQ(read<msgpack::Reader<>>(in).code, EC::unexpected_eof);
}
TEST(Bson, NestedArraysAndOpaqueTypes) {
  auto arr = doc(element(16, "0", le(1)) + element(3, "1", doc("")));
  std::vector<TT> seq;
  EXPECT_TRUE(read<bson::Reader<>>(doc(element(4, "items", arr)), &seq).is_ok());
  EXPECT_EQ(seq, (std::vector<TT>{TT::ObjectBegin, TT::Scalar, TT::ArrayBegin,
                                  TT::Scalar, TT::ObjectBegin, TT::ObjectEnd,
                                  TT::ArrayEnd, TT::ObjectEnd, TT::End}));
  for (auto [type, payload] : std::vector<std::pair<unsigned, std::string>>{
           {1, le(std::bit_cast<std::uint64_t>(1.5), 8)},
           {5, le(3) + bytes({0, 1, 2, 3})},
           {5, le(6) + bytes({2}) + le(2) + bytes({1, 2})},
           {7, std::string(12, 'a')},
           {9, le(0, 8)},
           {10, ""},
           {11, std::string("x\0i\0", 4)},
           {12, le(1) + bytes({0}) + std::string(12, 'x')},
           {13, le(1) + bytes({0})},
           {14, le(1) + bytes({0})},
           {17, le(0, 8)},
           {18, le(0, 8)},
           {19, std::string(16, 0)},
           {127, ""},
           {255, ""}})
    EXPECT_TRUE(read<bson::Reader<>>(doc(element(type, "x", payload))).is_ok())
        << type;
}
TEST(Bson, RejectsBadLengthsMarkersAndArrayKeys) {
  EXPECT_EQ(read<bson::Reader<>>(le(4)).code, EC::invalid_length);
  EXPECT_EQ(read<bson::Reader<>>(le(0xffffffff)).code, EC::invalid_length);
  EXPECT_EQ(read<bson::Reader<>>(doc(element(8, "x", bytes({2})))).code,
            EC::invalid_token);
  EXPECT_EQ(read<bson::Reader<>>(doc(element(0x20, "x", ""))).code,
            EC::invalid_type_marker);
  EXPECT_EQ(read<bson::Reader<>>(doc(element(15, "x", ""))).code,
            EC::unexpected_eof);
  EXPECT_EQ(read<bson::Reader<>>(doc(element(2, "x", le(0)))).code,
            EC::invalid_length);
  EXPECT_EQ(read<bson::Reader<>>(doc(element(2, "x", le(2) + "ab"))).code,
            EC::invalid_token);
  EXPECT_EQ(
      read<bson::Reader<>>(doc(element(4, "x", doc(element(16, "1", le(0))))))
          .code,
      EC::invalid_token);
  EXPECT_EQ(read<bson::Reader<>>(doc("") + "x").code, EC::trailing_data);
  EXPECT_EQ(read<bson::Reader<1>>(doc(element(3, "x", doc("")))).code,
            EC::depth_limit_exceeded);
  auto input = doc(element(3, "x", doc(element(2, "a", le(2) + "b" + '\0'))));
  for (std::size_t n = 0; n < input.size(); ++n)
    EXPECT_FALSE(
        read<bson::Reader<>>(std::string_view(input).substr(0, n)).is_ok())
        << n;
}
TEST(Toml, NestedArraysInlineTablesAndHeaders) {
  std::vector<TT> seq;
  EXPECT_TRUE(
      read<toml::Reader<>>("a = [1, {x = [true, false]},] # end\n[meta]\nx = "
                           "2\n[meta.child]\ny = 'z'\n[other]\nz = 3\n",
                           &seq)
          .is_ok());
  EXPECT_EQ(seq.front(), TT::ObjectBegin);
  EXPECT_EQ(seq.back(), TT::End);
  EXPECT_TRUE(read<toml::Reader<>>("").is_ok());
  EXPECT_TRUE(read<toml::Reader<>>("# comment\r\nx=1\r\n").is_ok());
  for (auto v : {"0", "-0", "+1", "1_000", "0xff", "0o77", "0b101",
                 "-9223372036854775808", "9223372036854775807", "1.5", "1e2",
                 "1.5e-2", "+inf", "-nan"})
    EXPECT_TRUE(read<toml::Reader<>>(std::string("x=") + v).is_ok()) << v;
  EXPECT_TRUE(read<toml::Reader<>>(R"(x="a\n\u0061")").is_ok());
}
TEST(Toml, InvalidSyntaxAndPreviouslyUnsupportedForms) {
  for (auto v : {"01", "1_", "_1", "0x_1", "1.", ".1", "1e", "null"})
    EXPECT_EQ(read<toml::Reader<>>(std::string("x=") + v).code,
              EC::invalid_number)
        << v;
  for (auto in : {"x=\n", "x=1 y=2", "x={a=1,}", "x=[1 2]", "[x", "x=1\ry=2"})
    EXPECT_FALSE(read<toml::Reader<>>(in).is_ok()) << in;
  EXPECT_EQ(read<toml::Reader<>>("x=9223372036854775808").code,
            EC::out_of_range);
  EXPECT_EQ(read<toml::Reader<>>("x=1\nx=2").code, EC::duplicate_member);
  EXPECT_EQ(read<toml::Reader<>>("x={a=1,a=2}").code, EC::duplicate_member);
  EXPECT_EQ(read<toml::Reader<>>("[x]\n[x]").code, EC::duplicate_member);
  EXPECT_EQ(read<toml::Reader<>>(R"(x="\q")").code, EC::invalid_escape);
  EXPECT_EQ(read<toml::Reader<>>(R"(x="\uD800")").code, EC::invalid_escape);
  for (auto in : {"a.b=1", "[[x]]", "x=1979-05-27", "x=\"\"\"long\"\"\"",
                  "[a.b]\nx=1\n[c]\nx=2\n[a.d]\nx=3"})
    EXPECT_TRUE(read<toml::Reader<>>(in).is_ok()) << in;
  EXPECT_EQ(read<toml::Reader<1>>("x=[]").code, EC::depth_limit_exceeded);
}
template <class R>
void checkpoint(std::string_view input) {
  R r;
  typename R::state_type s;
  r.reset(s, input);
  ASSERT_TRUE(r.next_token(s));
  const auto saved = s.checkpoint();
  ASSERT_TRUE(r.next_token(s));
  const auto token = s.token;
  s.rollback(saved);
  ASSERT_TRUE(r.next_token(s));
  EXPECT_EQ(s.token.type, token.type);
  EXPECT_EQ(s.token.offset, token.offset);
  EXPECT_EQ(s.token.length, token.length);
  r.report_error(s, {EC::validation_failed, 5, "first"});
  r.report_error(s, {EC::unknown_error, 6, "second"});
  EXPECT_EQ(r.error(s).code, EC::validation_failed);
  EXPECT_FALSE(r.next_token(s));
  r.reset(s, input);
  EXPECT_TRUE(r.error(s).is_ok());
  EXPECT_TRUE(r.next_token(s));
}
TEST(Formats, CheckpointFirstErrorAndReset) {
  checkpoint<msgpack::Reader<>>(bytes({0x91, 42}));
  checkpoint<bson::Reader<>>(doc(element(16, "n", le(42))));
  checkpoint<toml::Reader<>>("[meta]\nn=42");
}
using IntArray =
    Elem<scan::array | decode::each<scan::scalar | decode::number_into<int>>,
         noop_encoder>;
struct Nested {
  Try<Entry<Key<"n">, Number<int>>> n;
};
using NestedValue =
    Elem<scan::object | decode::to<Nested> | store::assign, noop_encoder>;
struct Collection {
  Try<Entry<Key<"values">, IntArray>> values;
  Try<Entry<Key<"meta">, NestedValue>> meta;
  Try<Entry<Elem<scan::scalar | decode::skip, noop_encoder>,
            Elem<decode::skip, noop_encoder>>>
      skip;
};
template <class R>
void collection(std::string_view input) {
  Collection value;
  const auto result = decode_into<R>(value, input);
  ASSERT_TRUE(result.is_ok())
      << static_cast<int>(result.code) << ' ' << result.detail;
  EXPECT_EQ(*value.values, (std::vector<int>{1, 2, 3}));
  EXPECT_EQ(*value.meta->n, 42);
}
TEST(Formats, NestedDecodeEachNumberIntoAndUnknownMemberSkip) {
  collection<msgpack::Reader<>>(bytes({0x83, 0xa7}) + "ignored" +
                                bytes({0x92, 0x80, 0xc0, 0xa6}) + "values" +
                                bytes({0x93, 1, 2, 3, 0xa4}) + "meta" +
                                bytes({0x81, 0xa1}) + "n" + bytes({42}));
  const auto arr = doc(element(16, "0", le(1)) + element(16, "1", le(2)) +
                       element(16, "2", le(3)));
  collection<bson::Reader<>>(
      doc(element(3, "ignored", doc(element(10, "null", ""))) +
          element(4, "values", arr) +
          element(3, "meta", doc(element(16, "n", le(42))))));
  collection<toml::Reader<>>(
      "ignored = [{x = true}, []]\nvalues = [1,2,3]\n[meta]\nn=42\n");
}
TEST(Formats, StringAndBooleanRejectOtherScalarTypes) {
  auto verify = []<class R>(std::string_view input) {
    String string;
    Boolean boolean;
    EXPECT_EQ(decode_into<R>(string, input).code, EC::type_mismatch);
    EXPECT_EQ(decode_into<R>(boolean, input).code, EC::type_mismatch);
  };
  verify.template operator()<msgpack::Reader<>>(bytes({42}));
  // BSON and TOML roots are objects, so verify the scalar adapters directly.
  auto member = []<class R>(std::string_view input) {
    R reader;
    typename R::state_type state;
    reader.reset(state, input);
    ASSERT_TRUE(reader.next_token(state));
    ASSERT_TRUE(reader.next_token(state));
    ASSERT_TRUE(reader.next_token(state));
    DecodeContext<void, R> ctx{reader, state};
    EXPECT_EQ(ext::DecodeStringView<R>{}(ext::ScanScalar<R>{}(ctx), ctx), "");
    EXPECT_EQ(reader.error(state).code, EC::type_mismatch);
  };
  member.template operator()<bson::Reader<>>(doc(element(16, "x", le(42))));
  member.template operator()<toml::Reader<>>("x=42");
}
TEST(Formats, Utf8AndDistinctQuotedTablePaths) {
  EXPECT_EQ(
      read<toml::Reader<>>(std::string("x='") + bytes({0xc0, 0xaf}) + "'").code,
      EC::invalid_utf8);
  EXPECT_EQ(
      read<toml::Reader<>>(std::string("# ") + bytes({0xed, 0xa0, 0x80})).code,
      EC::invalid_utf8);
  EXPECT_TRUE(
      read<toml::Reader<>>("x='日本語'\n['a/b']\nx=1\n[a.b]\nx=2\n").is_ok());
  EXPECT_EQ(
      read<bson::Reader<>>(doc(element(2, "x", le(3) + bytes({0xc0, 0xaf, 0}))))
          .code,
      EC::invalid_utf8);
  EXPECT_EQ(read<bson::Reader<>>(doc(element(16, bytes({0xff}), le(0)))).code,
            EC::invalid_utf8);
  EXPECT_EQ(read<toml::Reader<0>>("").code, EC::depth_limit_exceeded);
}
TEST(MessagePack, AllTruncatedPrefixesAndFloatIntegerLimits) {
  for (const auto& input :
       {bytes({0x81, 0xa1}) + "x" +
            bytes({0x92, 0xcb, 0x3f, 0xf0, 0, 0, 0, 0, 0, 0, 0xc4, 2, 1, 2}),
        bytes({0xc9, 0, 0, 0, 3, 255, 1, 2, 3}),
        bytes({0xdb, 0, 0, 0, 3}) + "abc"}) {
    EXPECT_TRUE(read<msgpack::Reader<>>(input).is_ok());
    for (std::size_t n = 0; n < input.size(); ++n)
      EXPECT_FALSE(
          read<msgpack::Reader<>>(std::string_view(input).substr(0, n)).is_ok())
          << n;
  }
  std::uint64_t u = 0;
  EXPECT_EQ(
      (scalar<msgpack::Reader<>>(bytes({0xcb, 0x43, 0xf0, 0, 0, 0, 0, 0, 0}), u)
           .code),
      EC::out_of_range);
  std::int64_t i = 0;
  EXPECT_EQ(
      (scalar<msgpack::Reader<>>(bytes({0xcb, 0x43, 0xe0, 0, 0, 0, 0, 0, 0}), i)
           .code),
      EC::out_of_range);
  EXPECT_EQ(
      (scalar<msgpack::Reader<>>(bytes({0xcb, 0x7f, 0xf0, 0, 0, 0, 0, 0, 0}), i)
           .code),
      EC::out_of_range);
}

TEST(Toml, LongNumbersAndCommentControls) {
  EXPECT_EQ(read<toml::Reader<>>("x=" + std::string(10000, '9')).code,
            EC::out_of_range);
  EXPECT_EQ(read<toml::Reader<>>("x=" + std::string(10000, '9') + "_").code,
            EC::invalid_number);
  EXPECT_EQ(read<toml::Reader<>>("x=1 #" + bytes({1})).code, EC::invalid_token);
  EXPECT_EQ(read<toml::Reader<>>("#" + bytes({127})).code, EC::invalid_token);
}
TEST(Formats, DeterministicMalformedByteCorpusTerminates) {
  std::uint32_t seed = 0x31415926;
  for (unsigned i = 0; i < 2000; ++i) {
    std::string input;
    for (unsigned j = 0; j < i % 64; ++j) {
      seed = seed * 1664525 + 1013904223;
      input += static_cast<char>(seed >> 24);
    }
    (void)read<msgpack::Reader<8>>(input);
    (void)read<bson::Reader<8>>(input);
    (void)read<toml::Reader<8>>(input);
  }
}
TEST(Toml, DottedAssignmentsAndEscapedConstantKeysDecode) {
  collection<toml::Reader<>>(
      "ignored.date = 1979-05-27T07:32:00Z\nvalues = [1,2,3]\nmeta . "
      "\"\\u006e\" = 42\n");
  Nested value;
  ASSERT_TRUE(decode_into<toml::Reader<>>(value, "\"\\u006e\" = 42").is_ok());
  EXPECT_EQ(*value.n, 42);
  EXPECT_EQ(read<toml::Reader<>>("a = 1\n\"\\u0061\" = 2").code,
            EC::duplicate_member);
  EXPECT_EQ(read<toml::Reader<>>("a.b=1\n[a]\nc=2").code, EC::duplicate_member);
  EXPECT_EQ(read<toml::Reader<>>("[a.b]\nn=1\n[a]\nb.x=2").code,
            EC::duplicate_member);
  EXPECT_EQ(read<toml::Reader<>>("a={b=1}\na.c=2").code, EC::duplicate_member);
}
using TableArray =
    Elem<scan::array | decode::collect<scan::object | decode::to<Nested>> |
             store::assign,
         noop_encoder>;
struct TableList {
  Try<Entry<Key<"items">, TableArray>> items;
};
TEST(Toml, ArraysOfTablesDecodeAndRejectConflicts) {
  TableList value;
  ASSERT_TRUE(
      decode_into<toml::Reader<>>(value, "[[items]]\nn=1\n[[items]]\nn=2")
          .is_ok());
  ASSERT_EQ(value.items->size(), 2u);
  EXPECT_EQ(*(*value.items)[0].n, 1);
  EXPECT_EQ(*(*value.items)[1].n, 2);
  EXPECT_TRUE(read<toml::Reader<>>("[[fruit]]\nname='apple'\n[fruit.physical]"
                                   "\ncolor='red'\n[[fruit.variety]]\nname='red "
                                   "delicious'\n[[fruit]]\nname='banana'\n["
                                   "fruit.physical]\ncolor='yellow'")
                  .is_ok());
  EXPECT_EQ(read<toml::Reader<>>("items=[]\n[[items]]").code,
            EC::duplicate_member);
  EXPECT_EQ(read<toml::Reader<>>("[[items]]\n[items]").code,
            EC::duplicate_member);
  EXPECT_EQ(read<toml::Reader<>>("[items]\n[[items]]").code,
            EC::duplicate_member);
}
struct Text {
  Try<Entry<Key<"x">, String>> x;
};
TEST(Toml, MultilineStringsKeepBorrowedRawSemantics) {
  for (auto input : {std::string("x=\"\"\"\nhello\nworld\"\"\""),
                     std::string("x='''\nhello\nworld'''")}) {
    Text text;
    ASSERT_TRUE(decode_into<toml::Reader<>>(text, input).is_ok());
    EXPECT_EQ(*text.x, "hello\nworld");
  }
  EXPECT_TRUE(read<toml::Reader<>>(
                  "x=\"\"\"The quick brown \\\n  fox jumps \\\n over\"\"\"")
                  .is_ok());
  Text quotes;
  ASSERT_TRUE(
      decode_into<toml::Reader<>>(quotes, "x=\"\"\"\"quoted\"\"\"\"").is_ok());
  EXPECT_EQ(*quotes.x, "\"quoted\"");
  for (auto input : {"x=\"\"\"unterminated", "x=\"\"\"bad\\q\"\"\"",
                     "\"\"\"key\"\"\"=1", "x=\"\"\"bad\"\"\"\"\"\""})
    EXPECT_FALSE(read<toml::Reader<>>(input).is_ok());
}
TEST(Toml, DatesTimesAreValidatedOpaqueScalarsAndSkipped) {
  for (auto input :
       {"x=1979-05-27", "x=07:32:00", "x=1979-05-27 07:32:00.123-07:00",
        "x=2000-02-29T23:59:60Z"}) {
    EXPECT_TRUE(read<toml::Reader<>>(input).is_ok()) << input;
    toml::Reader<> reader;
    toml::Reader<>::state_type state;
    reader.reset(state, input);
    ASSERT_TRUE(reader.next_token(state));
    ASSERT_TRUE(reader.next_token(state));
    ASSERT_TRUE(reader.next_token(state));
    EXPECT_EQ(state.token.context,
              static_cast<std::uint8_t>(detail::NativeContext::datetime));
    DecodeContext<void, toml::Reader<>> ctx{reader, state};
    EXPECT_EQ(ext::DecodeNumber<toml::Reader<>>{}.template operator()<int>(
                  ext::ScanScalar<toml::Reader<>>{}(ctx), ctx),
              0);
    EXPECT_EQ(reader.error(state).code, EC::type_mismatch);
  }
  for (auto input :
       {"x=1900-02-29", "x=2024-13-01", "x=2024-04-31", "x=24:00:00",
        "x=00:00:61", "x=1979-05-27T00:00:00+24:00", "x=00:00:00."})
    EXPECT_FALSE(read<toml::Reader<>>(input).is_ok()) << input;
}
std::string scoped_code(std::string code, std::string scope) {
  const auto payload = le(code.size() + 1) + code + '\0' + scope;
  return le(payload.size() + 4) + payload;
}
struct SkipRecord {
  Try<Entry<Key<"n">, Number<int>>> n;
  Try<Entry<Elem<scan::scalar | decode::skip, noop_encoder>,
            Elem<decode::skip, noop_encoder>>>
      skip;
};
TEST(Bson, CodeWithScopeIsValidatedAndSkipped) {
  const auto scope = doc(element(3, "inner", doc(element(16, "x", le(42)))));
  const auto code = scoped_code("return x", scope);
  const auto input = doc(element(15, "code", code) + element(16, "n", le(42)));
  SkipRecord record;
  ASSERT_TRUE(decode_into<bson::Reader<>>(record, input).is_ok());
  EXPECT_EQ(*record.n, 42);
  EXPECT_TRUE(
      read<bson::Reader<>>(
          doc(element(15, "outer",
                      scoped_code("", doc(element(15, "inner",
                                                  scoped_code("", doc(""))))))))
          .is_ok());
  EXPECT_EQ(read<bson::Reader<1>>(input).code, EC::depth_limit_exceeded);
  EXPECT_EQ(
      read<bson::Reader<>>(doc(element(15, "x", le(13) + std::string(9, 0))))
          .code,
      EC::invalid_length);
  auto too_big = code;
  too_big.replace(0, 4, le(code.size() + 1));
  EXPECT_FALSE(read<bson::Reader<>>(doc(element(15, "x", too_big))).is_ok());
  auto too_small = code;
  too_small.replace(0, 4, le(code.size() - 1));
  EXPECT_FALSE(read<bson::Reader<>>(doc(element(15, "x", too_small))).is_ok());
  EXPECT_EQ(read<bson::Reader<>>(
                doc(element(15, "x", scoped_code("", doc("") + "extra"))))
                .code,
            EC::invalid_length);
  auto bad_code = scoped_code("bad", doc(""));
  bad_code[11] = 'x';
  EXPECT_EQ(read<bson::Reader<>>(doc(element(15, "x", bad_code))).code,
            EC::invalid_token);
  EXPECT_EQ(read<bson::Reader<>>(
                doc(element(15, "x", scoped_code(bytes({255}), doc("")))))
                .code,
            EC::invalid_utf8);
  for (std::size_t n = 0; n < input.size(); ++n)
    EXPECT_FALSE(
        read<bson::Reader<>>(std::string_view(input).substr(0, n)).is_ok())
        << n;
}

TEST(MessagePack, NonStringKeysRemainTraversableAndSkippable) {
  const auto arbitrary =
      bytes({0x83, 1, 2, 0x91, 3, 0xc3, 0x81, 0xc0, 4, 0xc4, 1, 0xff});
  EXPECT_TRUE(read<msgpack::Reader<>>(arbitrary).is_ok());
  SkipRecord record;
  const auto input = bytes({0x82, 0xa7}) + "ignored" + arbitrary +
                     bytes({0xa1}) + "n" + bytes({42});
  ASSERT_TRUE(decode_into<msgpack::Reader<>>(record, input).is_ok());
  EXPECT_EQ(*record.n, 42);
  Nested strict;
  // Preserve the reader's non-string-key type error instead of masking it as
  // an unknown member during constant-key dispatch.
  EXPECT_EQ(decode_into<msgpack::Reader<>>(strict, bytes({0x81, 1, 2})).code,
            EC::type_mismatch);
  EXPECT_EQ(read<msgpack::Reader<>>(bytes({0x81, 0x91})).code,
            EC::unexpected_eof);
}
TEST(Toml, SkipArrayTablesDatesAndRevisitedSubtables) {
  collection<toml::Reader<>>(
      "values=[1,2,3]\n[[ignored]]\ndate=1979-05-27\n[[ignored]]\n[ignored."
      "child]\nx='''multiline\ntext'''\n[meta]\nn=42");
  collection<toml::Reader<>>(
      "values=[1,2,3]\n[ignored.child]\nx=1\n[meta]\nn=42\n[ignored]\nx=2\n["
      "ignored.other]\ny=3");
  collection<toml::Reader<>>("values=[1,2,3]\n[\"m\\u0065ta\"]\n\"\\u006e\"=42");
}

struct OwnedTomlString {
  template <class Context>
  auto operator()(const ScalarView&, const Context& ctx) const -> std::string {
    std::string value;
    (void)ctx.reader().read_string(ctx.reader_state(), value);
    return value;
  }
};
using ResolvedText =
    Elem<scan::scalar | step<OwnedTomlString> | store::assign, noop_encoder>;
struct OwnedText {
  Try<Entry<Key<"x">, ResolvedText>> x;
};
TEST(Toml, OwnedStringsResolveEscapesAndMultilineContinuations) {
  for (const auto& [input, expected] :
       std::vector<std::pair<std::string, std::string>>{
           {"x=\"tab\\t\\u0061\\U0001F600\"", "tab\ta😀"},
           {"x='literal\\n'", "literal\\n"},
           {"x=\"\"\"\nThe quick brown \\\n  fox jumps \\\n over\"\"\"",
            "The quick brown fox jumps over"},
           {"x='''\nline1\r\nline2'''", "line1\nline2"},
           {"x=\"\\r\\n\"", "\r\n"},
           {"x=\"\"\"one \"\" two\"\"\"\"\"", "one \"\" two\"\""}}) {
    OwnedText value;
    ASSERT_TRUE(decode_into<toml::Reader<>>(value, input).is_ok()) << input;
    EXPECT_EQ(*value.x, expected) << input;
  }
  OwnedText bad;
  EXPECT_EQ(decode_into<toml::Reader<>>(bad, "x=42").code, EC::type_mismatch);
}
TEST(Toml, OwnedDynamicKeysResolveEscapesAndSurviveReaderState) {
  using Mapping = Elem<scan::object |
                           decode::collect<scan::scalar | step<OwnedTomlString>,
                                           scan::scalar | decode::number<int>> |
                           store::assign,
                       noop_encoder>;
  Mapping value;
  std::string input = "\"\\u006e\"=42\n'raw\\n'=7";
  ASSERT_TRUE(decode_into<toml::Reader<>>(value, input).is_ok());
  input.assign(input.size(), 'x');
  EXPECT_EQ(value.value().keys(), (std::vector<std::string>{"n", "raw\\n"}));
  EXPECT_EQ(value.value().values(), (std::vector<int>{42, 7}));
}

TEST(Bson, ReservedBinarySubtypesAndRegexOptionOrderAreRejected) {
  EXPECT_EQ(read<bson::Reader<>>(doc(element(5, "x", le(0) + bytes({10})))).code,
            EC::invalid_type_marker);
  EXPECT_TRUE(
      read<bson::Reader<>>(doc(element(5, "x", le(0) + bytes({128})))).is_ok());
  EXPECT_TRUE(
      read<bson::Reader<>>(doc(element(11, "x", std::string("x\0imsux\0", 8))))
          .is_ok());
  EXPECT_EQ(
      read<bson::Reader<>>(doc(element(11, "x", std::string("x\0mi\0", 5))))
          .code,
      EC::invalid_token);
  EXPECT_EQ(
      read<bson::Reader<>>(doc(element(11, "x", std::string("x\0q\0", 4)))).code,
      EC::invalid_token);
}
}  // namespace
