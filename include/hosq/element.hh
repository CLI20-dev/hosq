#pragma once

#include <cstddef>
#include <optional>
#include <tuple>
#include <type_traits>
#include <utility>

#include "hosq/context.hh"
#include "hosq/detail/as_tuple.hh"
#include "hosq/pipeline.hh"

namespace hosq {

#ifndef HOSQ_ALWAYS_INLINE
#if defined(__GNUC__) || defined(__clang__)
#define HOSQ_ALWAYS_INLINE inline __attribute__((always_inline))
#elif defined(_MSC_VER)
#define HOSQ_ALWAYS_INLINE __forceinline
#else
#define HOSQ_ALWAYS_INLINE inline
#endif
#endif

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
struct Elem {
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
  HOSQ_ALWAYS_INLINE auto decode(DecodeContext<storage_type, R>& ctx) {
    auto result = decoder_type::invoke(ctx);
    if (result) ++occurrences_;
    return result;
  }

  template <ReaderConcept R>
    requires(decoder_const_invokable<DecodeContext<storage_type, R>>)
  HOSQ_ALWAYS_INLINE auto decode(const DecodeContext<storage_type, R>& ctx) {
    auto result = decoder_type::invoke(ctx);
    if (result) ++occurrences_;
    return result;
  }

  // ── encode(EncodeContext)
  // ────────────────────────────────────────────────────

  template <WriterConcept W>
    requires(!encoder_const_invokable<EncodeContext<storage_type, W>>)
  HOSQ_ALWAYS_INLINE auto encode(EncodeContext<storage_type, W>& ctx) const {
    return encoder_type::invoke(value_, ctx);
  }

  template <WriterConcept W>
    requires(encoder_const_invokable<EncodeContext<storage_type, W>>)
  HOSQ_ALWAYS_INLINE auto encode(
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
  HOSQ_ALWAYS_INLINE auto decode(const R& reader) {
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
  HOSQ_ALWAYS_INLINE auto decode(R& reader) {
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
struct IsElem<::hosq::Elem<Decoder, Encoder>> : std::true_type {};

template <class T>
concept ElemT = IsElem<std::remove_cvref_t<T>>::value;

template <class Tuple>
struct ElemTuple : std::false_type {};

template <class... Ts>
struct ElemTuple<std::tuple<Ts...>>
    : std::bool_constant<(sizeof...(Ts) > 0) && (ElemT<Ts> && ...)> {};

template <class T>
concept ElemTupleT = ElemTuple<std::remove_cvref_t<T>>::value;

template <class T>
concept AsElemTupleT = requires(T t) {
  { hosq::detail::as_tuple(t) } -> ElemTupleT;
};

}  // namespace detail

/**
 * @brief Prevents Elem from being decomposed as an aggregate schema.
 *
 * Elem is itself an aggregate, but semantically it is a leaf field. This
 * overload is intentionally more constrained than the generic aggregate
 * `as_tuple` overload, so `hosq::as_tuple(elem)` selects this overload instead
 * of recursively decomposing Elem's implementation details.
 *
 * The overload is deleted because calling `as_tuple` on an Elem is a misuse of
 * the schema decomposition API.
 */
template <detail::ElemT T>
constexpr auto as_tuple(T&) = delete;
template <detail::ElemT T>
constexpr auto as_tuple(const T&) = delete;

#undef HOSQ_ALWAYS_INLINE

}  // namespace hosq
