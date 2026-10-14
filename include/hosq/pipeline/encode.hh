#pragma once

#include <array>
#include <cassert>
#include <charconv>
#include <concepts>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include "hosq/detail/native_encode.hh"
#include "hosq/pipeline.hh"

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
  auto operator()(T value) const -> std::string {
    // std::to_chars for integral and floating-point types writes into a
    // caller-provided contiguous character buffer.
    std::array<char, 128> buffer{};

    const auto result =
        std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);

    assert(result.ec == std::errc{});

    return std::string{buffer.data(), result.ptr};
  }

  template <class U>
    requires(OptionalT<U> &&
             std::same_as<typename std::remove_cvref_t<U>::value_type, T>)
  auto operator()(U&& value) const -> std::string {
    assert(value.has_value());
    return (*this)(*std::forward<U>(value));
  }
};

struct BooleanImpl {
  auto operator()(bool value) const -> std::string_view {
    return value ? std::string_view{"true"} : std::string_view{"false"};
  }

  auto operator()(const std::optional<bool>& value) const -> std::string_view {
    assert(value.has_value());
    return (*this)(*value);
  }
};

struct StringViewImpl {
  auto operator()(std::string_view sv) const -> std::string_view { return sv; }

  auto operator()(const std::optional<std::string_view>& value) const
      -> std::string_view {
    assert(value.has_value());
    return *value;
  }
};

struct IdentityImpl {
  template <class T>
  auto operator()(T value) const -> T {
    return value;
  }
};

// Terminal semantic encoding: storage and traversal stay format-independent.
struct ValueImpl {
  template <class T, class Context>
  auto operator()(const T& input, Context& ctx) const -> void {
    using W = std::remove_cvref_t<decltype(ctx.writer())>;
    if (!ext::EncodeValue<W>{}(input, ctx.writer()) &&
        ctx.writer().error().is_ok())
      (void)hosq::detail::encode_error(
          ctx.writer(), ErrorCode::write_failed,
          "EncodeValue returned false without reporting an error");
  }
};

}  // namespace detail

/// Writes a semantic value through the concrete writer. Supports scalar,
/// optional/variant, range, Map and schema storage; empty optionals are errors.
inline constexpr auto value = step<detail::ValueImpl>;

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
