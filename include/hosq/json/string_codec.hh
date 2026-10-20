#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "hosq/detail/utf8.hh"

namespace hosq::json::string_codec {
inline void append_text_utf8(std::string& out, std::uint32_t code) {
  if (code < 0x80)
    out += static_cast<char>(code);
  else if (code < 0x800) {
    out += static_cast<char>(0xc0 | (code >> 6));
    out += static_cast<char>(0x80 | (code & 63));
  } else if (code < 0x10000) {
    out += static_cast<char>(0xe0 | (code >> 12));
    out += static_cast<char>(0x80 | ((code >> 6) & 63));
    out += static_cast<char>(0x80 | (code & 63));
  } else {
    out += static_cast<char>(0xf0 | (code >> 18));
    out += static_cast<char>(0x80 | ((code >> 12) & 63));
    out += static_cast<char>(0x80 | ((code >> 6) & 63));
    out += static_cast<char>(0x80 | (code & 63));
  }
}

/// Strictly resolves one JSON string body, including UTF-16 surrogate pairs.
inline auto unescape_json_string(std::string_view body, std::string& out)
    -> bool {
  out.clear();
  auto fail = [&]() {
    out.clear();
    return false;
  };
  auto hex4 = [&](std::size_t& pos, std::uint32_t& code) {
    if (body.size() - pos < 4) return false;
    code = 0;
    for (unsigned j = 0; j < 4; ++j) {
      const char c = body[pos++];
      const unsigned digit = c >= '0' && c <= '9'   ? c - '0'
                             : c >= 'a' && c <= 'f' ? c - 'a' + 10
                             : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                                    : 16;
      if (digit == 16) return false;
      code = (code << 4) | digit;
    }
    return true;
  };
  for (std::size_t i = 0; i < body.size();) {
    const unsigned char c = static_cast<unsigned char>(body[i++]);
    if (c < 0x20 || c == '"') return fail();
    if (c != '\\') {
      out += static_cast<char>(c);
      continue;
    }
    if (i == body.size()) return fail();
    const char escape = body[i++];
    if (escape != 'u') {
      const auto index = std::string_view("\"\\/bfnrt").find(escape);
      if (index == std::string_view::npos) return fail();
      out += std::string_view("\"\\/\b\f\n\r\t")[index];
      continue;
    }
    std::uint32_t code;
    if (!hex4(i, code)) return fail();
    if (code >= 0xd800 && code <= 0xdbff) {
      if (body.size() - i < 6 || body[i] != '\\' || body[i + 1] != 'u')
        return fail();
      i += 2;
      std::uint32_t low;
      if (!hex4(i, low) || low < 0xdc00 || low > 0xdfff) return fail();
      code = 0x10000 + ((code - 0xd800) << 10) + low - 0xdc00;
    } else if (code >= 0xdc00 && code <= 0xdfff)
      return fail();
    append_text_utf8(out, code);
  }
  if (hosq::detail::invalid_utf8_offset(out) != out.size()) return fail();
  return true;
}
}  // namespace hosq::json::string_codec
