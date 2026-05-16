#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "ser20/scanner_types.hh"

namespace ser20 {

class Scanner {
 public:
  explicit Scanner(std::string_view input) noexcept : input_(input) {}

  [[nodiscard]] auto consume_next_token() noexcept -> ScannerToken {
    if (token_cursor_ >= token_count_) {
      if (eof_) return {input_.size(), TokenKind::End};
      refill_batch();
      if (token_count_ == 0) return {input_.size(), TokenKind::End};
    }
    const PackedToken pt = tokens_[token_cursor_++];
    return {batch_base_ + (static_cast<size_t>(pt) >> kOffsetBits),
            static_cast<TokenKind>(pt & kKindMask)};
  }

  void consume_next_two(ScannerToken& first, ScannerToken& second) noexcept {
    if (token_cursor_ + 1 < token_count_) {
      const PackedToken first_packed = tokens_[token_cursor_++];
      const PackedToken second_packed = tokens_[token_cursor_++];
      first = token_from_packed(first_packed);
      second = token_from_packed(second_packed);
      return;
    }
    first = consume_next_token();
    second = consume_next_token();
  }

  [[nodiscard]] auto consume_until_matching(TokenKind open,
                                            TokenKind close) noexcept
      -> ScannerToken {
    int depth = 1;
    while (true) {
      while (token_cursor_ < token_count_) {
        const PackedToken pt = tokens_[token_cursor_++];
        const auto kind = static_cast<TokenKind>(pt & kKindMask);
        if (kind == TokenKind::Quote) {
          (void)skip_string();
          continue;
        }
        if (kind == open) {
          ++depth;
          continue;
        }
        if (kind == close && --depth == 0) return token_from_packed(pt);
      }
      if (eof_) return {input_.size(), TokenKind::End};
      refill_batch();
      if (token_count_ == 0 && eof_) return {input_.size(), TokenKind::End};
    }
  }

  // Called after an opening Quote has been consumed.
  // Consumes tokens until the matching unescaped close Quote and returns it.
  auto skip_string() noexcept -> ScannerToken {
    while (true) {
      while (token_cursor_ < token_count_) {
        const PackedToken pt = tokens_[token_cursor_++];
        if (static_cast<TokenKind>(pt & kKindMask) != TokenKind::Quote) continue;
        const size_t pos = batch_base_ + (static_cast<size_t>(pt) >> kOffsetBits);
        if (!is_escaped_at(pos)) return token_from_packed(pt);
      }
      if (eof_) return {input_.size(), TokenKind::End};
      refill_batch();
      if (token_count_ == 0) return {input_.size(), TokenKind::End};
    }
  }

  [[nodiscard]] auto eof() const noexcept -> bool {
    return eof_ && token_cursor_ >= token_count_;
  }

  void reset(size_t pos = 0) noexcept {
    index_ = pos;
    token_count_ = 0;
    token_cursor_ = 0;
    batch_base_ = pos;
    eof_ = false;
  }

 private:
  static constexpr size_t kBatchSize = 64;
  static constexpr uint16_t kKindMask = 0b111u;
  static constexpr uint16_t kOffsetBits = 3u;
  static constexpr size_t kMaxOffset = (1u << 13) - 1;

  using PackedToken = uint16_t;

  PackedToken tokens_[kBatchSize];
  size_t token_count_{0};
  size_t token_cursor_{0};
  size_t batch_base_{0};
  size_t index_{0};

  bool eof_{false};

  std::string_view input_;

  auto is_escaped_at(size_t pos) const noexcept -> bool {
    size_t count = 0;
    while (count < pos && input_[pos - 1 - count] == '\\') ++count;
    return (count & 1u) != 0;
  }

  void emit(size_t pos, TokenKind kind) noexcept {
    tokens_[token_count_++] = static_cast<PackedToken>(
        ((pos - batch_base_) << kOffsetBits) | static_cast<uint8_t>(kind));
  }

  [[nodiscard]] auto token_from_packed(PackedToken pt) const noexcept
      -> ScannerToken {
    return {batch_base_ + (static_cast<size_t>(pt) >> kOffsetBits),
            static_cast<TokenKind>(pt & kKindMask)};
  }

  void refill_batch() noexcept {
    token_count_ = 0;
    token_cursor_ = 0;
    batch_base_ = index_;

    while (token_count_ < kBatchSize && index_ < input_.size()) {
      if (index_ - batch_base_ > kMaxOffset) break;

      switch (input_[index_]) {
        case '"':  emit(index_++, TokenKind::Quote);    break;
        case '{':  emit(index_++, TokenKind::LBrace);   break;
        case '}':  emit(index_++, TokenKind::RBrace);   break;
        case '[':  emit(index_++, TokenKind::LBracket); break;
        case ']':  emit(index_++, TokenKind::RBracket); break;
        case ':':  emit(index_++, TokenKind::Colon);    break;
        case ',':  emit(index_++, TokenKind::Comma);    break;
        default:   ++index_;                             break;
      }
    }

    if (index_ >= input_.size()) eof_ = true;
  }
};

}  // namespace ser20
