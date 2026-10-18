#pragma once

#include <functional>
#include <utility>

#include "hosq/reader_base.hh"
#include "hosq/writer_base.hh"

namespace hosq {

namespace detail {

// Per-decode state. A first hard failure freezes the deepest active location.
// This is passed explicitly and never stored in a Reader or global state.
struct PathDiagnostics {
  ErrorPath active;
  ErrorPath failure;
  bool captured = false;

  void capture(const Error& error) {
    if (error && !captured) {
      failure = active;
      failure.insert(failure.end(), error.path.begin(), error.path.end());
      captured = true;
    }
  }
  auto attach(Error error) -> Error {
    capture(error);
    if (error && captured) error.path = failure;
    return error;
  }
};

template <class R>
class DecodePathScope {
 public:
  DecodePathScope(PathDiagnostics* diagnostics, const R& reader,
                  const typename R::state_type& state, PathSegment segment)
      : diagnostics_(diagnostics), reader_(reader), state_(state) {
    if (diagnostics_) diagnostics_->active.push_back(std::move(segment));
  }
  template <class Key>
  void name_key(const Key& key) {
    if constexpr (std::convertible_to<const Key&, std::string_view>) {
      if (diagnostics_ && !reader_.error(state_)) {
        diagnostics_->active.back().name = std::string(std::string_view(key));
        diagnostics_->active.back().has_name = true;
      }
    }
  }
  DecodePathScope(const DecodePathScope&) = delete;
  auto operator=(const DecodePathScope&) -> DecodePathScope& = delete;
  ~DecodePathScope() {
    if (diagnostics_) {
      diagnostics_->capture(reader_.error(state_));
      diagnostics_->active.pop_back();
    }
  }

 private:
  PathDiagnostics* diagnostics_;
  const R& reader_;
  const typename R::state_type& state_;
};

template <ReaderConcept R>
struct DecodeContextBase {
  using reader_state_type = typename R::state_type;
  using reader_type = R;

 protected:
  explicit DecodeContextBase(R& reader, reader_state_type& state,
                             PathDiagnostics* diagnostics = nullptr) noexcept
      : reader_(reader), reader_state_(state), diagnostics_(diagnostics) {}

  [[nodiscard]] auto path_diagnostics() const noexcept -> PathDiagnostics* {
    return diagnostics_;
  }

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
  auto input() const noexcept -> BytesView {
    return reader().input(reader_state());
  }

 private:
  std::reference_wrapper<R> reader_;
  std::reference_wrapper<reader_state_type> reader_state_;
  PathDiagnostics* diagnostics_;
};

}  // namespace detail

enum class TryPhase {
  normal,
  first,
};

template <class T, class R, TryPhase Phase = TryPhase::normal>
struct DecodeContext {
  static_assert(ReaderConcept<R>,
                "DecodeContext reader type must satisfy ReaderConcept");
};

template <class T, ReaderConcept R, TryPhase Phase>
struct DecodeContext<T, R, Phase> : private detail::DecodeContextBase<R> {
  template <class, class, TryPhase>
  friend struct DecodeContext;

  using base_type = detail::DecodeContextBase<R>;
  using reader_type = typename base_type::reader_type;
  using reader_state_type = typename base_type::reader_state_type;

  static constexpr auto scan_phase = Phase;

  using base_type::input;
  using base_type::path_diagnostics;
  using base_type::reader;
  using base_type::reader_state;

  explicit DecodeContext(T& value, R& reader, reader_state_type& state,
                         detail::PathDiagnostics* diagnostics = nullptr) noexcept
      : base_type(reader, state, diagnostics), elem_(value) {}

  template <TryPhase OtherPhase>
    requires(Phase == OtherPhase)
  explicit DecodeContext(T& value,
                         DecodeContext<void, R, OtherPhase>& other) noexcept
      : base_type(other.reader(), other.reader_state(),
                  other.path_diagnostics()),
        elem_(value) {}

  template <TryPhase OtherPhase>
    requires(Phase == TryPhase::normal && OtherPhase == TryPhase::first)
  /* implicit */ DecodeContext(DecodeContext<T, R, OtherPhase>& other) noexcept
      : base_type(other.reader(), other.reader_state(),
                  other.path_diagnostics()),
        elem_(other.elem()) {}

  [[nodiscard]]
  auto value() noexcept -> T& {
    return elem_.get();
  }

  [[nodiscard]]
  auto value() const noexcept -> T& {
    return elem_.get();
  }

 private:
  std::reference_wrapper<T> elem_;
};

template <ReaderConcept R, TryPhase Phase>
struct DecodeContext<void, R, Phase> : private detail::DecodeContextBase<R> {
  template <class, class, TryPhase>
  friend struct DecodeContext;

  using base_type = detail::DecodeContextBase<R>;
  using reader_type = typename base_type::reader_type;
  using reader_state_type = typename base_type::reader_state_type;

  static constexpr auto scan_phase = Phase;

  using base_type::input;
  using base_type::path_diagnostics;
  using base_type::reader;
  using base_type::reader_state;

  explicit DecodeContext(R& reader, reader_state_type& state,
                         detail::PathDiagnostics* diagnostics = nullptr) noexcept
      : base_type(reader, state, diagnostics) {}

  template <class T, TryPhase OtherPhase>
    requires(!std::same_as<T, void> &&
             (Phase == TryPhase::normal || OtherPhase == TryPhase::first))
  /* implicit */ DecodeContext(DecodeContext<T, R, OtherPhase>& other) noexcept
      : base_type(other.reader(), other.reader_state(),
                  other.path_diagnostics()) {}

  template <TryPhase OtherPhase>
    requires(Phase == TryPhase::normal && OtherPhase == TryPhase::first)
  /* implicit */ DecodeContext(
      DecodeContext<void, R, OtherPhase>& other) noexcept
      : base_type(other.reader(), other.reader_state(),
                  other.path_diagnostics()) {}
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
  auto writer() const noexcept -> W& {
    return writer_.get();
  }

 protected:
  std::reference_wrapper<W> writer_;  // NOLINT
};

}  // namespace detail

template <class T, class W>
struct EncodeContext {
  static_assert(WriterConcept<W>,
                "EncodeContext writer type must satisfy WriterConcept");
};

template <class T, WriterConcept W>
struct EncodeContext<T, W> : private detail::EncodeContextBase<W> {
  template <class, class>
  friend struct EncodeContext;
  using base_type = detail::EncodeContextBase<W>;

  explicit EncodeContext(T& value, W& writer) noexcept
      : detail::EncodeContextBase<W>(writer), arg_(value) {}

  using base_type::writer;

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
};

}  // namespace hosq
