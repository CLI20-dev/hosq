#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <string_view>

#include "hosq/hosq.hh"
#include "hosq/json/compact_writer.hh"
#include "hosq/json/dom.hh"
#include "hosq/json/fast_reader.hh"
#include "hosq/json/simple_reader.hh"
#include "hosq/msgpack/reader.hh"
#include "hosq/msgpack/writer.hh"

namespace {
using namespace hosq;

template <class T>
using DomNumber =
    Elem<scan::scalar | decode::number<T> | store::assign, noop_encoder>;
using DomString =
    Elem<scan::scalar | decode::string_source | construct<std::string> |
             decode::json_unescape | store::assign,
         noop_encoder>;
struct DomRecord {
  Entry<Key<"n">, DomNumber<int>> n;
};
struct TimesTwo {
  auto operator()(int value) const -> int { return value * 2; }
};

template <class Dom>
class OwnedDomTest : public ::testing::Test {};
using DomImplementations =
    ::testing::Types<json::GenericDom<>, json::DeferredDom<>>;
TYPED_TEST_SUITE(OwnedDomTest, DomImplementations);

TYPED_TEST(OwnedDomTest, OwnsInputAndSelectionSurvivesOwnerAndReassignment) {
  auto selected = [] {
    TypeParam dom;
    std::string source = R"({"x":"a\nb"})";
    EXPECT_TRUE(dom.assign(source).is_ok());
    source.assign(source.size(), 'x');
    auto result = dom["/x"];
    EXPECT_TRUE(dom.assign("false").is_ok());
    return result;
  }();
  EXPECT_TRUE(selected.exists());
  EXPECT_EQ(selected.source(), R"("a\nb")");
  DomString text;
  ASSERT_TRUE(selected.decode_into(text).is_ok());
  EXPECT_EQ(text.value(), "a\nb");
}

TYPED_TEST(OwnedDomTest, ResolvesPointersEmptyKeysEscapesAndContainers) {
  TypeParam dom;
  ASSERT_TRUE(dom.assign(R"({"":{"a/b":{"~key":[{"n":7},[3,4]]}},"a\u0062":9})")
                  .is_ok());
  EXPECT_EQ(dom[""].source(),
            R"({"":{"a/b":{"~key":[{"n":7},[3,4]]}},"a\u0062":9})");
  EXPECT_EQ(dom["/"].source(), R"({"a/b":{"~key":[{"n":7},[3,4]]}})");
  EXPECT_EQ(dom["/ab"].source(), "9");
  DomRecord record;
  ASSERT_TRUE(dom["//a~1b/~0key/0"].decode_into(record).is_ok());
  EXPECT_EQ(record.n.value().value(), 7);
  using Pair =
      Elem<scan::array |
               decode::each<scan::scalar | decode::number<int> | store::assign>,
           noop_encoder>;
  Pair pair;
  ASSERT_TRUE(dom["//a~1b/~0key/1"].decode_into(pair).is_ok());
  ASSERT_EQ(pair.value().size(), 2u);
  EXPECT_EQ(pair.value()[0], 3);
  EXPECT_EQ(pair.value()[1], 4);
}

TYPED_TEST(OwnedDomTest, LeavesConversionAndValidationToChosenPipeline) {
  TypeParam dom;
  ASSERT_TRUE(dom.assign(R"({"n":7,"s":"a\nb"})").is_ok());
  using Converted = Elem<scan::scalar | decode::number<int> | step<TimesTwo> |
                             validate::match<14> | store::assign,
                         noop_encoder>;
  Converted converted;
  ASSERT_TRUE(dom["/n"].decode_into(converted).is_ok());
  EXPECT_EQ(converted.value(), 14);
  using Rejected = Elem<scan::scalar | decode::number<int> | validate::match<8> |
                            store::assign,
                        noop_encoder>;
  Rejected rejected;
  EXPECT_EQ(dom["/n"].decode_into(rejected).code, ErrorCode::invalid_choice);
  using Raw =
      Elem<scan::scalar | decode::string_view | store::assign, noop_encoder>;
  Raw raw;
  ASSERT_TRUE(dom["/s"].decode_into(raw).is_ok());
  EXPECT_EQ(raw.value(), R"(a\nb)");
  DomString decoded;
  ASSERT_TRUE(dom["/s"].decode_into(decoded).is_ok());
  EXPECT_EQ(decoded.value(), "a\nb");
}

TYPED_TEST(OwnedDomTest, PreservesIntegerPrecisionAndUnboundedNumberSource) {
  TypeParam dom;
  ASSERT_TRUE(
      dom.assign(
             R"([18446744073709551615,123456789012345678901234567890,1e9999])")
          .is_ok());
  DomNumber<std::uint64_t> integer;
  ASSERT_TRUE(dom["/0"].decode_into(integer).is_ok());
  EXPECT_EQ(integer.value(), UINT64_MAX);
  EXPECT_EQ(dom["/1"].source(), "123456789012345678901234567890");
  EXPECT_EQ(dom["/1"].decode_into(integer).code, ErrorCode::out_of_range);
  DomNumber<double> floating;
  EXPECT_EQ(dom["/2"].decode_into(floating).code, ErrorCode::out_of_range);
}

TYPED_TEST(OwnedDomTest, DistinguishesNullMissingAndInvalidPointer) {
  TypeParam dom;
  DomNumber<int> number;
  EXPECT_FALSE(dom[""].exists());
  ASSERT_TRUE(dom.assign(R"({"n":null,"a":[5],"object":{"01":8}})").is_ok());
  EXPECT_TRUE(dom["/n"].exists());
  EXPECT_EQ(dom["/n"].source(), "null");
  for (const auto path : {"/absent", "/a/1", "/n/child"}) {
    EXPECT_FALSE(dom[path].exists()) << path;
    EXPECT_TRUE(dom[path].source().empty()) << path;
    EXPECT_EQ(dom[path].decode_into(number).code, ErrorCode::missing_required)
        << path;
  }
  for (const auto path : {"n", "/~", "/~2"}) {
    EXPECT_FALSE(dom[path].exists()) << path;
    EXPECT_EQ(dom[path].decode_into(number).code, ErrorCode::invalid_token)
        << path;
  }
  EXPECT_FALSE(dom["/a/00"].exists());
  EXPECT_EQ(dom["/a/01"].decode_into(number).code, ErrorCode::missing_required);
  ASSERT_TRUE(dom["/object/01"].decode_into(number).is_ok());
  EXPECT_EQ(number.value(), 8);
  EXPECT_FALSE(dom["/a/-"].exists());
}

TYPED_TEST(OwnedDomTest, ReportsWholeDocumentDiagnosticAndAllowsPathOptOut) {
  TypeParam dom;
  constexpr std::string_view source =
      R"({"padding":0,"rows":[{"n":999999999999999999999999}]})";
  ASSERT_TRUE(dom.assign(source).is_ok());
  DomRecord record;
  auto error = dom["/rows/0"].decode_into(record);
  EXPECT_EQ(error.code, ErrorCode::out_of_range);
  EXPECT_EQ(error.offset, source.find("999999"));
  EXPECT_EQ(error.path,
            (ErrorPath{PathSegment::key("rows", 1), PathSegment::element(0),
                       PathSegment::key("n")}));
  error = dom["/rows/0"].decode_into(record, {.error_path = false});
  EXPECT_EQ(error.code, ErrorCode::out_of_range);
  EXPECT_EQ(error.offset, source.find("999999"));
  EXPECT_TRUE(error.path.empty());
}

TYPED_TEST(OwnedDomTest, ValidatesWholeDocumentAndRetainsPriorValueOnFailure) {
  TypeParam dom;
  ASSERT_TRUE(dom.assign(R"({"keep":42})").is_ok());
  for (const auto source : {R"({"unaccessed":"\q"})", R"({"unaccessed":01})",
                            R"({"unaccessed":tru})", R"({"unaccessed":[1,]})",
                            R"({"unaccessed":{"x":1,"x":2}})",
                            R"({"unaccessed":{"a":1,"\u0061":2}})", "{} []",
                            "{} trailing", "", "[", R"("\uD800")"}) {
    EXPECT_TRUE(dom.assign(source).has_error()) << source;
    EXPECT_EQ(dom["/keep"].source(), "42") << source;
  }
}

TYPED_TEST(OwnedDomTest, AssignsFromItsOwnBorrowedSource) {
  TypeParam dom;
  ASSERT_TRUE(dom.assign(R"({"nested":[1,{"x":2}]})").is_ok());
  ASSERT_TRUE(dom.assign(dom.source()).is_ok());
  ASSERT_TRUE(dom.assign(dom["/nested"].source()).is_ok());
  EXPECT_EQ(dom.source(), R"([1,{"x":2}])");
  EXPECT_EQ(dom["/1/x"].source(), "2");
}

TEST(OwnedDom, EnforcesExactContainerDepthLimit) {
  json::GenericDom<2> generic;
  json::DeferredDom<2> deferred;
  EXPECT_TRUE(generic.assign("[[0]]").is_ok());
  EXPECT_TRUE(deferred.assign("[[0]]").is_ok());
  EXPECT_EQ(generic.assign("[[[0]]]").code, ErrorCode::depth_limit_exceeded);
  EXPECT_EQ(deferred.assign("[[[0]]]").code, ErrorCode::depth_limit_exceeded);
}

template <class Reader, auto Capture>
void check_typed_dom_containers() {
  using Value = Elem<Capture | store::assign, encode::json_dom>;
  struct Document {
    Entry<Key<"array">, types::value::Array<Value>> array;
    Entry<Key<"mapping">, types::value::Mapping<types::value::String, Value>>
        mapping;
    Entry<Key<"tail">, types::value::Int<int>> tail;
  };
  constexpr auto source =
      R"({"array":[true,{"n":2},[3]],"mapping":{"x":null,"y":"a\nb"},"tail":9})";
  Document document;
  ASSERT_TRUE(decode_document_into<Reader>(document, source).is_ok());
  const auto& array = document.array.value().value();
  ASSERT_EQ(array.size(), 3u);
  EXPECT_EQ(array[0].source(), "true");
  EXPECT_EQ(array[1]["/n"].source(), "2");
  EXPECT_EQ(array[2]["/0"].source(), "3");
  const auto& entries = document.mapping.value().value().entries();
  ASSERT_EQ(entries.size(), 2u);
  EXPECT_EQ(entries[0].second.source(), "null");
  EXPECT_EQ(entries[1].second.source(), R"("a\nb")");
  EXPECT_EQ(document.tail.value().value(), 9);
  std::string output;
  json::CompactWriter writer{output};
  ASSERT_TRUE(encode_into(document, writer));
  EXPECT_EQ(output, source);
}

TEST(OwnedDomPipeline, CapturesWithinTypedContainersAndContinuesReader) {
  check_typed_dom_containers<json::SimpleReader, decode::json_dom<>>();
  check_typed_dom_containers<json::FastReader<>, decode::json_dom<>>();
  check_typed_dom_containers<json::SimpleReader, decode::json_deferred<>>();
  check_typed_dom_containers<json::FastReader<>, decode::json_deferred<>>();
}

template <auto Capture>
void check_root_lexeme_roundtrip() {
  using Value = Elem<Capture | store::assign, encode::json_dom>;
  for (const auto source :
       {"true", "false", "null", "[]", "{}", R"("")", R"("a\u0062\n")", "-0",
        "1.2500e+03", "1e9999", "123456789012345678901234567890"}) {
    Value value;
    ASSERT_TRUE(decode_document_into<json::FastReader<>>(value, source).is_ok())
        << source;
    EXPECT_EQ(value.value().source(), source);
    std::string output;
    json::CompactWriter writer{output};
    ASSERT_TRUE(encode_into(value, writer)) << source;
    EXPECT_EQ(output, source);
  }
}

TEST(OwnedDomPipeline, PreservesRootKindsEscapesAndNumericLexemes) {
  check_root_lexeme_roundtrip<decode::json_dom<>>();
  check_root_lexeme_roundtrip<decode::json_deferred<>>();
}

TEST(OwnedDomPipeline, CapturesAndEncodesBothKindsInsideSchema) {
  using Generic = Elem<decode::json_dom<> | store::assign, encode::json_dom>;
  using Deferred =
      Elem<decode::json_deferred<> | store::assign, encode::json_dom>;
  struct Document {
    Entry<Key<"first">, Generic> first;
    Entry<Key<"second">, Deferred> second;
  };
  Document document;
  constexpr auto source =
      R"({"first":{"x":[true,null,"a\nb",18446744073709551615]},"second":[{"n":7},false]})";
  ASSERT_TRUE(
      decode_document_into<json::FastReader<>>(document, source).is_ok());
  EXPECT_EQ(document.first.value().value()["/x/3"].source(),
            "18446744073709551615");
  EXPECT_EQ(document.second.value().value()["/0/n"].source(), "7");
  std::string output;
  json::CompactWriter writer{output};
  ASSERT_TRUE(encode_into(document, writer));
  EXPECT_EQ(output, source);
}

TEST(OwnedDomPipeline, RejectsEncodingThroughNonJsonWriter) {
  using Value = Elem<decode::json_dom<> | store::assign, encode::json_dom>;
  Value value;
  ASSERT_TRUE(value.value().assign(R"({"n":1})").is_ok());
  std::string output;
  msgpack::Writer writer{output};
  EXPECT_FALSE(encode_into(value, writer));
  EXPECT_EQ(writer.error().code, ErrorCode::unsupported_type);
}

TEST(OwnedDomPipeline, RejectsCaptureThroughNonJsonReader) {
  using Value = Elem<decode::json_dom<> | store::assign, encode::json_dom>;
  Value value;
  EXPECT_EQ(decode_document_into<msgpack::Reader<>>(value, "\x01").code,
            ErrorCode::unsupported_type);
}
}  // namespace
