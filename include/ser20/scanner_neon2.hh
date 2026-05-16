#pragma once

#include <arm_neon.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <cstring>
#include <string_view>

#include "ser20/scanner_types.hh"

namespace ser20 {

#if defined(__GNUC__) || defined(__clang__)
#define SER20_ALWAYS_INLINE inline __attribute__((always_inline))
#elif defined(_MSC_VER)
#define SER20_ALWAYS_INLINE __forceinline
#else
#define SER20_ALWAYS_INLINE inline
#endif

// Simplified variant of ScannerNeon: no string-interior masking.
// Structural tokens inside quoted strings are emitted as-is.
// Removes quote/backslash tracking, prefix-xor, and escape-carry logic.
class ScannerNeon2 {
 public:
  explicit ScannerNeon2(std::string_view input) noexcept : input_(input) {
    reset();
  }

  [[nodiscard]]
  SER20_ALWAYS_INLINE auto consume_next_token() noexcept -> ScannerToken {
    if (token_cursor_ >= token_count_) [[unlikely]] {
      if (eof_) return {.pos = input_.size(), .kind = TokenKind::End};
      refill_batch();
      if (token_count_ == 0)
        return {.pos = input_.size(), .kind = TokenKind::End};
    }
    const PackedToken pt = tokens_[token_cursor_++];
    return {.pos = batch_base_ + (static_cast<size_t>(pt) >> kOffsetBits),
            .kind = static_cast<TokenKind>(pt & kKindMask)};
  }

  SER20_ALWAYS_INLINE void consume_next_two(ScannerToken& first,
                                            ScannerToken& second) noexcept {
    if (token_cursor_ + 1 < token_count_) [[likely]] {
      const PackedToken first_packed = tokens_[token_cursor_++];
      const PackedToken second_packed = tokens_[token_cursor_++];
      first = token_from_packed(first_packed);
      second = token_from_packed(second_packed);
      return;
    }
    first = consume_next_token();
    second = consume_next_token();
  }

  [[nodiscard]]
  SER20_ALWAYS_INLINE auto consume_until_matching(TokenKind open,
                                                  TokenKind close) noexcept
      -> ScannerToken {
    int depth = 1;
    while (true) {
      while (token_cursor_ < token_count_) {
        const PackedToken pt = tokens_[token_cursor_++];
        const auto kind = static_cast<TokenKind>(pt & kKindMask);
        if (kind == open) [[unlikely]] {
          ++depth;
          continue;
        }
        if (kind == close && --depth == 0) return token_from_packed(pt);
      }
      if (eof_) return {.pos = input_.size(), .kind = TokenKind::End};
      refill_batch();
      if (token_count_ == 0 && eof_)
        return {.pos = input_.size(), .kind = TokenKind::End};
    }
  }

  [[nodiscard]] auto eof() const noexcept -> bool {
    return eof_ && token_cursor_ >= token_count_;
  }

  void reset(size_t pos = 0) noexcept {
    index_ = pos;
    block_base_ = 0;
    block_end_ = pos;
    valid_mask_ = 0;
    structural_mask_ = 0;
    token_count_ = 0;
    token_cursor_ = 0;
    batch_base_ = pos;
    eof_ = false;
  }

 private:
  static constexpr size_t kBlockSize = 64;
  std::array<uint8_t, kBlockSize> block_kind_{};
  static constexpr size_t kBatchSize = 64;
  static constexpr uint16_t kKindMask = 0b111u;
  static constexpr uint16_t kOffsetBits = 3u;
  static constexpr size_t kMaxOffset = (1u << 13) - 1;

  using PackedToken = uint32_t;

  uint64_t structural_mask_{0};
  uint64_t valid_mask_{0};
  size_t block_base_{0};
  size_t block_end_{0};
  size_t index_{0};

  alignas(64) PackedToken tokens_[kBatchSize]{};
  size_t token_count_{0};
  size_t token_cursor_{0};
  size_t batch_base_{0};

  bool eof_{false};

  std::string_view input_;

  SER20_ALWAYS_INLINE void emit(size_t pos, TokenKind kind) noexcept {
    tokens_[token_count_++] = static_cast<PackedToken>(
        ((pos - batch_base_) << kOffsetBits) | static_cast<uint8_t>(kind));
  }

  [[nodiscard]]
  SER20_ALWAYS_INLINE auto token_from_packed(PackedToken pt) const noexcept
      -> ScannerToken {
    return {.pos = batch_base_ + (static_cast<size_t>(pt) >> kOffsetBits),
            .kind = static_cast<TokenKind>(pt & kKindMask)};
  }

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
    token_count_ = 0;
    token_cursor_ = 0;
    batch_base_ = index_;

    while (token_count_ < kBatchSize && !eof_) {
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

      while (interest && token_count_ < kBatchSize) {
        const auto off = static_cast<uint32_t>(std::countr_zero(interest));
        const size_t pos = block_base_ + off;
        if (pos - batch_base_ > kMaxOffset) [[unlikely]] {
          index_ = pos;
          return;
        }

        emit(pos, static_cast<TokenKind>(block_kind_[off]));

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
    const uint8x16_t bit20_v = vdupq_n_u8(0x20);

    const uint8x16_t folded_l_v = vdupq_n_u8('[');
    const uint8x16_t folded_r_v = vdupq_n_u8(']');

    const uint8x16_t co_v = vdupq_n_u8(':');
    const uint8x16_t cm_v = vdupq_n_u8(',');

    const uint8x16_t zero_v = vdupq_n_u8(0);

    const uint8x16_t tk_rbrace_v =
        vdupq_n_u8(static_cast<uint8_t>(TokenKind::RBrace));
    const uint8x16_t tk_lbracket_v =
        vdupq_n_u8(static_cast<uint8_t>(TokenKind::LBracket));
    const uint8x16_t tk_rbracket_v =
        vdupq_n_u8(static_cast<uint8_t>(TokenKind::RBracket));
    const uint8x16_t tk_colon_v =
        vdupq_n_u8(static_cast<uint8_t>(TokenKind::Colon));
    const uint8x16_t tk_comma_v =
        vdupq_n_u8(static_cast<uint8_t>(TokenKind::Comma));

#define SER20_SCAN16_AT(OFFSET)                                          \
  do {                                                                   \
    const uint8x16_t v = vld1q_u8(p + (OFFSET));                         \
                                                                         \
    const uint8x16_t folded = vandq_u8(v, mask_df_v);                    \
                                                                         \
    const uint8x16_t is_left = vceqq_u8(folded, folded_l_v);             \
    const uint8x16_t is_right = vceqq_u8(folded, folded_r_v);            \
    const uint8x16_t is_co = vceqq_u8(v, co_v);                          \
    const uint8x16_t is_cm = vceqq_u8(v, cm_v);                          \
                                                                         \
    const uint8x16_t structural_v =                                      \
        vorrq_u8(vorrq_u8(is_left, is_right), vorrq_u8(is_co, is_cm));   \
                                                                         \
    structural_mask_ |= uint64_t(movemask_u8(structural_v)) << (OFFSET); \
                                                                         \
    const uint8x16_t has_bit20 = vtstq_u8(v, bit20_v);                   \
    const uint8x16_t not_bit20 = vmvnq_u8(has_bit20);                    \
                                                                         \
    const uint8x16_t is_rbrace = vandq_u8(is_right, has_bit20);          \
    const uint8x16_t is_lbracket = vandq_u8(is_left, not_bit20);         \
    const uint8x16_t is_rbracket = vandq_u8(is_right, not_bit20);        \
                                                                         \
    uint8x16_t k = zero_v;                                               \
    k = vbslq_u8(is_rbrace, tk_rbrace_v, k);                             \
    k = vbslq_u8(is_lbracket, tk_lbracket_v, k);                         \
    k = vbslq_u8(is_rbracket, tk_rbracket_v, k);                         \
    k = vbslq_u8(is_co, tk_colon_v, k);                                  \
    k = vbslq_u8(is_cm, tk_comma_v, k);                                  \
                                                                         \
    vst1q_u8(block_kind_.data() + (OFFSET), k);                          \
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
