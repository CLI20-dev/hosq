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
  mutable size_t length;
  TokenType type;
  uint8_t context;
};

template <class T>
concept ReaderConcept =
    std::default_initializable<T> && std::copy_constructible<T> &&
    std::assignable_from<T&, const T&> &&
    requires(const T reader, typename T::state_type state,
             const typename T::state_type const_state,
             typename T::input_type input) {
      typename T::input_type;
      typename T::state_type;
      { reader.reset(state, input) } -> std::same_as<void>;
      { reader.next_token(state) } -> std::same_as<Result<void, Error>>;
      { reader.current_token(state) } -> std::same_as<Token&>;
      { reader.current_token(const_state) } -> std::same_as<const Token&>;
      { reader.input(const_state) } -> std::same_as<typename T::input_type>;
    };

template <class T>
concept TokenReaderConcept =
    ReaderConcept<T> && requires(const T reader, typename T::state_type state) {
      { reader.read_one(state) } -> std::same_as<Result<Token, Error>>;
      { reader.read_value(state) } -> std::same_as<Result<Token, Error>>;
      {
        reader.read_array_value_or_end(state)
      } -> std::same_as<Result<Token, Error>>;
      {
        reader.read_object_key(state, true)
      } -> std::same_as<Result<Token, Error>>;
      {
        reader.read_object_colon_then_value(state)
      } -> std::same_as<Result<Token, Error>>;
      { reader.read_value_end(state) } -> std::same_as<Result<Token, Error>>;
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
