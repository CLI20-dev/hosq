#pragma once

#include <type_traits>

#include "hosq/context.hh"
#include "hosq/detail/compiler.hh"
#include "hosq/detail/pipeline_analysis.hh"
#include "hosq/detail/pipeline_analysis_io.hh"
#include "hosq/pipeline.hh"

namespace hosq {

template <auto Decoder, auto Encoder>
struct Elem {
  static_assert(detail::PipelineObject<decltype(Decoder)>,
                "Decoder must be a hosq::Pipeline");
  static_assert(detail::PipelineObject<decltype(Encoder)>,
                "Encoder must be a hosq::Pipeline");
};

/**
 * A single decoded/encoded field backed by a Decoder and Encoder pipeline pair.
 *
 * `storage_type` is derived from the decoder's `storage_type<Input>`
 * declaration.
 *
 * - `decoder_const_invokable<Context>` is `true` when the decoder pipeline
 *   can run with a `const Context&`, i.e. no step requires a mutable Context&.
 * - `encoder_const_invokable<Context>` is `true` when the encoder pipeline
 *   can run with a `const Context&`.
 */
template <auto Decoder, auto Encoder>
  requires detail::PipelineObject<decltype(Decoder)> &&
           detail::PipelineObject<decltype(Encoder)>
struct Elem<Decoder, Encoder> {
  using decoder_type = std::remove_cvref_t<decltype(Decoder)>;
  using encoder_type = std::remove_cvref_t<decltype(Encoder)>;

  using decoder_info = detail::pipeline_analysis_t<
      detail::PipelineVerificationMode::at_most_one_storage,
      detail::PipelineDecodeAnalysisContext, void, decoder_type>;

  using storage_type = detail::pipeline_storage_t<decoder_info>;
  using decoder_terminal = detail::pipeline_terminal_t<decoder_info>;

  static_assert(
      !(std::same_as<decoder_terminal, void> &&
        std::same_as<storage_type, detail::NoStorage>),
      "Decoder pipeline with void terminal must declare a storage_type<Input>; "
      "a no-storage decoder pipeline must have MatchResult terminal");

  using encoder_info =
      detail::pipeline_analysis_t<detail::PipelineVerificationMode::no_storage,
                                  detail::PipelineEncodeAnalysisContext,
                                  storage_type, encoder_type>;
  using encoder_terminal = detail::pipeline_terminal_t<encoder_info>;

  static_assert(std::same_as<encoder_terminal, void>,
                "Encoder pipeline must have a void terminal type");

  static_assert(
      std::same_as<decoder_terminal, void> ||
          detail::MatchResultT<decoder_terminal>,
      "Decoder pipeline must have a terminal type of void or MatchResult");

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

  // Internal storage access used by Decoder to build a context even for
  // no-storage elements such as object keys.
  [[nodiscard]] auto storage() noexcept -> storage_type& { return value_; }
  [[nodiscard]] auto storage() const noexcept -> const storage_type& {
    return value_;
  }

  template <ReaderConcept R, TryPhase Phase>
  HOSQ_ALWAYS_INLINE auto decode(DecodeContext<storage_type, R, Phase>& ctx) {
    using context_type = DecodeContext<storage_type, R, Phase>;
    static constexpr bool use_const = decoder_const_invokable<context_type>;
    using active_context =
        std::conditional_t<use_const, const context_type, context_type>;
    using actual_decoder_info = detail::pipeline_analysis_t<
        detail::PipelineVerificationMode::at_most_one_storage, active_context,
        void, decoder_type>;

    using actual_decoder_terminal =
        detail::pipeline_terminal_t<actual_decoder_info>;
    static_assert(
        std::same_as<actual_decoder_terminal, void> ||
            detail::MatchResultT<actual_decoder_terminal>,
        "Decoder pipeline must have a terminal type of void or MatchResult "
        "for this Reader/Context");
    if constexpr (use_const) {
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
    using Terminal = typename decoder_type::template final_output_type<
        void, const DecodeContext<storage_type, R, Phase>>;
    static_assert(std::same_as<Terminal, void> || detail::MatchResultT<Terminal>,
                  "decoder terminals must be void or MatchResult; bool is data");
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

}  // namespace hosq
