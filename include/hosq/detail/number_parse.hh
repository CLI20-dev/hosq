#pragma once

#include <charconv>
#include <type_traits>
#ifndef HOSQ_USE_FAST_FLOAT
#define HOSQ_USE_FAST_FLOAT 1
#endif
#if HOSQ_USE_FAST_FLOAT
#include "hosq/third_party/fast_float/fast_float.h"
#endif

namespace hosq::detail {
// Select the backend at compile time. Keep the platform's existing error
// behavior (including output writes) through the cold standard fallback.
// Json selects the floating-point grammar for a JSON reader's prefix parse.
// Generic pipelines keep from_chars semantics for other formats and strings.
template <class T, bool Json = false>
inline auto number_from_chars(const char* first, const char* last, T& value)
    -> std::from_chars_result {
#if HOSQ_USE_FAST_FLOAT
  if constexpr (std::is_same_v<T, float> || std::is_same_v<T, double>) {
    T parsed{};
    const auto result = fast_float::from_chars(
        first, last, parsed, Json ? fast_float::chars_format::json
                                 : fast_float::chars_format::general);
    if (result.ec != std::errc{}) [[unlikely]]
      return std::from_chars(first, last, value);
    value = parsed;
    return {result.ptr, result.ec};
  } else if constexpr (std::is_integral_v<T> && !std::is_same_v<T, bool> &&
                       sizeof(T) <= sizeof(unsigned long long)) {
    const auto result = fast_float::from_chars(first, last, value);
    return {result.ptr, result.ec};
  } else
#endif
  {
    return std::from_chars(first, last, value);
  }
}
}  // namespace hosq::detail
