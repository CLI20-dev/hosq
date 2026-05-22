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
  std::uint8_t context;
};

template <class T>
concept CheckpointableStateConcept = requires(T state) {
  { state.rollback(state.checkpoint()) } -> std::same_as<void>;
};

template <class T>
concept ReaderStateConcept = std::copyable<T> || CheckpointableStateConcept<T>;

template <class T>
concept ReaderConcept = requires(T r, const T cr, typename T::state_type state,
                                 const typename T::state_type const_state,
                                 typename T::input_type input) {
  typename T::input_type;
  typename T::state_type;
  requires ReaderStateConcept<typename T::state_type>;
  { r.reset(state, input) } -> std::same_as<void>;
  { r.next_token(state) } -> std::same_as<bool>;
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

/// Opt-in trait for readers that emit scalar tokens with `length =
/// unresolved_length`. Specialize to `std::true_type` for readers that resolve
/// lengths lazily via mutable `ext::ScanString` / `ext::ScanLiteral` overloads.
/// When true, the const overloads of `scan::string` and `scan::literal` are
/// disabled, making key decoders non-const-invokable so that partial checkpoints
/// are taken instead of full state copies.
template <class R>
struct has_lazy_token_resolution : std::false_type {};

template <class R>
inline constexpr bool has_lazy_token_resolution_v =
    has_lazy_token_resolution<std::remove_cvref_t<R>>::value;

}  // namespace hosq
