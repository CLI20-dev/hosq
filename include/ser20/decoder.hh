#pragma once

#include <string_view>
#include <tuple>

#include "ser20/as_tuple.hh"
#include "ser20/element.hh"
#include "ser20/reader_base.hh"

namespace ser20 {

#ifndef SER20_ALWAYS_INLINE
#if defined(__GNUC__) || defined(__clang__)
#define SER20_ALWAYS_INLINE __attribute__((always_inline))
#elif defined(_MSC_VER)
#define SER20_ALWAYS_INLINE __forceinline
#else
#define SER20_ALWAYS_INLINE
#endif
#endif

namespace detail {

template <class Tuple>
struct EntryTuple : std::false_type {};

template <class... Ts>
struct EntryTuple<std::tuple<Ts...>>
    : std::bool_constant<(sizeof...(Ts) > 0) && (is_entry_v<Ts> && ...)> {};

};  // namespace detail

template <ReaderConcept R>
class Decoder {
 public:
  Decoder(const Decoder&) = delete;
  Decoder(Decoder&&) = delete;
  auto operator=(const Decoder&) -> Decoder& = delete;
  auto operator=(Decoder&&) -> Decoder& = delete;
  ~Decoder() = default;

  template <class T>
    requires(
        detail::IsElem<T>::value ||
        requires(T t) {
          std::type_identity<std::remove_cvref_t<decltype(as_tuple(t))>>::type;
          requires detail::EntryTuple<std::type_identity_t<
              std::remove_cvref_t<decltype(as_tuple(t))>>>::value;
        })
  [[nodiscard]]
  auto decode(T& t, std::string_view input) {
    R reader_(input);
  }

 private:
  template <class T>
  [[nodiscard]]
  static constexpr auto is_candidate_rejected(const Result<T, Error>& result)
      -> bool {
    return !result && result.error().code == ErrorCode::candidate_rejected;
  }

  template <class T>
  static constexpr auto decode_impl(T& t, auto& reader) -> Result<void, Error> {
    if constexpr (detail::IsElem<std::remove_cvref_t<T>>::value) {
      return t.decode(reader);
    } else {
      Result<void, Error> result{
          unexpected(Error{.code = ErrorCode::candidate_rejected})};

      const bool matched_or_error = std::apply(
          [&](auto&... entry) -> bool {
            return ([&]() -> bool {
              auto key_result = entry.decode_key(reader);

              if (is_candidate_rejected(key_result)) {
                return false;
              }

              if (!key_result) {
                result = unexpected(key_result.error());
                return true;
              }

              result = entry.decode_value(reader);
              return true;
            }() || ...);
          },
          ser20::as_tuple(t));

      if (!matched_or_error) {
        return unexpected(Error{.code = ErrorCode::candidate_rejected});
      }

      return result;
    }
  }
};

#undef SER20_ALWAYS_INLINE

}  // namespace ser20
