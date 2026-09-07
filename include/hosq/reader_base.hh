#pragma once

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>
#include <utility>

#include "hosq/bytes_view.hh"
#include "hosq/error.hh"

#if defined(__GNUC__) || defined(__clang__)
#define HOSQ_READER_INLINE inline __attribute__((always_inline))
#elif defined(_MSC_VER)
#define HOSQ_READER_INLINE __forceinline
#else
#define HOSQ_READER_INLINE inline
#endif

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

// Static traversal hints. They describe the caller's operation, not a promise
// that untrusted input has the expected type. Readers must retain validation.
enum class Expect : std::uint8_t {
  value,
  member_value,
  next_member_or_end,
  next_element_or_end,
  document_end,
};

// Proven cursor positions, unlike advisory Expect hints. Use only immediately
// after reset or a key operation that cannot advance to another token.
enum class ReadPosition : std::uint8_t { value, member_value };

namespace detail {
template <auto... Hints, class R, class State>
  requires((sizeof...(Hints) != 0 && requires(R& r, State& s) {
             { r.template next_token_impl<Hints...>(s) } -> std::same_as<bool>;
           }) || requires(R& r, State& s) {
             { r.next_token_impl(s) } -> std::same_as<bool>;
           })
HOSQ_READER_INLINE constexpr auto reader_next_impl(R& reader, State& state)
    noexcept([] {
      if constexpr (sizeof...(Hints) != 0 && requires(R& r, State& s) {
                      { r.template next_token_impl<Hints...>(s) }
                          -> std::same_as<bool>;
                    }) {
        return noexcept(std::declval<R&>().template next_token_impl<Hints...>(
            std::declval<State&>()));
      } else {
        return noexcept(std::declval<R&>().next_token_impl(
            std::declval<State&>()));
      }
    }()) -> bool {
  if constexpr (sizeof...(Hints) != 0 && requires {
                  { reader.template next_token_impl<Hints...>(state) }
                      -> std::same_as<bool>;
                }) {
    return reader.template next_token_impl<Hints...>(state);
  } else {
    return reader.next_token_impl(state);
  }
}
}  // namespace detail

// Optional adapter: a Reader only needs next_token_impl(state). Specializations
// can accept their own static hint vocabulary; unsupported packs fall back.
// No state, virtual dispatch, or mandatory change to ReaderConcept is involved.
template <class Derived>
struct ReaderBase {
  // Available only when the derived Reader supports the fused operation.
  template <class T, bool Into = false, class State>
    requires requires(const Derived& r, State& s, T& value) {
      { r.template read_number_impl<T, Into>(s, value) } -> std::same_as<bool>;
    }
  HOSQ_READER_INLINE constexpr auto read_number(State& state, T& value) const
      noexcept(noexcept(std::declval<const Derived&>()
                            .template read_number_impl<T, Into>(state, value)))
      -> bool {
    return static_cast<const Derived&>(*this)
        .template read_number_impl<T, Into>(state, value);
  }

  template <auto... Hints, class State>
    requires requires(Derived& r, State& s) {
      detail::reader_next_impl<Hints...>(r, s);
    }
  HOSQ_READER_INLINE constexpr auto next_token(State& state)
      noexcept(noexcept(detail::reader_next_impl<Hints...>(
          std::declval<Derived&>(), state))) -> bool {
    return detail::reader_next_impl<Hints...>(static_cast<Derived&>(*this), state);
  }

  template <auto... Hints, class State>
    requires requires(const Derived& r, State& s) {
      detail::reader_next_impl<Hints...>(r, s);
    }
  HOSQ_READER_INLINE constexpr auto next_token(State& state) const
      noexcept(noexcept(detail::reader_next_impl<Hints...>(
          std::declval<const Derived&>(), state))) -> bool {
    return detail::reader_next_impl<Hints...>(static_cast<const Derived&>(*this),
                                             state);
  }
};

// Keep readers implementing the original protocol usable without inheritance.
template <auto... Hints, class R, class State>
HOSQ_READER_INLINE constexpr auto next_token(R& reader, State& state)
    noexcept([] {
      if constexpr (requires(R& r, State& s) {
                      { r.template next_token<Hints...>(s) } -> std::same_as<bool>;
                    }) {
        return noexcept(std::declval<R&>().template next_token<Hints...>(
            std::declval<State&>()));
      } else {
        return noexcept(std::declval<R&>().next_token(std::declval<State&>()));
      }
    }()) -> bool {
  if constexpr (requires {
                  { reader.template next_token<Hints...>(state) }
                      -> std::same_as<bool>;
                }) {
    return reader.template next_token<Hints...>(state);
  } else {
    return reader.next_token(state);
  }
}

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

#undef HOSQ_READER_INLINE
