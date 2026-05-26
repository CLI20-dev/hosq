#pragma once

#include <tuple>
#include <type_traits>

#include "hosq/detail/as_tuple.hh"
#include "hosq/fwd.hh"

namespace hosq::detail {

/// Always-false helper for static_assert inside unreachable `constexpr if`
/// branches.
template <class Head>
static constexpr bool always_false = false;

// ----------------------------------------------------------------------
// Elem
// ----------------------------------------------------------------------
template <class T>
struct IsElem : std::false_type {};

template <auto Decoder, auto Encoder>
struct IsElem<::hosq::Elem<Decoder, Encoder>> : std::true_type {};

template <class T>
concept ElemT = IsElem<std::remove_cvref_t<T>>::value;

// ----------------------------------------------------------------------
// ElemTuple
// ----------------------------------------------------------------------
template <class Tuple>
struct ElemTuple : std::false_type {};

template <class... Ts>
struct ElemTuple<std::tuple<Ts...>>
    : std::bool_constant<(sizeof...(Ts) > 0) && (ElemT<Ts> && ...)> {};

template <class T>
concept ElemTupleT = ElemTuple<std::remove_cvref_t<T>>::value;

template <class T>
concept AsElemTupleT = requires(T t) {
  { hosq::detail::as_tuple(t) } -> ElemTupleT;
};

// ----------------------------------------------------------------------
// Entry
// ----------------------------------------------------------------------
template <class T>
struct IsEntry : std::false_type {};

template <class Key, class Value>
struct IsEntry<::hosq::Entry<Key, Value>> : std::true_type {};

template <class T>
concept EntryT = IsEntry<std::remove_cvref_t<T>>::value;

// ----------------------------------------------------------------------
// EntryTuple
// ----------------------------------------------------------------------
template <class Tuple>
struct EntryTuple : std::false_type {};

template <class T>
struct IsConstKeyEntry : std::false_type {};

template <StringLiteral K, detail::ElemT Value>

struct IsConstKeyEntry<::hosq::Entry<::hosq::Key<K>, Value>> : std::true_type {};

template <class T>
concept ConstKeyEntryT = IsConstKeyEntry<std::remove_cvref_t<T>>::value;

template <class... Ts>
struct EntryTuple<std::tuple<Ts...>>
    : std::bool_constant<(sizeof...(Ts) > 0) && (EntryT<Ts> && ...)> {};

template <class T>
concept EntryTupleT = EntryTuple<std::remove_cvref_t<T>>::value;

template <class T>
concept AsEntryTupleT = requires(T t) {
  { hosq::detail::as_tuple(t) } -> EntryTupleT;
};

template <class T>
struct IsTry : std::false_type {};

template <class T>
struct IsTry<::hosq::Try<T>> : std::true_type {};

template <class T>
concept TryT = IsTry<std::remove_cvref_t<T>>::value;

template <class T>
struct TryBase {};

template <class U>
struct TryBase<::hosq::Try<U>> {
  using type = U;
};

template <class T>
using try_base_t = typename TryBase<std::remove_cvref_t<T>>::type;

// ----------------------------------------------------------------------
// TryElemTuple
// ----------------------------------------------------------------------
template <class Tuple>
struct TryElemTuple : std::false_type {};

template <class... Ts>
  requires(sizeof...(Ts) > 0 && (TryT<Ts> && ...))
struct TryElemTuple<std::tuple<Ts...>>
    : std::bool_constant<(ElemT<try_base_t<Ts>> && ...)> {};

template <class T>
concept TryElemTupleT = TryElemTuple<std::remove_cvref_t<T>>::value;

template <class T>
concept AsTryElemTupleT = requires(T t) {
  { hosq::detail::as_tuple(t) } -> TryElemTupleT;
};

// ----------------------------------------------------------------------
// TryEntryTuple
// ----------------------------------------------------------------------
template <class Tuple>
struct TryEntryTuple : std::false_type {};

template <class... Ts>
  requires(sizeof...(Ts) > 0 && (TryT<Ts> && ...))
struct TryEntryTuple<std::tuple<Ts...>>
    : std::bool_constant<(EntryT<try_base_t<Ts>> && ...)> {};

template <class T>
concept TryEntryTupleT = TryEntryTuple<std::remove_cvref_t<T>>::value;

template <class T>
concept AsTryEntryTupleT = requires(T t) {
  { hosq::detail::as_tuple(t) } -> TryEntryTupleT;
};

// ----------------------------------------------------------------------
// DecoderAcceptableType
// ----------------------------------------------------------------------
template <class T>
struct DecoderAcceptableType : std::false_type {};

template <class T>
  requires(ElemT<T> || AsElemTupleT<T> || AsEntryTupleT<T> ||
           AsTryElemTupleT<T> || AsTryEntryTupleT<T>)
struct DecoderAcceptableType<T> : std::true_type {};

template <class T>
concept DecoderAcceptableT = DecoderAcceptableType<T>::value;

// ----------------------------------------------------------------------
// MatchResult
// ----------------------------------------------------------------------
template <class T>
struct IsMatchResult : std::false_type {};

template <class T>
struct IsMatchResult<MatchResult<T>> : std::true_type {
  using value_type = T;
};

template <class T>
concept MatchResultT = IsMatchResult<std::remove_cvref_t<T>>::value;

// ----------------------------------------------------------------------
// MatchResult
// ----------------------------------------------------------------------
template <class T, class = void>
struct GetAdditionalProperty {
  using type = std::tuple<>;
};

template <class T>
struct GetAdditionalProperty<T, std::void_t<typename T::additional_property>> {
  using type = typename T::additional_property;
};

template <class T>
using get_additional_property_t =
    typename GetAdditionalProperty<std::remove_cvref_t<T>>::type;

template <class Terminal>
  requires(std::same_as<Terminal, bool> || std::same_as<Terminal, void>)
[[nodiscard]] inline constexpr auto terminal_return(bool value) -> Terminal {
  if constexpr (std::same_as<Terminal, bool>) {
    return value;
  } else {
    static_cast<void>(value);
    return;
  }
}

}  // namespace hosq::detail
