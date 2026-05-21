#pragma once

#include <concepts>
#include <cstddef>
#include <optional>
#include <tuple>
#include <type_traits>
#include <utility>

#include "ser20/as_tuple.hh"
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

}  // namespace detail

/// A single decoded/encoded field backed by a Decoder and Encoder pipeline pair.
///
/// `storage_type` is derived from the decoder's `storage_type<Input>`
/// declaration. `occurrences()` counts how many times the field has been
/// successfully decoded.
///
/// - `decoder_const_invokable<Context>` is `true` when the decoder pipeline
///   can run with a `const Context&`, i.e. no step requires a mutable Context&.
/// - `encoder_const_invokable<Context>` is `true` when the encoder pipeline
///   can run with a `const Context&`.
template <auto Decoder, auto Encoder>
  requires detail::PipelineObject<decltype(Decoder)> &&
           detail::PipelineObject<decltype(Encoder)>
struct Elem : public detail::ElementTag {
  using decoder_type = std::remove_cvref_t<decltype(Decoder)>;
  using decoder_info = decltype(decoder_type::analyze_decoder());
  using storage_type = typename std::tuple_element_t<1, decoder_info>::type;
  using encoder_type = std::remove_cvref_t<decltype(Encoder)>;
  using encoder_info =
      decltype(encoder_type::template analyze_encoder<storage_type>());

  /// True when the decoder pipeline can be invoked with `const Context&`.
  template <class Context>
  static constexpr bool decoder_const_invokable =
      decoder_type::template callable_with_const_context<Context, void>();

  /// True when the encoder pipeline can be invoked with `const Context&`.
  template <class Context>
  static constexpr bool encoder_const_invokable =
      encoder_type::template callable_with_const_context<Context,
                                                         storage_type>();

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
    if (occurrences_ > 0) return value_;
    return static_cast<storage_type>(std::forward<U>(fallback));
  }

  [[nodiscard]] explicit operator std::optional<storage_type>() const {
    if (occurrences_ > 0) return value_;
    return std::nullopt;
  }

  // ── decode(DecodeContext)
  // ──────────────────────────────────────────────────── Mutually exclusive via
  // requires: only the form matching decoder_const_invokable is enabled.

  template <ReaderConcept R>
    requires(!decoder_const_invokable<DecodeContext<storage_type, R>>)
  SER20_ALWAYS_INLINE auto decode(DecodeContext<storage_type, R>& ctx) {
    auto result = decoder_type::invoke(ctx);
    if (result) ++occurrences_;
    return result;
  }

  template <ReaderConcept R>
    requires(decoder_const_invokable<DecodeContext<storage_type, R>>)
  SER20_ALWAYS_INLINE auto decode(const DecodeContext<storage_type, R>& ctx) {
    auto result = decoder_type::invoke(ctx);
    if (result) ++occurrences_;
    return result;
  }

  // ── encode(EncodeContext)
  // ────────────────────────────────────────────────────

  template <WriterConcept W>
    requires(!encoder_const_invokable<EncodeContext<storage_type, W>>)
  SER20_ALWAYS_INLINE auto encode(EncodeContext<storage_type, W>& ctx) const {
    return encoder_type::invoke(value_, ctx);
  }

  template <WriterConcept W>
    requires(encoder_const_invokable<EncodeContext<storage_type, W>>)
  SER20_ALWAYS_INLINE auto encode(
      const EncodeContext<storage_type, W>& ctx) const {
    return encoder_type::invoke(value_, ctx);
  }

  // ── decode(R& reader)
  // ──────────────────────────────────────────────────────── Creates a
  // DecodeContext from the raw reader, then delegates to the appropriate
  // decode(context) overload using decoder_const_invokable.

  // decoder_const_invokable pipeline — only const reader methods are needed
  // (advance / current_token / input). const_cast is safe: Reader::advance()
  // only modifies the mutable pos_ member.
  template <ReaderConcept R>
    requires(decoder_const_invokable<DecodeContext<storage_type, R>>)
  SER20_ALWAYS_INLINE auto decode(const R& reader) {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
    DecodeContext<storage_type, R> ctx{value_, const_cast<R&>(reader),
                                       occurrences_};
    const auto& cctx = ctx;
    return decode(cctx);
  }

  // non-decoder_const_invokable pipeline — mutable context required, so
  // mutable reader.
  template <ReaderConcept R>
    requires(!decoder_const_invokable<DecodeContext<storage_type, R>>)
  SER20_ALWAYS_INLINE auto decode(R& reader) {
    DecodeContext<storage_type, R> ctx{value_, reader, occurrences_};
    return decode(ctx);
  }

 private:
  storage_type value_{};
  std::size_t occurrences_{};
};

namespace detail {

template <class T>
struct IsElem : std::false_type {};

template <auto Decoder, auto Encoder>
struct IsElem<::ser20::Elem<Decoder, Encoder>> : std::true_type {};

template <class T>
concept ElemT = IsElem<std::remove_cvref_t<T>>::value;

}  // namespace detail

namespace detail {

struct EntryTag {};

template <class T>
concept EntryT = std::derived_from<std::remove_cvref_t<T>, EntryTag>;

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

/// A key–value pair where both Key and Value are Elem types.
///
/// `*_decoder_const_invokable` / `*_encoder_const_invokable` mirror the
/// underlying Elem's flags and gate the const/mutable overloads of decode_key /
/// decode_value / encode_key / encode_value at compile time.
template <detail::ElemT Key, detail::ElemT Value>
struct Entry : public detail::EntryTag {
  using value_type = Value;
  using key_type = Key;

  template <class R>
  static constexpr bool key_decoder_const_invokable =
      Key::template decoder_const_invokable<
          DecodeContext<typename Key::storage_type, R>>;

  template <class R>
  static constexpr bool value_decoder_const_invokable =
      Value::template decoder_const_invokable<
          DecodeContext<typename Value::storage_type, R>>;

  template <class W>
  static constexpr bool key_encoder_const_invokable =
      Key::template encoder_const_invokable<
          EncodeContext<typename Key::storage_type, W>>;

  template <class W>
  static constexpr bool value_encoder_const_invokable =
      Value::template encoder_const_invokable<
          EncodeContext<typename Value::storage_type, W>>;

  Entry() = default;

  [[nodiscard]] auto key() const -> const key_type& { return key_; }
  [[nodiscard]] auto key() -> key_type& { return key_; }

  [[nodiscard]] auto value() const -> const value_type& { return value_; }
  [[nodiscard]] auto value() -> value_type& { return value_; }

  [[nodiscard]] auto operator*() -> value_type& { return value_; }
  [[nodiscard]] auto operator*() const -> const value_type& { return value_; }

  [[nodiscard]] auto operator->() -> value_type* { return &value_; }
  [[nodiscard]] auto operator->() const -> const value_type* { return &value_; }

 public:
  key_type key_{};      // NOLINT
  value_type value_{};  // NOLINT

  // ── decode_key
  // ───────────────────────────────────────────────────────────────
  template <ReaderConcept R>
    requires(!key_decoder_const_invokable<R>)
  [[nodiscard]] auto decode_key(R& reader) {
    return key().decode(reader);
  }

  template <ReaderConcept R>
    requires(key_decoder_const_invokable<R>)
  [[nodiscard]] auto decode_key(const R& reader) {
    return key().decode(reader);
  }

  // ── decode_value
  // ─────────────────────────────────────────────────────────────

  template <ReaderConcept R>
    requires(!value_decoder_const_invokable<R>)
  [[nodiscard]] auto decode_value(R& reader) {
    return value().decode(reader);
  }

  template <ReaderConcept R>
    requires(value_decoder_const_invokable<R>)
  [[nodiscard]] auto decode_value(const R& reader) {
    return value().decode(reader);
  }
};

template <class T>
struct Try {
  static_assert(detail::always_false<T>,
                "Try<T>: T must be either an Elem or an Entry.");
};

template <detail::ElemT T>
struct Try<T> : public T {
  using base_type = T;
  using T::T;
  using terminal_type = typename std::tuple_element_t<
      2, typename std::remove_cvref_t<T>::decoder_info>::type;
  static_assert(std::same_as<terminal_type, bool>,
                "Try<T>: Elem decoder terminal type must be "
                "bool/Result<bool, Error>.");
  Try() = default;
};

template <detail::EntryT T>
struct Try<T> : public T {
  using base_type = T;
  using T::T;
  using key_type = typename std::remove_cvref_t<T>::key_type;
  using terminal_type = typename std::tuple_element_t<
      2, typename std::remove_cvref_t<key_type>::decoder_info>::type;
  static_assert(std::same_as<terminal_type, bool>,
                "Try<T>: Entry key Elem decoder terminal type must be "
                "bool/Result<bool, Error>.");
  Try() = default;
};

namespace detail {

template <class T>
struct IsTry : std::false_type {};

template <class T>
struct IsTry<::ser20::Try<T>> : std::true_type {};

template <class T>
concept TryT = IsTry<std::remove_cvref_t<T>>::value;

}  // namespace detail

/**
 * @brief Prevents Elem from being decomposed as an aggregate schema.
 *
 * Elem is itself an aggregate, but semantically it is a leaf field. This
 * overload is intentionally more constrained than the generic aggregate
 * `as_tuple` overload, so `ser20::as_tuple(elem)` selects this overload instead
 * of recursively decomposing Elem's implementation details.
 *
 * The overload is deleted because calling `as_tuple` on an Elem is a misuse of
 * the schema decomposition API.
 */
template <detail::ElemT T>
constexpr auto as_tuple(T&) = delete;
template <detail::ElemT T>
constexpr auto as_tuple(const T&) = delete;

/**
 * @brief Prevents Entry from being decomposed as an aggregate schema.
 *
 * Entry is itself an aggregate, but semantically it is already one element of an
 * entry tuple. This overload is intentionally more constrained than the generic
 * aggregate `as_tuple` overload, so `ser20::as_tuple(entry)` selects this
 * overload instead of recursively decomposing Entry's implementation details.
 *
 * The overload is deleted because calling `as_tuple` on an Entry is a misuse of
 * the schema decomposition API.
 */
template <detail::EntryT T>
constexpr auto as_tuple(T&) = delete;
template <detail::EntryT T>
constexpr auto as_tuple(const T&) = delete;

#undef SER20_ALWAYS_INLINE
}  // namespace ser20
