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

class ScannerNeon {
 public:
  explicit ScannerNeon(std::string_view input) noexcept : input_(input) {
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

  [[nodiscard]] auto eof() const noexcept -> bool {
    return eof_ && token_cursor_ >= token_count_;
  }

  void reset(size_t pos = 0) noexcept {
    index_ = pos;
    block_base_ = 0;
    block_end_ = pos;
    valid_mask_ = 0;
    structural_mask_ = 0;
    quote_mask_ = 0;
    backslash_mask_ = 0;
    token_count_ = 0;
    token_cursor_ = 0;
    batch_base_ = pos;
    eof_ = false;
    in_string_ = false;
  }

 private:
  static constexpr size_t kBlockSize = 64;
  std::array<uint8_t, kBlockSize> block_kind_{};
  static constexpr size_t kBatchSize = 64;
  static constexpr uint16_t kKindMask = 0b111u;
  static constexpr uint16_t kOffsetBits = 3u;
  static constexpr size_t kMaxOffset = (1u << 13) - 1;

  using PackedToken = uint32_t;

  // ── SIMD block state ─────────────────────────────────────────────────────
  uint64_t structural_mask_{0};
  uint64_t quote_mask_{0};
  uint64_t backslash_mask_{0};
  uint64_t valid_mask_{0};
  size_t block_base_{0};
  size_t block_end_{0};
  size_t index_{0};

  // Per-character kind bits for branchless structural dispatch.
  // TokenKind: LBrace=000, RBrace=001, LBracket=010, RBracket=011,
  //            Colon=100, Comma=101.
  // kind_lo  set for: RBrace, RBracket, Comma
  // kind_mid set for: LBracket, RBracket
  // kind_hi  set for: Colon, Comma

  // ── Token batch ──────────────────────────────────────────────────────────
  alignas(64) PackedToken tokens_[kBatchSize]{};
  size_t token_count_{0};
  size_t token_cursor_{0};
  size_t batch_base_{0};

  // ── Parser micro-state ───────────────────────────────────────────────────
  bool eof_{false};
  bool in_string_{false};

  std::string_view input_;

  // ── Helpers ──────────────────────────────────────────────────────────────

  SER20_ALWAYS_INLINE void emit(size_t pos, TokenKind kind) noexcept {
    tokens_[token_count_++] = static_cast<PackedToken>(
        ((pos - batch_base_) << kOffsetBits) | static_cast<uint8_t>(kind));
  }

  [[nodiscard]]
  SER20_ALWAYS_INLINE auto after_cursor_mask() const noexcept -> uint64_t {
    const auto off = static_cast<uint32_t>(index_ - block_base_);
    return off < kBlockSize ? (~uint64_t{0} << off) : uint64_t{0};
  }

  template <bool ComputeEscapeCarry = false>
  SER20_ALWAYS_INLINE void load_block() noexcept {
    uint64_t carry = 0;

    if constexpr (ComputeEscapeCarry) {
      const uint64_t bs = backslash_mask_ & valid_mask_;
      if (bs != 0) {
        const auto used = static_cast<uint32_t>(std::popcount(valid_mask_));
        carry = static_cast<uint64_t>(std::countl_one(bs << (64 - used)) & 1u);
      }
    }

    block_base_ = (index_ / kBlockSize) * kBlockSize;
    block_end_ = std::min(block_base_ + kBlockSize, input_.size());

    structural_mask_ = quote_mask_ = backslash_mask_ = 0;

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

    if constexpr (ComputeEscapeCarry) {
      carry &= 1u;
      const uint64_t q0 = quote_mask_ & 1u;
      const uint64_t b0 = backslash_mask_ & 1u;
      quote_mask_ &= ~(carry & q0);
      backslash_mask_ &= ~(carry & (q0 ^ 1u) & b0);
    }
    if (backslash_mask_ != 0) {
      quote_mask_ &= ~compute_escaped_mask(backslash_mask_);
    }

    // quote_mask_ は escape 済み quote のみ
    const uint64_t string_mask =
        compute_string_mask(quote_mask_ & valid_mask_, in_string_) & valid_mask_;

    // string 内の structural を事前に消す
    structural_mask_ &= ~string_mask;

    // 次 block の初期 string 状態を更新
    in_string_ ^= (std::popcount(quote_mask_ & valid_mask_) & 1u) != 0;
  }

  SER20_ALWAYS_INLINE void advance_block() noexcept {
    if (block_end_ >= input_.size()) {
      eof_ = true;
      index_ = input_.size();
      return;
    }
    index_ = block_end_;
    load_block<true>();
  }

  [[nodiscard]]
  SER20_ALWAYS_INLINE static auto prefix_xor(uint64_t x) noexcept -> uint64_t {
    x ^= x << 1;
    x ^= x << 2;
    x ^= x << 4;
    x ^= x << 8;
    x ^= x << 16;
    x ^= x << 32;
    return x;
  }

  [[nodiscard]]
  SER20_ALWAYS_INLINE static auto compute_string_mask(
      uint64_t quote_mask, bool initially_in_string) noexcept -> uint64_t {
    uint64_t m = prefix_xor(quote_mask);

    if (initially_in_string) {
      m = ~m;
    }

    // quote 自体は token として残す
    m &= ~quote_mask;
    return m;
  }

  void refill_batch() noexcept {
    token_count_ = 0;
    token_cursor_ = 0;
    batch_base_ = index_;

    while (token_count_ < kBatchSize && !eof_) {
      if (index_ >= block_end_) {
        advance_block();
        if (eof_) break;
      }

      if (index_ - batch_base_ > kMaxOffset) break;

      const auto start_off = static_cast<uint32_t>(index_ - block_base_);

      const uint64_t after =
          start_off < 64 ? (~uint64_t{0} << start_off) : uint64_t{0};

      uint64_t interest = (quote_mask_ | structural_mask_) & valid_mask_ & after;

      if (interest == 0) {
        index_ = block_end_;
        continue;
      }

      while (interest && token_count_ < kBatchSize) {
        const auto off = static_cast<uint32_t>(std::countr_zero(interest));
        const size_t pos = block_base_ + off;
        if (pos - batch_base_ > kMaxOffset) {
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
  SER20_ALWAYS_INLINE static auto compute_escaped_mask(uint64_t bs) noexcept
      -> uint64_t {
    const uint64_t starts = bs & ~(bs << 1);
    const uint64_t even_escaped =
        (bs + (starts & 0x5555555555555555ULL)) & ~bs & 0xAAAAAAAAAAAAAAAAULL;
    const uint64_t odd_escaped =
        (bs + (starts & 0xAAAAAAAAAAAAAAAAULL)) & ~bs & 0x5555555555555555ULL;
    return even_escaped | odd_escaped;
  }

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
    const uint8x16_t lb_v = vdupq_n_u8('{');
    const uint8x16_t rb_v = vdupq_n_u8('}');
    const uint8x16_t lk_v = vdupq_n_u8('[');
    const uint8x16_t rk_v = vdupq_n_u8(']');
    const uint8x16_t co_v = vdupq_n_u8(':');
    const uint8x16_t cm_v = vdupq_n_u8(',');
    const uint8x16_t q_v = vdupq_n_u8('"');
    const uint8x16_t bs_v = vdupq_n_u8('\\');

#define SER20_SCAN16_AT(OFFSET)                                                 \
  do {                                                                          \
    const uint8x16_t v = vld1q_u8(p + (OFFSET));                                \
                                                                                \
    const uint8x16_t is_lb = vceqq_u8(v, lb_v);                                 \
    const uint8x16_t is_rb = vceqq_u8(v, rb_v);                                 \
    const uint8x16_t is_lk = vceqq_u8(v, lk_v);                                 \
    const uint8x16_t is_rk = vceqq_u8(v, rk_v);                                 \
    const uint8x16_t is_co = vceqq_u8(v, co_v);                                 \
    const uint8x16_t is_cm = vceqq_u8(v, cm_v);                                 \
    const uint8x16_t is_q = vceqq_u8(v, q_v);                                   \
                                                                                \
    const uint8x16_t structural_v =                                             \
        vorrq_u8(vorrq_u8(vorrq_u8(is_lb, is_rb), vorrq_u8(is_lk, is_rk)),      \
                 vorrq_u8(is_co, is_cm));                                       \
                                                                                \
    structural_mask_ |= uint64_t(movemask_u8(structural_v)) << (OFFSET);        \
    quote_mask_ |= uint64_t(movemask_u8(is_q)) << (OFFSET);                     \
    backslash_mask_ |= uint64_t(movemask_u8(vceqq_u8(v, bs_v))) << (OFFSET);    \
                                                                                \
    uint8x16_t k = vdupq_n_u8(0);                                               \
    k = vbslq_u8(is_rb, vdupq_n_u8(static_cast<uint8_t>(TokenKind::RBrace)),    \
                 k);                                                            \
    k = vbslq_u8(is_lk, vdupq_n_u8(static_cast<uint8_t>(TokenKind::LBracket)),  \
                 k);                                                            \
    k = vbslq_u8(is_rk, vdupq_n_u8(static_cast<uint8_t>(TokenKind::RBracket)),  \
                 k);                                                            \
    k = vbslq_u8(is_co, vdupq_n_u8(static_cast<uint8_t>(TokenKind::Colon)), k); \
    k = vbslq_u8(is_cm, vdupq_n_u8(static_cast<uint8_t>(TokenKind::Comma)), k); \
    k = vbslq_u8(is_q, vdupq_n_u8(static_cast<uint8_t>(TokenKind::Quote)), k);  \
                                                                                \
    vst1q_u8(block_kind_.data() + (OFFSET), k);                                 \
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
