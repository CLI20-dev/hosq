#pragma once

#include <arm_neon.h>

#include <algorithm>
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

// Emits uint16_t offsets (from batch_base_) for every structural candidate:
// { } [ ] : , "  — the parser checks input[pos] to determine the actual char.
class ScannerNeon {
 public:
  explicit ScannerNeon(std::string_view input) noexcept : input_(input) {
    reset();
  }

  // Returns the absolute input position of the next structural candidate.
  // Returns input_.size() when exhausted.
  [[nodiscard]]
  SER20_ALWAYS_INLINE auto next() noexcept -> size_t {
    if (cursor_ >= count_) [[unlikely]] {
      if (eof_) return input_.size();
      refill_batch();
      if (count_ == 0) return input_.size();
    }
    return batch_base_ + candidates_[cursor_++];
  }

  // Fast path: fills first and second with the next two candidate positions.
  SER20_ALWAYS_INLINE void next_two(size_t& first, size_t& second) noexcept {
    if (cursor_ + 1 < count_) [[likely]] {
      first = batch_base_ + candidates_[cursor_++];
      second = batch_base_ + candidates_[cursor_++];
      return;
    }
    first = next();
    second = next();
  }

  [[nodiscard]] auto eof() const noexcept -> bool {
    return eof_ && cursor_ >= count_;
  }

  [[nodiscard]] auto input() const noexcept -> std::string_view {
    return input_;
  }

  void reset(size_t pos = 0) noexcept {
    index_ = pos;
    block_base_ = 0;
    block_end_ = pos;
    valid_mask_ = 0;
    structural_mask_ = 0;
    count_ = 0;
    cursor_ = 0;
    batch_base_ = pos;
    eof_ = false;
  }

 private:
  static constexpr size_t kBlockSize = 64;
  static constexpr size_t kBatchSize = 64;
  static constexpr size_t kMaxOffset = (size_t{1} << 16) - 1;

  // ── SIMD block state ─────────────────────────────────────────────────────
  uint64_t structural_mask_{0};
  uint64_t valid_mask_{0};
  size_t block_base_{0};
  size_t block_end_{0};
  size_t index_{0};

  // ── Candidate batch: plain uint16_t offsets from batch_base_ ─────────────
  alignas(64) uint16_t candidates_[kBatchSize]{};
  size_t count_{0};
  size_t cursor_{0};
  size_t batch_base_{0};

  bool eof_{false};
  std::string_view input_;

  SER20_ALWAYS_INLINE void load_block() noexcept {
    block_base_ = (index_ / kBlockSize) * kBlockSize;
    block_end_ = std::min(block_base_ + kBlockSize, input_.size());
    structural_mask_ = 0;

    const size_t remaining = input_.size() - block_base_;
    if (remaining == 0) [[unlikely]] {
      valid_mask_ = 0;
      eof_ = true;
      return;
    }
    if (remaining >= kBlockSize) [[likely]] {
      scan64(reinterpret_cast<const uint8_t*>(input_.data() + block_base_));
      valid_mask_ = ~uint64_t{0};
    } else {
      alignas(16) std::array<uint8_t, kBlockSize> tmp{};
      std::memcpy(tmp.data(), input_.data() + block_base_, remaining);
      scan64(tmp.data());
      valid_mask_ = (uint64_t{1} << remaining) - 1;
    }
  }

  SER20_ALWAYS_INLINE void advance_block() noexcept {
    if (block_end_ >= input_.size()) {
      eof_ = true;
      index_ = input_.size();
      return;
    }
    index_ = block_end_;
    load_block();
  }

  SER20_ALWAYS_INLINE void refill_batch() noexcept {
    count_ = 0;
    cursor_ = 0;
    batch_base_ = index_;

    while (count_ < kBatchSize && !eof_) {
      if (index_ >= block_end_) [[unlikely]] {
        advance_block();
        if (eof_) [[unlikely]]
          break;
      }

      if (index_ - batch_base_ > kMaxOffset) break;

      const auto start_off = static_cast<uint32_t>(index_ - block_base_);
      const uint64_t after =
          start_off < 64 ? (~uint64_t{0} << start_off) : uint64_t{0};
      uint64_t interest = structural_mask_ & valid_mask_ & after;

      if (interest == 0) {
        index_ = block_end_;
        continue;
      }

      while (interest && count_ < kBatchSize) {
        const auto off = static_cast<uint32_t>(std::countr_zero(interest));
        const size_t pos = block_base_ + off;
        if (pos - batch_base_ > kMaxOffset) [[unlikely]] {
          index_ = pos;
          return;
        }
        candidates_[count_++] = static_cast<uint16_t>(pos - batch_base_);
        index_ = pos + 1;
        interest &= interest - 1;
      }
    }
  }

  // ── SIMD internals ───────────────────────────────────────────────────────

  [[nodiscard]]
  SER20_ALWAYS_INLINE static auto pack8_lsb(uint64_t a) noexcept -> uint32_t {
    a &= 0x0101010101010101ull;
    return static_cast<uint32_t>((a * 0x0102040810204080ull) >> 56);
  }

  [[nodiscard]]
  SER20_ALWAYS_INLINE static auto movemask_u8(uint8x16_t v) noexcept
      -> uint16_t {
    const uint64x2_t x = vreinterpretq_u64_u8(v);
    const uint64_t lo = vgetq_lane_u64(x, 0);
    const uint64_t hi = vgetq_lane_u64(x, 1);
    return static_cast<uint16_t>(pack8_lsb(lo) | (pack8_lsb(hi) << 8));
  }

  SER20_ALWAYS_INLINE void scan64(const uint8_t* p) noexcept {
    const uint8x16_t mask_df_v = vdupq_n_u8(0xdf);
    const uint8x16_t folded_l_v = vdupq_n_u8('[');
    const uint8x16_t folded_r_v = vdupq_n_u8(']');
    const uint8x16_t co_v = vdupq_n_u8(':');
    const uint8x16_t cm_v = vdupq_n_u8(',');
    const uint8x16_t q_v = vdupq_n_u8('"');

#define SER20_SCAN16_AT(OFFSET)                                               \
  do {                                                                        \
    const uint8x16_t v = vld1q_u8(p + (OFFSET));                              \
    const uint8x16_t folded = vandq_u8(v, mask_df_v);                         \
    const uint8x16_t is_left = vceqq_u8(folded, folded_l_v);                  \
    const uint8x16_t is_right = vceqq_u8(folded, folded_r_v);                 \
    const uint8x16_t is_co = vceqq_u8(v, co_v);                               \
    const uint8x16_t is_cm = vceqq_u8(v, cm_v);                               \
    const uint8x16_t is_q = vceqq_u8(v, q_v);                                 \
    const uint8x16_t sv = vorrq_u8(                                           \
        vorrq_u8(vorrq_u8(is_left, is_right), vorrq_u8(is_co, is_cm)), is_q); \
    structural_mask_ |= uint64_t(movemask_u8(sv)) << (OFFSET);                \
  } while (false)

    SER20_SCAN16_AT(0);
    SER20_SCAN16_AT(16);
    SER20_SCAN16_AT(32);
    SER20_SCAN16_AT(48);

#undef SER20_SCAN16_AT
  }
};

#undef SER20_ALWAYS_INLINE

}  // namespace ser20
