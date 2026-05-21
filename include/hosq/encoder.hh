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
  auto encode(const T& t) -> Result<void, Error> {
    auto result = encode_value(t);
    if (!result) {
      return unexpected(result.error());
    }

    auto ended = writer().write(WriteRequest{
        .length = 0,
        .type = TokenType::End,
        .context = 0,
    });
    if (!ended) {
      return unexpected(ended.error());
    }

    return {};
  }

  template <class T>
    requires(detail::ElemT<T> || detail::AsEntryTupleT<T>)
  [[nodiscard]]
  static auto encode_to_writer(const T& t, W& writer) -> Result<void, Error> {
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
  HOSQ_ENCODER_FUNCTION_ALWAYS_INLINE auto encode_value(const T& t)
      -> Result<void, Error> {
    if constexpr (detail::ElemT<T>) {
      return encode_element(t);
    } else if constexpr (detail::AsEntryTupleT<T>) {
      return encode_as_entry_tuple(t);
    }
  }

  template <detail::ElemT T>
  [[nodiscard]]
  HOSQ_ENCODER_FUNCTION_ALWAYS_INLINE auto encode_element(const T& t)
      -> Result<void, Error> {
    using Elem = std::remove_cvref_t<T>;
    using Storage = typename Elem::storage_type;
    auto& value = const_cast<Storage&>(t.value());  // NOLINT
    EncodeContext<Storage, W> ctx{value, writer()};
    return t.encode(ctx);
  }

  template <detail::AsEntryTupleT T>
  [[nodiscard]]
  HOSQ_ENCODER_FUNCTION_ALWAYS_INLINE auto encode_as_entry_tuple(const T& t)
      -> Result<void, Error> {
    auto begun = writer().write(WriteRequest{
        .length = 0,
        .type = TokenType::ObjectBegin,
        .context = 0,
    });
    if (!begun) {
      return unexpected(begun.error());
    }

    auto result = std::apply(
        [&](const auto&... entry)
            HOSQ_ENCODER_ALWAYS_INLINE_ATTR -> Result<void, Error> {
              Result<void, Error> current{};
              ([&]() HOSQ_ENCODER_ALWAYS_INLINE_ATTR -> bool {
                current = encode_one_member(entry);
                return !current;
              }() || ...);
              return current;
            },
        hosq::as_tuple(t));
    if (!result) {
      return unexpected(result.error());
    }

    auto ended = writer().write(WriteRequest{
        .length = 0,
        .type = TokenType::ObjectEnd,
        .context = 0,
    });
    if (!ended) {
      return unexpected(ended.error());
    }

    return {};
  }

  template <class Entry>
    requires(detail::EntryT<std::remove_cvref_t<Entry>>)
  [[nodiscard]]
  HOSQ_ENCODER_FUNCTION_ALWAYS_INLINE auto encode_one_member(const Entry& entry)
      -> Result<void, Error> {
    auto key_result = encode_entry_key(entry);
    if (!key_result) {
      return unexpected(key_result.error());
    }
    auto value_result = encode_entry_value(entry);
    if (!value_result) {
      return unexpected(value_result.error());
    }
    return {};
  }

  template <class Entry>
    requires(detail::EntryT<std::remove_cvref_t<Entry>>)
  [[nodiscard]]
  HOSQ_ENCODER_FUNCTION_ALWAYS_INLINE auto encode_entry_key(const Entry& entry)
      -> Result<void, Error> {
    return encode_element(entry.key());
  }

  template <class Entry>
    requires(detail::EntryT<std::remove_cvref_t<Entry>>)
  [[nodiscard]]
  HOSQ_ENCODER_FUNCTION_ALWAYS_INLINE auto encode_entry_value(const Entry& entry)
      -> Result<void, Error> {
    return encode_element(entry.value());
  }

  std::reference_wrapper<W> writer_ref_;
};

#undef HOSQ_ENCODER_ALWAYS_INLINE_ATTR
#undef HOSQ_ENCODER_FUNCTION_ALWAYS_INLINE

template <WriterConcept W, class T>
  requires(detail::ElemT<T> || detail::AsEntryTupleT<T>)
[[nodiscard]]
auto encode_into(const T& t, W& writer) -> Result<void, Error> {
  Encoder<W> encoder{writer};
  return encoder.encode(t);
}

}  // namespace hosq
