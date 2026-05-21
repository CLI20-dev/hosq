#pragma once

#include <tuple>

#include "ser20/detail/as_tuple.hh"
#include "ser20/element.hh"

namespace ser20 {

/// A key–value pair where both Key and Value are Elem types.
///
/// `*_decoder_const_invokable` / `*_encoder_const_invokable` mirror the
/// underlying Elem's flags and gate the const/mutable overloads of decode_key /
/// decode_value / encode_key / encode_value at compile time.
template <detail::ElemT Key, detail::ElemT Value>
struct Entry {
  using value_type = Value;
  using key_type = Key;

  template <class R>
  static constexpr bool key_decoder_const_invokable =
      Key::template decoder_const_invokable<
          DecodeContext<typename Key::storage_type, R>>;

  template <class R>
  static constexpr bool value_decoder_const_invokable =
      Value::template decoder_const_invokable<
          DecodeContext<typename Value::storage_type, R>>;

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

  [[nodiscard]] auto operator*() -> value_type& { return value_; }
  [[nodiscard]] auto operator*() const -> const value_type& { return value_; }

  [[nodiscard]] auto operator->() -> value_type* { return &value_; }
  [[nodiscard]] auto operator->() const -> const value_type* { return &value_; }

 public:
  key_type key_{};      // NOLINT
  value_type value_{};  // NOLINT

  // ── decode_key
  // ───────────────────────────────────────────────────────────────
  template <ReaderConcept R>
    requires(!key_decoder_const_invokable<R>)
  [[nodiscard]] auto decode_key(R& reader) {
    return key().decode(reader);
  }

  template <ReaderConcept R>
    requires(key_decoder_const_invokable<R>)
  [[nodiscard]] auto decode_key(const R& reader) {
    return key().decode(reader);
  }

  // ── decode_value
  // ─────────────────────────────────────────────────────────────

  template <ReaderConcept R>
    requires(!value_decoder_const_invokable<R>)
  [[nodiscard]] auto decode_value(R& reader) {
    return value().decode(reader);
  }

  template <ReaderConcept R>
    requires(value_decoder_const_invokable<R>)
  [[nodiscard]] auto decode_value(const R& reader) {
    return value().decode(reader);
  }
};

namespace detail {

template <class T>
struct IsEntry : std::false_type {};

template <ElemT Key, ElemT Value>
struct IsEntry<::ser20::Entry<Key, Value>> : std::true_type {};

template <class T>
concept EntryT = IsEntry<std::remove_cvref_t<T>>::value;

template <class Tuple>
struct EntryTuple : std::false_type {};

template <class... Ts>
struct EntryTuple<std::tuple<Ts...>>
    : std::bool_constant<(sizeof...(Ts) > 0) && (EntryT<Ts> && ...)> {};

template <class T>
concept EntryTupleT = EntryTuple<std::remove_cvref_t<T>>::value;

template <class T>
concept AsEntryTupleT = requires(T t) {
  { ser20::detail::as_tuple(t) } -> EntryTupleT;
};

/**
 * @brief Prevents Entry from being decomposed as an aggregate schema.
 *
 * Entry is itself an aggregate, but semantically it is already one element of an
 * entry tuple. This overload is intentionally more constrained than the generic
 * aggregate `as_tuple` overload, so `ser20::as_tuple(entry)` selects this
 * overload instead of recursively decomposing Entry's implementation details.
 *
 * The overload is deleted because calling `as_tuple` on an Entry is a misuse of
 * the schema decomposition API.
 */
template <detail::EntryT T>
constexpr auto as_tuple(T&) = delete;
template <detail::EntryT T>
constexpr auto as_tuple(const T&) = delete;

}  // namespace detail

}  // namespace ser20
