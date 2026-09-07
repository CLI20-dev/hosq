#pragma once

#include <charconv>
#include <type_traits>
#if defined(HOSQ_USE_FAST_FLOAT) && HOSQ_USE_FAST_FLOAT
#include <fast_float/fast_float.h>
#endif

namespace hosq::detail {
// Select the backend at compile time. Keep the platform's existing error
// behavior (including output writes) through the cold standard fallback.
template <class T>
inline auto number_from_chars(const char* first, const char* last, T& value)
    -> std::from_chars_result {
#if defined(HOSQ_USE_FAST_FLOAT) && HOSQ_USE_FAST_FLOAT
  if constexpr (std::is_same_v<T, float> || std::is_same_v<T, double>) {
    T parsed{};
    const auto result = fast_float::from_chars(first, last, parsed);
    if (result.ec != std::errc{}) [[unlikely]]
      return std::from_chars(first, last, value);
    value = parsed;
    return {result.ptr, result.ec};
  } else
#endif
  {
    return std::from_chars(first, last, value);
  }
}
}  // namespace hosq::detail
