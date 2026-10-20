#include <gtest/gtest.h>

#include <string>
#include <string_view>

#include "hosq/hosq.hh"
#include "hosq/json/dom.hh"
#include "hosq/json/simple_reader.hh"

namespace {
using namespace hosq;

template <class Dom>
void special_keys() {
  Dom dom;
  ASSERT_TRUE(
      dom.assign(R"({"\u0000":1,"\uD834\uDD1E":2,"é":3,"é":4})").is_ok());
  const std::string nul_pointer("/\0", 2);
  EXPECT_EQ(dom[nul_pointer].source(), "1");
  EXPECT_EQ(dom["/𝄞"].source(), "2");
  EXPECT_EQ(dom["/é"].source(), "3");
  EXPECT_EQ(dom["/é"].source(), "4");
  using Number = types::value::Int<int>;
  Number number;
  EXPECT_EQ(dom[std::string("/\xc0\xaf", 3)].decode_into(number).code,
            ErrorCode::invalid_token);
  EXPECT_TRUE(dom.assign(std::string("{\"\xc0\xaf\":0}")).has_error());
  EXPECT_TRUE(dom.assign(std::string("{\"x\":\"\xed\xa0\x80\"}")).has_error());
  EXPECT_EQ(dom[nul_pointer].source(), "1");
  EXPECT_EQ(dom.assign(R"({"\u0000":1,"\u0000":2})").code,
            ErrorCode::duplicate_member);
}
TEST(OwnedDomEdges, NulAndUnicodeKeysAreExact) {
  special_keys<json::GenericDom<>>();
  special_keys<json::DeferredDom<>>();
}

template <class Dom>
void explicit_categories() {
  Dom dom;
  ASSERT_TRUE(dom.assign("[7,7.0]").is_ok());
  types::value::Int<> integer;
  types::value::Float<> floating;
  EXPECT_TRUE(dom["/0"].decode_into(integer).is_ok());
  EXPECT_TRUE(dom["/1"].decode_into(floating).is_ok());
  EXPECT_EQ(dom["/1"].decode_into(integer).code, ErrorCode::type_mismatch);
  EXPECT_EQ(dom["/0"].decode_into(floating).code, ErrorCode::type_mismatch);
}
TEST(OwnedDomEdges, PipelinesDetermineNumericCategory) {
  explicit_categories<json::GenericDom<>>();
  explicit_categories<json::DeferredDom<>>();
}

template <class R>
void prior_error() {
  using Value = Elem<decode::json_dom<> | store::assign, encode::json_dom>;
  Value value;
  ASSERT_TRUE(value.value().assign(R"({"preserved":1})").is_ok());
  R reader;
  typename R::state_type state;
  reader.reset(state, R"({"other":2})");
  ASSERT_TRUE(reader.next_token(state));
  detail::report_reader_error(
      reader, state, {ErrorCode::validation_failed, 3, "prior failure"});
  const auto before = reader.current_token(state);
  auto ctx = Decoder<R>::make_decode_context(value, reader, state);
  const auto captured = decode::json_dom<>.invoke(ctx);
  EXPECT_TRUE(captured.source().empty());
  EXPECT_EQ(reader.error(state).code, ErrorCode::validation_failed);
  EXPECT_EQ(reader.error(state).offset, 3u);
  EXPECT_EQ(value.value()["/preserved"].source(), "1");
  const auto after = reader.current_token(state);
  EXPECT_EQ(after.type, before.type);
  EXPECT_EQ(after.offset, before.offset);
  EXPECT_EQ(after.length, before.length);
  EXPECT_EQ(after.context, before.context);
}
TEST(OwnedDomEdges, CapturePreservesPriorReaderErrorWithoutTraversal) {
  prior_error<json::FastReader<>>();
  prior_error<json::SimpleReader>();
}

}  // namespace
