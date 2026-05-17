#pragma once

#include <array>
#include <cstddef>
#include <span>
#include <string_view>
#include <vector>

#include "ser20/parse_context.hh"

namespace ser20 {

// Forward-declare ser20::parse<T> to avoid circular includes
// (parser.hh includes pipeline.hh which includes json_view.hh).
// The definition is provided by parser.hh, which the user must include.
template <class T>
auto parse(std::string_view text) -> T;

// ── Internal: token collection ────────────────────────────────────────────────
//
// Walk the scanner from first_pos (which must be '{' or '[') and collect
// the positions of every structural candidate, re-indexed relative to
// first_pos (i.e. relative to the slice start).
//
// String interiors are skipped: when '"' is seen, json_skip_string() is called
// to advance the scanner past the closing '"', so candidates inside the string
// value are never added to the output list.
//
// `push(rel)` is called with each relative position; returns false to signal
// overflow (used by TokenView<N>).  `close` is '}' for objects, ']' for
// arrays.  Returns the absolute end position (one past the closing bracket).

template <class PushFn>
SER20_ALWAYS_INLINE auto collect_structural(ParserScanner& sc,
                                             std::string_view input,
                                             size_t first_pos,
                                             PushFn&& push) -> size_t {
  push(size_t{0});  // opening bracket at relative offset 0
  int depth = 1;
  while (true) {
    const size_t pos = sc.next();
    if (pos >= input.size()) return input.size();
    const char c = input[pos];
    const size_t rel = pos - first_pos;
    if (c == '"') {
      push(rel);                                   // opening quote
      const size_t cq = json_skip_string(sc);      // skip string interior
      if (cq < input.size()) push(cq - first_pos); // closing quote
    } else if (c == '{' || c == '[') {
      ++depth;
      push(rel);
    } else if (c == '}' || c == ']') {
      push(rel);
      if (--depth == 0) return pos + 1;
    } else {  // ':' or ','
      push(rel);
    }
  }
}

// ── TokenVector ───────────────────────────────────────────────────────────────
// Heap-allocated collection of structural positions for an object/array slice.

class TokenVector {
 public:
  TokenVector() = default;
  TokenVector(std::string_view slice, std::vector<size_t> positions)
      : slice_(slice), positions_(std::move(positions)) {}

  [[nodiscard]] auto slice() const noexcept -> std::string_view { return slice_; }
  [[nodiscard]] auto positions() const noexcept -> std::span<const size_t> {
    return positions_;
  }

  // Re-parse the captured slice as T.
  template <class T>
  [[nodiscard]] auto as() const -> T {
    return ser20::parse<T>(slice_);
  }

 private:
  std::string_view slice_;
  std::vector<size_t> positions_;
};

// ── TokenView<N> ──────────────────────────────────────────────────────────────
// Stack-allocated, fixed-capacity collection of structural positions.
// If more than N candidates are encountered, overflow() becomes true and
// the pipeline propagates an error.

template <std::size_t N>
class TokenView {
 public:
  TokenView() = default;
  TokenView(std::string_view slice) : slice_(slice) {}

  auto push(size_t rel) noexcept -> bool {
    if (count_ >= N) { overflow_ = true; return false; }
    positions_[count_++] = rel;
    return true;
  }

  void set_slice(std::string_view s) noexcept { slice_ = s; }

  [[nodiscard]] auto slice() const noexcept -> std::string_view { return slice_; }
  [[nodiscard]] auto size() const noexcept -> std::size_t { return count_; }
  [[nodiscard]] auto overflow() const noexcept -> bool { return overflow_; }
  [[nodiscard]] auto positions() const noexcept -> std::span<const size_t> {
    return {positions_.data(), count_};
  }

  // Re-parse the captured slice as T.
  template <class T>
  [[nodiscard]] auto as() const -> T {
    return ser20::parse<T>(slice_);
  }

 private:
  std::string_view slice_;
  std::array<size_t, N> positions_{};
  std::size_t count_{0};
  bool overflow_{false};
};

}  // namespace ser20
