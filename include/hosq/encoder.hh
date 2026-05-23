#pragma once

#include <cstdint>
#include <functional>
#include <tuple>
#include <type_traits>

#include "hosq/as_tuple.hh"
#include "hosq/element.hh"
#include "hosq/entry.hh"
#include "hosq/writer_base.hh"

namespace hosq {

#if defined(__GNUC__) || defined(__clang__)
#define HOSQ_ENCODER_ALWAYS_INLINE_ATTR __attribute__((always_inline))
#define HOSQ_ENCODER_FUNCTION_ALWAYS_INLINE inline __attribute__((always_inline))
#elif defined(_MSC_VER)
#define HOSQ_ENCODER_ALWAYS_INLINE_ATTR
#define HOSQ_ENCODER_FUNCTION_ALWAYS_INLINE __forceinline
#else
#define HOSQ_ENCODER_ALWAYS_INLINE_ATTR
#define HOSQ_ENCODER_FUNCTION_ALWAYS_INLINE inline
#endif

template <WriterConcept W>
class Encoder {
 public:
  Encoder(const Encoder&) = delete;
  Encoder(Encoder&&) = delete;
  auto operator=(const Encoder&) -> Encoder& = delete;
  auto operator=(Encoder&&) -> Encoder& = delete;
  explicit Encoder(W& writer) : writer_ref_(writer) {}
  ~Encoder() = default;

  template <class T>
    requires(detail::ElemT<T> || detail::AsEntryTupleT<T>)
  [[nodiscard]]
  auto encode(const T& t) -> bool {
    if (!encode_value(t)) {
      return false;
    }

    writer().write(WriteRequest{
        .length = 0,
        .type = TokenType::End,
        .context = 0,
    });
    return !writer().error().has_error();
  }

  template <class T>
    requires(detail::ElemT<T> || detail::AsEntryTupleT<T>)
  [[nodiscard]]
  static auto encode_to_writer(const T& t, W& writer) -> bool {
    Encoder encoder{writer};
    return encoder.encode_value(t);
  }

 private:
  [[nodiscard]]
  HOSQ_ENCODER_FUNCTION_ALWAYS_INLINE constexpr auto writer() -> W& {
    return writer_ref_.get();
  }

  template <class T>
    requires(detail::ElemT<T> || detail::AsEntryTupleT<T>)
  [[nodiscard]]
  HOSQ_ENCODER_FUNCTION_ALWAYS_INLINE auto encode_value(const T& t) -> bool {
    if constexpr (detail::ElemT<T>) {
      return encode_element(t);
    } else if constexpr (detail::AsEntryTupleT<T>) {
      return encode_as_entry_tuple(t);
    }
  }

  template <detail::ElemT T>
  [[nodiscard]]
  HOSQ_ENCODER_FUNCTION_ALWAYS_INLINE auto encode_element(const T& t) -> bool {
    using Elem = std::remove_cvref_t<T>;
    using Storage = typename Elem::storage_type;
    auto& value = const_cast<Storage&>(t.value());  // NOLINT
    EncodeContext<Storage, W> ctx{value, writer()};
    t.encode(ctx);
    return !writer().error().has_error();
  }

  template <detail::AsEntryTupleT T>
  [[nodiscard]]
  HOSQ_ENCODER_FUNCTION_ALWAYS_INLINE auto encode_as_entry_tuple(const T& t)
      -> bool {
    writer().write(WriteRequest{
        .length = 0,
        .type = TokenType::ObjectBegin,
        .context = 0,
    });
    if (writer().error().has_error()) {
      return false;
    }

    bool ok = true;
    std::apply(
        [&](const auto&... entry) HOSQ_ENCODER_ALWAYS_INLINE_ATTR {
          ([&]() HOSQ_ENCODER_ALWAYS_INLINE_ATTR -> bool {
            ok = encode_one_member(entry);
            return !ok;
          }() || ...);
        },
        hosq::as_tuple(t));
    if (!ok) {
      return false;
    }

    writer().write(WriteRequest{
        .length = 0,
        .type = TokenType::ObjectEnd,
        .context = 0,
    });
    return !writer().error().has_error();
  }

  template <class Entry>
    requires(detail::EntryT<std::remove_cvref_t<Entry>>)
  [[nodiscard]]
  HOSQ_ENCODER_FUNCTION_ALWAYS_INLINE auto encode_one_member(const Entry& entry)
      -> bool {
    if (!encode_entry_key(entry)) {
      return false;
    }
    return encode_entry_value(entry);
  }

  template <class Entry>
    requires(detail::EntryT<std::remove_cvref_t<Entry>>)
  [[nodiscard]]
  HOSQ_ENCODER_FUNCTION_ALWAYS_INLINE auto encode_entry_key(const Entry& entry)
      -> bool {
    return encode_element(entry.key());
  }

  template <class Entry>
    requires(detail::EntryT<std::remove_cvref_t<Entry>>)
  [[nodiscard]]
  HOSQ_ENCODER_FUNCTION_ALWAYS_INLINE auto encode_entry_value(const Entry& entry)
      -> bool {
    return encode_element(entry.value());
  }

  std::reference_wrapper<W> writer_ref_;
};

#undef HOSQ_ENCODER_ALWAYS_INLINE_ATTR
#undef HOSQ_ENCODER_FUNCTION_ALWAYS_INLINE

template <WriterConcept W, class T>
  requires(detail::ElemT<T> || detail::AsEntryTupleT<T>)
[[nodiscard]]
auto encode_into(const T& t, W& writer) -> bool {
  Encoder<W> encoder{writer};
  return encoder.encode(t);
}

}  // namespace hosq
