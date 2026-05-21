#include "hosq/pipeline/scan.hh"

#include <gtest/gtest.h>

// #include <string_view>
//
// #include "hosq/context.hh"
// #include "hosq/json/reader.hh"

using namespace std::string_view_literals;

// namespace {
//
// using Reader = hosq::json::Reader<>;
// using DecodeCtx = hosq::DecodeContext<int, Reader>;
//
// auto make_context(Reader& reader) -> DecodeCtx {
//   static int value = 0;
//   return DecodeCtx{value, reader};
// }
//
// void expect_current_literal(Reader& reader, std::string_view expected) {
//   const auto& token = reader.current_token();
//   ASSERT_EQ(token.type, hosq::TokenType::Scalar);
//   ASSERT_EQ(static_cast<hosq::JsonTokenContext>(token.context),
//             hosq::JsonTokenContext::literal);
//
//   auto ctx = make_context(reader);
//   auto scalar = decltype(hosq::scan::literal)::invoke(ctx);
//   ASSERT_TRUE(scalar.has_value());
//   EXPECT_EQ(*scalar, expected);
// }
//
// }  // namespace
//
// TEST(JsonScanObject, ConsumesNestedObjectAndReturnsFullSubstring) {
//   Reader reader;
//   reader.reset(R"({"a":{"b":1},"c":[true,false]} 123)"sv);
//
//   ASSERT_TRUE(reader.next_token().has_value());
//   auto ctx = make_context(reader);
//   auto object = decltype(hosq::scan::object)::invoke(ctx);
//
//   ASSERT_TRUE(object.has_value());
//   EXPECT_EQ(*object, R"({"a":{"b":1},"c":[true,false]})"sv);
//
//   ASSERT_TRUE(reader.next_token().has_value());
//   expect_current_literal(reader, "123"sv);
// }
//
// TEST(JsonScanArray, ConsumesNestedArrayAndReturnsFullSubstring) {
//   Reader reader;
//   reader.reset(R"([{"a":1},[2,3],"x"] null)"sv);
//
//   ASSERT_TRUE(reader.next_token().has_value());
//   auto ctx = make_context(reader);
//   auto array = decltype(hosq::scan::array)::invoke(ctx);
//
//   ASSERT_TRUE(array.has_value());
//   EXPECT_EQ(*array, R"([{"a":1},[2,3],"x"])"sv);
//
//   ASSERT_TRUE(reader.next_token().has_value());
//   expect_current_literal(reader, "null"sv);
// }
//
// TEST(JsonScanObject, IgnoresBracketsInsideStrings) {
//   Reader reader;
//   reader.reset(R"({"s":"[not bracket]","o":{"x":"}"}} true)"sv);
//
//   ASSERT_TRUE(reader.next_token().has_value());
//   auto ctx = make_context(reader);
//   auto object = decltype(hosq::scan::object)::invoke(ctx);
//
//   ASSERT_TRUE(object.has_value());
//   EXPECT_EQ(*object, R"({"s":"[not bracket]","o":{"x":"}"}})"sv);
//
//   ASSERT_TRUE(reader.next_token().has_value());
//   expect_current_literal(reader, "true"sv);
// }
//
// TEST(JsonScanConstInvocability, CompoundScannersRequireMutableContext) {
//   using ObjectPipeline = decltype(hosq::scan::object);
//   using ArrayPipeline = decltype(hosq::scan::array);
//
//   static_assert(
//       !ObjectPipeline::template callable_with_const_context<DecodeCtx, void>());
//   static_assert(
//       !ArrayPipeline::template callable_with_const_context<DecodeCtx, void>());
//
//   static_assert(
//       ObjectPipeline::template callable_with_mutable_context<DecodeCtx, void>());
//   static_assert(
//       ArrayPipeline::template callable_with_mutable_context<DecodeCtx, void>());
// }
//
// TEST(JsonScanConstInvocability, ScalarScannersRemainConstInvokable) {
//   using LiteralPipeline = decltype(hosq::scan::literal);
//   using ScalarPipeline = decltype(hosq::scan::scalar);
//
//   static_assert(
//       LiteralPipeline::template callable_with_const_context<DecodeCtx, void>());
//   static_assert(
//       ScalarPipeline::template callable_with_const_context<DecodeCtx, void>());
// }
//
// TEST(JsonScanScalar, ReusesResolvedLengthWithoutAdvancingAgain) {
//   Reader reader;
//   reader.reset(R"({"id": 2})"sv);
//
//   ASSERT_TRUE(reader.next_token().has_value());
//   ASSERT_TRUE(reader.advance(1).has_value());
//   ASSERT_TRUE(reader.next_token().has_value());
//
//   auto ctx = make_context(reader);
//   auto first = decltype(hosq::scan::string)::invoke(ctx);
//   ASSERT_TRUE(first.has_value());
//   EXPECT_EQ(*first, "id"sv);
//
//   auto second = decltype(hosq::scan::string)::invoke(ctx);
//   ASSERT_TRUE(second.has_value());
//   EXPECT_EQ(*second, "id"sv);
//
//   ASSERT_TRUE(reader.next_token().has_value());
//   auto value = decltype(hosq::scan::literal)::invoke(ctx);
//   ASSERT_TRUE(value.has_value());
//   EXPECT_EQ(*value, "2"sv);
// }
