#pragma once

#include <optional>
#include <type_traits>
#include <utility>

namespace hosq {
namespace match::detail {
template <auto>
struct LiteralMatchImpl;
template <auto>
struct PredicateImpl;
}  // namespace match::detail
namespace decode::detail {
template <class, bool>
struct CollectCompletion;
template <auto...>
struct EachImpl;
}  // namespace decode::detail

/// Candidate control result produced only by library matching operations.
/// A mismatch carries no payload; successful payloads remain owned and movable.
template <class T>
struct MatchResult {
  using value_type = std::remove_cvref_t<T>;

  [[nodiscard]] constexpr auto matched() const -> bool {
    return value_.has_value();
  }
  [[nodiscard]] constexpr auto value() & -> value_type& { return *value_; }
  [[nodiscard]] constexpr auto value() const& -> const value_type& {
    return *value_;
  }
  [[nodiscard]] constexpr auto value() && -> value_type&& {
    return *std::move(value_);
  }

  explicit constexpr operator bool() const { return matched(); }
  constexpr auto operator*() & -> value_type& { return value(); }
  constexpr auto operator*() const& -> const value_type& { return value(); }
  constexpr auto operator*() && -> value_type&& {
    return std::move(*this).value();
  }

 private:
  constexpr MatchResult(bool matched, value_type value) {
    if (matched) value_.emplace(std::move(value));
  }
  explicit constexpr MatchResult(std::nullopt_t) : value_(std::nullopt) {}

  template <auto... Fns>
    requires(sizeof...(Fns) > 0)
  friend struct Pipeline;
  template <auto>
  friend struct match::detail::LiteralMatchImpl;
  template <auto>
  friend struct match::detail::PredicateImpl;
  template <auto...>
  friend struct decode::detail::EachImpl;

  template <class, bool>
  friend struct decode::detail::CollectCompletion;

  std::optional<value_type> value_;
};

template <>
struct MatchResult<void> {
  using value_type = void;

  [[nodiscard]] constexpr auto matched() const -> bool { return matched_; }
  explicit constexpr operator bool() const { return matched_; }

 private:
  explicit constexpr MatchResult(bool matched) : matched_(matched) {}
  explicit constexpr MatchResult(std::nullopt_t) : matched_(false) {}

  template <auto... Fns>
    requires(sizeof...(Fns) > 0)
  friend struct Pipeline;
  template <auto>
  friend struct match::detail::LiteralMatchImpl;
  template <auto>
  friend struct match::detail::PredicateImpl;
  template <auto...>
  friend struct decode::detail::EachImpl;

  bool matched_;
};

}  // namespace hosq
