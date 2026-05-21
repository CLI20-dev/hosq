#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>

#include "hosq/error.hh"
#include "hosq/result.hh"

namespace hosq {

enum class TokenType : uint8_t {
  ObjectBegin,
  ObjectEnd,
  ArrayBegin,
  ArrayEnd,
  Scalar,
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
  size_t length;
  TokenType type;
  uint8_t context;
};

template <class T>
concept ReaderConcept = requires(T r, const T cr, typename T::state_type state,
                                 const typename T::state_type const_state,
                                 typename T::input_type input) {
  typename T::input_type;
  typename T::state_type;
  { r.reset(state, input) } -> std::same_as<void>;
  { r.next_token(state) } -> std::same_as<Result<void, Error>>;
  { cr.current_token(const_state) } -> std::same_as<const Token&>;
  { cr.input(const_state) } -> std::same_as<const typename T::input_type&>;
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

};  // namespace hosq
