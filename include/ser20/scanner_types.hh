#pragma once

#include <cstddef>
#include <cstdint>

namespace ser20 {

enum class TokenKind : uint8_t {
  LBrace,      // {
  RBrace,      // }
  LBracket,    // [
  RBracket,    // ]
  Colon,       // :
  Comma,       // ,
  Quote,       // unescaped "  (parser alternates open/close)
  ValueStart,  // first non-ws after :, [, ,  that is not " { [
  End,         // sentinel — never packed, only returned by consume_next_token
};

struct ScannerToken {
  size_t pos;
  TokenKind kind;
  [[nodiscard]] auto is_end() const noexcept -> bool {
    return kind == TokenKind::End;
  }
};

}  // namespace ser20
