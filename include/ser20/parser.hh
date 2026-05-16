#pragma once

#include <iostream>
#include <string>
#include <string_view>

#ifdef __ARM_NEON
#include "ser20/scanner_neon.hh"
#else
#include "ser20/scanner.hh"
#endif

namespace ser20 {

template <typename T = void>
class Parser {
#ifdef __ARM_NEON
  using ScannerImpl = ScannerNeon;
#else
  using ScannerImpl = Scanner;
#endif

 public:
  void parse(std::string_view input) {  // NOLINT(readability-function-cognitive-complexity)
    ScannerImpl scanner(input);
    depth_ = 0;

    while (true) {
      const auto tok = scanner.consume_next_token();
      if (tok.is_end()) break;

      switch (tok.kind) {
        case TokenKind::LBrace:
          print("OBJECT_START");
          ++depth_;
          break;
        case TokenKind::RBrace:
          --depth_;
          print("OBJECT_END");
          break;
        case TokenKind::LBracket:
          print("ARRAY_START");
          ++depth_;
          break;
        case TokenKind::RBracket:
          --depth_;
          print("ARRAY_END");
          break;
        case TokenKind::Quote: {
          const std::string_view str = read_string(scanner, input, tok.pos);
          const auto next = scanner.consume_next_token();
          if (!next.is_end() && next.kind == TokenKind::Colon) {
            print("KEY=", str);
            emit_value(scanner, input, next.pos);
          } else {
            print("VALUE=\"", str, "\"");
            replay(next);
          }
          break;
        }
        default:
          break;
      }
    }
  }

 private:
  int depth_{0};

  void print(std::string_view label) {
    for (int i = 0; i < depth_; ++i) std::cout << "  ";
    std::cout << label << '\n';
  }

  void print(std::string_view prefix, std::string_view value,
             std::string_view suffix = {}) {
    for (int i = 0; i < depth_; ++i) std::cout << "  ";
    std::cout << prefix << value << suffix << '\n';
  }

  static auto read_string(ScannerImpl& scanner, std::string_view input,
                          size_t open_pos) -> std::string_view {
    auto close = scanner.consume_next_token();
    while (!close.is_end() && close.kind != TokenKind::Quote) {
      close = scanner.consume_next_token();
    }
    if (close.is_end()) return input.substr(open_pos + 1);
    return input.substr(open_pos + 1, close.pos - open_pos - 1);
  }

  void emit_value(ScannerImpl& scanner, std::string_view input,
                  size_t colon_pos) {
    const auto tok = scanner.consume_next_token();
    if (tok.is_end()) return;

    switch (tok.kind) {
      case TokenKind::LBrace:
        print("OBJECT_START");
        ++depth_;
        break;
      case TokenKind::LBracket:
        print("ARRAY_START");
        ++depth_;
        break;
      case TokenKind::Quote: {
        const std::string_view str = read_string(scanner, input, tok.pos);
        print("VALUE=\"", str, "\"");
        break;
      }
      default: {
        const size_t val_start = colon_pos + 1;
        std::string_view raw = input.substr(val_start, tok.pos - val_start);
        while (!raw.empty() && (raw.front() == ' ' || raw.front() == '\t' ||
                                raw.front() == '\n' || raw.front() == '\r')) {
          raw.remove_prefix(1);
        }
        while (!raw.empty() && (raw.back() == ' ' || raw.back() == '\t' ||
                                raw.back() == '\n' || raw.back() == '\r')) {
          raw.remove_suffix(1);
        }
        print("VALUE=", raw);
        replay(tok);
        break;
      }
    }
  }

  void replay(ScannerToken tok) {
    if (tok.is_end()) return;
    switch (tok.kind) {
      case TokenKind::RBrace:
        --depth_;
        print("OBJECT_END");
        break;
      case TokenKind::RBracket:
        --depth_;
        print("ARRAY_END");
        break;
      default:
        break;
    }
  }
};

}  // namespace ser20
