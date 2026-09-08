#pragma once

#include "hosq/detail/compiler.hh"

#include <functional>
#include <tuple>
#include <type_traits>

#include "hosq/detail/as_tuple.hh"
#include "hosq/detail/type_traits.hh"
#include "hosq/writer_base.hh"

namespace hosq {

template <class W>
class Encoder;

template <WriterConcept W>
class Encoder<W> {
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

    (void)writer().write(WriteRequest{
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
  HOSQ_ALWAYS_INLINE constexpr auto writer() -> W& {
    return writer_ref_.get();
  }

  template <class T>
    requires(detail::ElemT<T> || detail::AsEntryTupleT<T>)
  [[nodiscard]]
  HOSQ_ALWAYS_INLINE auto encode_value(const T& t) -> bool {
    if constexpr (detail::ElemT<T>) {
      return encode_element(t);
    } else if constexpr (detail::AsEntryTupleT<T>) {
      return encode_as_entry_tuple(t);
    }
  }

  template <detail::ElemT T>
  [[nodiscard]]
  HOSQ_ALWAYS_INLINE auto encode_element(const T& t) -> bool {
    using Elem = std::remove_cvref_t<T>;
    using Storage = typename Elem::storage_type;
    auto& value = const_cast<Storage&>(t.value());  // NOLINT
    EncodeContext<Storage, W> ctx{value, writer()};
    t.encode(ctx);
    return !writer().error().has_error();
  }

  template <detail::AsEntryTupleT T>
  [[nodiscard]]
  HOSQ_ALWAYS_INLINE auto encode_as_entry_tuple(const T& t)
      -> bool {
    (void)writer().write(WriteRequest{
        .length = 0,
        .type = TokenType::ObjectBegin,
        .context = 0,
    });
    if (writer().error().has_error()) {
      return false;
    }

    bool ok = true;
    std::apply(
        [&](const auto&... entry) HOSQ_ALWAYS_INLINE_ATTR -> auto {
          ([&]() HOSQ_ALWAYS_INLINE_ATTR -> bool {
            ok = encode_one_member(entry);
            return !ok;
          }() || ...);
        },
        hosq::detail::as_tuple(t));
    if (!ok) {
      return false;
    }

    (void)writer().write(WriteRequest{
        .length = 0,
        .type = TokenType::ObjectEnd,
        .context = 0,
    });
    return !writer().error().has_error();
  }

  template <class Entry>
    requires(detail::EntryT<std::remove_cvref_t<Entry>>)
  [[nodiscard]]
  HOSQ_ALWAYS_INLINE auto encode_one_member(const Entry& entry)
      -> bool {
    if (!encode_entry_key(entry)) {
      return false;
    }
    return encode_entry_value(entry);
  }

  template <class Entry>
    requires(detail::EntryT<std::remove_cvref_t<Entry>>)
  [[nodiscard]]
  HOSQ_ALWAYS_INLINE auto encode_entry_key(const Entry& entry)
      -> bool {
    return encode_element(entry.key());
  }

  template <class Entry>
    requires(detail::EntryT<std::remove_cvref_t<Entry>>)
  [[nodiscard]]
  HOSQ_ALWAYS_INLINE auto encode_entry_value(const Entry& entry)
      -> bool {
    return encode_element(entry.value());
  }

  std::reference_wrapper<W> writer_ref_;
};

template <WriterConcept W, class T>
  requires(detail::ElemT<T> || detail::AsEntryTupleT<T>)
[[nodiscard]]
auto encode_into(const T& t, W& writer) -> bool {
  Encoder<W> encoder{writer};
  return encoder.encode(t);
}

}  // namespace hosq
