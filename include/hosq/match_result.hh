#pragma once

#include <utility>

namespace hosq {

template <class T>
struct MatchResult {
  MatchResult(bool matched, T value)
      : matched_(matched), value_(std::move(value)) {}
  [[nodiscard]] auto matched() const -> bool { return matched_; }
  [[nodiscard]] auto value() const -> const T& { return value_; }
  [[nodiscard]] auto value() -> T& { return value_; }

  /* implicit */ operator bool() const { return matched_; }
  auto operator*() -> T& { return value_; }
  auto operator*() const -> const T& { return value_; }

 private:
  bool matched_;
  T value_;
};

namespace detail {

template <class T>
struct IsMatchResult : std::false_type {};

template <class T>
struct IsMatchResult<MatchResult<T>> : std::true_type {
  using value_type = T;
};

template <class T>
concept MatchResultT = IsMatchResult<std::remove_cvref_t<T>>::value;

}  // namespace detail

}  // namespace hosq
