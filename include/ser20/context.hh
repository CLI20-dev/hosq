#pragma once

#include <cstddef>
#include <functional>
#include <string_view>

#include "ser20/reader_base.hh"
#include "ser20/writer_base.hh"

namespace ser20 {

template <class T, Reader R>
struct DecodeContext {
  explicit DecodeContext(T& value) : elem(value) {}
  std::reference_wrapper<T> elem;
  R* reader{nullptr};
  std::string_view input{};
  std::size_t occurrences{0};
};

template <Reader R>
struct DecodeContext<void, R> {
  template <class T>
  /*implicit*/ DecodeContext(const DecodeContext<T, R>& other)
      : reader(other.reader),
        input(other.input),
        occurrences(other.occurrences) {}
  DecodeContext() = default;
  R* reader{nullptr};
  std::string_view input{};
  std::size_t occurrences{0};
};

template <class T, Writer W>
struct EncodeContext {
  std::reference_wrapper<T> arg;
  explicit EncodeContext(T& value) : arg(value) {}
  W* writer{nullptr};
};

template <Writer W>
struct EncodeContext<void, W> {
  template <class T>
  /*implicit*/ EncodeContext(EncodeContext<T, W>& other)
      : writer(other.writer) {}
  EncodeContext() = default;

  W* writer{nullptr};
};

enum class ContextKind {
  decode,
  encode,
};

template <class C>
struct ContextTraits;

template <class T, Reader R>
struct ContextTraits<DecodeContext<T, R>> {
  static constexpr ContextKind kind = ContextKind::decode;
  using value_type = T;
  using reader_type = R;
};

template <Reader R>
struct ContextTraits<DecodeContext<void, R>> {
  static constexpr ContextKind kind = ContextKind::decode;
  using value_type = void;
  using reader_type = R;
};

template <class T, Writer W>
struct ContextTraits<EncodeContext<T, W>> {
  static constexpr ContextKind kind = ContextKind::encode;
  using value_type = T;
  using writer_type = W;
};

template <Writer W>
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
  { ctx.reader.operator->() } -> Reader;
  { ctx.input } -> std::same_as<std::string_view>;
  { ctx.occurrences } -> std::convertible_to<std::size_t>;
} && context_traits_t<C>::kind == ContextKind::decode;

template <class C>
concept EncodeContextLike =
    requires {
      typename context_traits_t<C>::value_type;
      typename context_traits_t<C>::writer_type;
    } && context_traits_t<C>::kind == ContextKind::encode &&
    Writer<typename context_traits_t<C>::writer_type>;

}  // namespace ser20
