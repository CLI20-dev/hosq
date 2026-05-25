#pragma once

#include <cstddef>
#include <string_view>

#include "hosq/context.hh"
#include "hosq/error.hh"
#include "hosq/reader_base.hh"
#include "hosq/writer_base.hh"

namespace hosq::detail {

struct PipelineAnalysisReaderState {
  struct Checkpoint {};

  [[nodiscard]]
  auto checkpoint() const noexcept -> Checkpoint {
    return {};
  }

  auto rollback(const Checkpoint&) noexcept -> void {}

  Token current{
      .offset = 0,
      .length = 0,
      .type = TokenType::Scalar,
      .context = static_cast<std::uint8_t>(JsonTokenContext::literal),
  };
};

struct DummyInput {};

struct PipelineAnalysisReader {
  using state_type = PipelineAnalysisReaderState;
  using input_type = DummyInput;

  auto reset(state_type&, DummyInput) noexcept -> void {}

  auto next_token(state_type&) noexcept -> bool { return false; }

  auto report_error(const state_type&, Error) const noexcept -> void {}

  [[nodiscard]]
  auto error(const state_type&) const noexcept -> Error {
    return {};
  }

  [[nodiscard]]
  auto current_token(const state_type& state) const noexcept -> const Token& {
    return state.current;
  }

  [[nodiscard]]
  auto input(const state_type&) const noexcept -> BytesView {
    return {nullptr, 0};
  }
};

struct PipelineAnalysisWriter {
  using output_type = std::string_view;
  using buffer_type = std::string_view;

  auto write(const WriteRequest&) noexcept -> buffer_type { return {}; }

  auto commit(std::size_t) noexcept -> bool { return true; }

  [[nodiscard]]
  auto error() noexcept -> Error {
    return {};
  }

  [[nodiscard]]
  auto output() const noexcept -> output_type {
    return {};
  }
};

struct PipelineAnalysisStorage {
  PipelineAnalysisStorage() = delete;
};

using PipelineDecodeAnalysisContext =
    DecodeContext<PipelineAnalysisStorage, PipelineAnalysisReader,
                  TryPhase::first>;

using PipelineEncodeAnalysisContext =
    EncodeContext<PipelineAnalysisStorage, PipelineAnalysisWriter>;

static_assert(ReaderConcept<PipelineAnalysisReader>);
static_assert(ReaderStateConcept<PipelineAnalysisReaderState>);

}  // namespace hosq::detail

template <>
struct hosq::ReaderTraits<hosq::detail::PipelineAnalysisReader> {
  static constexpr Format format = Format::json;
  static constexpr bool is_binary_format = false;
};

template <>
struct hosq::WriterTraits<hosq::detail::PipelineAnalysisWriter> {
  static constexpr Format format = Format::json;
  static constexpr bool is_binary_format = false;
};
