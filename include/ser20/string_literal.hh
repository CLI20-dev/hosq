#pragma once

#include <algorithm>
#include <array>
#include <string_view>

namespace ser20 {

/**
 * @brief Compile-time string type usable as a non-type template parameter.
 *
 * C++20 allows class-type NTTPs provided the type satisfies structural
 * requirements. `StringLiteral<N>` wraps a null-terminated character array so
 * that string names such as `"verbose"` can appear directly in template
 * argument lists:
 *
 * @tparam N Total storage size including the null terminator.
 */
template <std::size_t N>
struct StringLiteral {
  /** Raw character storage (null-terminated, length N). */
  std::array<char, N> value{};

  /**
   * @brief Constructs from a C string literal, copying all N characters.
   *
   * @param str Null-terminated string literal of length N.
   */
  // NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays, modernize-avoid-c-arrays)
  consteval StringLiteral(const char (&str)[N]) noexcept {
    std::copy_n(str, N, value.begin());
  }

  /**
   * @brief Returns the string length excluding the null terminator.
   *
   * @return N - 1.
   */
  [[nodiscard]]
  consteval auto size() const noexcept -> std::size_t {
    return N - 1;
  }

  /**
   * @brief Returns a raw pointer to the first character.
   *
   * @return Pointer to the underlying character array.
   */
  [[nodiscard]]
  consteval auto data() const noexcept -> const char* {
    return value.data();
  }

  /**
   * @brief Returns a `std::string_view` over the stored characters.
   *
   * The returned view does not include the null terminator.
   *
   * @return A string_view of length `size()`.
   */
  [[nodiscard]]
  consteval auto view() const noexcept -> std::string_view {
    return {value.data(), size()};
  }

  /**
   * @brief Returns the character at index `i`.
   *
   * @param i Zero-based character index.
   * @return The character at position `i`.
   */
  [[nodiscard]]
  constexpr auto operator[](std::size_t i) const noexcept -> char {
    return value[i];
  }

  /**
   * @brief Equality comparison (compares the underlying arrays).
   */
  consteval auto operator==(const StringLiteral&) const noexcept
      -> bool = default;
};

/**
 * @brief Deduction guide: `StringLiteral("hello")` deduces `StringLiteral<6>`.
 */
template <std::size_t N>
// NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays, modernize-avoid-c-arrays)
StringLiteral(const char (&)[N]) -> StringLiteral<N>;
template <StringLiteral Str>

[[nodiscard]]
consteval auto operator""_literal() noexcept {
  return Str;
}

}  // namespace ser20
