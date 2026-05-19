#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "ser20/reader_base.hh"

#ifdef __ARM_NEON
#include "ser20/json/scanner_neon.hh"
#else
#include "ser20/json/scanner.hh"
#endif

namespace ser20 {

#ifdef __ARM_NEON
using Scanner = ScannerNeon;
#else
using Scanner = ScannerSimple;
#endif

class JsonReader {
 public:
  explicit JsonReader(std::string_view data) : scanner_(data), data_(data) {}

  [[nodiscard]] [[nodiscard]]
  auto next_token() -> Result<Token, Error> {
    if (has_pending_) {
      has_pending_ = false;
      current_ = pending_;
      return *current_;
    }

    current_ = read_one();
    return *current_;
  }

  [[nodiscard]]
  auto current_token() const -> Result<Token, Error> {
    return *current_;
  }

  [[nodiscard]]
  auto input() const -> std::string_view {
    return data_;
  }

 private:
  Scanner scanner_;

  std::span<const uint64_t> batch_{};
  size_t cursor_{0};

  // Valid structural token deferred after emitting a bare-scalar SCALAR.
  bool has_pending_{false};
  Token pending_{};
  std::optional<Result<Token, Error>> current_{};

  // Context stack: true = inside array, false = inside object.
  std::vector<bool> ctx_{};

  // Bare-scalar tracking.
  bool expecting_value_{false};
  size_t value_start_{0};

  // ── helpers ────────────────────────────────────────────────────────────────

  static auto is_ws(char c) noexcept -> bool {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
  }

  auto next_candidates() noexcept -> size_t {
    if (cursor_ >= batch_.size()) {
      batch_ = scanner_.get_next_candidates();
      cursor_ = 0;
      if (batch_.empty()) {
        return data_.size();
      }
    }
    return static_cast<size_t>(batch_[cursor_++]);
  }

  // Returns the position of the unescaped closing '"', or input().size() on
  // unterminated string (caller must treat that as unexpected_eof).
  auto string_close() -> size_t {
    while (true) {
      const size_t pos = next_candidates();
      if (pos >= data_.size()) return pos;
      if (data_[pos] != '"') continue;
      size_t bs = 0;
      while (bs < pos && data_[pos - 1 - bs] == '\\') ++bs;
      if ((bs & 1u) == 0u) return pos;
    }
  }

  [[nodiscard]] auto trim(size_t s, size_t e) const noexcept
      -> std::pair<size_t, size_t> {
    while (s < e && is_ws(data_[s])) ++s;
    while (e > s && is_ws(data_[e - 1])) --e;
    return {s, e};
  }

  // Process the delimiter that terminates a bare scalar.
  auto handle_delim_after_scalar(char c, size_t pos) -> void {
    if (c == '}') {
      if (!ctx_.empty()) ctx_.pop_back();
      has_pending_ = true;
      pending_ = Token{.offset = pos, .length = 1, .type = TokenType::ObjectEnd};
    } else if (c == ']') {
      if (!ctx_.empty()) ctx_.pop_back();
      has_pending_ = true;
      pending_ = Token{.offset = pos, .length = 1, .type = TokenType::ArrayEnd};
    } else {  // ','
      if (!ctx_.empty() && ctx_.back()) {
        expecting_value_ = true;
        value_start_ = pos + 1;
      }
    }
  }

  // ── state machine ──────────────────────────────────────────────────────────

  auto read_one() -> Result<Token, Error> {
    while (true) {
      const size_t pos = next_candidates();
      if (pos >= data_.size())
        return Token{
            .offset = data_.size(), .length = 0, .type = TokenType::END};

      const char c = data_[pos];

      if (expecting_value_) {
        expecting_value_ = false;

        if (c == '"') {
          const size_t cl = string_close();
          if (cl >= data_.size())
            return unexpected(Error{.code = ErrorCode::unexpected_eof});
          return Token{.offset = pos + 1,
                       .length = cl > pos + 1 ? cl - pos - 1 : 0,
                       .type = TokenType::SCALAR};
        }
        if (c == '{') {
          ctx_.push_back(false);
          return Token{
              .offset = pos, .length = 1, .type = TokenType::ObjectBegin};
        }
        if (c == '[') {
          ctx_.push_back(true);
          expecting_value_ = true;
          value_start_ = pos + 1;
          return Token{
              .offset = pos, .length = 1, .type = TokenType::ArrayBegin};
        }

        // Bare scalar terminated by a delimiter.
        auto [s, e] = trim(value_start_, pos);
        if (s >= e) {
          // Empty gap: handle delimiter directly (e.g. empty array []).
          if (c == '}') {
            if (!ctx_.empty()) ctx_.pop_back();
            return Token{
                .offset = pos, .length = 1, .type = TokenType::ObjectEnd};
          }
          if (c == ']') {
            if (!ctx_.empty()) ctx_.pop_back();
            return Token{
                .offset = pos, .length = 1, .type = TokenType::ArrayEnd};
          }
          if (!ctx_.empty() && ctx_.back()) {
            expecting_value_ = true;
            value_start_ = pos + 1;
          }
          continue;
        }

        handle_delim_after_scalar(c, pos);
        return Token{.offset = s, .length = e - s, .type = TokenType::SCALAR};
      }

      switch (c) {
        case '{':
          ctx_.push_back(false);
          return Token{
              .offset = pos, .length = 1, .type = TokenType::ObjectBegin};

        case '}':
          if (!ctx_.empty()) ctx_.pop_back();
          return Token{.offset = pos, .length = 1, .type = TokenType::ObjectEnd};

        case '[':
          ctx_.push_back(true);
          expecting_value_ = true;
          value_start_ = pos + 1;
          return Token{
              .offset = pos, .length = 1, .type = TokenType::ArrayBegin};

        case ']':
          if (!ctx_.empty()) ctx_.pop_back();
          return Token{.offset = pos, .length = 1, .type = TokenType::ArrayEnd};

        case '"': {
          const size_t cl = string_close();
          if (cl >= data_.size())
            return unexpected(Error{.code = ErrorCode::unexpected_eof});
          return Token{.offset = pos + 1,
                       .length = cl > pos + 1 ? cl - pos - 1 : 0,
                       .type = TokenType::SCALAR};
        }

        case ':':
          expecting_value_ = true;
          value_start_ = pos + 1;
          continue;

        case ',':
          if (!ctx_.empty() && ctx_.back()) {
            expecting_value_ = true;
            value_start_ = pos + 1;
          }
          continue;

        default:
          continue;
      }
    }
  }

 private:
  std::string_view data_;
};

static_assert(ReaderContext<JsonReader>,
              "JsonReader must satisfy Reader concept");

template <>
struct ReaderTraits<JsonReader> {
  static constexpr Format format = Format::json;
  static constexpr bool is_binary_format = false;
};

}  // namespace ser20
