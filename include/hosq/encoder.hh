#pragma once

#include <functional>
#include <tuple>
#include <type_traits>

#include "hosq/detail/as_tuple.hh"
#include "hosq/detail/compiler.hh"
#include "hosq/detail/native_encode.hh"
#include "hosq/detail/type_traits.hh"
#include "hosq/entry.hh"
#include "hosq/pipeline_util.hh"
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
    requires(detail::DecoderAcceptableT<T>)
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
    return !::hosq::detail::writer_error(writer()).has_error();
  }

  template <class T>
    requires(detail::DecoderAcceptableT<T>)
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
    requires(detail::DecoderAcceptableT<T> || detail::TryT<T>)
  [[nodiscard]]
  HOSQ_ALWAYS_INLINE auto encode_value(const T& t) -> bool {
    if constexpr (detail::TryT<T>) {
      return encode_value(static_cast<const detail::try_base_t<T>&>(t));
    } else if constexpr (detail::ElemT<T>) {
      return encode_element(t);
    } else {
      return encode_as_tuple(t);
    }
  }

  template <detail::ElemT T>
  [[nodiscard]]
  HOSQ_ALWAYS_INLINE auto encode_element(const T& t) -> bool {
    using Elem = std::remove_cvref_t<T>;
    if constexpr (std::same_as<typename Elem::encoder_type,
                               std::remove_cvref_t<decltype(noop_encoder)>>) {
      return detail::encode_error(
          writer(), ErrorCode::unsupported_type,
          "noop_encoder is reader-only; provide an encoder pipeline");
    } else {
      using Storage = typename Elem::storage_type;
      auto& value = const_cast<Storage&>(t.value());  // NOLINT
      EncodeContext<Storage, W> ctx{value, writer()};
      t.encode(ctx);
      return !::hosq::detail::writer_error(writer()).has_error();
    }
  }

  template <class T>
    requires(detail::AsEntryTupleT<T> || detail::AsTryEntryTupleT<T> ||
             detail::AsElemTupleT<T> || detail::AsTryElemTupleT<T>)
  [[nodiscard]]
  HOSQ_ALWAYS_INLINE auto encode_as_tuple(const T& t) -> bool {
    constexpr bool object =
        detail::AsEntryTupleT<T> || detail::AsTryEntryTupleT<T>;
    (void)writer().write(WriteRequest{
        .length = 0,
        .type = object ? TokenType::ObjectBegin : TokenType::ArrayBegin,
        .context = 0,
    });
    if (::hosq::detail::writer_error(writer()).has_error()) {
      return false;
    }

    bool ok = true;
    std::apply(
        [&](const auto&... entry) HOSQ_ALWAYS_INLINE_ATTR -> auto {
          ([&]() HOSQ_ALWAYS_INLINE_ATTR -> bool {
            if constexpr (object)
              ok = encode_one_member(entry);
            else
              ok = encode_value(entry);
            return !ok;
          }() || ...);
        },
        hosq::detail::as_tuple(t));
    if (!ok) {
      return false;
    }

    (void)writer().write(WriteRequest{
        .length = 0,
        .type = object ? TokenType::ObjectEnd : TokenType::ArrayEnd,
        .context = 0,
    });
    return !::hosq::detail::writer_error(writer()).has_error();
  }

  template <class Entry>
    requires(detail::EntryT<std::remove_cvref_t<Entry>> || detail::TryT<Entry>)
  [[nodiscard]]
  HOSQ_ALWAYS_INLINE auto encode_one_member(const Entry& entry) -> bool {
    if constexpr (detail::TryT<Entry>) {
      return encode_one_member(
          static_cast<const detail::try_base_t<Entry>&>(entry));
    } else {
      if (!encode_entry_key(entry)) return false;
      return encode_entry_value(entry);
    }
  }

  template <class Entry>
    requires(detail::EntryT<std::remove_cvref_t<Entry>>)
  [[nodiscard]]
  HOSQ_ALWAYS_INLINE auto encode_entry_key(const Entry& entry) -> bool {
    if constexpr (detail::ConstKeyEntryT<Entry>) {
      static constexpr auto name = std::remove_cvref_t<Entry>::key_literal;
      return ext::EncodeValue<W>{}(name.view(), writer());
    } else {
      return encode_element(::hosq::detail::EntryAccess::key_elem(entry));
    }
  }

  template <class Entry>
    requires(detail::EntryT<std::remove_cvref_t<Entry>>)
  [[nodiscard]]
  HOSQ_ALWAYS_INLINE auto encode_entry_value(const Entry& entry) -> bool {
    return encode_element(::hosq::detail::EntryAccess::value_elem(entry));
  }

  std::reference_wrapper<W> writer_ref_;
};

template <WriterConcept W, class T>
  requires(detail::DecoderAcceptableT<T>)
[[nodiscard]]
auto encode_into(const T& t, W& writer) -> bool {
  Encoder<W> encoder{writer};
  return encoder.encode(t);
}

}  // namespace hosq
