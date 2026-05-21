#pragma once

#include <cstddef>
#include <functional>
#include <string_view>

#include "ser20/reader_base.hh"
#include "ser20/writer_base.hh"

namespace ser20 {

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

template <class T, ReaderConcept R, bool First = false>
struct DecodeContext;

template <class T, ReaderConcept R>
struct DecodeContext<T, R, true> : public DecodeContextBase<R> {
  template <class, ReaderConcept, bool>
  friend struct DecodeContext;

  using base_type = DecodeContextBase<R>;
  using reader_state_type = typename base_type::reader_state_type;

  static constexpr bool first_scan = true;

  explicit DecodeContext(T& value, R& reader, reader_state_type& state,
                         std::size_t occurrences_val = 0) noexcept
      : base_type(reader, state, occurrences_val), elem_(value) {}

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

template <class T, ReaderConcept R>
struct DecodeContext<T, R, false> : public DecodeContextBase<R> {
  template <class, ReaderConcept, bool>
  friend struct DecodeContext;

  using base_type = DecodeContextBase<R>;
  using reader_state_type = typename base_type::reader_state_type;

  static constexpr bool first_scan = false;

  explicit DecodeContext(T& value, R& reader, reader_state_type& state,
                         std::size_t occurrences_val = 0) noexcept
      : base_type(reader, state, occurrences_val), elem_(value) {}

  template <bool OtherFirst>
  /* implicit */ DecodeContext(
      const DecodeContext<T, R, OtherFirst>& other) noexcept
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

template <ReaderConcept R>
struct DecodeContext<void, R, true> : public DecodeContextBase<R> {
  template <class, ReaderConcept, bool>
  friend struct DecodeContext;

  using base_type = DecodeContextBase<R>;

  static constexpr bool first_scan = true;

  template <class T>
  /* implicit */ DecodeContext(const DecodeContext<T, R, true>& other) noexcept
      : base_type(other.reader(), other.reader_state(), other.occurrences()) {}

  // Probe-only constructor.
  // Declared but intentionally not defined.
  // Used only in unevaluated requires expressions.
  /* implicit */ DecodeContext(detail::AnyContext);
  /* implicit */ DecodeContext(detail::AnyDecodeContext);
};

template <ReaderConcept R>
struct DecodeContext<void, R, false> : public DecodeContextBase<R> {
  template <class, ReaderConcept, bool>
  friend struct DecodeContext;

  using base_type = DecodeContextBase<R>;

  static constexpr bool first_scan = false;

  template <class T, bool OtherFirst>
  /* implicit */ DecodeContext(
      const DecodeContext<T, R, OtherFirst>& other) noexcept
      : base_type(other.reader(), other.reader_state(), other.occurrences()) {}

  template <bool OtherFirst>
  /* implicit */ DecodeContext(
      const DecodeContext<void, R, OtherFirst>& other) noexcept
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

enum class ContextKind {
  decode,
  encode,
};

template <class C>
struct ContextTraits;

template <class T, ReaderConcept R>
struct ContextTraits<DecodeContext<T, R>> {
  static constexpr ContextKind kind = ContextKind::decode;
  using value_type = T;
  using reader_type = R;
};

template <ReaderConcept R>
struct ContextTraits<DecodeContext<void, R>> {
  static constexpr ContextKind kind = ContextKind::decode;
  using value_type = void;
  using reader_type = R;
};

template <class T, WriterConcept W>
struct ContextTraits<EncodeContext<T, W>> {
  static constexpr ContextKind kind = ContextKind::encode;
  using value_type = T;
  using writer_type = W;
};

template <WriterConcept W>
struct ContextTraits<EncodeContext<void, W>> {
  static constexpr ContextKind kind = ContextKind::encode;
  using value_type = void;
  using writer_type = W;
};

template <class C>
using context_traits_t = ContextTraits<std::remove_cvref_t<C>>;

template <class C>
concept DecodeContextLike = requires(C ctx) {
  typename context_traits_t<C>::value_type;
  typename context_traits_t<C>::reader_type;
  { ctx.reader.operator->() } -> ReaderConcept;
  { ctx.input } -> std::same_as<std::string_view>;
  { ctx.occurrences } -> std::convertible_to<std::size_t>;
} && context_traits_t<C>::kind == ContextKind::decode;

template <class C>
concept EncodeContextLike =
    requires {
      typename context_traits_t<C>::value_type;
      typename context_traits_t<C>::writer_type;
    } && context_traits_t<C>::kind == ContextKind::encode &&
    WriterConcept<typename context_traits_t<C>::writer_type>;

}  // namespace ser20
