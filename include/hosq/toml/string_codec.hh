#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "hosq/detail/utf8.hh"
#include "hosq/error.hh"
#include "hosq/reader_base.hh"

namespace hosq::toml::string_codec {
struct Cursor {
  std::string_view input;
  std::size_t pos = 0;
  Token token{};
  Error failure = Error::ok();
};
inline void append_utf8(std::string& text, std::uint32_t code) {
  if (code < 0x80)
    text += static_cast<char>(code);
  else if (code < 0x800) {
    text += static_cast<char>(0xc0 | (code >> 6));
    text += static_cast<char>(0x80 | (code & 63));
  } else if (code < 0x10000) {
    text += static_cast<char>(0xe0 | (code >> 12));
    text += static_cast<char>(0x80 | ((code >> 6) & 63));
    text += static_cast<char>(0x80 | (code & 63));
  } else {
    text += static_cast<char>(0xf0 | (code >> 18));
    text += static_cast<char>(0x80 | ((code >> 12) & 63));
    text += static_cast<char>(0x80 | ((code >> 6) & 63));
    text += static_cast<char>(0x80 | (code & 63));
  }
}

// TOML string syntax shared by tokenization and owned string decoding.
inline auto parse(Cursor& cursor, std::string* decoded = nullptr,
                  bool is_key = false) -> bool {
  auto at = [&](char c) {
    return cursor.pos < cursor.input.size() && cursor.input[cursor.pos] == c;
  };
  auto fail = [&](ErrorCode code, std::string_view why) {
    if (cursor.failure.is_ok()) cursor.failure = {code, cursor.pos, why};
    return false;
  };
  auto need = [&](std::size_t count) {
    return count <= cursor.input.size() - cursor.pos ||
           fail(ErrorCode::unexpected_eof, "truncated value");
  };
  if (!at('"') && !at('\''))
    return fail(ErrorCode::type_mismatch, "expected TOML string");
  const char quote = cursor.input[cursor.pos++];
  const bool multiline =
      cursor.input.substr(cursor.pos, 2) == std::string(2, quote);
  if (multiline && is_key)
    return fail(ErrorCode::invalid_token, "multiline TOML keys are forbidden");
  if (multiline) {
    cursor.pos += 2;
    if (at('\r')) {
      ++cursor.pos;
      if (!at('\n'))
        return fail(ErrorCode::invalid_token, "bare TOML carriage return");
      ++cursor.pos;
    } else if (at('\n'))
      ++cursor.pos;
  }
  const auto start = cursor.pos;
  while (cursor.pos < cursor.input.size()) {
    if (at(quote)) {
      const auto quotes = cursor.pos;
      while (at(quote)) ++cursor.pos;
      const auto n = cursor.pos - quotes;
      if (!multiline) {
        cursor.pos = quotes + 1;
        cursor.token = {start, quotes - start, TokenType::Scalar, 0};
        return true;
      }
      if (n >= 3) {
        if (n > 5)
          return fail(ErrorCode::invalid_token, "too many closing TOML quotes");
        if (decoded) decoded->append(n - 3, quote);
        cursor.token = {start, quotes - start + n - 3, TokenType::Scalar, 0};
        return true;
      }
      if (decoded) decoded->append(n, quote);
      continue;
    }
    const unsigned c = static_cast<unsigned char>(cursor.input[cursor.pos++]);
    if ((c < 0x20 && c != '\t' && !(multiline && (c == '\n' || c == '\r'))) ||
        c == 0x7f)
      return fail(ErrorCode::invalid_token, "control byte in TOML string");
    if (c == '\r') {
      if (!at('\n'))
        return fail(ErrorCode::invalid_token, "bare TOML carriage return");
      ++cursor.pos;
      if (decoded) *decoded += '\n';
      continue;
    }
    if (quote == '"' && c == '\\') {
      if (!need(1)) return false;
      const auto escape_start = cursor.pos;
      if (multiline) {
        while (at(' ') || at('\t')) ++cursor.pos;
        if (at('\r') || at('\n')) {
          if (at('\r')) {
            ++cursor.pos;
            if (!at('\n'))
              return fail(ErrorCode::invalid_escape,
                          "invalid TOML line continuation");
          }
          ++cursor.pos;
          // Trim whitespace, but comments are string content here.
          while (cursor.pos < cursor.input.size()) {
            if (at(' ') || at('\t') || at('\n'))
              ++cursor.pos;
            else if (at('\r')) {
              ++cursor.pos;
              if (!at('\n'))
                return fail(ErrorCode::invalid_escape,
                            "invalid TOML line continuation");
              ++cursor.pos;
            } else
              break;
          }
          continue;
        }
        cursor.pos = escape_start;
      }
      const char escape = cursor.input[cursor.pos++];
      if (escape == 'u' || escape == 'U') {
        const unsigned count = escape == 'u' ? 4 : 8;
        if (!need(count)) return false;
        std::uint32_t code = 0;
        for (unsigned i = 0; i < count; ++i) {
          const char h = cursor.input[cursor.pos++];
          unsigned v;
          if (h >= '0' && h <= '9')
            v = h - '0';
          else if (h >= 'a' && h <= 'f')
            v = h - 'a' + 10;
          else if (h >= 'A' && h <= 'F')
            v = h - 'A' + 10;
          else
            return fail(ErrorCode::invalid_escape,
                        "invalid TOML Unicode escape");
          code = (code << 4) | v;
        }
        if (code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff))
          return fail(ErrorCode::invalid_escape, "invalid Unicode scalar");
        if (decoded) append_utf8(*decoded, code);
      } else {
        const auto index = std::string_view("btnfr\"\\").find(escape);
        if (index == std::string_view::npos)
          return fail(ErrorCode::invalid_escape, "invalid TOML escape");
        if (decoded) *decoded += std::string_view("\b\t\n\f\r\"\\")[index];
      }
    } else if (decoded)
      *decoded += static_cast<char>(c);
  }
  return fail(ErrorCode::unexpected_eof, "unterminated TOML string");
}
}  // namespace hosq::toml::string_codec
