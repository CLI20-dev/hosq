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

template <class T, ReaderConcept R>
struct DecodeContext {
  template <class, ReaderConcept>
  friend struct DecodeContext;

  explicit DecodeContext(T& value, R& reader)
      : reader_(reader), input_(reader.input()), elem_(value) {}

  // Probe-only constructor.
  // Declared but intentionally not defined.
  // Used only in unevaluated requires expressions.
  /* implicit */ DecodeContext(detail::AnyContext /* context */);
  /* implicit */ DecodeContext(detail::AnyDecodeContext /* context */);

  [[nodiscard]]
  auto reader() noexcept -> R& {
    return reader_.get();
  }

  [[nodiscard]]
  auto reader() const noexcept -> const R& {
    return reader_.get();
  }

  [[nodiscard]]
  auto elem() noexcept -> T& {
    return elem_.get();
  }

  [[nodiscard]]
  auto elem() const noexcept -> T& {
    return elem_.get();
  }

  [[nodiscard]]
  auto input() const noexcept -> std::string_view {
    return input_;
  }

 private:
  std::reference_wrapper<R> reader_;
  std::string_view input_{};
  std::reference_wrapper<T> elem_;
};

template <ReaderConcept R>
struct DecodeContext<void, R> {
  template <class, ReaderConcept>
  friend struct DecodeContext;

  template <class T>
  /* implicit */ DecodeContext(const DecodeContext<T, R>& other)
      : reader_(other.reader_), input_(other.input_) {}

  // Probe-only constructor.
  // Declared but intentionally not defined.
  // Used only in unevaluated requires expressions.
  /* implicit */ DecodeContext(detail::AnyContext /* context */);
  /* implicit */ DecodeContext(detail::AnyDecodeContext /* context */);

  [[nodiscard]]
  auto reader() noexcept -> R& {
    return reader_.get();
  }

  [[nodiscard]]
  auto reader() const noexcept -> const R& {
    return reader_.get();
  }

  [[nodiscard]]
  auto input() const noexcept -> std::string_view {
    return input_;
  }

 private:
  std::reference_wrapper<R> reader_;
  std::string_view input_{};
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
