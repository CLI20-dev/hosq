#pragma once

#include <cstddef>
#include <functional>

#include "hosq/reader_base.hh"
#include "hosq/writer_base.hh"

namespace hosq {

namespace detail {

struct AnyContext {
  AnyContext() = delete;
};

struct AnyEncodeContext {
  AnyEncodeContext() = delete;
};

struct AnyDecodeContext {
  AnyDecodeContext() = delete;
};

};  // namespace detail

enum class ScanPhase {
  normal,
  first,
};

template <ReaderConcept R>
struct DecodeContextBase {
  using reader_state_type = typename R::state_type;
  using input_type = typename R::input_type;

  explicit DecodeContextBase(R& reader, reader_state_type& state,
                             std::size_t occurrences_val = 0) noexcept
      : reader_(reader), reader_state_(state), occurrences_(occurrences_val) {}

  [[nodiscard]]
  auto reader() noexcept -> R& {
    return reader_.get();
  }

  [[nodiscard]]
  auto reader() const noexcept -> const R& {
    return reader_.get();
  }

  [[nodiscard]]
  auto reader_state() noexcept -> reader_state_type& {
    return reader_state_.get();
  }

  [[nodiscard]]
  auto reader_state() const noexcept -> const reader_state_type& {
    return reader_state_.get();
  }

  [[nodiscard]]
  auto input() const noexcept -> input_type {
    return reader().input(reader_state());
  }

  [[nodiscard]]
  auto occurrences() const noexcept -> std::size_t {
    return occurrences_;
  }

  auto set_occurrences(std::size_t value) noexcept -> void {
    occurrences_ = value;
  }

  auto increment_occurrences() noexcept -> void { ++occurrences_; }

 protected:
  std::reference_wrapper<R> reader_;                        // NOLINT
  std::reference_wrapper<reader_state_type> reader_state_;  // NOLINT
  std::size_t occurrences_{0};                              // NOLINT
};

template <class T, ReaderConcept R, ScanPhase Phase = ScanPhase::normal>
struct DecodeContext : public DecodeContextBase<R> {
  template <class, ReaderConcept, ScanPhase>
  friend struct DecodeContext;

  using base_type = DecodeContextBase<R>;
  using reader_state_type = typename base_type::reader_state_type;

  static constexpr auto first_scan = Phase;

  explicit DecodeContext(T& value, R& reader, reader_state_type& state,
                         std::size_t occurrences_val = 0) noexcept
      : base_type(reader, state, occurrences_val), elem_(value) {}

  template <ScanPhase OtherPhase>
    requires(Phase == ScanPhase::normal && OtherPhase == ScanPhase::first)
  /* implicit */ DecodeContext(
      const DecodeContext<T, R, OtherPhase>& other) noexcept
      : base_type(other.reader(), other.reader_state(), other.occurrences()),
        elem_(other.elem()) {}

  // Probe-only constructor.
  // Declared but intentionally not defined.
  // Used only in unevaluated requires expressions.
  /* implicit */ DecodeContext(detail::AnyContext);
  /* implicit */ DecodeContext(detail::AnyDecodeContext);

  [[nodiscard]]
  auto elem() noexcept -> T& {
    return elem_.get();
  }

  [[nodiscard]]
  auto elem() const noexcept -> T& {
    return elem_.get();
  }

 private:
  std::reference_wrapper<T> elem_;
};

template <ReaderConcept R, ScanPhase Phase>
struct DecodeContext<void, R, Phase> : public DecodeContextBase<R> {
  template <class, ReaderConcept, ScanPhase>
  friend struct DecodeContext;

  using base_type = DecodeContextBase<R>;
  using reader_state_type = typename base_type::reader_state_type;

  static constexpr auto first_scan = Phase;

  template <class T, ScanPhase OtherPhase>
    requires(!std::same_as<T, void> &&
             (Phase == ScanPhase::normal || OtherPhase == ScanPhase::first))
  /* implicit */ DecodeContext(
      const DecodeContext<T, R, OtherPhase>& other) noexcept
      : base_type(other.reader(), other.reader_state(), other.occurrences()) {}

  template <ScanPhase OtherPhase>
    requires(Phase == ScanPhase::normal && OtherPhase == ScanPhase::first)
  /* implicit */ DecodeContext(
      const DecodeContext<void, R, OtherPhase>& other) noexcept
      : base_type(other.reader(), other.reader_state(), other.occurrences()) {}

  // Probe-only constructor.
  // Declared but intentionally not defined.
  // Used only in unevaluated requires expressions.
  /* implicit */ DecodeContext(detail::AnyContext);
  /* implicit */ DecodeContext(detail::AnyDecodeContext);
};

template <class T, WriterConcept W>
struct EncodeContext {
  std::reference_wrapper<T> arg;
  std::reference_wrapper<W> writer;
  explicit EncodeContext(T& value, W& writer) : arg(value), writer(writer) {}

  // Probe-only constructor.
  // Declared but intentionally not defined.
  // Used only in unevaluated requires expressions.
  /* implicit */ EncodeContext(detail::AnyContext /* context */);
  /* implicit */ EncodeContext(detail::AnyEncodeContext /* context */);
};

template <WriterConcept W>
struct EncodeContext<void, W> {
  template <class T>
  /*implicit*/ EncodeContext(EncodeContext<T, W>& other)
      : writer(other.writer) {}

  // Probe-only constructor.
  // Declared but intentionally not defined.
  // Used only in unevaluated requires expressions.
  /* implicit */ EncodeContext(detail::AnyContext /* context */);
  /* implicit */ EncodeContext(detail::AnyEncodeContext /* context */);

  W* writer{nullptr};
};

}  // namespace hosq
