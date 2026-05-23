#pragma once

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>

#include "hosq/error.hh"

namespace hosq {

enum class TokenType : std::uint8_t {
  ObjectBegin,
  ObjectEnd,
  ArrayBegin,
  ArrayEnd,
  Scalar,
  End,
  Error,
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
  std::uint8_t context;
};

template <class T>
concept ReaderStateConcept = requires(T state) {
  { state.rollback(state.checkpoint()) } -> std::same_as<void>;
};

template <class T>
concept ReaderConcept = requires(T r, const T cr, typename T::state_type state,
                                 const typename T::state_type const_state,
                                 Error error, typename T::input_type input) {
  typename T::input_type;
  typename T::state_type;
  requires ReaderStateConcept<typename T::state_type>;
  { r.reset(state, input) } -> std::same_as<void>;
  { r.next_token(state) } -> std::same_as<bool>;
  { cr.report_error(const_state, error) } -> std::same_as<void>;
  { cr.error(const_state) } -> std::same_as<Error>;
  { cr.current_token(const_state) } -> std::same_as<const Token&>;
  { cr.input(const_state) } -> std::same_as<const typename T::input_type&>;
};

enum class Format : std::uint8_t {
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

}  // namespace hosq
