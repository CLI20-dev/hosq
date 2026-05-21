#pragma once

#include <charconv>
#include <concepts>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>

#include "hosq/error.hh"
#include "hosq/pipeline.hh"
#include "hosq/result.hh"

namespace hosq::encode {

namespace detail {

template <class T>
struct IsOptional : std::false_type {};

template <class T>
struct IsOptional<std::optional<T>> : std::true_type {};

template <class T>
concept OptionalT = IsOptional<std::remove_cvref_t<T>>::value;

template <class T>
concept Number = (std::integral<std::remove_cvref_t<T>> &&
                  !std::same_as<std::remove_cvref_t<T>, bool>) ||
                 std::floating_point<std::remove_cvref_t<T>>;

template <Number T>
struct NumberImpl {
  auto operator()(T value) const -> Result<std::string, Error> {
    char buffer[128]{};
    auto* first = buffer;
    auto* last = buffer + sizeof(buffer);
    const auto result = std::to_chars(first, last, value);
    if (result.ec == std::errc::value_too_large) {
      return unexpected(Error{.code = ErrorCode::output_too_large});
    }
    if (result.ec != std::errc{}) {
      return unexpected(Error{.code = ErrorCode::conversion_error});
    }
    return std::string{first, result.ptr};
  }

  template <class U>
    requires(OptionalT<U> &&
             std::same_as<typename std::remove_cvref_t<U>::value_type, T>)
  auto operator()(U&& value) const -> Result<std::string, Error> {
    if (!value.has_value()) [[unlikely]] {
      return unexpected(Error{.code = ErrorCode::invalid_state});
    }

    return (*this)(*std::forward<U>(value));
  }
};

struct BooleanImpl {
  auto operator()(bool value) const -> std::string_view {
    return value ? std::string_view{"true"} : std::string_view{"false"};
  }

  auto operator()(const std::optional<bool>& value) const
      -> Result<std::string_view, Error> {
    if (!value.has_value()) [[unlikely]] {
      return unexpected(Error{.code = ErrorCode::invalid_state});
    }

    return (*this)(*value);
  }
};

struct StringViewImpl {
  auto operator()(std::string_view sv) const -> std::string_view { return sv; }

  auto operator()(const std::optional<std::string_view>& value) const
      -> Result<std::string_view, Error> {
    if (!value.has_value()) [[unlikely]] {
      return unexpected(Error{.code = ErrorCode::invalid_state});
    }

    return *value;
  }
};

struct IdentityImpl {
  template <class T>
  auto operator()(T value) const -> T {
    return value;
  }
};

}  // namespace detail

/// Encodes arithmetic values as JSON number text.
template <detail::Number T>
inline constexpr auto number = step<detail::NumberImpl<T>>;

/// Encodes bool as "true" or "false".
inline constexpr auto boolean = step<detail::BooleanImpl>;

/// Passes an already encoded string representation through unchanged.
inline constexpr auto string_view = step<detail::StringViewImpl>;

/// Passes any value through unchanged.
inline constexpr auto identity = step<detail::IdentityImpl>;

}  // namespace hosq::encode
