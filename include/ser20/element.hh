#pragma once

#include <cstddef>
#include <type_traits>
#include <utility>

#include "ser20/context.hh"
#include "ser20/pipeline.hh"

namespace ser20 {

#ifndef SER20_ALWAYS_INLINE
#if defined(__GNUC__) || defined(__clang__)
#define SER20_ALWAYS_INLINE inline __attribute__((always_inline))
#elif defined(_MSC_VER)
#define SER20_ALWAYS_INLINE __forceinline
#else
#define SER20_ALWAYS_INLINE inline
#endif
#endif

namespace detail {

struct ElementTag {};

template <class T>
struct IsPipeline : std::false_type {};

template <auto... Fns>
struct IsPipeline<Pipeline<Fns...>> : std::true_type {};

template <class T>
concept PipelineObject = IsPipeline<std::remove_cvref_t<T>>::value;

};  // namespace detail

/**
 * @brief Parameter bag used when constructing `ElemImpl` or `PositionalImpl`
 * with named fields.
 *
 * Allows brace-initialisation with named members, e.g.:
 * @code
 *   cli::Option<int, "count"> count{cli::ArgParameter<int>{
 *       .help = "Number of repetitions",
 *       .presence = cli::required,
 *       .default_value = 1,
 *   }};
 * @endcode
 *
 * @tparam T The storage value type of the argument.
 */
template <auto Decoder, auto Encoder>
  requires detail::PipelineObject<decltype(Decoder)> &&
           detail::PipelineObject<decltype(Encoder)>
struct Elem : public detail::ElementTag {
  using decoder_type = std::remove_cvref_t<decltype(Encoder)>;
  using decoder_info = decltype(decoder_type::analyze_decoder());
  using storage_type = typename std::tuple_element_t<1, decoder_info>::type;
  using encoder_type = std::remove_cvref_t<decltype(Decoder)>;
  using encoder_info =
      decltype(encoder_type::template analyze_encoder<storage_type>());

  Elem() = default;

  [[nodiscard]] auto value() const -> const storage_type& { return value_; }
  [[nodiscard]] auto value() -> storage_type& { return value_; }
  [[nodiscard]] auto occurrences() const -> std::size_t { return occurrences_; }
  [[nodiscard]] auto operator*() -> storage_type& { return value_; }
  [[nodiscard]] auto operator*() const -> const storage_type& { return value_; }
  [[nodiscard]] auto operator->() -> storage_type* { return &value_; }
  [[nodiscard]] auto operator->() const -> const storage_type* {
    return &value_;
  }

  operator const storage_type&() const noexcept { return value_; }
  operator storage_type&() noexcept { return value_; }

  template <class U>
  [[nodiscard]] auto value_or(U&& fallback) const -> storage_type {
    return static_cast<storage_type>(std::forward<U>(fallback));
  }

  [[nodiscard]] explicit operator std::optional<storage_type>() const {
    return std::nullopt;
  }

  template <ReaderConcept R>
  SER20_ALWAYS_INLINE auto decode(DecodeContext<storage_type, R>& ctx)
      -> Result<void, Error> {
    ctx.occurrences = occurrences_;
    auto result = decoder_type::invoke(ctx, Result<void, Error>{});
    if (result) ++occurrences_;
    return result;
  }

  template <WriterConcept W>
  SER20_ALWAYS_INLINE auto encode(DecodeContext<storage_type, W>& ctx,
                                  Result<storage_type, Error> value) const
      -> Result<void, Error> {
    return encoder_type::invoke(ctx, value);
  }

 protected:
  template <ReaderConcept R>
  SER20_ALWAYS_INLINE auto decode(R& reader) -> Result<void, Error> {
    DecodeContext<storage_type, R> ctx{reader, value_, occurrences_};
    return decoder_type::invoke(ctx, Result<void, Error>{});
  }

 private:
  storage_type value_{};
  std::size_t occurrences_{};
};

namespace detail {
template <auto Decoder, auto Encoder>
struct Elem;

template <class T>
struct IsElem : std::false_type {};

template <auto Decoder, auto Encoder>
struct IsElem<::ser20::Elem<Decoder, Encoder>> : std::true_type {};

template <class T>
concept ElemLike = IsElem<std::remove_cvref_t<T>>::value;
};  // namespace detail

template <detail::ElemLike Key, detail::ElemLike Value>
struct Entry : public detail::ElementTag {
  using key_type = Key;
  using value_type = Value;

  Entry() = default;

  [[nodiscard]] auto key() const -> const key_type& { return key_; }

  [[nodiscard]] auto value() const -> const value_type& { return value_; }

  [[nodiscard]] auto value() -> value_type& { return value_; }

  [[nodiscard]] auto operator*() -> value_type& { return value_; }

  [[nodiscard]] auto operator*() const -> const value_type& { return value_; }

  [[nodiscard]] auto operator->() -> value_type* { return &value_; }

  [[nodiscard]] auto operator->() const -> const value_type* { return &value_; }

  template <class Context>
  [[nodiscard]]
  constexpr auto invoke_key(Context& ctx) {
    return key_.invoke_decode(ctx);
  }

  template <class Context>
  [[nodiscard]]
  constexpr auto invoke_value(Context& ctx) {
    return value_.invoke_decode(ctx);
  }

 private:
  key_type key_{};
  value_type value_{};
};

namespace detail {

template <class T>
struct IsEntry : std::false_type {};

template <class K, class V>
struct IsEntry<Entry<K, V>> : std::true_type {};

template <class T>
inline constexpr bool is_entry_v = IsEntry<std::remove_cvref_t<T>>::value;

}  // namespace detail

#undef SER20_ALWAYS_INLINE
}  // namespace ser20
