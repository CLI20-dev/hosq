#pragma once

#include <utility>

namespace hosq {

template <class T>
struct MatchResult {
  using value_type = std::remove_cvref_t<T>;

  MatchResult(bool matched, T value)
      : matched_(matched), value_(std::move(value)) {}

  [[nodiscard]] auto matched() const -> bool { return matched_; }
  [[nodiscard]] auto value() & -> value_type& { return value_; }
  [[nodiscard]] auto value() const& -> const value_type& { return value_; }
  [[nodiscard]] auto value() && -> value_type&& { return std::move(value_); }

  /* implicit */ operator bool() const { return matched_; }
  auto operator*() & -> value_type& { return value_; }
  auto operator*() const& -> const value_type& { return value_; }
  auto operator*() && -> value_type&& { return std::move(value_); }

 private:
  bool matched_;
  value_type value_;
};

template <class T>
MatchResult(bool, T) -> MatchResult<std::remove_cvref_t<T>>;

}  // namespace hosq
