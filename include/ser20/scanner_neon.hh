#pragma once

#include <arm_neon.h>

#include <array>
#include <bit>
#include <cstdint>
#include <cstring>
#include <string_view>

namespace ser20 {

#ifndef SER20_ALWAYS_INLINE
#if defined(__GNUC__) || defined(__clang__)
#define SER20_ALWAYS_INLINE inline __attribute__((always_inline))
#elif defined(_MSC_VER)
#define SER20_ALWAYS_INLINE __forceinline
#else
#define SER20_ALWAYS_INLINE inline
#endif
#endif

class ScannerNeon {
 public:
  explicit ScannerNeon(std::string_view input) noexcept
      : eof_(input.empty()), input_(input) {}

  [[nodiscard]]
  SER20_ALWAYS_INLINE auto next() noexcept -> size_t {
    if (cursor_ >= count_) [[unlikely]] {
      if (eof_) [[unlikely]]
        return input_.size();
      refill_batch();
      if (count_ == 0) [[unlikely]]
        return input_.size();
    }

    return batch_base_ + candidates_[cursor_++];
  }

  SER20_ALWAYS_INLINE void next_two(size_t& first, size_t& second) noexcept {
    if (cursor_ + 1 < count_) [[likely]] {
      first = batch_base_ + candidates_[cursor_++];
      second = batch_base_ + candidates_[cursor_++];
      return;
    }
    first = next();
    second = next();
  }

  [[nodiscard]]
  auto eof() const noexcept -> bool {
    return eof_ && cursor_ >= count_;
  }

  [[nodiscard]]
  auto input() const noexcept -> std::string_view {
    return input_;
  }

 private:
  static constexpr size_t kBlockSize = 64;
  static constexpr size_t kBatchSize = 64;

  static_assert(std::has_single_bit(kBlockSize));

  // structural_mask_ is the unconsumed structural mask for the current block.
  uint64_t structural_mask_{0};
  size_t block_base_{0};
  size_t block_end_{0};
  size_t index_{0};

  std::array<uint32_t, kBatchSize> candidates_{};
  size_t count_{0};
  size_t cursor_{0};
  size_t batch_base_{0};

  bool eof_{false};
  std::string_view input_;

  SER20_ALWAYS_INLINE void load_full_block_at(size_t base) noexcept {
    block_base_ = base;
    block_end_ = base + kBlockSize;
    structural_mask_ =
        scan64(reinterpret_cast<const uint8_t*>(input_.data() + base));
  }

  SER20_ALWAYS_INLINE void load_tail_block_at(size_t base) noexcept {
    block_base_ = base;
    block_end_ = input_.size();
    const size_t remaining = input_.size() - base;
    alignas(16) std::array<uint8_t, kBlockSize> tmp{};
    std::memcpy(tmp.data(), input_.data() + base, remaining);
    structural_mask_ = scan64(tmp.data());

    // remaining is guaranteed to be 1..63 here.
    structural_mask_ &= (uint64_t{1} << remaining) - 1;
  }

  SER20_ALWAYS_INLINE void load_block_at(size_t base) noexcept {
    const size_t size = input_.size();

    // Precondition in hot path: base < size.
    if (base + kBlockSize <= size) [[likely]] {
      load_full_block_at(base);
    } else {
      load_tail_block_at(base);
    }
  }

  SER20_ALWAYS_INLINE void advance_block() noexcept {
    const size_t size = input_.size();

    if (block_end_ >= size) [[unlikely]] {
      eof_ = true;
      index_ = size;
      structural_mask_ = 0;
      return;
    }

    index_ = block_end_;
    load_block_at(block_end_);
  }

  SER20_ALWAYS_INLINE void refill_batch() noexcept {
    count_ = 0;
    cursor_ = 0;
    batch_base_ = index_;

    for (;;) {
      if (structural_mask_ == 0) {
        advance_block();

        if (eof_) [[unlikely]] {
          return;
        }

        if (structural_mask_ == 0) {
          continue;
        }
      }

      const size_t base_delta = block_base_ - batch_base_;
      uint32_t last_off = 0;

      do {
        const auto off =
            static_cast<uint32_t>(std::countr_zero(structural_mask_));
        candidates_[count_++] = static_cast<uint32_t>(base_delta + off);
        last_off = off;
        structural_mask_ &= structural_mask_ - 1;
      } while (structural_mask_ && count_ < kBatchSize);

      index_ = block_base_ + last_off + 1;
      return;
    }
  }

  [[nodiscard]]
  SER20_ALWAYS_INLINE static auto pack8_lsb(uint64_t a) noexcept -> uint64_t {
    a &= 0x0101010101010101ull;
    return (a * 0x0102040810204080ull) >> 56;
  }

  [[nodiscard]]
  SER20_ALWAYS_INLINE static auto movemask_u8(uint8x16_t v) noexcept
      -> uint16_t {
    const uint64x2_t x = vreinterpretq_u64_u8(v);
    const uint64_t lo = vgetq_lane_u64(x, 0);
    const uint64_t hi = vgetq_lane_u64(x, 1);

    return static_cast<uint16_t>(pack8_lsb(lo) | (pack8_lsb(hi) << 8));
  }

  [[nodiscard]]
  SER20_ALWAYS_INLINE auto scan64(const uint8_t* p) noexcept -> uint64_t {
    uint64_t mask = 0;

    const uint8x16_t low_nibble_mask = vdupq_n_u8(0x0f);

    const uint8x16_t low_table = {
        0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x08, 0xa0, 0x04, 0xa0, 0x00, 0x00,
    };

    const uint8x16_t high_table = {
        0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    };

#define SER20_SCAN16_AT(OFFSET)                              \
  do {                                                       \
    const uint8x16_t v = vld1q_u8(p + (OFFSET));             \
    const uint8x16_t lo = vandq_u8(v, low_nibble_mask);      \
    const uint8x16_t hi = vshrq_n_u8(v, 4);                  \
                                                             \
    const uint8x16_t low_mask = vqtbl1q_u8(low_table, lo);   \
    const uint8x16_t high_mask = vqtbl1q_u8(high_table, hi); \
    const uint8x16_t sv = vtstq_u8(low_mask, high_mask);     \
                                                             \
    mask |= uint64_t(movemask_u8(sv)) << (OFFSET);           \
  } while (0)

    SER20_SCAN16_AT(0);
    SER20_SCAN16_AT(16);
    SER20_SCAN16_AT(32);
    SER20_SCAN16_AT(48);

#undef SER20_SCAN16_AT

    return mask;
  }
};

#undef SER20_ALWAYS_INLINE

}  // namespace ser20
