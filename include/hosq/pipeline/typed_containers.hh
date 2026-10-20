#pragma once

#include <algorithm>
#include <array>
#include <optional>
#include <ranges>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "hosq/element.hh"
#include "hosq/decode_outcome.hh"
#include "hosq/entry.hh"
#include "hosq/map.hh"
#include "hosq/pipeline/encode.hh"
#include "hosq/pipeline/store.hh"
#include "hosq/pipeline/typed_decode.hh"
#include "hosq/pipeline_util.hh"
#include "hosq/try.hh"
#include "hosq/types_field.hh"

namespace hosq::detail {

template <class T>
struct IsContainerTuple : std::false_type {};
template <class... T>
struct IsContainerTuple<std::tuple<T...>> : std::true_type {};

template <class T>
auto record_tuple(T& value) {
  if constexpr (IsContainerTuple<std::remove_cvref_t<T>>::value)
    return std::apply([](auto&... field) { return std::tie(field...); }, value);
  else
    return as_tuple(value);
}

template <class T>
struct RecordEntry {
  using type = std::remove_cvref_t<T>;
};
template <class T>
struct RecordEntry<Try<T>> {
  using type = T;
};
template <class T>
using record_entry_t = typename RecordEntry<std::remove_cvref_t<T>>::type;

template <class Entry>
inline constexpr auto record_key = Entry::key();

template <class Schema>
struct RecordInfo {
  using tuple_type = decltype(record_tuple(std::declval<Schema&>()));
  static constexpr auto size = std::tuple_size_v<tuple_type>;
  static constexpr bool valid = []<std::size_t... I>(std::index_sequence<I...>) {
    return (
        ConstKeyEntryT<record_entry_t<std::tuple_element_t<I, tuple_type>>> &&
        ...);
  }(std::make_index_sequence<size>{});
  static_assert(
      valid, "Record schema requires constant-key Entry or Try<Entry> fields");
  static constexpr bool unique = []<std::size_t... I>(
                                     std::index_sequence<I...>) {
    constexpr auto names = std::array<std::string_view, size>{
        record_key<record_entry_t<std::tuple_element_t<I, tuple_type>>>.view()...};
    for (std::size_t a = 0; a < size; ++a)
      for (std::size_t b = a + 1; b < size; ++b)
        if (names[a] == names[b]) return false;
    return true;
  }(std::make_index_sequence<size>{});
  static_assert(unique, "Record schema has duplicate constant keys");
};

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
  const auto outcome = invoke_decode(element, ctx);
  (void)require_decode_match(outcome, reader, state);
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
// than input order; nested arrays and schemas need no user-defined operator==.
template <class T>
auto container_equal(const T& a, const T& b) -> bool {
  using U = std::remove_cvref_t<T>;
  if constexpr (ElemT<U>) {
    return container_equal(a.value(), b.value());
  } else if constexpr (EntryT<U> || TryT<U>) {
    return container_equal(a.value(), b.value());
  } else if constexpr (IsEncodeOptional<U>::value) {
    return a.has_value() == b.has_value() && (!a || container_equal(*a, *b));
  } else if constexpr (std::same_as<U, RawTextValue>) {
    std::string left, right;
    return resolve_raw_text(a, left) && resolve_raw_text(b, right) &&
           left == right;
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
    const auto at = record_tuple(a);
    const auto bt = record_tuple(b);
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
  using result_type = Map<std::vector<typename K::storage_type>,
                          std::vector<typename V::storage_type>>;
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
      for (std::size_t i = 0; i < result.keys().size(); ++i) {
        const typename K::storage_type& existing = result.keys()[i];
        if (hosq::detail::container_equal(existing, key))
          return hosq::detail::container_error<result_type>(
              ctx, ErrorCode::duplicate_member,
              "typed mapping has duplicate key");
      }
      if (!hosq::next_token<Expect::member_value>(reader, state)) break;
      auto value = hosq::detail::decode_container_element<V>(reader, state);
      if (reader.error(state)) break;
      result.keys().push_back(std::move(key));
      result.values().push_back(std::move(value));
    }
    return result;
  }
};

template <class Schema>
struct TypedRecordImpl {
  using info = hosq::detail::RecordInfo<Schema>;
  static_assert(info::valid && info::unique);
  template <class Context>
    requires(!std::is_const_v<Context>)
  auto operator()(Context& ctx) const -> Schema {
    auto& reader = ctx.reader();
    auto& state = ctx.reader_state();
    Schema result{};
    if (reader.error(state)) return result;
    if (reader.current_token(state).type != TokenType::ObjectBegin)
      return hosq::detail::container_error<Schema>(
          ctx, ErrorCode::type_mismatch, "typed record expected object");
    auto fields = hosq::detail::record_tuple(result);
    std::array<bool, info::size> seen{};
    while (hosq::next_token<Expect::next_member_or_end>(reader, state)) {
      if (reader.current_token(state).type == TokenType::ObjectEnd) {
        bool missing = false;
        [&]<std::size_t... I>(std::index_sequence<I...>) {
          ((missing |=
            !seen[I] && !hosq::detail::RecordFieldPresence<
                            std::remove_cvref_t<std::tuple_element_t<I, typename info::tuple_type>>>::allows_missing),
           ...);
        }(std::make_index_sequence<info::size>{});
        if (missing)
          return hosq::detail::container_error<Schema>(
              ctx, ErrorCode::missing_required,
              "typed record required member missing");
        return result;
      }
      // Resolve escapes before matching names, including TOML quoted keys.
      DecodeContext<void, typename Context::reader_type, TryPhase::first>
          key_ctx{reader, state};
      const auto name = (scan::scalar | decode::text).invoke(key_ctx);
      if (reader.error(state)) return result;
      bool matched = false;
      [&]<std::size_t... I>(std::index_sequence<I...>) {
        (
            [&] {
              using Entry = hosq::detail::record_entry_t<
                  std::tuple_element_t<I, typename info::tuple_type>>;
              if (matched || name != hosq::detail::record_key<Entry>.view())
                return;
              matched = true;
              if (seen[I]) {
                reader.report_error(state,
                                    Error{ErrorCode::duplicate_member,
                                          reader.current_token(state).offset,
                                          "typed record has duplicate member"});
                return;
              }
              seen[I] = true;
              if (!hosq::next_token<Expect::member_value>(reader, state)) return;
              auto& field = std::get<I>(fields).value();
              hosq::detail::decode_container_into(field, reader, state);
            }(),
            ...);
      }(std::make_index_sequence<info::size>{});
      if (!matched)
        return hosq::detail::container_error<Schema>(
            ctx, ErrorCode::unknown_member, "typed record unknown member");
      if (reader.error(state)) return result;
    }
    return result;
  }
};

template <hosq::detail::ElemT E>
struct TypedOptionalImpl {
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
template <class Schema>
inline constexpr auto record = step<detail::TypedRecordImpl<Schema>>;
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
  using value_type = Map<std::vector<typename K::storage_type>,
                         std::vector<typename V::storage_type>>;
  template <class Context>
    requires(!std::is_const_v<Context>)
  auto operator()(const value_type& value, Context& ctx) const -> void {
    auto& writer = ctx.writer();
    if (value.keys().size() != value.values().size()) {
      (void)hosq::detail::encode_error(writer, ErrorCode::size_mismatch,
                                       "typed mapping key/value lengths differ");
      return;
    }
    if (hosq::detail::duplicate_container_keys(value.keys())) {
      (void)hosq::detail::encode_error(writer, ErrorCode::duplicate_member,
                                       "typed mapping has duplicate key");
      return;
    }
    if (!hosq::detail::encode_token(writer, TokenType::ObjectBegin)) return;
    for (std::size_t i = 0; i < value.keys().size(); ++i)
      if (!hosq::detail::encode_container_element<K>(value.keys()[i], writer) ||
          !hosq::detail::encode_container_element<V>(value.values()[i], writer))
        return;
    (void)hosq::detail::encode_token(writer, TokenType::ObjectEnd);
  }
};

template <class Schema>
struct TypedRecordImpl {
  using info = hosq::detail::RecordInfo<Schema>;
  static_assert(info::valid && info::unique);
  template <class Context>
    requires(!std::is_const_v<Context>)
  auto operator()(const Schema& value, Context& ctx) const -> void {
    auto& writer = ctx.writer();
    if (!hosq::detail::encode_token(writer, TokenType::ObjectBegin)) return;
    bool ok = true;
    std::apply(
        [&](const auto&... field) {
          (
              [&] {
                using Field = std::remove_cvref_t<decltype(field)>;
                using Entry = hosq::detail::record_entry_t<Field>;
                using E = typename Entry::value_type;
                if (!ok) return;
                if (hosq::detail::RecordFieldPresence<Field>::omit(field)) return;
                ok = hosq::ext::EncodeValue<
                         std::remove_cvref_t<decltype(writer)>>{}(
                         hosq::detail::record_key<Entry>.view(), writer) &&
                     hosq::detail::encode_container_element<E>(
                         field.value().value(), writer);
              }(),
              ...);
        },
        hosq::detail::record_tuple(value));
    if (ok) (void)hosq::detail::encode_token(writer, TokenType::ObjectEnd);
  }
};

template <hosq::detail::ElemT E>
struct TypedOptionalImpl {
  template <class Context>
    requires(!std::is_const_v<Context>)
  auto operator()(const std::optional<typename E::storage_type>& value,
                  Context& ctx) const -> void {
    if (!value) {
      (void)hosq::detail::encode_error(ctx.writer(),
                                       ErrorCode::invalid_object_state,
                                       "empty optional is missing, not null; "
                                       "omit a Try record member or use Null");
      return;
    }
    (void)hosq::detail::encode_container_element<E>(*value, ctx.writer());
  }
};

}  // namespace hosq::encode::detail
