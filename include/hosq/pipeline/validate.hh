#pragma once

#include <cstddef>
#include <type_traits>

#include "hosq/error.hh"
#include "hosq/pipeline.hh"
#include "hosq/result.hh"

namespace hosq::validate {

namespace detail {

template <auto V>
struct MatchImpl {
  template <class T>
  auto operator()(T value) const -> Result<T, Error> {
    if (value != V) return unexpected(Error{.code = ErrorCode::invalid_choice});
    return value;
  }
};

template <auto... Vs>
struct OneOfImpl {
  template <class T>
  auto operator()(T value) const -> Result<T, Error> {
    if (!((value == Vs) || ...))
      return unexpected(Error{.code = ErrorCode::invalid_choice});
    return value;
  }
};

template <auto Min, auto Max>
struct RangeImpl {
  template <class T>
  auto operator()(T value) const -> Result<T, Error> {
    if (value < Min || value > Max)
      return unexpected(Error{.code = ErrorCode::out_of_range});
    return value;
  }
};

template <auto Min>
struct MinImpl {
  template <class T>
  auto operator()(T value) const -> Result<T, Error> {
    if (value < Min) return unexpected(Error{.code = ErrorCode::out_of_range});
    return value;
  }
};

template <auto Max>
struct MaxImpl {
  template <class T>
  auto operator()(T value) const -> Result<T, Error> {
    if (value > Max) return unexpected(Error{.code = ErrorCode::out_of_range});
    return value;
  }
};

struct NonEmptyImpl {
  template <class T>
  auto operator()(T value) const -> Result<T, Error> {
    if (value.empty())
      return unexpected(Error{.code = ErrorCode::validation_failed});
    return value;
  }
};

template <std::size_t N>
struct SizeImpl {
  template <class T>
  auto operator()(T value) const -> Result<T, Error> {
    if (value.size() != N)
      return unexpected(Error{.code = ErrorCode::size_mismatch});
    return value;
  }
};

template <std::size_t Min, std::size_t Max>
struct SizeRangeImpl {
  template <class T>
  auto operator()(T value) const -> Result<T, Error> {
    if (value.size() < Min || value.size() > Max)
      return unexpected(Error{.code = ErrorCode::validation_failed});
    return value;
  }
};

template <auto F>
struct PredicateImpl {
  template <class T>
  auto operator()(T value) const -> Result<T, Error> {
    if (!F(value))
      return unexpected(Error{.code = ErrorCode::validation_failed});
    return value;
  }
};

template <auto F>
struct CustomImpl {
  template <class T>
  auto operator()(T value) const -> Result<std::remove_cvref_t<T>, Error> {
    using R = std::invoke_result_t<decltype(F), T>;
    if constexpr (std::same_as<R, bool>) {
      if (!F(value))
        return unexpected(Error{.code = ErrorCode::validation_failed});
      return value;
    } else if constexpr (std::same_as<R, Result<void, Error>>) {
      auto r = F(value);
      if (!r) return unexpected(r.error());
      return value;
    } else {
      return F(std::move(value));
    }
  }
};

struct IdentityImpl {
  template <class T>
  auto operator()(T value) const -> T {
    return value;
  }
};

}  // namespace detail

/// Checks that the decoded value equals V; returns invalid_choice on mismatch.
template <auto V>
inline constexpr auto match = step<detail::MatchImpl<V>>;

/// Checks that the decoded value equals one of Vs...; returns invalid_choice if
/// none match.
template <auto... Vs>
inline constexpr auto one_of = step<detail::OneOfImpl<Vs...>>;

/// Checks that the decoded value is within [Min, Max] (closed); returns
/// out_of_range otherwise.
template <auto Min, auto Max>
inline constexpr auto range = step<detail::RangeImpl<Min, Max>>;

/// Checks that the decoded value is >= Min; returns out_of_range otherwise.
template <auto Min>
inline constexpr auto min = step<detail::MinImpl<Min>>;

/// Checks that the decoded value is <= Max; returns out_of_range otherwise.
template <auto Max>
inline constexpr auto max = step<detail::MaxImpl<Max>>;

/// Checks that the decoded container/string is not empty; returns
/// validation_failed otherwise.
inline constexpr auto non_empty = step<detail::NonEmptyImpl>;

/// Checks that the decoded container/string has exactly size N; returns
/// size_mismatch otherwise.
template <std::size_t N>
inline constexpr auto size = step<detail::SizeImpl<N>>;

/// Checks that the decoded container/string has size in [Min, Max]; returns
/// validation_failed otherwise.
template <std::size_t Min, std::size_t Max>
inline constexpr auto size_range = step<detail::SizeRangeImpl<Min, Max>>;

/// Applies predicate F(value) -> bool; returns validation_failed if F returns
/// false.
template <auto F>
inline constexpr auto predicate = step<detail::PredicateImpl<F>>;

/// Applies F(value) which may return bool, Result<void, Error>, or Result<T,
/// Error>.
template <auto F>
inline constexpr auto custom = step<detail::CustomImpl<F>>;

/// Passes any value through unchanged.
inline constexpr auto identity = step<detail::IdentityImpl>;

}  // namespace hosq::validate
