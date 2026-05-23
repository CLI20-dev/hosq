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
#include "hosq/decoder.hh"
#include "hosq/detail/always_false.hh"
#include "hosq/detail/pipeline_analysis.hh"
#include "hosq/detail/pipeline_analysis_io.hh"
#include "hosq/error.hh"
#include "hosq/map.hh"
#include "hosq/pipeline.hh"
#include "hosq/view.hh"

namespace hosq::decode {

namespace detail {

template <class T>
concept Number = (std::integral<std::remove_cvref_t<T>> &&
                  !std::same_as<std::remove_cvref_t<T>, bool>) ||
                 std::floating_point<std::remove_cvref_t<T>>;

template <Number T>
struct NumberImpl {
  template <class Context>
  auto operator()(const std::string_view& input, const Context& ctx) const
      -> std::remove_cvref_t<T> {
    using Value = std::remove_cvref_t<T>;
    Value value{};
    const char* first = input.data();
    const char* last = input.data() + input.size();
    const auto result = std::from_chars(first, last, value);
    if (result.ec == std::errc::result_out_of_range) [[unlikely]] {
      ctx.reader().report_error(
          ctx.reader_state(),
          Error{
              .code = ErrorCode::out_of_range,
              .offset = ctx.reader().current_token(ctx.reader_state()).offset,
          });
      return static_cast<Value>(0);
    }
    if (result.ec != std::errc{} || result.ptr != last) [[unlikely]] {
      ctx.reader().report_error(
          ctx.reader_state(),
          Error{
              .code = ErrorCode::invalid_number,
              .offset = ctx.reader().current_token(ctx.reader_state()).offset,
          });
      return static_cast<Value>(0);
    }
    return value;
  }
};

struct IdentityImpl {
  template <class T>
  constexpr auto operator()(T&& value) const noexcept -> decltype(auto) {
    return std::forward<T>(value);
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

  // Required by the pipeline-analysis interface.
  //
  // For each<P>, storage is independent of the step input. The input is the
  // surrounding ArrayView; the actual storage is the element storage declared by
  // the inner pipeline P.
  template <class Input>
  using storage_type = ::hosq::detail::pipeline_storage_t<info>;

  template <ReaderConcept R, TryPhase Phase, class Storage>
  auto operator()(const ArrayView&, DecodeContext<Storage, R, Phase>& ctx) const
      -> std::conditional_t<std::same_as<terminal_type, bool>, bool, void> {
    using actual_decoder_info = ::hosq::detail::pipeline_analysis_t<
        ::hosq::detail::PipelineVerificationMode::at_most_one_storage,
        DecodeContext<Storage, R, TryPhase::first>, void, pipeline_type>;
    using actual_decoder_terminal =
        ::hosq::detail::pipeline_terminal_t<actual_decoder_info>;
    using expected_storage_type = storage_type<ArrayView>;

    static_assert(std::same_as<actual_decoder_terminal, void> ||
                      std::same_as<actual_decoder_terminal, bool>,
                  "Decoder pipeline must have a terminal type of void or bool "
                  "for this Reader/Context");
    static_assert(std::same_as<terminal_type, actual_decoder_terminal>,
                  "Decoder pipeline terminal type must be void or bool, and if "
                  "bool, must be the same as the EachImpl terminal type");
    static_assert(
        std::same_as<Storage, expected_storage_type>,
        "decode::each<P> context storage must match the storage declared by P");
    auto& reader = ctx.reader();
    auto& state = ctx.reader_state();

    if constexpr (std::same_as<terminal_type, bool>) {
      while (true) [[likely]] {
        if (!reader.next_token(state)) [[unlikely]] {
          return false;
        }
        const auto& tok = reader.current_token(state);
        if (tok.type == TokenType::ArrayEnd) {
          return true;
        }
        auto elem_ctx = DecodeContext<Storage, R, TryPhase::first>{
            ctx.elem(), reader, state};
        if (!pipeline_type::invoke(elem_ctx)) {
          return false;
        }
      }
    } else {
      while (true) [[likely]] {
        if (!reader.next_token(state)) [[unlikely]] {
          return;
        }
        const auto& tok = reader.current_token(state);
        if (tok.type == TokenType::ArrayEnd) {
          return;
        }
        auto elem_ctx = DecodeContext<Storage, R, TryPhase::first>{
            ctx.elem(), reader, state};
        pipeline_type::invoke(elem_ctx);
      }
    }
  }
};

template <auto KeyPipeline, auto ValuePipeline>
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
  using storage_type = ::hosq::Map<key_storage_type, value_storage_type>;

  using pipeline_output_type =
      std::conditional_t<std::same_as<key_terminal_type, bool> ||
                             std::same_as<value_terminal_type, bool>,
                         bool, void>;

  template <ReaderConcept R, TryPhase Phase, class Storage>
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

    if constexpr (std::same_as<actual_key_terminal, void> &&
                  std::same_as<actual_value_terminal, void>) {
      while (true) [[likely]] {
        if (!reader.next_token(state)) [[unlikely]] {
          return;
        }
        const auto& tok = reader.current_token(state);
        if (tok.type == TokenType::ObjectEnd) {
          return;
        }
        auto key_ctx = DecodeContext<key_storage_type, R, TryPhase::first>{
            ctx.elem().key_storage(), reader, state};
        key_pipeline_type::invoke(key_ctx);
        if (!reader.next_token(state)) [[unlikely]] {
          return;
        }
        auto value_ctx = DecodeContext<value_storage_type, R, TryPhase::first>{
            ctx.elem().value_storage(), reader, state};
        value_pipeline_type::invoke(value_ctx);
      }
    } else if constexpr (std::same_as<actual_key_terminal, bool> &&
                         std::same_as<actual_value_terminal, void>) {
      while (true) [[likely]] {
        if (!reader.next_token(state)) [[unlikely]] {
          return false;
        }
        const auto& tok = reader.current_token(state);
        if (tok.type == TokenType::ObjectEnd) {
          return true;
        }
        auto key_ctx = DecodeContext<key_storage_type, R, TryPhase::first>{
            ctx.elem().key_storage(), reader, state};
        if (!key_pipeline_type::invoke(key_ctx)) {
          return false;
        }
        if (!reader.next_token(state)) [[unlikely]] {
          return false;
        }
        auto value_ctx = DecodeContext<value_storage_type, R, TryPhase::first>{
            ctx.elem().value_storage(), reader, state};
        value_pipeline_type::invoke(value_ctx);
      }
    } else if constexpr (std::same_as<actual_key_terminal, void> &&
                         std::same_as<actual_value_terminal, bool>) {
      while (true) [[likely]] {
        if (!reader.next_token(state)) [[unlikely]] {
          return false;
        }
        const auto& tok = reader.current_token(state);
        if (tok.type == TokenType::ObjectEnd) {
          return true;
        }
        auto key_ctx = DecodeContext<key_storage_type, R, TryPhase::first>{
            ctx.elem().key_storage(), reader, state};
        key_pipeline_type::invoke(key_ctx);
        if (!reader.next_token(state)) [[unlikely]] {
          return false;
        }
        auto value_ctx = DecodeContext<value_storage_type, R, TryPhase::first>{
            ctx.elem().value_storage(), reader, state};
        if (!value_pipeline_type::invoke(value_ctx)) {
          return false;
        }
      }
    } else {
      while (true) [[likely]] {
        if (!reader.next_token(state)) [[unlikely]] {
          return false;
        }
        const auto& tok = reader.current_token(state);
        if (tok.type == TokenType::ObjectEnd) {
          return true;
        }
        auto key_ctx = DecodeContext<key_storage_type, R, TryPhase::first>{
            ctx.elem().key_storage(), reader, state};
        if (!key_pipeline_type::invoke(key_ctx)) {
          return false;
        }
        if (!reader.next_token(state)) [[unlikely]] {
          return false;
        }
        auto value_ctx = DecodeContext<value_storage_type, R, TryPhase::first>{
            ctx.elem().value_storage(), reader, state};
        if (!value_pipeline_type::invoke(value_ctx)) {
          return false;
        }
      }
    }
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

    while (true) [[likely]] {
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

    while (true) [[likely]] {
      if (!reader.next_token(state)) [[unlikely]] {
        return ret;
      }

      const auto& tok = reader.current_token(state);
      if (tok.type == TokenType::ObjectEnd) {
        return ret;
      }

      DecodeContext<void, R, TryPhase::first> key_ctx{reader, state};
      ret.keys.push_back(key_pipeline_type::invoke(key_ctx));

      if (!reader.next_token(state)) [[unlikely]] {
        return ret;
      }

      DecodeContext<void, R, TryPhase::first> value_ctx{reader, state};
      ret.values.push_back(value_pipeline_type::invoke(value_ctx));
    }
  }
};

struct BooleanImpl {
  template <ReaderConcept R>
  auto operator()(const std::string_view& sv, DecodeContext<void, R>& ctx) const
      -> bool {
    if (sv == "true") return true;
    if (sv == "false") return false;
    ctx.reader().report_error(
        ctx.reader_state(),
        Error{
            .code = ErrorCode::type_mismatch,
            .offset = ctx.reader().current_token(ctx.reader_state()).offset,
        });
    return false;
  }
};

struct StringViewImpl {
  auto operator()(const std::string_view& sv) const -> std::string_view {
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

/// Converts "true"/"false" to bool; returns type_mismatch for anything else.
inline constexpr auto boolean = step<detail::BooleanImpl>;

/// Passes string_view through unchanged; escape resolution is left to the
/// caller.
inline constexpr auto string_view = step<detail::StringViewImpl>;

/// Passes any value through unchanged.
inline constexpr auto identity = step<detail::IdentityImpl>;

template <auto F>
inline constexpr auto custom = step<detail::CustomImpl<F>>;

template <auto... Pipelines>
inline constexpr auto each = step<detail::EachImpl<Pipelines...>>;

template <auto... Pipelines>
inline constexpr auto collect = step<detail::CollectImpl<Pipelines...>>;

template <class T>
inline constexpr auto to = step<detail::ToImpl<T>>;

}  // namespace hosq::decode
