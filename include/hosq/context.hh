#pragma once

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

template <ReaderConcept R>
struct DecodeContextBase {
  using reader_state_type = typename R::state_type;
  using input_type = typename R::input_type;

 protected:
  explicit DecodeContextBase(R& reader, reader_state_type& state) noexcept
      : reader_(reader), reader_state_(state) {}

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

 private:
  std::reference_wrapper<R> reader_;
  std::reference_wrapper<reader_state_type> reader_state_;
};

}  // namespace detail

enum class TryPhase {
  normal,
  first,
};

template <class T, ReaderConcept R, TryPhase Phase = TryPhase::normal>
struct DecodeContext : private detail::DecodeContextBase<R> {
  template <class, ReaderConcept, TryPhase>
  friend struct DecodeContext;

  using base_type = detail::DecodeContextBase<R>;
  using reader_state_type = typename base_type::reader_state_type;

  static constexpr auto scan_phase = Phase;

  using base_type::input;
  using base_type::reader;
  using base_type::reader_state;

  explicit DecodeContext(T& value, R& reader, reader_state_type& state) noexcept
      : base_type(reader, state), elem_(value) {}

  template <TryPhase OtherPhase>
    requires(Phase == OtherPhase)
  explicit DecodeContext(T& value,
                         DecodeContext<void, R, OtherPhase>& other) noexcept
      : base_type(other.reader(), other.reader_state()), elem_(value) {}

  template <TryPhase OtherPhase>
    requires(Phase == TryPhase::normal && OtherPhase == TryPhase::first)
  /* implicit */ DecodeContext(DecodeContext<T, R, OtherPhase>& other) noexcept
      : base_type(other.reader(), other.reader_state()), elem_(other.elem()) {}

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

  [[nodiscard]]
  auto value() noexcept -> T& {
    return elem();
  }

  [[nodiscard]]
  auto value() const noexcept -> T& {
    return elem();
  }

 private:
  std::reference_wrapper<T> elem_;
};

template <ReaderConcept R, TryPhase Phase>
struct DecodeContext<void, R, Phase> : private detail::DecodeContextBase<R> {
  template <class, ReaderConcept, TryPhase>
  friend struct DecodeContext;

  using base_type = detail::DecodeContextBase<R>;
  using reader_state_type = typename base_type::reader_state_type;

  static constexpr auto scan_phase = Phase;

  using base_type::input;
  using base_type::reader;
  using base_type::reader_state;

  explicit DecodeContext(R& reader, reader_state_type& state) noexcept
      : base_type(reader, state) {}

  template <class T, TryPhase OtherPhase>
    requires(!std::same_as<T, void> &&
             (Phase == TryPhase::normal || OtherPhase == TryPhase::first))
  /* implicit */ DecodeContext(DecodeContext<T, R, OtherPhase>& other) noexcept
      : base_type(other.reader(), other.reader_state()) {}

  template <TryPhase OtherPhase>
    requires(Phase == TryPhase::normal && OtherPhase == TryPhase::first)
  /* implicit */ DecodeContext(
      DecodeContext<void, R, OtherPhase>& other) noexcept
      : base_type(other.reader(), other.reader_state()) {}

  // Probe-only constructor.
  // Declared but intentionally not defined.
  // Used only in unevaluated requires expressions.
  /* implicit */ DecodeContext(detail::AnyContext);
  /* implicit */ DecodeContext(detail::AnyDecodeContext);
};

namespace detail {

template <WriterConcept W>
struct EncodeContextBase {
 protected:
  explicit EncodeContextBase(W& writer) noexcept : writer_(writer) {}

 public:
  [[nodiscard]]
  auto writer() noexcept -> W& {
    return writer_.get();
  }

  [[nodiscard]]
  auto writer() const noexcept -> const W& {
    return writer_.get();
  }

 protected:
  std::reference_wrapper<W> writer_;  // NOLINT
};

}  // namespace detail

template <class T, WriterConcept W>
struct EncodeContext : private detail::EncodeContextBase<W> {
  template <class, WriterConcept>
  friend struct EncodeContext;
  using base_type = detail::EncodeContextBase<W>;

  explicit EncodeContext(T& value, W& writer) noexcept
      : detail::EncodeContextBase<W>(writer), arg_(value) {}

  using base_type::writer;

  // Probe-only constructor.
  // Declared but intentionally not defined.
  // Used only in unevaluated requires expressions.
  /* implicit */ EncodeContext(detail::AnyContext);
  /* implicit */ EncodeContext(detail::AnyEncodeContext);

  [[nodiscard]]
  auto arg() noexcept -> T& {
    return arg_.get();
  }

  [[nodiscard]]
  auto arg() const noexcept -> T& {
    return arg_.get();
  }

 private:
  std::reference_wrapper<T> arg_;  // NOLINT
};

template <WriterConcept W>
struct EncodeContext<void, W> : private detail::EncodeContextBase<W> {
  template <class, WriterConcept>
  friend struct EncodeContext;

  using base_type = detail::EncodeContextBase<W>;

  explicit EncodeContext(W& writer) noexcept : base_type(writer) {}

  using base_type::writer;

  template <class T>
    requires(!std::same_as<T, void>)
  /* implicit */ EncodeContext(EncodeContext<T, W>& other) noexcept
      : base_type(other.writer()) {}

  // Probe-only constructor.
  // Declared but intentionally not defined.
  // Used only in unevaluated requires expressions.
  /* implicit */ EncodeContext(detail::AnyContext);
  /* implicit */ EncodeContext(detail::AnyEncodeContext);
};

namespace detail {

template <class T, ReaderConcept R, TryPhase Phase>
auto make_decode_context_from_const(
    T& value, const DecodeContext<void, R, Phase>& other) noexcept {
  auto& mutable_other =
      const_cast<DecodeContext<void, R, Phase>&>(other);  // NOLINT

  return DecodeContext<T, R, Phase>{value, mutable_other};
}

}  // namespace detail

}  // namespace hosq
