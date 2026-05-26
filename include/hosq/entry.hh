#pragma once

#include <type_traits>

#include "hosq/context.hh"
#include "hosq/detail/type_traits.hh"
#include "hosq/string_literal.hh"

namespace hosq {

template <class Key, class Value>
struct Entry;

/// A key–value pair where both Key and Value are Elem types.
///
/// `*_decoder_const_invokable` / `*_encoder_const_invokable` mirror the
/// underlying Elem's flags and gate the const/mutable overloads of decode_key /
/// decode_value / encode_key / encode_value at compile time.
template <detail::ElemT Key, detail::ElemT Value>
struct Entry<Key, Value> {
  using value_type = Value;
  using key_type = Key;
  static constexpr bool is_const_key = false;

  template <class R, TryPhase Phase>
  static constexpr bool key_decoder_const_invokable =
      Key::template decoder_const_invokable<
          DecodeContext<typename Key::storage_type, R, Phase>>;

  template <class R, TryPhase Phase>
  static constexpr bool value_decoder_const_invokable =
      Value::template decoder_const_invokable<
          DecodeContext<typename Value::storage_type, R, Phase>>;

  template <class W>
  static constexpr bool key_encoder_const_invokable =
      Key::template encoder_const_invokable<
          EncodeContext<typename Key::storage_type, W>>;

  template <class W>
  static constexpr bool value_encoder_const_invokable =
      Value::template encoder_const_invokable<
          EncodeContext<typename Value::storage_type, W>>;

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

template <StringLiteral Key, detail::ElemT Value>
struct Entry<std::integral_constant<decltype(Key), Key>, Value> {
  using value_type = Value;
  using key_type = decltype(Key);
  static constexpr bool is_const_key = true;

  template <class R, TryPhase Phase>
  static constexpr bool key_decoder_const_invokable = true;

  template <class R, TryPhase Phase>
  static constexpr bool value_decoder_const_invokable =
      Value::template decoder_const_invokable<
          DecodeContext<typename Value::storage_type, R, Phase>>;

  template <class W>
  static constexpr bool key_encoder_const_invokable = true;

  template <class W>
  static constexpr bool value_encoder_const_invokable =
      Value::template encoder_const_invokable<
          EncodeContext<typename Value::storage_type, W>>;

  Entry() = default;

  [[nodiscard]] static constexpr auto key() -> key_type { return Key; }

  [[nodiscard]] auto value() const -> const value_type& { return value_; }
  [[nodiscard]] auto value() -> value_type& { return value_; }

  [[nodiscard]] auto operator*() -> value_type& { return value_; }
  [[nodiscard]] auto operator*() const -> const value_type& { return value_; }

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
