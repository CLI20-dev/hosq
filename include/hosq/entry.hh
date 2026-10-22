#pragma once

#include <type_traits>

#include "hosq/context.hh"
#include "hosq/detail/type_traits.hh"
#include "hosq/element.hh"
#include "hosq/pipeline/decode.hh"
#include "hosq/pipeline/encode.hh"
#include "hosq/pipeline/store.hh"
#include "hosq/string_literal.hh"

namespace hosq::detail {

// Entry accepts the same object/array schemas as Decoder. Adapt them through
// ordinary pipeline composition, without adding field or traversal policies.
template <class T>
struct EntryCodec {};

template <ElemT T>
struct EntryCodec<T> {
  using type = T;
};

template <class T>
  requires(!ElemT<T> && RequiredDecoderAcceptableT<T>)
struct EntryCodec<T> {
  static constexpr auto decoder = [] {
    if constexpr (AsEntryTupleT<T> || AsTryEntryTupleT<T>)
      return scan::object | decode::to<T> | store::assign;
    else
      return scan::array | decode::to<T> | store::assign;
  }();
  using type = Elem<decoder, encode::value>;
};

template <class T>
concept EntryCodecT = requires { typename EntryCodec<T>::type; };

}  // namespace hosq::detail

namespace hosq {

template <class Key, class Value>
struct Entry;

/// A key–value pair whose Key and Value are Elems or aggregate schemas.
/// Aggregate schemas are stored in Elems built from the existing traversal,
/// decode::to, assignment and value-encoding steps.
///
/// `*_decoder_const_invokable` / `*_encoder_const_invokable` mirror the
/// underlying Elem's flags and gate the const/mutable overloads of decode_key /
/// decode_value / encode_key / encode_value at compile time.
template <detail::EntryCodecT Key, detail::EntryCodecT Value>
struct Entry<Key, Value> {
  using value_type = typename detail::EntryCodec<Value>::type;
  using key_type = typename detail::EntryCodec<Key>::type;
  static constexpr bool is_const_key = false;

  template <class R, TryPhase Phase>
  static constexpr bool key_decoder_const_invokable =
      key_type::template decoder_const_invokable<
          DecodeContext<typename key_type::storage_type, R, Phase>>;

  template <class R, TryPhase Phase>
  static constexpr bool value_decoder_const_invokable =
      value_type::template decoder_const_invokable<
          DecodeContext<typename value_type::storage_type, R, Phase>>;

  template <class W>
  static constexpr bool key_encoder_const_invokable =
      key_type::template encoder_const_invokable<
          EncodeContext<typename key_type::storage_type, W>>;

  template <class W>
  static constexpr bool value_encoder_const_invokable =
      value_type::template encoder_const_invokable<
          EncodeContext<typename value_type::storage_type, W>>;

  Entry() = default;

  [[nodiscard]] auto key() const -> const key_type& { return key_; }
  [[nodiscard]] auto key() -> key_type& { return key_; }

  [[nodiscard]] auto value() const -> const value_type& { return value_; }
  [[nodiscard]] auto value() -> value_type& { return value_; }

  [[nodiscard]] auto operator*() -> typename value_type::storage_type& {
    return *value_;
  }
  [[nodiscard]] auto operator*() const -> const
      typename value_type::storage_type& {
    return *value_;
  }

  [[nodiscard]] auto operator->() -> typename value_type::storage_type* {
    return &*value_;
  }
  [[nodiscard]] auto operator->() const -> const
      typename value_type::storage_type* {
    return &*value_;
  }

 private:
  key_type key_{};      // NOLINT
  value_type value_{};  // NOLINT
};

template <StringLiteral Key, detail::EntryCodecT Value>
struct Entry<std::integral_constant<decltype(Key), Key>, Value> {
  using value_type = typename detail::EntryCodec<Value>::type;
  using key_type = decltype(Key);
  static constexpr bool is_const_key = true;

  template <class R, TryPhase Phase>
  static constexpr bool key_decoder_const_invokable = true;

  template <class R, TryPhase Phase>
  static constexpr bool value_decoder_const_invokable =
      value_type::template decoder_const_invokable<
          DecodeContext<typename value_type::storage_type, R, Phase>>;

  template <class W>
  static constexpr bool key_encoder_const_invokable = true;

  template <class W>
  static constexpr bool value_encoder_const_invokable =
      value_type::template encoder_const_invokable<
          EncodeContext<typename value_type::storage_type, W>>;

  Entry() = default;

  [[nodiscard]] static constexpr auto key() -> key_type { return Key; }

  [[nodiscard]] auto value() const -> const value_type& { return value_; }
  [[nodiscard]] auto value() -> value_type& { return value_; }

  [[nodiscard]] auto operator*() -> typename value_type::storage_type& {
    return *value_;
  }
  [[nodiscard]] auto operator*() const -> const
      typename value_type::storage_type& {
    return *value_;
  }

  [[nodiscard]] auto operator->() -> typename value_type::storage_type* {
    return &*value_;
  }
  [[nodiscard]] auto operator->() const -> const
      typename value_type::storage_type* {
    return &*value_;
  }

 private:
  value_type value_{};  // NOLINT
};

/**
 * @brief Prevents Entry from being decomposed as an aggregate schema.
 *
 * Entry is itself an aggregate, but semantically it is already one element of an
 * entry tuple. This overload is intentionally more constrained than the generic
 * aggregate `as_tuple` overload, so `hosq::as_tuple(entry)` selects this
 * overload instead of recursively decomposing Entry's implementation details.
 *
 * The overload is deleted because calling `as_tuple` on an Entry is a misuse of
 * the schema decomposition API.
 */
template <detail::EntryT T>
constexpr auto as_tuple(T&) = delete;

template <detail::EntryT T>
constexpr auto as_tuple(const T&) = delete;

}  // namespace hosq
