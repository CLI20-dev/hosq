#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace ser20 {

class Scanner {
 public:
  explicit Scanner(std::string_view input) noexcept : input_(input) {}

  [[nodiscard]] auto next() noexcept -> size_t {
    if (cursor_ >= count_) {
      if (eof_) return input_.size();
      refill_batch();
      if (count_ == 0) return input_.size();
    }
    return batch_base_ + candidates_[cursor_++];
  }

  void next_two(size_t& first, size_t& second) noexcept {
    if (cursor_ + 1 < count_) {
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

 private:
  static constexpr size_t kBatchSize = 64;
  static constexpr size_t kMaxOffset = (size_t{1} << 16) - 1;

  uint16_t candidates_[kBatchSize]{};
  size_t count_{0};
  size_t cursor_{0};
  size_t batch_base_{0};
  size_t index_{0};

  bool eof_{false};
  std::string_view input_;

  void refill_batch() noexcept {
    count_ = 0;
    cursor_ = 0;
    batch_base_ = index_;

    while (count_ < kBatchSize && index_ < input_.size()) {
      if (index_ - batch_base_ > kMaxOffset) break;
      switch (input_[index_]) {
        case '"':
        case '{':
        case '}':
        case '[':
        case ']':
        case ':':
        case ',':
          candidates_[count_++] = static_cast<uint16_t>(index_ - batch_base_);
          break;
        default:
          break;
      }
      ++index_;
    }
    if (index_ >= input_.size()) eof_ = true;
  }
};

}  // namespace ser20
