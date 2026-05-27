#pragma once

#include <charconv>
#include <concepts>
#include <cstddef>
#include <string_view>
#include <system_error>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "hosq/context.hh"
#include "hosq/detail/pipeline_analysis.hh"
#include "hosq/detail/pipeline_analysis_io.hh"
#include "hosq/detail/type_traits.hh"
#include "hosq/error.hh"
#include "hosq/pipeline.hh"
#include "hosq/view.hh"

namespace hosq::ext {

template <ReaderConcept R>
struct DecodeSkipValue {
  template <class T, TryPhase Phase>
  auto operator()(const ScalarView&, DecodeContext<T, R, Phase>&) const -> void {
  }

  template <class T, TryPhase Phase>
  auto operator()(const ObjectView&, DecodeContext<T, R, Phase>& ctx) const
      -> void {
    skip_nested(ctx);
  }

  template <class T, TryPhase Phase>
  auto operator()(const ArrayView&, DecodeContext<T, R, Phase>& ctx) const
      -> void {
    skip_nested(ctx);
  }

  template <class T, TryPhase Phase>
  auto operator()(DecodeContext<T, R, Phase>& ctx) const -> void {
    auto& reader = ctx.reader();
    auto& state = ctx.reader_state();
    if constexpr (Phase == TryPhase::normal) {
      if (!reader.next_token(state)) [[unlikely]] {
        return;
      }
    }

    const auto& tok = reader.current_token(state);
    if (tok.type == TokenType::Scalar) {
      return;
    }
    if (tok.type == TokenType::ObjectBegin ||
        tok.type == TokenType::ArrayBegin) {
      return skip_nested(ctx);
    }
    reader.report_error(
        state,
        Error{.code = ErrorCode::type_mismatch,
              .offset = tok.offset,
              .detail = "decode::skip expected scalar, array, or object"});
  }

 private:
  template <class T, TryPhase Phase>
  static auto skip_nested(DecodeContext<T, R, Phase>& ctx) -> void {
    auto& reader = ctx.reader();
    auto& state = ctx.reader_state();
    int depth = 1;
    while (depth > 0) {
      if (!reader.next_token(state)) [[unlikely]] {
        return;
      }
      const auto& tok = reader.current_token(state);
      if (tok.type == TokenType::ObjectBegin ||
          tok.type == TokenType::ArrayBegin) {
        ++depth;
      } else if (tok.type == TokenType::ObjectEnd ||
                 tok.type == TokenType::ArrayEnd) {
        --depth;
      }
    }
  }
};

template <ReaderConcept R>
struct DecodeNumber {
  template <class T, class Context>
  auto operator()(const ScalarView&, const Context& ctx) const
      -> std::remove_cvref_t<T> {
    using Value = std::remove_cvref_t<T>;

    Value value{};
    const Token& tok = ctx.reader().current_token(ctx.reader_state());
    const BytesView bytes = ctx.reader().input(ctx.reader_state());
    const char* first = bytes.as_string_view().data() + tok.offset;
    const char* last = bytes.as_string_view().data() + tok.offset + tok.length;

    const auto result = std::from_chars(first, last, value);

    if (result.ec == std::errc::result_out_of_range) [[unlikely]] {
      ctx.reader().report_error(
          ctx.reader_state(),
          Error{
              .code = ErrorCode::out_of_range,
              .offset = ctx.reader().current_token(ctx.reader_state()).offset,
              .detail = "decode::number result out of range",
          });
      return static_cast<Value>(0);
    }

    if (result.ec != std::errc{} || result.ptr != last) [[unlikely]] {
      ctx.reader().report_error(
          ctx.reader_state(),
          Error{
              .code = ErrorCode::invalid_number,
              .offset = ctx.reader().current_token(ctx.reader_state()).offset,
              .detail = "decode::number could not consume scalar token",
          });
      return static_cast<Value>(0);
    }

    return value;
  }
};

template <ReaderConcept R>
struct DecodeNumberInto {
  template <class T, class Context>
  auto operator()(const ScalarView&, const Context& ctx) const -> void {
    const Token& tok = ctx.reader().current_token(ctx.reader_state());
    const BytesView bytes = ctx.reader().input(ctx.reader_state());
    const char* first = bytes.as_string_view().data() + tok.offset;
    const char* last = bytes.as_string_view().data() + tok.offset + tok.length;

    const auto result = std::from_chars(first, last, ctx.value());

    if (result.ec == std::errc::result_out_of_range) [[unlikely]] {
      ctx.reader().report_error(
          ctx.reader_state(),
          Error{
              .code = ErrorCode::out_of_range,
              .offset = ctx.reader().current_token(ctx.reader_state()).offset,
              .detail = "decode::number_into result out of range",
          });
      return;
    }
    if (result.ec != std::errc{} || result.ptr != last) [[unlikely]] {
      ctx.reader().report_error(
          ctx.reader_state(),
          Error{
              .code = ErrorCode::invalid_number,
              .offset = ctx.reader().current_token(ctx.reader_state()).offset,
              .detail = "decode::number_into could not consume scalar token",
          });
      return;
    }
  }
};

}  // namespace hosq::ext

namespace hosq::decode {

namespace detail {

struct SkipImpl {
  template <class Input>
  using storage_type = std::monostate;

  template <class Storage, ReaderConcept R, TryPhase Phase>
  auto operator()(const ScalarView& view,
                  DecodeContext<Storage, R, Phase>& ctx) const -> void {
    ext::DecodeSkipValue<R>{}(view, ctx);
  }

  template <class Storage, ReaderConcept R, TryPhase Phase>
  auto operator()(const ObjectView& view,
                  DecodeContext<Storage, R, Phase>& ctx) const -> void {
    ext::DecodeSkipValue<R>{}(view, ctx);
  }

  template <class Storage, ReaderConcept R, TryPhase Phase>
  auto operator()(const ArrayView& view,
                  DecodeContext<Storage, R, Phase>& ctx) const -> void {
    ext::DecodeSkipValue<R>{}(view, ctx);
  }

  template <class Storage, ReaderConcept R, TryPhase Phase>
  auto operator()(DecodeContext<Storage, R, Phase>& ctx) const -> void {
    ext::DecodeSkipValue<R>{}(ctx);
  }
};

template <class T>
concept Number = (std::integral<std::remove_cvref_t<T>> &&
                  !std::same_as<std::remove_cvref_t<T>, bool>) ||
                 std::floating_point<std::remove_cvref_t<T>>;

template <Number T>
struct NumberImpl {
  template <class Storage, ReaderConcept R, TryPhase Phase>
  auto operator()(const ScalarView& input,
                  DecodeContext<Storage, R, Phase>& ctx) const
      -> std::remove_cvref_t<T> {
    return ext::DecodeNumber<R>{}.template operator()<T>(input, ctx);
  }
};

template <Number T>
struct NumberIntoImpl {
  template <class Input>
  using storage_type = T;
  template <class Storage, ReaderConcept R, TryPhase Phase>
  auto operator()(const ScalarView& input,
                  DecodeContext<Storage, R, Phase>& ctx) const -> void {
    ext::DecodeNumberInto<R>{}.template operator()<T>(input, ctx);
  }
};

template <auto F>
struct CustomImpl {
  template <class T>
    requires requires(T&& value) { std::invoke(F, std::forward<T>(value)); }
  constexpr auto operator()(T&& value) const -> decltype(auto) {
    return std::invoke(F, std::forward<T>(value));
  }
};

template <auto... Pipelines>
struct EachImpl {
  static_assert(
      hosq::detail::always_false<
          std::tuple<std::integral_constant<std::size_t, sizeof...(Pipelines)>>>,
      "decode::each requires one array pipeline or two object "
      "pipelines");
};

template <auto Pipeline>
  requires(::hosq::detail::PipelineObject<decltype(Pipeline)>)
struct EachImpl<Pipeline> {
  static_assert(hosq::detail::PipelineObject<decltype(Pipeline)>,
                "decode::each<P> requires P to be a hosq::Pipeline");

  using pipeline_type = std::remove_cvref_t<decltype(Pipeline)>;

  using info = ::hosq::detail::pipeline_analysis_t<
      ::hosq::detail::PipelineVerificationMode::at_most_one_storage,
      DecodeContext<void, ::hosq::detail::PipelineAnalysisReader,
                    TryPhase::first>,
      void, pipeline_type>;

  using terminal_type = ::hosq::detail::pipeline_terminal_t<info>;

  template <class Input>
  using storage_type = std::vector<::hosq::detail::pipeline_storage_t<info>>;

  template <auto T, class U>
  struct InvalidPipelineTerminal;

  template <ReaderConcept R, TryPhase Phase, class Storage>
  auto operator()(const ArrayView&, DecodeContext<Storage, R, Phase>& ctx) const
      -> std::conditional_t<std::same_as<terminal_type, bool>, bool, void> {
    using actual_decoder_info = ::hosq::detail::pipeline_analysis_t<
        ::hosq::detail::PipelineVerificationMode::at_most_one_storage,
        DecodeContext<typename Storage::value_type, R, TryPhase::first>, void,
        pipeline_type>;
    using actual_decoder_terminal =
        ::hosq::detail::pipeline_terminal_t<actual_decoder_info>;

    if constexpr (!std::same_as<actual_decoder_terminal, void> &&
                  !std::same_as<actual_decoder_terminal, bool>) {
      static_assert(
          sizeof(InvalidPipelineTerminal<Pipeline, actual_decoder_terminal>) ==
              0,
          "Decoder pipeline must have a terminal type of void or bool "
          "for this Reader/Context");
    }
    static_assert(std::same_as<terminal_type, actual_decoder_terminal>,
                  "Decoder pipeline terminal type must be void or bool, and if "
                  "bool, must be the same as the EachImpl terminal type");
    auto& reader = ctx.reader();
    auto& state = ctx.reader_state();
    using expected_storage_type = storage_type<ArrayView>;
    static_assert(
        std::same_as<Storage, expected_storage_type>,
        "decode::each<P> context storage must be std::vector<PipelineStorage>");

    while (true) {
      if (!reader.next_token(state)) [[unlikely]] {
        ctx.value().clear();
        return hosq::detail::terminal_return<terminal_type>(false);
      }

      const auto& tok = reader.current_token(state);
      if (tok.type == TokenType::ArrayEnd) {
        return hosq::detail::terminal_return<terminal_type>(true);
      }

      ctx.value().emplace_back();

      auto elem_ctx =
          DecodeContext<typename Storage::value_type, R, TryPhase::first>{
              ctx.value().back(), reader, state};

      if constexpr (std::same_as<actual_decoder_terminal, bool>) {
        if (!pipeline_type::invoke(elem_ctx)) {
          ctx.value().clear();
          return hosq::detail::terminal_return<terminal_type>(false);
        }
      } else {
        pipeline_type::invoke(elem_ctx);
      }
    }
  }
};

template <auto Pipeline, size_t N>
  requires(::hosq::detail::PipelineObject<decltype(Pipeline)>)
struct EachImpl<Pipeline, N> {
  static_assert(hosq::detail::PipelineObject<decltype(Pipeline)>,
                "decode::each<P> requires P to be a hosq::Pipeline");

  using pipeline_type = std::remove_cvref_t<decltype(Pipeline)>;

  using info = ::hosq::detail::pipeline_analysis_t<
      ::hosq::detail::PipelineVerificationMode::at_most_one_storage,
      DecodeContext<void, ::hosq::detail::PipelineAnalysisReader,
                    TryPhase::first>,
      void, pipeline_type>;

  using terminal_type = ::hosq::detail::pipeline_terminal_t<info>;

  template <class Input>
  using storage_type = std::array<::hosq::detail::pipeline_storage_t<info>, N>;

  template <auto T, class U>
  struct InvalidPipelineTerminal;

  template <ReaderConcept R, TryPhase Phase, class Storage>
  auto operator()(const ArrayView&, DecodeContext<Storage, R, Phase>& ctx) const
      -> terminal_type {
    using actual_decoder_info = ::hosq::detail::pipeline_analysis_t<
        ::hosq::detail::PipelineVerificationMode::at_most_one_storage,
        DecodeContext<Storage, R, TryPhase::first>, void, pipeline_type>;
    using actual_decoder_terminal =
        ::hosq::detail::pipeline_terminal_t<actual_decoder_info>;

    if constexpr (!std::same_as<actual_decoder_terminal, void> &&
                  !std::same_as<actual_decoder_terminal, bool>) {
      static_assert(
          sizeof(InvalidPipelineTerminal<Pipeline, actual_decoder_terminal>) ==
              0,
          "Decoder pipeline must have a terminal type of void or bool "
          "for this Reader/Context");
    }
    static_assert(std::same_as<terminal_type, actual_decoder_terminal>,
                  "Decoder pipeline terminal type must be void or bool, and if "
                  "bool, must be the same as the EachImpl terminal type");
    auto& reader = ctx.reader();
    auto& state = ctx.reader_state();
    size_t index = 0;
    while (index < N) {
      if (!reader.next_token(state)) [[unlikely]] {
        return hosq::detail::terminal_return<terminal_type>(false);
      }
      const auto& tok = reader.current_token(state);
      if (tok.type == TokenType::ArrayEnd) {
        reader.report_error(
            state, Error{
                       .code = ErrorCode::invalid_length,
                       .offset = tok.offset,
                       .detail = "fixed-size decode::each array ended early",
                   });
        return hosq::detail::terminal_return<terminal_type>(false);
      }
      auto elem_ctx =
          DecodeContext<typename Storage::value_type, R, TryPhase::first>{
              ctx.value()[index], reader, state};
      if constexpr (std::same_as<terminal_type, bool>) {
        if (!pipeline_type::invoke(elem_ctx)) {
          return false;
        }
      } else {
        pipeline_type::invoke(elem_ctx);
      }
      index++;
    }
    if (!reader.next_token(state)) [[unlikely]] {
      return hosq::detail::terminal_return<terminal_type>(false);
    }
    const auto& tok = reader.current_token(state);
    if (tok.type == TokenType::ArrayEnd) {
      return hosq::detail::terminal_return<terminal_type>(true);
    }
    reader.report_error(
        state, Error{
                   .code = ErrorCode::invalid_length,
                   .offset = reader.current_token(state).offset,
                   .detail = "fixed-size decode::each array has extra element",
               });
    return hosq::detail::terminal_return<terminal_type>(false);
  }
};

template <auto KeyPipeline, auto ValuePipeline>
  requires(::hosq::detail::PipelineObject<decltype(KeyPipeline)> &&
           ::hosq::detail::PipelineObject<decltype(ValuePipeline)>)
struct EachImpl<KeyPipeline, ValuePipeline> {
  static_assert(hosq::detail::PipelineObject<decltype(KeyPipeline)> &&
                    hosq::detail::PipelineObject<decltype(ValuePipeline)>,
                "decode::each<KeyP, ValueP> requires hosq::Pipeline operands");

  using key_pipeline_type = std::remove_cvref_t<decltype(KeyPipeline)>;
  using value_pipeline_type = std::remove_cvref_t<decltype(ValuePipeline)>;

  using key_info = ::hosq::detail::pipeline_analysis_t<
      ::hosq::detail::PipelineVerificationMode::at_most_one_storage,
      ::hosq::detail::PipelineDecodeAnalysisContext, void, key_pipeline_type>;
  using value_info = ::hosq::detail::pipeline_analysis_t<
      ::hosq::detail::PipelineVerificationMode::at_most_one_storage,
      ::hosq::detail::PipelineDecodeAnalysisContext, void, value_pipeline_type>;

  using key_terminal_type = ::hosq::detail::pipeline_terminal_t<key_info>;
  using value_terminal_type = ::hosq::detail::pipeline_terminal_t<value_info>;

  using key_storage_type = ::hosq::detail::pipeline_storage_t<key_info>;
  using value_storage_type = ::hosq::detail::pipeline_storage_t<value_info>;

  template <class Input>
  using storage_type = ::hosq::Map<std::vector<key_storage_type>,
                                   std::vector<value_storage_type>>;

  using pipeline_output_type =
      std::conditional_t<std::same_as<key_terminal_type, bool> ||
                             std::same_as<value_terminal_type, bool>,
                         bool, void>;

  template <class Storage, ReaderConcept R, TryPhase Phase>
  auto operator()(const ObjectView&, DecodeContext<Storage, R, Phase>& ctx) const
      -> pipeline_output_type {
    using actual_key_info = ::hosq::detail::pipeline_analysis_t<
        ::hosq::detail::PipelineVerificationMode::at_most_one_storage,
        DecodeContext<key_storage_type, R, TryPhase::first>, void,
        key_pipeline_type>;
    using actual_key_terminal =
        ::hosq::detail::pipeline_terminal_t<actual_key_info>;
    using actual_key_storage =
        ::hosq::detail::pipeline_storage_t<actual_key_info>;
    static_assert(std::same_as<actual_key_terminal, void> ||
                      std::same_as<actual_key_terminal, bool>,
                  "Decoder pipeline must have a terminal type of void or bool "
                  "for this Reader/Context");
    static_assert(std::same_as<key_terminal_type, actual_key_terminal>,
                  "Decoder pipeline terminal type must be void or bool, and if "
                  "bool, must be the same as the EachImpl terminal type");

    static_assert(std::same_as<key_storage_type, actual_key_storage>,
                  "decode::each<KeyP, ValueP> key pipeline storage_type must be "
                  "Reader-independent");

    using actual_value_info = ::hosq::detail::pipeline_analysis_t<
        ::hosq::detail::PipelineVerificationMode::at_most_one_storage,
        DecodeContext<value_storage_type, R, TryPhase::first>, void,
        value_pipeline_type>;
    using actual_value_terminal =
        ::hosq::detail::pipeline_terminal_t<actual_value_info>;
    using actual_value_storage =
        ::hosq::detail::pipeline_storage_t<actual_value_info>;
    static_assert(std::same_as<actual_value_terminal, void> ||
                      std::same_as<actual_value_terminal, bool>,
                  "Decoder pipeline must have a terminal type of void or bool "
                  "for this Reader/Context");
    static_assert(std::same_as<value_terminal_type, actual_value_terminal>,
                  "Decoder pipeline terminal type must be void or bool, and if "
                  "bool, must be the same as the EachImpl terminal type");
    static_assert(std::same_as<value_storage_type, actual_value_storage>,
                  "decode::each<KeyP, ValueP> value pipeline storage_type must "
                  "be Reader-independent");

    using expected_storage_type = storage_type<ObjectView>;
    static_assert(std::same_as<Storage, expected_storage_type>,
                  "decode::each<KeyP, ValueP> context storage must be "
                  "hosq::Map<KeyStorage, ValueStorage>");

    auto& reader = ctx.reader();
    auto& state = ctx.reader_state();

    while (true) {
      if (!reader.next_token(state)) [[unlikely]] {
        ctx.value().clear();
        return hosq::detail::terminal_return<pipeline_output_type>(false);
      }
      const auto& tok = reader.current_token(state);
      if (tok.type == TokenType::ObjectEnd) {
        return hosq::detail::terminal_return<pipeline_output_type>(true);
      }
      ctx.value().keys().emplace_back();
      auto key_ctx = DecodeContext<key_storage_type, R, TryPhase::first>{
          ctx.value().keys().back(), reader, state};

      if constexpr (std::same_as<actual_key_terminal, bool>) {
        if (!key_pipeline_type::invoke(key_ctx)) {
          ctx.value().clear();
          return hosq::detail::terminal_return<pipeline_output_type>(false);
        }
      } else {
        key_pipeline_type::invoke(key_ctx);
      }
      if (!reader.next_token(state)) [[unlikely]] {
        ctx.value().clear();
        return hosq::detail::terminal_return<pipeline_output_type>(false);
      }
      ctx.value().values().emplace_back();
      auto value_ctx = DecodeContext<value_storage_type, R, TryPhase::first>{
          ctx.value().values().back(), reader, state};
      if constexpr (std::same_as<actual_value_terminal, bool>) {
        if (!value_pipeline_type::invoke(value_ctx)) {
          ctx.value().clear();
          return hosq::detail::terminal_return<pipeline_output_type>(false);
        }
      } else {
        value_pipeline_type::invoke(value_ctx);
      }
    }
  }
};

template <auto KeyPipeline, auto ValuePipeline, size_t N>
  requires(::hosq::detail::PipelineObject<decltype(KeyPipeline)> &&
           ::hosq::detail::PipelineObject<decltype(ValuePipeline)>)
struct EachImpl<KeyPipeline, ValuePipeline, N> {
  static_assert(hosq::detail::PipelineObject<decltype(KeyPipeline)> &&
                    hosq::detail::PipelineObject<decltype(ValuePipeline)>,
                "decode::each<KeyP, ValueP> requires hosq::Pipeline operands");

  using key_pipeline_type = std::remove_cvref_t<decltype(KeyPipeline)>;
  using value_pipeline_type = std::remove_cvref_t<decltype(ValuePipeline)>;

  using key_info = ::hosq::detail::pipeline_analysis_t<
      ::hosq::detail::PipelineVerificationMode::at_most_one_storage,
      ::hosq::detail::PipelineDecodeAnalysisContext, void, key_pipeline_type>;
  using value_info = ::hosq::detail::pipeline_analysis_t<
      ::hosq::detail::PipelineVerificationMode::at_most_one_storage,
      ::hosq::detail::PipelineDecodeAnalysisContext, void, value_pipeline_type>;

  using key_terminal_type = ::hosq::detail::pipeline_terminal_t<key_info>;
  using value_terminal_type = ::hosq::detail::pipeline_terminal_t<value_info>;

  using key_storage_type = ::hosq::detail::pipeline_storage_t<key_info>;
  using value_storage_type = ::hosq::detail::pipeline_storage_t<value_info>;

  template <class Input>
  using storage_type = ::hosq::Map<std::array<key_storage_type, N>,
                                   std::array<value_storage_type, N>>;

  using pipeline_output_type =
      std::conditional_t<std::same_as<key_terminal_type, bool> ||
                             std::same_as<value_terminal_type, bool>,
                         bool, void>;

  template <class Storage, ReaderConcept R, TryPhase Phase>
  auto operator()(const ObjectView&, DecodeContext<Storage, R, Phase>& ctx) const
      -> pipeline_output_type {
    using actual_key_info = ::hosq::detail::pipeline_analysis_t<
        ::hosq::detail::PipelineVerificationMode::at_most_one_storage,
        DecodeContext<key_storage_type, R, TryPhase::first>, void,
        key_pipeline_type>;
    using actual_key_terminal =
        ::hosq::detail::pipeline_terminal_t<actual_key_info>;
    using actual_key_storage =
        ::hosq::detail::pipeline_storage_t<actual_key_info>;
    static_assert(std::same_as<actual_key_terminal, void> ||
                      std::same_as<actual_key_terminal, bool>,
                  "Decoder pipeline must have a terminal type of void or bool "
                  "for this Reader/Context");
    static_assert(std::same_as<key_terminal_type, actual_key_terminal>,
                  "Decoder pipeline terminal type must be void or bool, and if "
                  "bool, must be the same as the EachImpl terminal type");

    static_assert(std::same_as<key_storage_type, actual_key_storage>,
                  "decode::each<KeyP, ValueP> key pipeline storage_type must be "
                  "Reader-independent");

    using actual_value_info = ::hosq::detail::pipeline_analysis_t<
        ::hosq::detail::PipelineVerificationMode::at_most_one_storage,
        DecodeContext<value_storage_type, R, TryPhase::first>, void,
        value_pipeline_type>;
    using actual_value_terminal =
        ::hosq::detail::pipeline_terminal_t<actual_value_info>;
    using actual_value_storage =
        ::hosq::detail::pipeline_storage_t<actual_value_info>;
    static_assert(std::same_as<actual_value_terminal, void> ||
                      std::same_as<actual_value_terminal, bool>,
                  "Decoder pipeline must have a terminal type of void or bool "
                  "for this Reader/Context");
    static_assert(std::same_as<value_terminal_type, actual_value_terminal>,
                  "Decoder pipeline terminal type must be void or bool, and if "
                  "bool, must be the same as the EachImpl terminal type");
    static_assert(std::same_as<value_storage_type, actual_value_storage>,
                  "decode::each<KeyP, ValueP> value pipeline storage_type must "
                  "be Reader-independent");

    using expected_storage_type = storage_type<ObjectView>;
    static_assert(std::same_as<Storage, expected_storage_type>,
                  "decode::each<KeyP, ValueP> context storage must be "
                  "hosq::Map<KeyStorage, ValueStorage>");

    auto& reader = ctx.reader();
    auto& state = ctx.reader_state();

    size_t index = 0;
    while (index < N) {
      if (!reader.next_token(state)) [[unlikely]] {
        return hosq::detail::terminal_return<pipeline_output_type>(false);
      }
      const auto& tok = reader.current_token(state);
      if (tok.type == TokenType::ObjectEnd) {
        reader.report_error(
            state, Error{
                       .code = ErrorCode::invalid_length,
                       .offset = tok.offset,
                       .detail = "fixed-size decode::each object ended early",
                   });
        return hosq::detail::terminal_return<pipeline_output_type>(false);
      }
      auto key_ctx = DecodeContext<key_storage_type, R, TryPhase::first>{
          ctx.value().keys()[index], reader, state};

      if constexpr (std::same_as<actual_key_terminal, bool>) {
        if (!key_pipeline_type::invoke(key_ctx)) {
          return hosq::detail::terminal_return<pipeline_output_type>(false);
        }
      } else {
        key_pipeline_type::invoke(key_ctx);
      }
      if (!reader.next_token(state)) [[unlikely]] {
        return hosq::detail::terminal_return<pipeline_output_type>(false);
      }
      auto value_ctx = DecodeContext<value_storage_type, R, TryPhase::first>{
          ctx.value().values()[index], reader, state};
      if constexpr (std::same_as<actual_value_terminal, bool>) {
        if (!value_pipeline_type::invoke(value_ctx)) {
          return hosq::detail::terminal_return<pipeline_output_type>(false);
        }
      } else {
        value_pipeline_type::invoke(value_ctx);
      }
      index++;
    }
    if (!reader.next_token(state)) [[unlikely]] {
      return hosq::detail::terminal_return<pipeline_output_type>(false);
    }
    const auto& tok = reader.current_token(state);
    if (tok.type == TokenType::ObjectEnd) {
      return hosq::detail::terminal_return<pipeline_output_type>(true);
    }
    reader.report_error(
        state, Error{
                   .code = ErrorCode::invalid_length,
                   .offset = tok.offset,
                   .detail = "fixed-size decode::each object has extra member",
               });
    return hosq::detail::terminal_return<pipeline_output_type>(false);
  }
};

template <auto... Pipelines>
struct CollectImpl {
  static_assert(
      hosq::detail::always_false<
          std::tuple<std::integral_constant<std::size_t, sizeof...(Pipelines)>>>,
      "decode::collect requires one array pipeline or two object "
      "pipelines");
};

template <auto Pipeline>
struct CollectImpl<Pipeline> {
  static_assert(hosq::detail::PipelineObject<decltype(Pipeline)>,
                "decode::collect<P> requires P to be a hosq::Pipeline");

  using pipeline_type = std::remove_cvref_t<decltype(Pipeline)>;

  using info = ::hosq::detail::pipeline_analysis_t<
      ::hosq::detail::PipelineVerificationMode::no_storage,
      ::hosq::detail::PipelineDecodeAnalysisContext, void, pipeline_type>;

  using terminal_type = ::hosq::detail::pipeline_terminal_t<info>;

  static_assert(!std::same_as<terminal_type, void>,
                "decode::collect<P> requires P to return a value");

  template <ReaderConcept R, TryPhase Phase, class Storage>
  auto operator()(const ArrayView&, DecodeContext<Storage, R, Phase>& ctx) const
      -> std::vector<terminal_type> {
    using actual_decoder_info = ::hosq::detail::pipeline_analysis_t<
        ::hosq::detail::PipelineVerificationMode::no_storage,
        DecodeContext<void, R, TryPhase::first>, void, pipeline_type>;

    using actual_decoder_terminal =
        ::hosq::detail::pipeline_terminal_t<actual_decoder_info>;

    static_assert(!std::same_as<actual_decoder_terminal, void>,
                  "decode::collect<P> requires P to return a value "
                  "for this Reader/Context");

    static_assert(std::same_as<terminal_type, actual_decoder_terminal>,
                  "decode::collect<P> pipeline terminal type must be "
                  "Reader-independent");

    auto& reader = ctx.reader();
    auto& state = ctx.reader_state();

    auto ret = std::vector<terminal_type>{};

    while (true) {
      if (!reader.next_token(state)) [[unlikely]] {
        return ret;
      }

      const auto& tok = reader.current_token(state);
      if (tok.type == TokenType::ArrayEnd) {
        return ret;
      }

      DecodeContext<void, R, TryPhase::first> elem_ctx{reader, state};
      ret.push_back(pipeline_type::invoke(elem_ctx));
    }
  }
};

template <auto Pipeline, size_t N>
  requires(::hosq::detail::PipelineObject<decltype(Pipeline)>)
struct CollectImpl<Pipeline, N> {
  static_assert(hosq::detail::PipelineObject<decltype(Pipeline)>,
                "decode::collect<P, N> requires P to be a hosq::Pipeline");

  using pipeline_type = std::remove_cvref_t<decltype(Pipeline)>;

  using info = ::hosq::detail::pipeline_analysis_t<
      ::hosq::detail::PipelineVerificationMode::no_storage,
      ::hosq::detail::PipelineDecodeAnalysisContext, void, pipeline_type>;

  using terminal_type = ::hosq::detail::pipeline_terminal_t<info>;

  static_assert(!std::same_as<terminal_type, void>,
                "decode::collect<P, N> requires P to return a value");

  template <ReaderConcept R, TryPhase Phase, class Storage>
  auto operator()(const ArrayView&, DecodeContext<Storage, R, Phase>& ctx) const
      -> std::array<terminal_type, N> {
    using actual_decoder_info = ::hosq::detail::pipeline_analysis_t<
        ::hosq::detail::PipelineVerificationMode::no_storage,
        DecodeContext<void, R, TryPhase::first>, void, pipeline_type>;

    using actual_decoder_terminal =
        ::hosq::detail::pipeline_terminal_t<actual_decoder_info>;

    static_assert(!std::same_as<actual_decoder_terminal, void>,
                  "decode::collect<P, N> requires P to return a value "
                  "for this Reader/Context");

    static_assert(std::same_as<terminal_type, actual_decoder_terminal>,
                  "decode::collect<P, N> pipeline terminal type must be "
                  "Reader-independent");

    auto& reader = ctx.reader();
    auto& state = ctx.reader_state();

    std::array<terminal_type, N> ret{};
    std::size_t index = 0;

    while (index < N) {
      if (!reader.next_token(state)) [[unlikely]] {
        return ret;
      }

      const auto& tok = reader.current_token(state);
      if (tok.type == TokenType::ArrayEnd) {
        reader.report_error(
            state, Error{
                       .code = ErrorCode::invalid_length,
                       .offset = tok.offset,
                       .detail = "fixed-size decode::collect array ended early",
                   });
        return ret;
      }

      DecodeContext<void, R, TryPhase::first> elem_ctx{reader, state};
      ret[index] = pipeline_type::invoke(elem_ctx);
      ++index;
    }

    if (!reader.next_token(state)) [[unlikely]] {
      return ret;
    }

    const auto& tok = reader.current_token(state);
    if (tok.type == TokenType::ArrayEnd) {
      return ret;
    }

    reader.report_error(
        state,
        Error{
            .code = ErrorCode::invalid_length,
            .offset = tok.offset,
            .detail = "fixed-size decode::collect array has extra element",
        });
    return ret;
  }
};

template <auto KeyPipeline, auto ValuePipeline>
struct CollectImpl<KeyPipeline, ValuePipeline> {
  static_assert(
      hosq::detail::PipelineObject<decltype(KeyPipeline)> &&
          hosq::detail::PipelineObject<decltype(ValuePipeline)>,
      "decode::collect<KeyP, ValueP> requires hosq::Pipeline operands");

  using key_pipeline_type = std::remove_cvref_t<decltype(KeyPipeline)>;
  using value_pipeline_type = std::remove_cvref_t<decltype(ValuePipeline)>;

  using key_info = ::hosq::detail::pipeline_analysis_t<
      ::hosq::detail::PipelineVerificationMode::no_storage,
      ::hosq::detail::PipelineDecodeAnalysisContext, void, key_pipeline_type>;

  using value_info = ::hosq::detail::pipeline_analysis_t<
      ::hosq::detail::PipelineVerificationMode::no_storage,
      ::hosq::detail::PipelineDecodeAnalysisContext, void, value_pipeline_type>;

  using key_terminal_type = ::hosq::detail::pipeline_terminal_t<key_info>;
  using value_terminal_type = ::hosq::detail::pipeline_terminal_t<value_info>;

  static_assert(
      !std::same_as<key_terminal_type, void>,
      "decode::collect<KeyP, ValueP> key pipeline must return a value");

  static_assert(
      !std::same_as<value_terminal_type, void>,
      "decode::collect<KeyP, ValueP> value pipeline must return a value");

  using output_type = ::hosq::Map<std::vector<key_terminal_type>,
                                  std::vector<value_terminal_type>>;

  template <ReaderConcept R, TryPhase Phase, class Storage>
  auto operator()(const ObjectView&, DecodeContext<Storage, R, Phase>& ctx) const
      -> output_type {
    using actual_key_info = ::hosq::detail::pipeline_analysis_t<
        ::hosq::detail::PipelineVerificationMode::no_storage,
        DecodeContext<void, R, TryPhase::first>, void, key_pipeline_type>;

    using actual_value_info = ::hosq::detail::pipeline_analysis_t<
        ::hosq::detail::PipelineVerificationMode::no_storage,
        DecodeContext<void, R, TryPhase::first>, void, value_pipeline_type>;

    using actual_key_terminal =
        ::hosq::detail::pipeline_terminal_t<actual_key_info>;
    using actual_value_terminal =
        ::hosq::detail::pipeline_terminal_t<actual_value_info>;

    static_assert(
        !std::same_as<actual_key_terminal, void>,
        "decode::collect<KeyP, ValueP> key pipeline must return a value "
        "for this Reader/Context");

    static_assert(
        !std::same_as<actual_value_terminal, void>,
        "decode::collect<KeyP, ValueP> value pipeline must return a value "
        "for this Reader/Context");

    static_assert(std::same_as<key_terminal_type, actual_key_terminal>,
                  "decode::collect<KeyP, ValueP> key terminal type must be "
                  "Reader-independent");

    static_assert(std::same_as<value_terminal_type, actual_value_terminal>,
                  "decode::collect<KeyP, ValueP> value terminal type must be "
                  "Reader-independent");

    auto& reader = ctx.reader();
    auto& state = ctx.reader_state();

    auto ret = output_type{};

    while (true) {
      if (!reader.next_token(state)) [[unlikely]] {
        return ret;
      }

      const auto& tok = reader.current_token(state);
      if (tok.type == TokenType::ObjectEnd) {
        return ret;
      }

      DecodeContext<void, R, TryPhase::first> key_ctx{reader, state};
      ret.keys().push_back(key_pipeline_type::invoke(key_ctx));

      if (!reader.next_token(state)) [[unlikely]] {
        return ret;
      }

      DecodeContext<void, R, TryPhase::first> value_ctx{reader, state};
      ret.values().push_back(value_pipeline_type::invoke(value_ctx));
    }
  }
};

template <auto KeyPipeline, auto ValuePipeline, size_t N>
  requires(::hosq::detail::PipelineObject<decltype(KeyPipeline)> &&
           ::hosq::detail::PipelineObject<decltype(ValuePipeline)>)
struct CollectImpl<KeyPipeline, ValuePipeline, N> {
  static_assert(
      hosq::detail::PipelineObject<decltype(KeyPipeline)> &&
          hosq::detail::PipelineObject<decltype(ValuePipeline)>,
      "decode::collect<KeyP, ValueP, N> requires hosq::Pipeline operands");

  using key_pipeline_type = std::remove_cvref_t<decltype(KeyPipeline)>;
  using value_pipeline_type = std::remove_cvref_t<decltype(ValuePipeline)>;

  using key_info = ::hosq::detail::pipeline_analysis_t<
      ::hosq::detail::PipelineVerificationMode::no_storage,
      ::hosq::detail::PipelineDecodeAnalysisContext, void, key_pipeline_type>;

  using value_info = ::hosq::detail::pipeline_analysis_t<
      ::hosq::detail::PipelineVerificationMode::no_storage,
      ::hosq::detail::PipelineDecodeAnalysisContext, void, value_pipeline_type>;

  using key_terminal_type = ::hosq::detail::pipeline_terminal_t<key_info>;
  using value_terminal_type = ::hosq::detail::pipeline_terminal_t<value_info>;

  static_assert(
      !std::same_as<key_terminal_type, void>,
      "decode::collect<KeyP, ValueP, N> key pipeline must return a value");

  static_assert(
      !std::same_as<value_terminal_type, void>,
      "decode::collect<KeyP, ValueP, N> value pipeline must return a value");

  using output_type = ::hosq::Map<std::array<key_terminal_type, N>,
                                  std::array<value_terminal_type, N>>;

  template <ReaderConcept R, TryPhase Phase, class Storage>
  auto operator()(const ObjectView&, DecodeContext<Storage, R, Phase>& ctx) const
      -> output_type {
    using actual_key_info = ::hosq::detail::pipeline_analysis_t<
        ::hosq::detail::PipelineVerificationMode::no_storage,
        DecodeContext<void, R, TryPhase::first>, void, key_pipeline_type>;

    using actual_value_info = ::hosq::detail::pipeline_analysis_t<
        ::hosq::detail::PipelineVerificationMode::no_storage,
        DecodeContext<void, R, TryPhase::first>, void, value_pipeline_type>;

    using actual_key_terminal =
        ::hosq::detail::pipeline_terminal_t<actual_key_info>;
    using actual_value_terminal =
        ::hosq::detail::pipeline_terminal_t<actual_value_info>;

    static_assert(
        !std::same_as<actual_key_terminal, void>,
        "decode::collect<KeyP, ValueP, N> key pipeline must return a value "
        "for this Reader/Context");

    static_assert(
        !std::same_as<actual_value_terminal, void>,
        "decode::collect<KeyP, ValueP, N> value pipeline must return a value "
        "for this Reader/Context");

    static_assert(std::same_as<key_terminal_type, actual_key_terminal>,
                  "decode::collect<KeyP, ValueP, N> key terminal type must be "
                  "Reader-independent");

    static_assert(std::same_as<value_terminal_type, actual_value_terminal>,
                  "decode::collect<KeyP, ValueP, N> value terminal type must be "
                  "Reader-independent");

    auto& reader = ctx.reader();
    auto& state = ctx.reader_state();

    auto ret = output_type{};
    std::size_t index = 0;

    while (index < N) {
      if (!reader.next_token(state)) [[unlikely]] {
        return ret;
      }

      const auto& tok = reader.current_token(state);
      if (tok.type == TokenType::ObjectEnd) {
        reader.report_error(
            state, Error{
                       .code = ErrorCode::invalid_length,
                       .offset = tok.offset,
                       .detail = "fixed-size decode::collect object ended early",
                   });
        return ret;
      }

      DecodeContext<void, R, TryPhase::first> key_ctx{reader, state};
      ret.keys()[index] = key_pipeline_type::invoke(key_ctx);

      if (!reader.next_token(state)) [[unlikely]] {
        return ret;
      }

      DecodeContext<void, R, TryPhase::first> value_ctx{reader, state};
      ret.values()[index] = value_pipeline_type::invoke(value_ctx);

      ++index;
    }

    if (!reader.next_token(state)) [[unlikely]] {
      return ret;
    }

    const auto& tok = reader.current_token(state);
    if (tok.type == TokenType::ObjectEnd) {
      return ret;
    }

    reader.report_error(
        state,
        Error{
            .code = ErrorCode::invalid_length,
            .offset = tok.offset,
            .detail = "fixed-size decode::collect object has extra member",
        });
    return ret;
  }
};

struct BooleanImpl {
  template <class Storage, ReaderConcept R, TryPhase Phase>
  auto operator()(const ScalarView&, DecodeContext<Storage, R, Phase>& ctx) const
      -> bool {
    const Token& tok = ctx.reader().current_token(ctx.reader_state());
    const BytesView bytes = ctx.reader().input(ctx.reader_state());
    const std::string_view sv =
        bytes.as_string_view().substr(tok.offset, tok.length);
    if (sv == "true") return true;
    if (sv == "false") return false;
    ctx.reader().report_error(
        ctx.reader_state(),
        Error{
            .code = ErrorCode::type_mismatch,
            .offset = ctx.reader().current_token(ctx.reader_state()).offset,
            .detail = "decode::boolean expected true or false literal",
        });
    return false;
  }
};

struct StringViewImpl {
  template <class Storage, ReaderConcept R, TryPhase Phase>
  auto operator()(const ScalarView&,
                  const DecodeContext<Storage, R, Phase>& ctx) const
      -> std::string_view {
    const Token& tok = ctx.reader().current_token(ctx.reader_state());
    const BytesView bytes = ctx.reader().input(ctx.reader_state());
    const std::string_view sv =
        bytes.as_string_view().substr(tok.offset, tok.length);
    return sv;
  }
};

template <class T>
  requires ::hosq::detail::DecoderAcceptableT<T>
struct ToImpl {
  template <class Context>
  auto operator()(const ObjectView&, Context& ctx) const -> T {
    using context_type = std::remove_cvref_t<Context>;
    using reader_type = typename context_type::reader_type;

    T value{};

    const auto error = ::hosq::Decoder<reader_type>::decode_nested(
        value, ctx.reader(), ctx.reader_state());

    if (error.code != ErrorCode::ok) {
      ctx.reader().report_error(ctx.reader_state(), error);
    }

    return value;
  }

  template <class Context>
  auto operator()(const ArrayView&, Context& ctx) const -> T {
    using context_type = std::remove_cvref_t<Context>;
    using reader_type = typename context_type::reader_type;

    T value{};

    const auto error = ::hosq::Decoder<reader_type>::decode_nested(
        value, ctx.reader(), ctx.reader_state());

    if (error.code != ErrorCode::ok) {
      ctx.reader().report_error(ctx.reader_state(), error);
    }

    return value;
  }
};

}  // namespace detail
   //
/// Parses a numeric string_view into arithmetic type T using std::from_chars.
template <detail::Number T>
inline constexpr auto number = step<detail::NumberImpl<T>>;

template <detail::Number T>
inline constexpr auto number_into = step<detail::NumberIntoImpl<T>>;

/// Converts "true"/"false" to bool; returns type_mismatch for anything else.
inline constexpr auto boolean = step<detail::BooleanImpl>;

/// Passes string_view through unchanged; escape resolution is left to the
/// caller.
inline constexpr auto string_view = step<detail::StringViewImpl>;

/// Consumes the current JSON value without storing it.
inline constexpr auto skip = step<detail::SkipImpl>;

// /// Passes any value through unchanged.
// inline constexpr auto identity = step<detail::IdentityImpl>;

template <auto F>
inline constexpr auto custom = step<detail::CustomImpl<F>>;

template <auto... Pipelines>
inline constexpr auto each = step<detail::EachImpl<Pipelines...>>;

template <auto... Pipelines>
inline constexpr auto collect = step<detail::CollectImpl<Pipelines...>>;

template <class T>
inline constexpr auto to = step<detail::ToImpl<T>>;

}  // namespace hosq::decode
