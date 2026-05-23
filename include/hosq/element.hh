#pragma once

#include <tuple>
#include <type_traits>

#include "hosq/context.hh"
#include "hosq/detail/as_tuple.hh"
#include "hosq/detail/pipeline_analysis.hh"
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
/// declaration.
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
  using encoder_type = std::remove_cvref_t<decltype(Encoder)>;

  using decoder_info = detail::pipeline_analysis_t<
      detail::PipelineVerificationMode::at_most_one_storage,
      detail::AnyDecodeContext, void, decoder_type>;
  using storage_type = detail::pipeline_storage_t<decoder_info>;
  using decoder_terminal = detail::pipeline_terminal_t<decoder_info>;

  static_assert(
      !(std::same_as<decoder_terminal, void> &&
        std::same_as<storage_type, detail::NoStorage>),
      "Decoder pipeline with void terminal must declare a storage_type<Input>; "
      "a no-storage decoder pipeline must have bool terminal");

  using encoder_info =
      detail::pipeline_analysis_t<detail::PipelineVerificationMode::no_storage,
                                  detail::AnyEncodeContext, storage_type,
                                  encoder_type>;
  using encoder_terminal = detail::pipeline_terminal_t<encoder_info>;

  static_assert(std::same_as<encoder_terminal, void>,
                "Encoder pipeline must have a void terminal type");

  static_assert(std::same_as<decoder_terminal, void> ||
                    std::same_as<decoder_terminal, bool>,
                "Decoder pipeline must have a terminal type of void or bool");

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

  [[nodiscard]] auto value() const -> const storage_type& {
    static_assert(!std::same_as<storage_type, detail::NoStorage>,
                  "Elem without storage cannot be dereferenced");
    return value_;
  }
  [[nodiscard]] auto value() -> storage_type& {
    static_assert(!std::same_as<storage_type, detail::NoStorage>,
                  "Elem without storage cannot be dereferenced");
    return value_;
  }

  [[nodiscard]] auto operator*() -> storage_type& {
    static_assert(!std::same_as<storage_type, detail::NoStorage>,
                  "Elem without storage cannot be dereferenced");
    return value_;
  }
  [[nodiscard]] auto operator*() const -> const storage_type& {
    static_assert(!std::same_as<storage_type, detail::NoStorage>,
                  "Elem without storage cannot be dereferenced");
    return value_;
  }

  template <ReaderConcept R, TryPhase Phase>
  HOSQ_ALWAYS_INLINE auto decode(DecodeContext<storage_type, R, Phase>& ctx) {
    using actual_decoder_info = detail::pipeline_analysis_t<
        detail::PipelineVerificationMode::at_most_one_storage,
        DecodeContext<storage_type, R, Phase>, void, decoder_type>;

    using actual_decoder_terminal =
        detail::pipeline_terminal_t<actual_decoder_info>;
    static_assert(std::same_as<actual_decoder_terminal, void> ||
                      std::same_as<actual_decoder_terminal, bool>,
                  "Decoder pipeline must have a terminal type of void or bool "
                  "for this Reader/Context");
    if constexpr (decoder_const_invokable<
                      DecodeContext<storage_type, R, Phase>>) {
      const auto& cctx = ctx;
      return decoder_type::invoke(cctx);
    } else {
      return decoder_type::invoke(ctx);
    }
  }

  template <ReaderConcept R, TryPhase Phase>
    requires(decoder_const_invokable<DecodeContext<storage_type, R, Phase>>)
  HOSQ_ALWAYS_INLINE auto decode(
      const DecodeContext<storage_type, R, Phase>& ctx) {
    return decoder_type::invoke(ctx);
  }

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

 private:
  [[no_unique_address]] storage_type value_{};
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
