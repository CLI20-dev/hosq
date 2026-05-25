#pragma once

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>

#include "hosq/bytes_view.hh"
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
concept ReaderConcept = requires(      //
    T& r,                              //
    const T& cr,                       //
    typename T::state_type& s,         //
    const typename T::state_type& cs,  //
    Error er,                          //
    typename T::input_type in          //
) {
  typename T::state_type;
  typename T::input_type;
  requires ReaderStateConcept<typename T::state_type>;

  { r.reset(s, in) } -> std::same_as<void>;
  { r.next_token(s) } -> std::same_as<bool>;

  { cr.report_error(cs, er) } -> std::same_as<void>;
  { cr.error(cs) } -> std::same_as<Error>;
  { cr.current_token(cs) } -> std::same_as<const Token&>;
  { cr.input(cs) } -> std::same_as<BytesView>;
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
