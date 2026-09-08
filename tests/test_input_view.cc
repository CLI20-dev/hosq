#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>

#include "hosq/pipeline.hh"
#include "hosq/pipeline/store.hh"

namespace input_view_test {
// No JSON Reader or token type participates in this customization.
struct BinaryWord {};
using Bytes = std::span<const std::byte, 4>;
struct Context {
  std::array<std::byte, 4> bytes{
      std::byte{0x12}, std::byte{0x34}, std::byte{0x56}, std::byte{0x78}};
  unsigned materializations = 0;
  std::uint32_t output = 0;
  auto value() -> std::uint32_t& { return output; }
};
struct ScanWord {
  auto operator()(Context&) const -> BinaryWord { return {}; }
};
struct ReadBigEndian {
  auto operator()(BinaryWord, Context& ctx) const -> std::uint32_t {
    std::uint32_t result = 0;
    for (auto byte : ctx.bytes)
      result = (result << 8) | std::to_integer<std::uint32_t>(byte);
    return result;
  }
};
struct OrdinaryConsumer {
  template <class Input>
  auto operator()(Input bytes, Context&) const -> std::uint32_t {
    static_assert(std::is_same_v<Input, Bytes>);
    return std::to_integer<std::uint32_t>(bytes[0]);
  }
};
}  // namespace input_view_test

namespace hosq::ext {
template <>
struct InputView<input_view_test::BinaryWord> {
  template <class Step, class Context>
  using type = std::conditional_t<
      std::is_same_v<Step, input_view_test::ReadBigEndian>,
      input_view_test::BinaryWord, input_view_test::Bytes>;

  template <class Step, class Context>
  static auto get(input_view_test::BinaryWord&, Context& ctx)
      -> type<Step, Context> {
    if constexpr (std::is_same_v<Step, input_view_test::ReadBigEndian>) {
      return {};
    } else {
      ++ctx.materializations;
      return input_view_test::Bytes{ctx.bytes};
    }
  }
};
}  // namespace hosq::ext

namespace {
using namespace input_view_test;

TEST(InputView, BinaryConsumerUsesTagAndAgreesWithStorageAnalysis) {
  static_assert(std::is_empty_v<BinaryWord>);
  constexpr auto pipeline = hosq::step<ScanWord> |
                            hosq::step<ReadBigEndian> | hosq::store::assign;
  using Info = decltype(pipeline)::info<void, Context>;
  static_assert(std::is_same_v<hosq::detail::pipeline_storage_t<Info>,
                               std::uint32_t>);
  Context ctx;
  pipeline.invoke(ctx);
  EXPECT_EQ(ctx.output, 0x12345678u);
  EXPECT_EQ(ctx.materializations, 0u);
}

TEST(InputView, OpaqueConsumerReceivesOrdinaryViewExactlyOnce) {
  Context ctx;
  (hosq::step<ScanWord> | hosq::step<OrdinaryConsumer> |
   hosq::store::assign).invoke(ctx);
  EXPECT_EQ(ctx.output, 0x12u);
  EXPECT_EQ(ctx.materializations, 1u);
}

TEST(InputView, TerminalScanReturnsOrdinaryViewWithMatchingAnalysis) {
  constexpr auto pipeline = hosq::step<ScanWord>;
  static_assert(std::is_same_v<
      decltype(pipeline)::final_output_type<void, Context>, Bytes>);
  Context ctx;
  auto bytes = pipeline.invoke(ctx);
  static_assert(std::is_same_v<decltype(bytes), Bytes>);
  EXPECT_EQ(bytes.data(), ctx.bytes.data());
  EXPECT_EQ(bytes.size(), 4u);
  EXPECT_EQ(ctx.materializations, 1u);
}
}  // namespace
