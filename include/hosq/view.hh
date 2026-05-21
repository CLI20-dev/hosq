#pragma once

#include <functional>
#include <string_view>

#include "hosq/context.hh"

namespace hosq {

namespace detail {

struct PipelineProbeReader {
  using input_type = std::string_view;

  struct state_type {};

  auto reset(state_type&, input_type) -> void {}

  auto next_token(state_type&) -> bool { return false; }

  auto error(const state_type&) const -> Error { return {}; }

  auto current_token(const state_type&) const -> const Token& {
    static constexpr auto token =
        Token{.offset = 0, .length = 0, .type = TokenType::End, .context = 0};
    return token;
  }

  auto input(const state_type&) const -> const input_type& {
    static constexpr auto empty = std::string_view{};
    return empty;
  }
};

static_assert(ReaderConcept<PipelineProbeReader>);

}  // namespace detail

template <ReaderConcept R, ScanPhase Phase>
struct ObjectView {
  using reader_type = R;
  using reader_state_type = typename R::state_type;

  static constexpr auto scan_phase = Phase;

  explicit ObjectView(R& reader, reader_state_type& state) noexcept
      : reader_(reader), reader_state_(state) {}

  [[nodiscard]] auto reader() noexcept -> R& { return reader_.get(); }
  [[nodiscard]] auto reader() const noexcept -> const R& {
    return reader_.get();
  }

  [[nodiscard]] auto reader_state() noexcept -> reader_state_type& {
    return reader_state_.get();
  }
  [[nodiscard]] auto reader_state() const noexcept -> const reader_state_type& {
    return reader_state_.get();
  }

 private:
  std::reference_wrapper<R> reader_;
  std::reference_wrapper<reader_state_type> reader_state_;
};

template <ReaderConcept R, ScanPhase Phase>
struct ArrayView {
  using reader_type = R;
  using reader_state_type = typename R::state_type;

  static constexpr auto scan_phase = Phase;

  explicit ArrayView(R& reader, reader_state_type& state) noexcept
      : reader_(reader), reader_state_(state) {}

  [[nodiscard]] auto reader() noexcept -> R& { return reader_.get(); }
  [[nodiscard]] auto reader() const noexcept -> const R& {
    return reader_.get();
  }

  [[nodiscard]] auto reader_state() noexcept -> reader_state_type& {
    return reader_state_.get();
  }
  [[nodiscard]] auto reader_state() const noexcept -> const reader_state_type& {
    return reader_state_.get();
  }

 private:
  std::reference_wrapper<R> reader_;
  std::reference_wrapper<reader_state_type> reader_state_;
};

}  // namespace hosq
