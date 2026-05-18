#pragma once

#include <charconv>
#include <concepts>
#include <string_view>
#include <system_error>
#include <type_traits>

#include "ser20/error.hh"
#include "ser20/pipeline.hh"
#include "ser20/pipeline_util.hh"
#include "ser20/result.hh"

namespace ser20::decode {

namespace detail {

template <class T>
concept Number = (std::integral<std::remove_cvref_t<T>> &&
                  !std::same_as<std::remove_cvref_t<T>, bool>) ||
                 std::floating_point<std::remove_cvref_t<T>>;

template <Number T>
struct NumberImpl {
  auto operator()(std::string_view input) const
      -> Result<std::remove_cvref_t<T>, Error> {
    using Value = std::remove_cvref_t<T>;
    Value value{};
    const char* first = input.data();
    const char* last = input.data() + input.size();
    const auto result = std::from_chars(first, last, value);
    if (result.ec == std::errc::result_out_of_range) [[unlikely]] {
      return unexpected(Error{.code = ErrorCode::out_of_range});
    }
    if (result.ec != std::errc{} || result.ptr != last) [[unlikely]] {
      return unexpected(Error{.code = ErrorCode::invalid_number});
    }
    return value;
  }
};

struct IdentityImpl {
  template <class T>
  auto operator()(T value) const -> T {
    return value;
  }
};

template <auto F>
struct CustomImpl {
  template <class T>
  auto operator()(T value) const -> std::invoke_result_t<decltype(F), T> {
    return F(std::move(value));
  }
};

}  // namespace detail

/// Parses a numeric string_view into arithmetic type T using std::from_chars.
template <detail::Number T>
inline constexpr auto number = step<detail::NumberImpl<T>>;

/// Converts "true"/"false" to bool; returns type_mismatch for anything else.
inline constexpr auto boolean =
    action<std::string_view, bool,
           [](std::string_view sv) -> Result<bool, Error> {
             if (sv == "true") return true;
             if (sv == "false") return false;
             return unexpected(Error{.code = ErrorCode::type_mismatch});
           }>;

/// Passes string_view through unchanged; escape resolution is left to the caller.
inline constexpr auto string_view =
    action<std::string_view, std::string_view,
           [](std::string_view sv) -> std::string_view { return sv; }>;

/// Passes any value through unchanged.
inline constexpr auto identity = step<detail::IdentityImpl>;

/// Applies user-defined conversion F to the input; F may return a plain value or Result<U, Error>.
template <auto F>
inline constexpr auto custom = step<detail::CustomImpl<F>>;

}  // namespace ser20::decode
