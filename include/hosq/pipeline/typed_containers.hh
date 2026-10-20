#pragma once

#include <algorithm>
#include <optional>
#include <ranges>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "hosq/decode_outcome.hh"
#include "hosq/element.hh"
#include "hosq/entry.hh"
#include "hosq/map.hh"
#include "hosq/mapping_value.hh"
#include "hosq/pipeline/encode.hh"
#include "hosq/pipeline/store.hh"
#include "hosq/pipeline/typed_decode.hh"
#include "hosq/pipeline_util.hh"
#include "hosq/try.hh"

namespace hosq::detail {

template <class T>
struct IsContainerTuple : std::false_type {};
template <class... T>
struct IsContainerTuple<std::tuple<T...>> : std::true_type {};

template <class T>
auto container_tuple(T& value) {
  if constexpr (IsContainerTuple<std::remove_cvref_t<T>>::value)
    return std::apply([](auto&... field) { return std::tie(field...); }, value);
  else
    return as_tuple(value);
}

template <class T, class Context>
auto container_error(Context& ctx, ErrorCode code, std::string_view detail)
    -> T {
  ctx.reader().report_error(
      ctx.reader_state(),
      Error{code, ctx.reader().current_token(ctx.reader_state()).offset,
            detail});
  return T{};
}

template <ElemT E, ReaderConcept R>
auto decode_container_into(E& element, R& reader, typename R::state_type& state)
    -> void {
  DecodeContext<typename E::storage_type, R, TryPhase::first> ctx{
      element.storage(), reader, state};
  (void)invoke_required_decode(element, ctx);
}

template <ElemT E, ReaderConcept R>
auto decode_container_element(R& reader, typename R::state_type& state) ->
    typename E::storage_type {
  E element;
  decode_container_into(element, reader, state);
  return std::move(element.storage());
}

template <ElemT E, class Value, WriterConcept W>
auto encode_container_element(const Value& value, W& writer) -> bool {
  if constexpr (std::same_as<typename E::encoder_type,
                             std::remove_cvref_t<decltype(noop_encoder)>>) {
    return encode_error(
        writer, ErrorCode::unsupported_type,
        "noop_encoder is reader-only; provide an encoder pipeline");
  } else if constexpr (!std::same_as<std::remove_cvref_t<Value>,
                                     typename E::storage_type>) {
    // vector<bool> exposes proxy references rather than bool references.
    const typename E::storage_type materialized(value);
    return encode_container_element<E>(materialized, writer);
  } else {
    // Match Encoder's const-facing interface while permitting mutable custom
    // contexts. Container encoders invoke the Elem pipeline, not storage
    // guessing.
    auto& storage = const_cast<typename E::storage_type&>(value);  // NOLINT
    EncodeContext<typename E::storage_type, W> ctx{storage, writer};
    E::encoder_type::invoke(value, ctx);
    return writer.error().is_ok();
  }
}

// Semantic equality for typed mapping keys. Objects compare by member rather
// than input order; nested arrays and aggregates need no user-defined
// operator==.
template <class T>
auto container_equal(const T& a, const T& b) -> bool {
  using U = std::remove_cvref_t<T>;
  if constexpr (ElemT<U>) {
    return container_equal(a.value(), b.value());
  } else if constexpr (EntryT<U> || TryT<U>) {
    return container_equal(a.value(), b.value());
  } else if constexpr (IsEncodeOptional<U>::value) {
    return a.has_value() == b.has_value() && (!a || container_equal(*a, *b));
  } else if constexpr (IsMappingValue<U>::value) {
    if (a.size() != b.size()) return false;
    for (const auto& [key, value] : a) {
      bool found = false;
      for (const auto& [other_key, other_value] : b)
        if (container_equal(key, other_key) &&
            container_equal(value, other_value)) {
          found = true;
          break;
        }
      if (!found) return false;
    }
    return true;
  } else if constexpr (IsEncodeMap<U>::value) {
    if (a.keys().size() != a.values().size() ||
        b.keys().size() != b.values().size() ||
        a.keys().size() != b.keys().size() ||
        a.values().size() != b.values().size())
      return false;
    for (std::size_t i = 0; i < a.keys().size(); ++i) {
      bool found = false;
      for (std::size_t j = 0; j < b.keys().size(); ++j)
        if (container_equal(a.keys()[i], b.keys()[j]) &&
            container_equal(a.values()[i], b.values()[j])) {
          found = true;
          break;
        }
      if (!found) return false;
    }
    return true;
  } else if constexpr (std::ranges::range<const U> &&
                       !std::convertible_to<const U&, std::string_view>) {
    return std::ranges::equal(a, b, [](const auto& x, const auto& y) {
      return container_equal(x, y);
    });
  } else if constexpr (requires {
                         { a == b } -> std::convertible_to<bool>;
                       }) {
    return a == b;
  } else {
    const auto at = container_tuple(a);
    const auto bt = container_tuple(b);
    return [&]<std::size_t... I>(std::index_sequence<I...>) {
      return (container_equal(std::get<I>(at), std::get<I>(bt)) && ...);
    }(std::make_index_sequence<std::tuple_size_v<decltype(at)>>{});
  }
}

template <class Keys>
auto duplicate_container_keys(const Keys& keys) -> bool {
  for (std::size_t i = 0; i < keys.size(); ++i)
    for (std::size_t j = 0; j < i; ++j)
      if (container_equal(keys[i], keys[j])) return true;
  return false;
}

}  // namespace hosq::detail

namespace hosq::decode::detail {

template <hosq::detail::ElemT E>
struct TypedArrayImpl {
  static_assert(
      hosq::detail::RequiredElemT<E>,
      "typed Array elements cannot be matching pipelines; use Try dispatch");
  template <class Context>
    requires(!std::is_const_v<Context>)
  auto operator()(Context& ctx) const -> std::vector<typename E::storage_type> {
    using Result = std::vector<typename E::storage_type>;
    auto& reader = ctx.reader();
    auto& state = ctx.reader_state();
    Result result;
    if (reader.error(state)) return result;
    if (reader.current_token(state).type != TokenType::ArrayBegin)
      return hosq::detail::container_error<Result>(ctx, ErrorCode::type_mismatch,
                                                   "typed array expected array");
    while (hosq::next_token<Expect::next_element_or_end>(reader, state)) {
      if (reader.current_token(state).type == TokenType::ArrayEnd) break;
      auto item = hosq::detail::decode_container_element<E>(reader, state);
      if (reader.error(state)) break;
      result.push_back(std::move(item));
    }
    return result;
  }
};

template <hosq::detail::ElemT K, hosq::detail::ElemT V>
struct TypedMappingImpl {
  static_assert(hosq::detail::RequiredElemT<K> && hosq::detail::RequiredElemT<V>,
                "typed Mapping keys/values cannot be matching pipelines");
  using result_type =
      MappingValue<typename K::storage_type, typename V::storage_type>;
  template <class Context>
    requires(!std::is_const_v<Context>)
  auto operator()(Context& ctx) const -> result_type {
    auto& reader = ctx.reader();
    auto& state = ctx.reader_state();
    result_type result;
    if (reader.error(state)) return result;
    if (reader.current_token(state).type != TokenType::ObjectBegin)
      return hosq::detail::container_error<result_type>(
          ctx, ErrorCode::type_mismatch, "typed mapping expected object");
    while (hosq::next_token<Expect::next_member_or_end>(reader, state)) {
      if (reader.current_token(state).type == TokenType::ObjectEnd) break;
      auto key = hosq::detail::decode_container_element<K>(reader, state);
      if (reader.error(state)) break;
      for (const auto& entry : result) {
        if (hosq::detail::container_equal(entry.first, key))
          return hosq::detail::container_error<result_type>(
              ctx, ErrorCode::duplicate_member,
              "typed mapping has duplicate key");
      }
      if (!hosq::next_token<Expect::member_value>(reader, state)) break;
      auto value = hosq::detail::decode_container_element<V>(reader, state);
      if (reader.error(state)) break;
      result.emplace_back(std::move(key), std::move(value));
    }
    return result;
  }
};

template <hosq::detail::ElemT E>
struct TypedOptionalImpl {
  static_assert(hosq::detail::RequiredElemT<E>,
                "optional values cannot be matching pipelines");
  template <class Context>
    requires(!std::is_const_v<Context>)
  auto operator()(Context& ctx) const
      -> std::optional<typename E::storage_type> {
    auto result = hosq::detail::decode_container_element<E>(ctx.reader(),
                                                            ctx.reader_state());
    if (ctx.reader().error(ctx.reader_state())) return std::nullopt;
    return result;
  }
};

}  // namespace hosq::decode::detail

namespace hosq::decode {
/// Value-returning strict container pipelines; append store::assign/once as
/// needed.
template <hosq::detail::ElemT E>
inline constexpr auto array = step<detail::TypedArrayImpl<E>>;
template <hosq::detail::ElemT K, hosq::detail::ElemT V>
inline constexpr auto mapping = step<detail::TypedMappingImpl<K, V>>;
template <hosq::detail::ElemT E>
inline constexpr auto optional = step<detail::TypedOptionalImpl<E>>;
}  // namespace hosq::decode

namespace hosq::encode::detail {

template <hosq::detail::ElemT E>
struct TypedArrayImpl {
  template <class Context>
    requires(!std::is_const_v<Context>)
  auto operator()(const std::vector<typename E::storage_type>& value,
                  Context& ctx) const -> void {
    auto& writer = ctx.writer();
    if (!hosq::detail::encode_token(writer, TokenType::ArrayBegin)) return;
    for (const auto& item : value)
      if (!hosq::detail::encode_container_element<E>(item, writer)) return;
    (void)hosq::detail::encode_token(writer, TokenType::ArrayEnd);
  }
};

template <hosq::detail::ElemT K, hosq::detail::ElemT V>
struct TypedMappingImpl {
  using value_type =
      MappingValue<typename K::storage_type, typename V::storage_type>;
  template <class Context>
    requires(!std::is_const_v<Context>)
  auto operator()(const value_type& value, Context& ctx) const -> void {
    auto& writer = ctx.writer();
    if (hosq::detail::duplicate_container_keys(value.entries() |
                                               std::views::keys)) {
      (void)hosq::detail::encode_error(writer, ErrorCode::duplicate_member,
                                       "typed mapping has duplicate key");
      return;
    }
    if (!hosq::detail::encode_token(writer, TokenType::ObjectBegin)) return;
    for (const auto& [key, item] : value)
      if (!hosq::detail::encode_container_element<K>(key, writer) ||
          !hosq::detail::encode_container_element<V>(item, writer))
        return;
    (void)hosq::detail::encode_token(writer, TokenType::ObjectEnd);
  }
};

template <hosq::detail::ElemT E>
struct TypedOptionalImpl {
  template <class Context>
    requires(!std::is_const_v<Context>)
  auto operator()(const std::optional<typename E::storage_type>& value,
                  Context& ctx) const -> void {
    if (!value) {
      (void)hosq::detail::encode_error(
          ctx.writer(), ErrorCode::invalid_object_state,
          "empty optional is missing and has no value to encode");
      return;
    }
    (void)hosq::detail::encode_container_element<E>(*value, ctx.writer());
  }
};

}  // namespace hosq::encode::detail
