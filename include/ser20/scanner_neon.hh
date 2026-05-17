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
      : full_chunk_end_(input.size() & ~(kChunkSize - 1)),
        eof_(input.empty()),
        input_(input) {}

  [[nodiscard]]
  SER20_ALWAYS_INLINE auto next() noexcept -> size_t {
    const size_t c = cursor_;
    if (c < count_) [[likely]] {
      cursor_ = c + 1;
      return batch_base_ + candidates_[c];
    }
    return next_slow();
  }

  [[nodiscard]]
  SER20_ALWAYS_INLINE auto next_slow() noexcept -> size_t {
    if (eof_) [[unlikely]] {
      return input_.size();
    }
    refill_batch();
    if (count_ == 0) [[unlikely]] {
      return input_.size();
    }
    cursor_ = 1;
    return batch_base_ + candidates_[0];
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
  static constexpr size_t kChunkBlocks = 4;
  static constexpr size_t kChunkSize = kBlockSize * kChunkBlocks;  // 256
  static constexpr size_t kBatchSize = 64;

  static_assert(std::has_single_bit(kBlockSize));

  std::array<uint64_t, kChunkBlocks> masks_{};
  uint32_t nonempty_masks_{0};  // bit i set ↔ masks_[i] != 0
  size_t chunk_base_{0};        // base of the currently loaded chunk
  size_t load_base_{0};         // base of the next chunk to load
  size_t full_chunk_end_{0};    // loads at load_base_ < this are safe full loads

  // candidates_ store offsets (0..255) from chunk_base_.
  // batch_base_ = chunk_base_, so next() returns chunk_base_ +
  // candidates_[cursor_].
  std::array<uint32_t, kBatchSize> candidates_{};
  size_t count_{0};
  size_t cursor_{0};
  size_t batch_base_{0};  // absolute base of the currently loaded chunk

  bool eof_{false};
  std::string_view input_;

  SER20_ALWAYS_INLINE void refresh_nonempty_masks() noexcept {
    nonempty_masks_ = (static_cast<uint32_t>(masks_[0] != 0) << 0) |
                      (static_cast<uint32_t>(masks_[1] != 0) << 1) |
                      (static_cast<uint32_t>(masks_[2] != 0) << 2) |
                      (static_cast<uint32_t>(masks_[3] != 0) << 3);
  }

  // ── Fast path: all 4 blocks fit entirely — zero bounds checks
  // ────────────────
  SER20_ALWAYS_INLINE void load_chunk_full() noexcept {
    chunk_base_ = load_base_;
    load_base_ += kChunkSize;
    const auto* p =
        reinterpret_cast<const uint8_t*>(input_.data() + chunk_base_);
    masks_[0] = scan64(p);
    masks_[1] = scan64(p + kBlockSize);
    masks_[2] = scan64(p + kBlockSize * 2);
    masks_[3] = scan64(p + kBlockSize * 3);
    refresh_nonempty_masks();
  }

  // ── Tail path: handles the final partial chunk and EOF
  // ───────────────────────
  SER20_ALWAYS_INLINE void load_chunk_tail() noexcept {
    chunk_base_ = load_base_;
    load_base_ += kChunkSize;
    const size_t size = input_.size();
    if (chunk_base_ >= size) [[unlikely]] {
      masks_.fill(0);
      nonempty_masks_ = 0;
      eof_ = true;
      return;
    }
    for (size_t i = 0; i < kChunkBlocks; ++i) {
      const size_t base = chunk_base_ + i * kBlockSize;
      if (base >= size) [[unlikely]] {
        for (size_t j = i; j < kChunkBlocks; ++j) masks_[j] = 0;
        refresh_nonempty_masks();
        return;
      }
      const size_t remaining = size - base;
      if (remaining >= kBlockSize) [[likely]] {
        masks_[i] =
            scan64(reinterpret_cast<const uint8_t*>(input_.data() + base));
      } else {
        alignas(16) std::array<uint8_t, kBlockSize> tmp{};
        std::memcpy(tmp.data(), input_.data() + base, remaining);
        masks_[i] = scan64(tmp.data());
        masks_[i] &= (uint64_t{1} << remaining) - 1;
        for (size_t j = i + 1; j < kChunkBlocks; ++j) masks_[j] = 0;
        refresh_nonempty_masks();
        return;
      }
    }
    refresh_nonempty_masks();
  }

  SER20_ALWAYS_INLINE void refill_batch() noexcept {
    count_ = 0;
    cursor_ = 0;

    // Find a chunk with at least one structural candidate.
    for (;;) {
      if (nonempty_masks_ != 0) break;
      if (eof_) [[unlikely]]
        return;
      if (load_base_ < full_chunk_end_) [[likely]] {
        load_chunk_full();
      } else {
        load_chunk_tail();
        if (eof_) [[unlikely]]
          return;
      }
    }

    // Drain all non-empty lanes in this chunk into candidates_.
    // candidates_ hold offsets 0..255 from chunk_base_.
    batch_base_ = chunk_base_;
    while (count_ < kBatchSize && nonempty_masks_ != 0) {
      const uint32_t lane =
          static_cast<uint32_t>(std::countr_zero(nonempty_masks_));
      uint64_t m = masks_[lane];
      const uint32_t lane_base = lane * static_cast<uint32_t>(kBlockSize);
      while (m != 0 && count_ < kBatchSize) {
        const auto off = static_cast<uint32_t>(std::countr_zero(m));
        candidates_[count_++] = lane_base + off;
        m &= m - 1;
      }
      masks_[lane] = m;
      if (m == 0) nonempty_masks_ &= nonempty_masks_ - 1;
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
