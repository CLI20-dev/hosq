#pragma once

#include <cstddef>
#include <string_view>

namespace hosq::detail {
// Returns the first invalid byte, or size() for valid UTF-8.
inline auto invalid_utf8_offset(std::string_view input) -> std::size_t {
  for (std::size_t i = 0; i < input.size();) {
    const auto start = i;
    const unsigned first = static_cast<unsigned char>(input[i++]);
    if (first < 0x80) continue;
    unsigned n = 0, code = 0, minimum = 0;
    if (first >= 0xc2 && first <= 0xdf) {
      n = 1;
      code = first & 31;
      minimum = 0x80;
    } else if (first >= 0xe0 && first <= 0xef) {
      n = 2;
      code = first & 15;
      minimum = 0x800;
    } else if (first >= 0xf0 && first <= 0xf4) {
      n = 3;
      code = first & 7;
      minimum = 0x10000;
    } else
      return start;
    if (n > input.size() - i) return start;
    for (unsigned j = 0; j < n; ++j) {
      const unsigned b = static_cast<unsigned char>(input[i++]);
      if ((b & 0xc0) != 0x80) return i - 1;
      code = (code << 6) | (b & 63);
    }
    if (code < minimum || code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff))
      return start;
  }
  return input.size();
}
}  // namespace hosq::detail
