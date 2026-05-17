#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "ser20/error.hh"
#include "ser20/result.hh"

namespace ser20 {

enum class TokenType : uint8_t {
  ObjectBegin,
  ObjectEnd,
  ArrayBegin,
  ArrayEnd,
  SCALAR,
  END,
};

struct Token {
  size_t offset;
  size_t length;
  TokenType type;
};

template <class T>
concept Reader = requires(T t, const T ct) {
  { t.next_token() } -> std::same_as<Result<Token, Error>>;
  { t.peek_token() } -> std::same_as<Result<Token, Error>>;
  { t.current_token() } -> std::same_as<Result<Token, Error>>;
  { ct.input() } -> std::same_as<std::string_view>;
};

enum class Format : uint8_t {
  unknown,
  json,
  // bson,
  // msgpack,
  // cbor,
};

template <class R>
struct ReaderTraits {
  static constexpr Format format = Format::unknown;
  static constexpr bool is_binary_format = false;
};

template <class R>
using reader_traits_t = ReaderTraits<std::remove_cvref_t<R>>;

};  // namespace ser20
