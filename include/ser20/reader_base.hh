#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>

#include "ser20/error.hh"
#include "ser20/result.hh"

namespace ser20 {

enum class TokenType : uint8_t {
  ObjectBegin,
  ObjectEnd,
  ArrayBegin,
  ArrayEnd,
  ScalarBegin,
  End,
};

enum class JsonTokenContext : std::uint8_t {
  none = 0,
  string,   // "foo"
  literal,  // number / true / false / null
};

inline constexpr std::size_t unresolved_length =
    std::numeric_limits<std::size_t>::max();

struct Token {
  size_t offset;
  mutable size_t length;
  TokenType type;
  uint8_t context;
};

template <class T>
concept ReaderConcept = requires(T t, const T ct) {
  typename T::input_type;
  { t.next_token() } -> std::same_as<Result<void, Error>>;
  { t.current_token() } -> std::same_as<Token&>;
  { ct.current_token() } -> std::same_as<const Token&>;
  { t.advance(std::size_t{}) } -> std::same_as<Result<void, Error>>;
  { ct.advance(std::size_t{}) } -> std::same_as<Result<void, Error>>;
  { ct.input() } -> std::same_as<typename T::input_type>;
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
