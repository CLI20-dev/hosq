#pragma once

#include <charconv>
#include <concepts>
#include <cstddef>
#include <string_view>
#include <system_error>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "hosq/context.hh"
#include "hosq/detail/compiler.hh"
#include "hosq/detail/number_parse.hh"
#include "hosq/detail/pipeline_analysis.hh"
#include "hosq/detail/pipeline_analysis_io.hh"
#include "hosq/detail/type_traits.hh"
#include "hosq/error.hh"
#include "hosq/pipeline.hh"
#include "hosq/pipeline/scan.hh"
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
    ::hosq::detail::report_reader_error(
        reader, state,
        ::hosq::detail::ErrorInfo{
            .code = ErrorCode::type_mismatch,
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
struct DecodeNumberDefault {
  template <class T, class Context>
  auto operator()(const ScalarView&, const Context& ctx) const
      -> std::remove_cvref_t<T> {
    using Value = std::remove_cvref_t<T>;

    Value value{};
    const Token& tok = ctx.reader().current_token(ctx.reader_state());
    const BytesView bytes = ctx.reader().input(ctx.reader_state());
    const char* first = bytes.as_string_view().data() + tok.offset;
    const char* last = bytes.as_string_view().data() + tok.offset + tok.length;

    const auto result = hosq::detail::number_from_chars(first, last, value);

    if (result.ec == std::errc::result_out_of_range) [[unlikely]] {
      ::hosq::detail::report_reader_error(
          ctx.reader(), ctx.reader_state(),
          ::hosq::detail::ErrorInfo{
              .code = ErrorCode::out_of_range,
              .offset = ctx.reader().current_token(ctx.reader_state()).offset,
              .detail = "decode::number result out of range",
          });
      return static_cast<Value>(0);
    }

    if (result.ec != std::errc{} || result.ptr != last) [[unlikely]] {
      ::hosq::detail::report_reader_error(
          ctx.reader(), ctx.reader_state(),
          ::hosq::detail::ErrorInfo{
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
struct DecodeNumberIntoDefault {
  template <class T, class Context>
  auto operator()(const ScalarView&, const Context& ctx) const -> void {
    const Token& tok = ctx.reader().current_token(ctx.reader_state());
    const BytesView bytes = ctx.reader().input(ctx.reader_state());
    const char* first = bytes.as_string_view().data() + tok.offset;
    const char* last = bytes.as_string_view().data() + tok.offset + tok.length;

    const auto result =
        hosq::detail::number_from_chars(first, last, ctx.value());

    if (result.ec == std::errc::result_out_of_range) [[unlikely]] {
      ::hosq::detail::report_reader_error(
          ctx.reader(), ctx.reader_state(),
          ::hosq::detail::ErrorInfo{
              .code = ErrorCode::out_of_range,
              .offset = ctx.reader().current_token(ctx.reader_state()).offset,
              .detail = "decode::number_into result out of range",
          });
      return;
    }
    if (result.ec != std::errc{} || result.ptr != last) [[unlikely]] {
      ::hosq::detail::report_reader_error(
          ctx.reader(), ctx.reader_state(),
          ::hosq::detail::ErrorInfo{
              .code = ErrorCode::invalid_number,
              .offset = ctx.reader().current_token(ctx.reader_state()).offset,
              .detail = "decode::number_into could not consume scalar token",
          });
      return;
    }
  }
};

template <ReaderConcept R>
struct DecodeNumber : DecodeNumberDefault<R> {};
template <ReaderConcept R>
struct DecodeNumberInto : DecodeNumberIntoDefault<R> {};
template <ReaderConcept R>
struct DecodeBoolean;
template <ReaderConcept R>
struct DecodeStringView;
template <ReaderConcept R, class Pipeline, std::size_t N>
struct DecodeCollect;
template <ReaderConcept R, class Pipeline, std::size_t N>
struct DecodeCollectDefault;

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
  using view_step = NumberImpl;
  template <class Context>
  using input_type = hosq::detail::extension_input_t<
      ext::DecodeNumber<typename std::remove_cvref_t<Context>::reader_type>,
      ScalarView>;

  template <class Storage, ReaderConcept R, TryPhase Phase, class View>
    requires requires(const View& input, DecodeContext<Storage, R, Phase>& ctx) {
      ext::DecodeNumber<R>{}.template operator()<T>(input, ctx);
    }
  HOSQ_ALWAYS_INLINE auto operator()(const View& input,
                                     DecodeContext<Storage, R, Phase>& ctx) const
      -> std::remove_cvref_t<T> {
    return ext::DecodeNumber<R>{}.template operator()<T>(input, ctx);
  }
};

template <Number T>
struct NumberIntoImpl {
  using view_step = NumberIntoImpl;
  template <class Context>
  using input_type = hosq::detail::extension_input_t<
      ext::DecodeNumberInto<typename std::remove_cvref_t<Context>::reader_type>,
      ScalarView>;

  template <class Input>
  using storage_type = T;
  template <class Storage, ReaderConcept R, TryPhase Phase, class View>
    requires requires(const View& input, DecodeContext<Storage, R, Phase>& ctx) {
      ext::DecodeNumberInto<R>{}.template operator()<T>(input, ctx);
    }
  HOSQ_ALWAYS_INLINE auto operator()(const View& input,
                                     DecodeContext<Storage, R, Phase>& ctx) const
      -> void {
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
 private:
  template <class Terminal>
  static auto completion(bool matched) -> Terminal {
    static_assert(std::same_as<Terminal, void> ||
                  std::same_as<Terminal, MatchResult<void>>);
    if constexpr (!std::same_as<Terminal, void>) return Terminal{matched};
  }

 public:
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
      -> std::conditional_t<hosq::detail::MatchResultT<terminal_type>,
                            MatchResult<void>, void> {
    using actual_decoder_info = ::hosq::detail::pipeline_analysis_t<
        ::hosq::detail::PipelineVerificationMode::at_most_one_storage,
        DecodeContext<typename Storage::value_type, R, TryPhase::first>, void,
        pipeline_type>;
    using actual_decoder_terminal =
        ::hosq::detail::pipeline_terminal_t<actual_decoder_info>;

    if constexpr (!std::same_as<actual_decoder_terminal, void> &&
                  !hosq::detail::MatchResultT<actual_decoder_terminal>) {
      static_assert(
          sizeof(InvalidPipelineTerminal<Pipeline, actual_decoder_terminal>) ==
              0,
          "Decoder pipeline must have a terminal type of void or MatchResult "
          "for this Reader/Context");
    }
    static_assert(
        std::same_as<terminal_type, actual_decoder_terminal>,
        "Decoder pipeline terminal type must be void or MatchResult, and if "
        "matching, must be the same as the EachImpl terminal type");
    auto& reader = ctx.reader();
    auto& state = ctx.reader_state();
    using expected_storage_type = storage_type<ArrayView>;
    static_assert(
        std::same_as<Storage, expected_storage_type>,
        "decode::each<P> context storage must be std::vector<PipelineStorage>");

    while (true) {
      if (!hosq::next_token<Expect::next_element_or_end>(reader, state))
          [[unlikely]] {
        ctx.value().clear();
        return completion<std::conditional_t<
            hosq::detail::MatchResultT<terminal_type>, MatchResult<void>, void>>(
            false);
      }

      const auto& tok = reader.current_token(state);
      if (tok.type == TokenType::ArrayEnd) {
        return completion<std::conditional_t<
            hosq::detail::MatchResultT<terminal_type>, MatchResult<void>, void>>(
            true);
      }

      ctx.value().emplace_back();

      auto elem_ctx =
          DecodeContext<typename Storage::value_type, R, TryPhase::first>{
              ctx.value().back(), reader, state};

      if constexpr (hosq::detail::MatchResultT<actual_decoder_terminal>) {
        if (!pipeline_type::invoke(elem_ctx)) {
          ctx.value().clear();
          return completion<
              std::conditional_t<hosq::detail::MatchResultT<terminal_type>,
                                 MatchResult<void>, void>>(false);
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
 private:
  template <class Terminal>
  static auto completion(bool matched) -> Terminal {
    static_assert(std::same_as<Terminal, void> ||
                  std::same_as<Terminal, MatchResult<void>>);
    if constexpr (!std::same_as<Terminal, void>) return Terminal{matched};
  }

 public:
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
      -> std::conditional_t<hosq::detail::MatchResultT<terminal_type>,
                            MatchResult<void>, void> {
    using actual_decoder_info = ::hosq::detail::pipeline_analysis_t<
        ::hosq::detail::PipelineVerificationMode::at_most_one_storage,
        DecodeContext<typename Storage::value_type, R, TryPhase::first>, void,
        pipeline_type>;
    using actual_decoder_terminal =
        ::hosq::detail::pipeline_terminal_t<actual_decoder_info>;

    if constexpr (!std::same_as<actual_decoder_terminal, void> &&
                  !hosq::detail::MatchResultT<actual_decoder_terminal>) {
      static_assert(
          sizeof(InvalidPipelineTerminal<Pipeline, actual_decoder_terminal>) ==
              0,
          "Decoder pipeline must have a terminal type of void or MatchResult "
          "for this Reader/Context");
    }
    static_assert(
        std::same_as<terminal_type, actual_decoder_terminal>,
        "Decoder pipeline terminal type must be void or MatchResult, and if "
        "matching, must be the same as the EachImpl terminal type");
    auto& reader = ctx.reader();
    auto& state = ctx.reader_state();
    size_t index = 0;
    while (index < N) {
      if (!hosq::next_token<Expect::next_element_or_end>(reader, state))
          [[unlikely]] {
        return completion<std::conditional_t<
            hosq::detail::MatchResultT<terminal_type>, MatchResult<void>, void>>(
            false);
      }
      const auto& tok = reader.current_token(state);
      if (tok.type == TokenType::ArrayEnd) {
        ::hosq::detail::report_reader_error(
            reader, state,
            ::hosq::detail::ErrorInfo{
                .code = ErrorCode::invalid_length,
                .offset = tok.offset,
                .detail = "fixed-size decode::each array ended early",
            });
        return completion<std::conditional_t<
            hosq::detail::MatchResultT<terminal_type>, MatchResult<void>, void>>(
            false);
      }
      auto elem_ctx =
          DecodeContext<typename Storage::value_type, R, TryPhase::first>{
              ctx.value()[index], reader, state};
      if constexpr (hosq::detail::MatchResultT<terminal_type>) {
        if (!pipeline_type::invoke(elem_ctx)) {
          return completion<MatchResult<void>>(false);
        }
      } else {
        pipeline_type::invoke(elem_ctx);
      }
      index++;
    }
    if (!hosq::next_token<Expect::next_element_or_end>(reader, state))
        [[unlikely]] {
      return completion<std::conditional_t<
          hosq::detail::MatchResultT<terminal_type>, MatchResult<void>, void>>(
          false);
    }
    const auto& tok = reader.current_token(state);
    if (tok.type == TokenType::ArrayEnd) {
      return completion<std::conditional_t<
          hosq::detail::MatchResultT<terminal_type>, MatchResult<void>, void>>(
          true);
    }
    ::hosq::detail::report_reader_error(
        reader, state,
        ::hosq::detail::ErrorInfo{
            .code = ErrorCode::invalid_length,
            .offset = reader.current_token(state).offset,
            .detail = "fixed-size decode::each array has extra element",
        });
    return completion<std::conditional_t<
        hosq::detail::MatchResultT<terminal_type>, MatchResult<void>, void>>(
        false);
  }
};

template <auto KeyPipeline, auto ValuePipeline>
  requires(::hosq::detail::PipelineObject<decltype(KeyPipeline)> &&
           ::hosq::detail::PipelineObject<decltype(ValuePipeline)>)
struct EachImpl<KeyPipeline, ValuePipeline> {
 private:
  template <class Terminal>
  static auto completion(bool matched) -> Terminal {
    static_assert(std::same_as<Terminal, void> ||
                  std::same_as<Terminal, MatchResult<void>>);
    if constexpr (!std::same_as<Terminal, void>) return Terminal{matched};
  }

 public:
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
      std::conditional_t<hosq::detail::MatchResultT<key_terminal_type> ||
                             hosq::detail::MatchResultT<value_terminal_type>,
                         MatchResult<void>, void>;

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
    static_assert(
        std::same_as<actual_key_terminal, void> ||
            hosq::detail::MatchResultT<actual_key_terminal>,
        "Decoder pipeline must have a terminal type of void or MatchResult "
        "for this Reader/Context");
    static_assert(
        std::same_as<key_terminal_type, actual_key_terminal>,
        "Decoder pipeline terminal type must be void or MatchResult, and if "
        "matching, must be the same as the EachImpl terminal type");

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
    static_assert(
        std::same_as<actual_value_terminal, void> ||
            hosq::detail::MatchResultT<actual_value_terminal>,
        "Decoder pipeline must have a terminal type of void or MatchResult "
        "for this Reader/Context");
    static_assert(
        std::same_as<value_terminal_type, actual_value_terminal>,
        "Decoder pipeline terminal type must be void or MatchResult, and if "
        "matching, must be the same as the EachImpl terminal type");
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
      if (!hosq::next_token<Expect::next_member_or_end>(reader, state))
          [[unlikely]] {
        ctx.value().clear();
        return completion<pipeline_output_type>(false);
      }
      const auto& tok = reader.current_token(state);
      if (tok.type == TokenType::ObjectEnd) {
        return completion<pipeline_output_type>(true);
      }
      ctx.value().keys().emplace_back();
      auto key_ctx = DecodeContext<key_storage_type, R, TryPhase::first>{
          ctx.value().keys().back(), reader, state};

      if constexpr (hosq::detail::MatchResultT<actual_key_terminal>) {
        if (!key_pipeline_type::invoke(key_ctx)) {
          ctx.value().clear();
          return completion<pipeline_output_type>(false);
        }
      } else {
        key_pipeline_type::invoke(key_ctx);
      }
      if (!hosq::next_token<Expect::member_value>(reader, state)) [[unlikely]] {
        ctx.value().clear();
        return completion<pipeline_output_type>(false);
      }
      ctx.value().values().emplace_back();
      auto value_ctx = DecodeContext<value_storage_type, R, TryPhase::first>{
          ctx.value().values().back(), reader, state};
      if constexpr (hosq::detail::MatchResultT<actual_value_terminal>) {
        if (!value_pipeline_type::invoke(value_ctx)) {
          ctx.value().clear();
          return completion<pipeline_output_type>(false);
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
 private:
  template <class Terminal>
  static auto completion(bool matched) -> Terminal {
    static_assert(std::same_as<Terminal, void> ||
                  std::same_as<Terminal, MatchResult<void>>);
    if constexpr (!std::same_as<Terminal, void>) return Terminal{matched};
  }

 public:
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
      std::conditional_t<hosq::detail::MatchResultT<key_terminal_type> ||
                             hosq::detail::MatchResultT<value_terminal_type>,
                         MatchResult<void>, void>;

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
    static_assert(
        std::same_as<actual_key_terminal, void> ||
            hosq::detail::MatchResultT<actual_key_terminal>,
        "Decoder pipeline must have a terminal type of void or MatchResult "
        "for this Reader/Context");
    static_assert(
        std::same_as<key_terminal_type, actual_key_terminal>,
        "Decoder pipeline terminal type must be void or MatchResult, and if "
        "matching, must be the same as the EachImpl terminal type");

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
    static_assert(
        std::same_as<actual_value_terminal, void> ||
            hosq::detail::MatchResultT<actual_value_terminal>,
        "Decoder pipeline must have a terminal type of void or MatchResult "
        "for this Reader/Context");
    static_assert(
        std::same_as<value_terminal_type, actual_value_terminal>,
        "Decoder pipeline terminal type must be void or MatchResult, and if "
        "matching, must be the same as the EachImpl terminal type");
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
      if (!hosq::next_token<Expect::next_member_or_end>(reader, state))
          [[unlikely]] {
        return completion<pipeline_output_type>(false);
      }
      const auto& tok = reader.current_token(state);
      if (tok.type == TokenType::ObjectEnd) {
        ::hosq::detail::report_reader_error(
            reader, state,
            ::hosq::detail::ErrorInfo{
                .code = ErrorCode::invalid_length,
                .offset = tok.offset,
                .detail = "fixed-size decode::each object ended early",
            });
        return completion<pipeline_output_type>(false);
      }
      auto key_ctx = DecodeContext<key_storage_type, R, TryPhase::first>{
          ctx.value().keys()[index], reader, state};

      if constexpr (hosq::detail::MatchResultT<actual_key_terminal>) {
        if (!key_pipeline_type::invoke(key_ctx)) {
          return completion<pipeline_output_type>(false);
        }
      } else {
        key_pipeline_type::invoke(key_ctx);
      }
      if (!hosq::next_token<Expect::member_value>(reader, state)) [[unlikely]] {
        return completion<pipeline_output_type>(false);
      }
      auto value_ctx = DecodeContext<value_storage_type, R, TryPhase::first>{
          ctx.value().values()[index], reader, state};
      if constexpr (hosq::detail::MatchResultT<actual_value_terminal>) {
        if (!value_pipeline_type::invoke(value_ctx)) {
          return completion<pipeline_output_type>(false);
        }
      } else {
        value_pipeline_type::invoke(value_ctx);
      }
      index++;
    }
    if (!hosq::next_token<Expect::next_member_or_end>(reader, state))
        [[unlikely]] {
      return completion<pipeline_output_type>(false);
    }
    const auto& tok = reader.current_token(state);
    if (tok.type == TokenType::ObjectEnd) {
      return completion<pipeline_output_type>(true);
    }
    ::hosq::detail::report_reader_error(
        reader, state,
        ::hosq::detail::ErrorInfo{
            .code = ErrorCode::invalid_length,
            .offset = tok.offset,
            .detail = "fixed-size decode::each object has extra member",
        });
    return completion<pipeline_output_type>(false);
  }
};

template <class T>
struct CollectPayload {
  using type = T;
};
template <class T>
struct CollectPayload<MatchResult<T>> {
  using type = T;
};
template <class T>
using collect_payload_t = typename CollectPayload<T>::type;

template <class T, bool Matching>
struct CollectCompletion {
  using type = std::conditional_t<Matching, MatchResult<T>, T>;

 private:
  template <auto...>
  friend struct CollectImpl;
  template <ReaderConcept R, class Pipeline, std::size_t N>
  friend struct hosq::ext::DecodeCollectDefault;

  static auto finish(T value) -> type {
    if constexpr (Matching)
      return type{true, std::move(value)};
    else
      return value;
  }
  static auto reject() -> type
    requires Matching
  {
    return type{std::nullopt};
  }
};

template <class T>
auto collect_matched(const T& value) -> bool {
  if constexpr (hosq::detail::MatchResultT<T>)
    return value.matched();
  else
    return true;
}
template <class T>
decltype(auto) collect_value(T&& value) {
  if constexpr (hosq::detail::MatchResultT<T>)
    return std::move(value).value();
  else
    return std::forward<T>(value);
}

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

  using container_type = std::vector<collect_payload_t<terminal_type>>;
  using completion =
      CollectCompletion<container_type,
                        hosq::detail::MatchResultT<terminal_type>>;

  template <ReaderConcept R, TryPhase Phase, class Storage>
  auto operator()(const ArrayView&, DecodeContext<Storage, R, Phase>& ctx) const
      -> typename completion::type {
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

    auto ret = container_type{};

    while (true) {
      if (!hosq::next_token<Expect::next_element_or_end>(reader, state))
          [[unlikely]] {
        return completion::finish(std::move(ret));
      }

      const auto& tok = reader.current_token(state);
      if (tok.type == TokenType::ArrayEnd) {
        return completion::finish(std::move(ret));
      }

      DecodeContext<void, R, TryPhase::first> elem_ctx{reader, state};
      auto value = pipeline_type::invoke(elem_ctx);
      if constexpr (hosq::detail::MatchResultT<terminal_type>) {
        if (::hosq::detail::reader_error(reader, state) ||
            !collect_matched(value))
          return completion::reject();
      }
      ret.push_back(collect_value(std::move(value)));
    }
  }
};

template <auto Pipeline, size_t N>
  requires(::hosq::detail::PipelineObject<decltype(Pipeline)>)
struct CollectImpl<Pipeline, N> {
  using view_step = CollectImpl;
  static_assert(hosq::detail::PipelineObject<decltype(Pipeline)>,
                "decode::collect<P, N> requires P to be a hosq::Pipeline");

  using pipeline_type = std::remove_cvref_t<decltype(Pipeline)>;

  using info = ::hosq::detail::pipeline_analysis_t<
      ::hosq::detail::PipelineVerificationMode::no_storage,
      ::hosq::detail::PipelineDecodeAnalysisContext, void, pipeline_type>;

  using terminal_type = ::hosq::detail::pipeline_terminal_t<info>;

  static_assert(!std::same_as<terminal_type, void>,
                "decode::collect<P, N> requires P to return a value");

  template <class R>
  using extension = ext::DecodeCollect<R, pipeline_type, N>;

  template <class Context>
  using input_type = hosq::detail::extension_input_t<
      extension<typename std::remove_cvref_t<Context>::reader_type>, ArrayView>;

  template <ReaderConcept R, TryPhase Phase, class Storage, class View>
    requires requires(const View& input, DecodeContext<Storage, R, Phase>& ctx) {
      extension<R>{}(input, ctx);
    }
  auto operator()(const View& input, DecodeContext<Storage, R, Phase>& ctx) const
      -> typename CollectCompletion<
          std::array<collect_payload_t<terminal_type>, N>,
          hosq::detail::MatchResultT<terminal_type>>::type {
    return extension<R>{}(input, ctx);
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

  using container_type =
      ::hosq::Map<std::vector<collect_payload_t<key_terminal_type>>,
                  std::vector<collect_payload_t<value_terminal_type>>>;

  using completion =
      CollectCompletion<container_type,
                        hosq::detail::MatchResultT<key_terminal_type> ||
                            hosq::detail::MatchResultT<value_terminal_type>>;
  using output_type = typename completion::type;

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

    auto ret = container_type{};

    while (true) {
      if (!hosq::next_token<Expect::next_member_or_end>(reader, state))
          [[unlikely]] {
        return completion::finish(std::move(ret));
      }

      const auto& tok = reader.current_token(state);
      if (tok.type == TokenType::ObjectEnd) {
        return completion::finish(std::move(ret));
      }

      DecodeContext<void, R, TryPhase::first> key_ctx{reader, state};
      auto key = key_pipeline_type::invoke(key_ctx);
      if constexpr (hosq::detail::MatchResultT<key_terminal_type> ||
                    hosq::detail::MatchResultT<value_terminal_type>) {
        if (::hosq::detail::reader_error(reader, state) || !collect_matched(key))
          return completion::reject();
      }
      ret.keys().push_back(collect_value(std::move(key)));

      if (!hosq::next_token<Expect::member_value>(reader, state)) [[unlikely]] {
        return completion::finish(std::move(ret));
      }

      DecodeContext<void, R, TryPhase::first> value_ctx{reader, state};
      auto value = value_pipeline_type::invoke(value_ctx);
      if constexpr (hosq::detail::MatchResultT<key_terminal_type> ||
                    hosq::detail::MatchResultT<value_terminal_type>) {
        if (::hosq::detail::reader_error(reader, state) ||
            !collect_matched(value))
          return completion::reject();
      }
      ret.values().push_back(collect_value(std::move(value)));
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

  using container_type =
      ::hosq::Map<std::array<collect_payload_t<key_terminal_type>, N>,
                  std::array<collect_payload_t<value_terminal_type>, N>>;

  using completion =
      CollectCompletion<container_type,
                        hosq::detail::MatchResultT<key_terminal_type> ||
                            hosq::detail::MatchResultT<value_terminal_type>>;
  using output_type = typename completion::type;

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

    auto ret = container_type{};
    std::size_t index = 0;

    while (index < N) {
      if (!hosq::next_token<Expect::next_member_or_end>(reader, state))
          [[unlikely]] {
        return completion::finish(std::move(ret));
      }

      const auto& tok = reader.current_token(state);
      if (tok.type == TokenType::ObjectEnd) {
        ::hosq::detail::report_reader_error(
            reader, state,
            ::hosq::detail::ErrorInfo{
                .code = ErrorCode::invalid_length,
                .offset = tok.offset,
                .detail = "fixed-size decode::collect object ended early",
            });
        return completion::finish(std::move(ret));
      }

      DecodeContext<void, R, TryPhase::first> key_ctx{reader, state};
      auto key = key_pipeline_type::invoke(key_ctx);
      if constexpr (hosq::detail::MatchResultT<key_terminal_type> ||
                    hosq::detail::MatchResultT<value_terminal_type>) {
        if (::hosq::detail::reader_error(reader, state) || !collect_matched(key))
          return completion::reject();
      }
      ret.keys()[index] = collect_value(std::move(key));

      if (!hosq::next_token<Expect::member_value>(reader, state)) [[unlikely]] {
        return completion::finish(std::move(ret));
      }

      DecodeContext<void, R, TryPhase::first> value_ctx{reader, state};
      auto value = value_pipeline_type::invoke(value_ctx);
      if constexpr (hosq::detail::MatchResultT<key_terminal_type> ||
                    hosq::detail::MatchResultT<value_terminal_type>) {
        if (::hosq::detail::reader_error(reader, state) ||
            !collect_matched(value))
          return completion::reject();
      }
      ret.values()[index] = collect_value(std::move(value));

      ++index;
    }

    if (!hosq::next_token<Expect::next_member_or_end>(reader, state))
        [[unlikely]] {
      return completion::finish(std::move(ret));
    }

    const auto& tok = reader.current_token(state);
    if (tok.type == TokenType::ObjectEnd) {
      return completion::finish(std::move(ret));
    }

    ::hosq::detail::report_reader_error(
        reader, state,
        ::hosq::detail::ErrorInfo{
            .code = ErrorCode::invalid_length,
            .offset = tok.offset,
            .detail = "fixed-size decode::collect object has extra member",
        });
    return completion::finish(std::move(ret));
  }
};

struct BooleanImpl {
  using view_step = BooleanImpl;
  template <class Context>
  using input_type = hosq::detail::extension_input_t<
      ext::DecodeBoolean<typename std::remove_cvref_t<Context>::reader_type>,
      ScalarView>;
  template <class Storage, ReaderConcept R, TryPhase Phase, class View>
    requires requires(const View& input, DecodeContext<Storage, R, Phase>& ctx) {
      ext::DecodeBoolean<R>{}(input, ctx);
    }
  HOSQ_ALWAYS_INLINE auto operator()(const View& input,
                                     DecodeContext<Storage, R, Phase>& ctx) const
      -> bool {
    return ext::DecodeBoolean<R>{}(input, ctx);
  }
};

struct StringViewImpl {
  using view_step = StringViewImpl;
  template <class Context>
  using input_type = hosq::detail::extension_input_t<
      ext::DecodeStringView<typename std::remove_cvref_t<Context>::reader_type>,
      ScalarView>;
  template <class Storage, ReaderConcept R, TryPhase Phase, class View>
    requires requires(const View& input,
                      const DecodeContext<Storage, R, Phase>& ctx) {
      ext::DecodeStringView<R>{}(input, ctx);
    }
  HOSQ_ALWAYS_INLINE auto operator()(
      const View& input, const DecodeContext<Storage, R, Phase>& ctx) const
      -> std::string_view {
    return ext::DecodeStringView<R>{}(input, ctx);
  }
};

template <class T>
  requires ::hosq::detail::DecoderAcceptableT<T>
struct ToImpl {
  template <class Context>
  HOSQ_ALWAYS_INLINE auto operator()(const ObjectView&, Context& ctx) const
      -> T {
    using context_type = std::remove_cvref_t<Context>;
    using reader_type = typename context_type::reader_type;

    T value{};

    const auto error = ::hosq::Decoder<reader_type>::decode_value(
        value, ctx.reader(), ctx.reader_state());

    if (error.has_error()) return T{};

    return value;
  }

  template <class Context>
  HOSQ_ALWAYS_INLINE auto operator()(const ArrayView&, Context& ctx) const -> T {
    using context_type = std::remove_cvref_t<Context>;
    using reader_type = typename context_type::reader_type;

    T value{};

    const auto error = ::hosq::Decoder<reader_type>::decode_value(
        value, ctx.reader(), ctx.reader_state());

    if (error.has_error()) return T{};

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

namespace hosq::ext {
template <ReaderConcept R>
struct DecodeBooleanDefault {
  template <class Storage, TryPhase Phase>
  auto operator()(const ScalarView&, DecodeContext<Storage, R, Phase>& ctx) const
      -> bool {
    const Token& tok = ctx.reader().current_token(ctx.reader_state());
    const BytesView bytes = ctx.reader().input(ctx.reader_state());
    const std::string_view sv =
        bytes.as_string_view().substr(tok.offset, tok.length);
    if (sv == "true") return true;
    if (sv == "false") return false;
    ::hosq::detail::report_reader_error(
        ctx.reader(), ctx.reader_state(),
        ::hosq::detail::ErrorInfo{
            .code = ErrorCode::type_mismatch,
            .offset = ctx.reader().current_token(ctx.reader_state()).offset,
            .detail = "decode::boolean expected true or false literal",
        });
    return false;
  }
};
template <ReaderConcept R>
struct DecodeBoolean : DecodeBooleanDefault<R> {};
template <ReaderConcept R>
struct DecodeStringViewDefault {
  template <class Storage, TryPhase Phase>
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
template <ReaderConcept R>
struct DecodeStringView : DecodeStringViewDefault<R> {};
}  // namespace hosq::ext
namespace hosq::ext {
template <ReaderConcept R, class Pipeline, std::size_t N>
struct DecodeCollectDefault {
  using pipeline_type = Pipeline;
  using info =
      detail::pipeline_analysis_t<detail::PipelineVerificationMode::no_storage,
                                  detail::PipelineDecodeAnalysisContext, void,
                                  pipeline_type>;
  using terminal_type = detail::pipeline_terminal_t<info>;
  using container_type =
      std::array<decode::detail::collect_payload_t<terminal_type>, N>;
  using completion =
      decode::detail::CollectCompletion<container_type,
                                        detail::MatchResultT<terminal_type>>;
  template <TryPhase Phase, class Storage>
  auto operator()(const ArrayView&, DecodeContext<Storage, R, Phase>& ctx) const
      -> typename completion::type {
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

    container_type ret{};

    std::size_t index = 0;

    while (index < N) {
      if (!hosq::next_token<Expect::next_element_or_end>(reader, state))
          [[unlikely]] {
        return completion::finish(std::move(ret));
      }

      const auto& tok = reader.current_token(state);
      if (tok.type == TokenType::ArrayEnd) {
        ::hosq::detail::report_reader_error(
            reader, state,
            ::hosq::detail::ErrorInfo{
                .code = ErrorCode::invalid_length,
                .offset = tok.offset,
                .detail = "fixed-size decode::collect array ended early",
            });
        return completion::finish(std::move(ret));
      }

      DecodeContext<void, R, TryPhase::first> elem_ctx{reader, state};
      auto value = pipeline_type::invoke(elem_ctx);
      if constexpr (detail::MatchResultT<terminal_type>) {
        if (::hosq::detail::reader_error(reader, state) ||
            !decode::detail::collect_matched(value))
          return completion::reject();
      }
      ret[index] = decode::detail::collect_value(std::move(value));
      ++index;
    }

    if (!hosq::next_token<Expect::next_element_or_end>(reader, state))
        [[unlikely]] {
      return completion::finish(std::move(ret));
    }

    const auto& tok = reader.current_token(state);
    if (tok.type == TokenType::ArrayEnd) {
      return completion::finish(std::move(ret));
    }

    ::hosq::detail::report_reader_error(
        reader, state,
        ::hosq::detail::ErrorInfo{
            .code = ErrorCode::invalid_length,
            .offset = tok.offset,
            .detail = "fixed-size decode::collect array has extra element",
        });
    return completion::finish(std::move(ret));
  }
};
template <ReaderConcept R, class Pipeline, std::size_t N>
struct DecodeCollect : DecodeCollectDefault<R, Pipeline, N> {};
}  // namespace hosq::ext
