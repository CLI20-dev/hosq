#pragma once

#include <tuple>
#include <type_traits>

#include "ser20/as_tuple.hh"
#include "ser20/element.hh"
#include "ser20/reader_base.hh"

namespace ser20 {

#if defined(__GNUC__) || defined(__clang__)
#define SER20_ALWAYS_INLINE_ATTR __attribute__((always_inline))
#define SER20_FUNCTION_ALWAYS_INLINE inline __attribute__((always_inline))
#elif defined(_MSC_VER)
#define SER20_ALWAYS_INLINE_ATTR
#define SER20_FUNCTION_ALWAYS_INLINE __forceinline
#else
#define SER20_ALWAYS_INLINE_ATTR
#define SER20_FUNCTION_ALWAYS_INLINE inline
#endif

namespace detail {

template <class Tuple>
struct EntryTuple : std::false_type {};

template <class... Ts>
struct EntryTuple<std::tuple<Ts...>>
    : std::bool_constant<(sizeof...(Ts) > 0) && (EntryT<Ts> && ...)> {};

template <class T>
concept EntryTupleT = EntryTuple<std::remove_cvref_t<T>>::value;

template <class T>
concept AsEntryTupleT = requires(T t) {
  { ser20::as_tuple(t) } -> EntryTupleT;
};

}  // namespace detail

template <ReaderConcept R>
class Decoder {
 public:
  Decoder(const Decoder&) = delete;
  Decoder(Decoder&&) = delete;
  auto operator=(const Decoder&) -> Decoder& = delete;
  auto operator=(Decoder&&) -> Decoder& = delete;
  Decoder() = default;
  ~Decoder() = default;

  template <class T>
    requires(detail::ElemT<T> || detail::AsEntryTupleT<T>)
  [[nodiscard]]
  auto decode(T& t, typename R::input_type input) -> Result<void, Error> {
    reader_.reset(input);
    if constexpr (detail::ElemT<T>) {
      return decode_element(t);
    } else if constexpr (detail::AsEntryTupleT<T>) {
      return decode_as_entry_tuple(t);
    } else {
      static_assert(detail::ElemT<T> || detail::AsEntryTupleT<T>,
                    "T must be an Elem or have an as_tuple() of Entries");
    }
  }

 private:
  template <detail::ElemT T>
  [[nodiscard]]
  SER20_FUNCTION_ALWAYS_INLINE constexpr auto decode_element(T& t)
      -> Result<void, Error> {
    if constexpr (std::remove_cvref_t<T>::template decoder_const_invokable<R>) {
      const auto& r = static_cast<const R&>(reader_);
      return t.decode(r);
    } else {
      return t.decode(reader_);
    }
  }

  template <detail::AsEntryTupleT T>
  [[nodiscard]]
  SER20_FUNCTION_ALWAYS_INLINE auto decode_as_entry_tuple(T& t)
      -> Result<void, Error> {
    return std::apply(
        [&](auto&... entry) SER20_ALWAYS_INLINE_ATTR -> Result<void, Error> {
          Result<void, Error> result{
              unexpected(Error{.code = ErrorCode::candidate_rejected})};
          ([&]() SER20_ALWAYS_INLINE_ATTR -> bool {
            if constexpr (std::remove_cvref_t<decltype(entry)>::
                              template key_decoder_const_invokable<R>) {
              const auto& r = static_cast<const R&>(reader_);
              auto key_result = entry.decode_key(r);
              if (is_candidate_rejected(key_result)) {
                return false;
              }
              if (!key_result) {
                result = unexpected(key_result.error());
                return true;
              }
              result = entry.decode_value(reader_);
              return true;
            } else {
              auto snapshot = reader_;
              auto key_result = entry.decode_key(reader_);
              if (is_candidate_rejected(key_result)) {
                reader_ = snapshot;
                return false;
              }
              if (!key_result) {
                reader_ = snapshot;
                result = unexpected(key_result.error());
                return true;
              }
              result = entry.decode_value(reader_);
              return true;
            }
          }() || ...);
          return result;
        },
        ser20::as_tuple(t));
  }

  template <class T>
  [[nodiscard]]
  SER20_FUNCTION_ALWAYS_INLINE static constexpr auto is_candidate_rejected(
      const Result<T, Error>& result) -> bool {
    return !result && result.error().code == ErrorCode::candidate_rejected;
  }

  R reader_;
};

#undef SER20_ALWAYS_INLINE_ATTR
#undef SER20_FUNCTION_ALWAYS_INLINE

}  // namespace ser20
