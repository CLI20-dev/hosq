#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace hosq {

class ScannerSimple {
 public:
  explicit ScannerSimple(std::string_view input) noexcept
      : eof_(input.empty()), input_(input) {}

  // Returns a span of absolute positions of structural candidates found in
  // the next batch.  The span is valid until the next call to this function.
  // Returns an empty span when all candidates are exhausted (eof()).
  [[nodiscard]] auto get_next_candidates() noexcept
      -> std::span<const uint64_t> {
    refill_batch();
    return {candidates_.data(), count_};
  }

  [[nodiscard]] auto eof() const noexcept -> bool { return eof_ && count_ == 0; }

  [[nodiscard]] auto input() const noexcept -> std::string_view {
    return input_;
  }

 private:
  static constexpr size_t kBatchSize = 64;

  std::array<uint64_t, kBatchSize> candidates_{};
  size_t count_{0};
  size_t index_{0};

  bool eof_{false};
  std::string_view input_;

  void refill_batch() noexcept {
    count_ = 0;

    while (count_ < kBatchSize && index_ < input_.size()) {
      switch (input_[index_]) {
        case '"':
        case '{':
        case '}':
        case '[':
        case ']':
        case ':':
        case ',':
          candidates_[count_++] = index_;  // absolute position
          break;
        default:
          break;
      }
      ++index_;
    }
    if (index_ >= input_.size()) eof_ = true;
  }
};

}  // namespace hosq
