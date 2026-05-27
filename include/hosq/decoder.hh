#pragma once

#include <concepts>
#include <cstddef>
#include <tuple>
#include <type_traits>
#include <utility>

#include "hosq/context.hh"
#include "hosq/detail/as_tuple.hh"
#include "hosq/detail/type_traits.hh"
#include "hosq/meta_match.hh"
#include "hosq/pipeline/decode.hh"
#include "hosq/pipeline/scan.hh"
#include "hosq/reader_base.hh"

namespace hosq {

#if defined(__GNUC__) || defined(__clang__)
#define HOSQ_ALWAYS_INLINE_ATTR __attribute__((always_inline))
#define HOSQ_FUNCTION_ALWAYS_INLINE inline __attribute__((always_inline))
#elif defined(_MSC_VER)
#define HOSQ_ALWAYS_INLINE_ATTR
#define HOSQ_FUNCTION_ALWAYS_INLINE __forceinline
#else
#define HOSQ_ALWAYS_INLINE_ATTR
#define HOSQ_FUNCTION_ALWAYS_INLINE inline
#endif

template <class R>
class Decoder {
  static_assert(ReaderConcept<R>, "Decoder requires a ReaderConcept");
};

template <ReaderConcept R>
class Decoder<R> {
 public:
  Decoder(const Decoder&) = delete;
  Decoder(Decoder&&) = delete;
  auto operator=(const Decoder&) -> Decoder& = delete;
  auto operator=(Decoder&&) -> Decoder& = delete;
  Decoder() = default;
  ~Decoder() = default;

  using reader_type = R;
  using input_type = typename reader_type::input_type;
  using reader_state_type = typename reader_type::state_type;

  template <class T>
    requires detail::DecoderAcceptableT<T>
  [[nodiscard]]
  auto decode(T& t, input_type input) -> Error {
    reader_.reset(reader_state_, input);
    if (!reader_.next_token(reader_state_)) [[unlikely]] {
      return reader_.error(reader_state_);
    }
    return decode_impl(t, reader_, reader_state_);
  }

  template <detail::ElemT T, TryPhase Phase = TryPhase::normal>
  [[nodiscard]]
  static auto make_decode_context(T& t, R& reader, reader_state_type& state) {
    return DecodeContext<typename T::storage_type, R, Phase>{t.storage(), reader,
                                                             state};
  }

  template <class T>
    requires detail::DecoderAcceptableT<T>
  [[nodiscard]]
  static auto decode_nested(T& t, reader_type& reader,
                            reader_state_type& reader_state) -> Error {
    return decode_impl(t, reader, reader_state);
  }

 private:
  template <detail::ElemT T>
  [[nodiscard]]
  static auto decode_impl(T& t, reader_type& reader,
                          reader_state_type& reader_state) -> Error {
    auto ctx = make_decode_context(t, reader, reader_state);
    t.decode(ctx);
    return reader.error(reader_state);
  }

  template <detail::AsElemTupleT T>
  [[nodiscard]]
  static auto decode_impl(T& t, reader_type& reader,
                          reader_state_type& reader_state) -> Error {
    if (reader.current_token(reader_state).type != TokenType::ArrayBegin)
        [[unlikely]] {
      return Error{
          .code = ErrorCode::type_mismatch,
          .offset = reader.current_token(reader_state).offset,
          .detail = "expected array while decoding tuple",
      };
    }
    std::apply(
        [&](auto&... elem) HOSQ_ALWAYS_INLINE_ATTR -> void {
          ([&]() HOSQ_ALWAYS_INLINE_ATTR -> bool {
            if (!reader.next_token(reader_state)) [[unlikely]] {
              return false;
            }
            auto ctx = make_decode_context(elem, reader, reader_state);
            elem.decode(ctx);
            // If decode puts the reader into an error state, subsequent
            // next_token() is a noop and the error is reported below.
            return true;
          }() && ...);
        },
        detail::as_tuple(t));
    if (!reader.next_token(reader_state)) [[unlikely]] {
      return reader.error(reader_state);
    }
    const auto& tok = reader.current_token(reader_state);
    if (tok.type != TokenType::ArrayEnd) [[unlikely]] {
      return Error{
          .code = ErrorCode::size_mismatch,
          .offset = tok.offset,
          .detail = "tuple has fewer elements than input array",
      };
    }
    return Error::ok();
  }

  template <detail::EntryT T, class ReaderT, class StateT>
  [[nodiscard]]
  static HOSQ_FUNCTION_ALWAYS_INLINE auto decode_entry(T& t, ReaderT& reader,
                                                       StateT& reader_state)
      -> bool {
    if (!reader.next_token(reader_state)) [[unlikely]] {
      return false;
    }
    auto key_ctx = make_decode_context(t.key(), reader, reader_state);
    t.key().decode(key_ctx);
    if (!reader.next_token(reader_state)) [[unlikely]] {
      return false;
    }
    auto value_ctx = make_decode_context(t.value(), reader, reader_state);
    t.value().decode(value_ctx);
    return true;
  }

  template <detail::AsEntryTupleT T>
  [[nodiscard]]
  static auto decode_impl(T& t, reader_type& reader,
                          reader_state_type& reader_state) -> Error {
    if (reader.current_token(reader_state).type != TokenType::ObjectBegin)
        [[unlikely]] {
      return Error{
          .code = ErrorCode::type_mismatch,
          .offset = reader.current_token(reader_state).offset,
          .detail = "expected object while decoding entry tuple",
      };
    }
    std::apply(
        [&](auto&... entry) HOSQ_ALWAYS_INLINE_ATTR -> void {
          ([&]() HOSQ_ALWAYS_INLINE_ATTR -> bool {
            if (!decode_entry(entry, reader, reader_state)) [[unlikely]] {
              return false;
            }
            // If decode puts the reader into an error state, subsequent
            // next_token() is a noop and the error is reported below.
            return true;
          }() && ...);
        },
        detail::as_tuple(t));
    if (!reader.next_token(reader_state)) [[unlikely]] {
      return reader.error(reader_state);
    }
    if (reader.current_token(reader_state).type != TokenType::ObjectEnd)
        [[unlikely]] {
      return Error{
          .code = ErrorCode::size_mismatch,
          .offset = reader.current_token(reader_state).offset,
          .detail = "entry tuple has fewer members than input object",
      };
    }
    return Error::ok();
  }

  template <detail::AsTryElemTupleT T>
  [[nodiscard]]
  static auto decode_impl(T& t, reader_type& reader,
                          reader_state_type& reader_state) -> Error {
    const auto& tok = reader.current_token(reader_state);
    if (tok.type != TokenType::ArrayBegin) [[unlikely]] {
      return Error{
          .code = ErrorCode::type_mismatch,
          .offset = tok.offset,
          .detail = "expected array while decoding try tuple",
      };
    }
    auto tuple = detail::as_tuple(t);
    constexpr auto tuple_size =
        std::tuple_size_v<std::remove_cvref_t<decltype(tuple)>>;
    bool matched = true;
    while (matched) [[likely]] {
      if (!reader.next_token(reader_state)) [[unlikely]] {
        return reader.error(reader_state);
      }
      const auto& tok = reader.current_token(reader_state);
      if (tok.type == TokenType::ArrayEnd) {
        return Error::ok();
      }
      matched = [&]<size_t... I>(auto& tuple, std::index_sequence<I...>)
                    HOSQ_ALWAYS_INLINE_ATTR -> bool {
        return ([&]() HOSQ_ALWAYS_INLINE_ATTR -> bool {
          auto& elem = std::get<I>(tuple);
          using elem_type = std::remove_cvref_t<decltype(elem)>;
          auto ctx = make_decode_context<elem_type, I == 0 ? TryPhase::first
                                                           : TryPhase::normal>(
              elem, reader, reader_state);
          using context_type = decltype(ctx);
          if constexpr (elem_type::template decoder_const_invokable<
                            context_type>) {
            const auto& const_ctx = ctx;
            return elem.decode(const_ctx);
          } else {
            auto checkpoint = reader_state.checkpoint();
            auto ret = elem.decode(ctx);
            if (!ret) {
              reader_state.rollback(std::move(checkpoint));
              return false;
            }
            return true;
          }
        }() || ...);
      }(tuple, std::make_index_sequence<tuple_size>{});
    }
    return Error{
        .code = ErrorCode::unknown_member,
        .offset = reader.current_token(reader_state).offset,
        .detail = "no try tuple candidate matched array element",
    };
  }

  template <bool IsConst, class Elem, class Context>
  struct decode_raw_result;

  template <class Elem, class Context>
  struct decode_raw_result<true, Elem, Context> {
    using type =
        decltype(std::declval<Elem&>().decode(std::declval<const Context&>()));
  };

  template <class Elem, class Context>
  struct decode_raw_result<false, Elem, Context> {
    using type =
        decltype(std::declval<Elem&>().decode(std::declval<Context&>()));
  };

  template <bool IsConst, class Elem, class Context>
  using decode_raw_result_t =
      typename decode_raw_result<IsConst, Elem, Context>::type;

  template <class... U>
  static auto decompose_const_key_entry_tuple_and_entry_tuple(
      std::tuple<U&...> tuple) {
    auto const_key_entries =
        [&]<std::size_t... I>(std::index_sequence<I...>) -> auto {
      return std::tuple_cat([&]() -> auto {
        using try_type = std::tuple_element_t<I, std::tuple<U...>>;
        using entry_type = detail::try_base_t<try_type>;

        if constexpr (detail::ConstKeyEntryT<entry_type>) {
          return std::forward_as_tuple(std::get<I>(tuple));
        } else {
          return std::tuple<>{};
        }
      }()...);
    }(std::index_sequence_for<U...>{});

    auto entries = [&]<std::size_t... I>(std::index_sequence<I...>) -> auto {
      return std::tuple_cat([&]() -> auto {
        using try_type = std::tuple_element_t<I, std::tuple<U...>>;
        using entry_type = detail::try_base_t<try_type>;

        if constexpr (detail::EntryT<entry_type> &&
                      !detail::ConstKeyEntryT<entry_type>) {
          return std::forward_as_tuple(std::get<I>(tuple));
        } else {
          return std::tuple<>{};
        }
      }()...);
    }(std::index_sequence_for<U...>{});

    return std::pair{const_key_entries, entries};
  }

  template <std::size_t... I>
  static auto match_try_constkey_entry_tuple(auto& tuple, reader_type& reader,
                                             reader_state_type& reader_state,
                                             std::index_sequence<I...>) -> bool {
    const auto key_ctx =
        DecodeContext<void, reader_type, TryPhase::first>{reader, reader_state};

    std::string_view key_view =
        (::hosq::scan::scalar | ::hosq::decode::string_view).invoke(key_ctx);

    bool decoded = false;
    auto handlers = std::tuple{hosq::meta::make_handler<detail::try_base_t<
        std::remove_cvref_t<decltype(std::get<I>(tuple))>>::key()>(
        [&]() HOSQ_ALWAYS_INLINE_ATTR -> void {
          auto& entry = std::get<I>(tuple);

          using try_entry_type = std::remove_cvref_t<decltype(entry)>;
          using entry_type = typename try_entry_type::base_type;
          using value_type = typename entry_type::value_type;
          using value_storage_type = typename value_type::storage_type;
          using value_context_type =
              DecodeContext<value_storage_type, R, TryPhase::first>;

          static constexpr bool value_is_const_decodable =
              value_type::template decoder_const_invokable<value_context_type>;

          using value_raw_result_type =
              decode_raw_result_t<value_is_const_decodable, value_type,
                                  value_context_type>;

          if constexpr (std::same_as<value_raw_result_type, bool>) {
            auto checkpoint = reader_state.checkpoint();

            if (!reader.next_token(reader_state)) [[unlikely]] {
              decoded = false;
              return;
            }

            auto& value = entry.value();
            auto value_ctx = make_decode_context<value_type, TryPhase::first>(
                value, reader, reader_state);

            bool ret = false;
            if constexpr (value_is_const_decodable) {
              const auto& const_value_ctx = value_ctx;
              ret = value.decode(const_value_ctx);
            } else {
              ret = value.decode(value_ctx);
            }

            if (!ret) {
              reader_state.rollback(std::move(checkpoint));
              decoded = false;
              return;
            }

            decoded = true;
            return;
          } else {
            if (!reader.next_token(reader_state)) [[unlikely]] {
              decoded = false;
              return;
            }

            auto& value = entry.value();
            auto value_ctx = make_decode_context<value_type, TryPhase::first>(
                value, reader, reader_state);

            if constexpr (value_is_const_decodable) {
              const auto& const_value_ctx = value_ctx;
              value.decode(const_value_ctx);
            } else {
              value.decode(value_ctx);
            }

            decoded = true;
            return;
          }
        })...};

    if (!hosq::meta::match(key_view, handlers)) {
      return false;
    }

    return decoded;
  }

  template <size_t... I>
  static auto match_try_entry_tuple(auto& tuple, reader_type& reader,
                                    reader_state_type& reader_state,
                                    std::index_sequence<I...>) -> bool {
    return ([&]() HOSQ_ALWAYS_INLINE_ATTR -> bool {
      auto& entry = std::get<I>(tuple);

      static constexpr TryPhase phase =
          I == 0 ? TryPhase::first : TryPhase::normal;
      using entry_type = std::remove_cvref_t<decltype(entry)>;

      using key_elem_type = entry_type::key_type;
      using key_elem_value_type = typename key_elem_type::storage_type;
      using key_elem_context_type = DecodeContext<key_elem_value_type, R, phase>;
      static constexpr bool key_is_const_decodable =
          key_elem_type::template decoder_const_invokable<key_elem_context_type>;
      using key_elem_raw_result_type =
          decode_raw_result_t<key_is_const_decodable, key_elem_type,
                              key_elem_context_type>;

      using value_elem_type = entry_type::value_type;
      using value_elem_value_type = typename value_elem_type::storage_type;
      using value_elem_context_type =
          DecodeContext<value_elem_value_type, R, TryPhase::first>;
      static constexpr bool value_is_const_decodable =
          value_elem_type::template decoder_const_invokable<
              value_elem_context_type>;
      using value_elem_raw_result_type =
          decode_raw_result_t<value_is_const_decodable, value_elem_type,
                              value_elem_context_type>;

      auto& key_elem = entry.key();
      auto key_ctx = make_decode_context<key_elem_type, phase>(key_elem, reader,
                                                               reader_state);
      if constexpr (key_is_const_decodable) {
        if constexpr (std::same_as<key_elem_raw_result_type, bool>) {
          const auto& const_key_ctx = key_ctx;
          if (!key_elem.decode(const_key_ctx)) {
            return false;
          }
        } else {
          key_elem.decode(key_ctx);
        }

        if constexpr (std::same_as<value_elem_raw_result_type, bool>) {
          auto checkpoint = reader_state.checkpoint();
          if (!reader.next_token(reader_state)) [[unlikely]] {
            return false;
          }

          auto& value_elem = entry.value();
          auto value_ctx = make_decode_context<value_elem_type, TryPhase::first>(
              value_elem, reader, reader_state);
          auto ret = value_elem.decode(value_ctx);
          if (!ret) {
            reader_state.rollback(std::move(checkpoint));
            return false;
          }
          return true;
        } else {
          if (!reader.next_token(reader_state)) [[unlikely]] {
            return false;
          }
          auto& value_elem = entry.value();
          auto value_ctx = make_decode_context<value_elem_type, TryPhase::first>(
              value_elem, reader, reader_state);
          value_elem.decode(value_ctx);
          return true;
        }

      } else {
        auto checkpoint = reader_state.checkpoint();
        if constexpr (std::same_as<key_elem_raw_result_type, bool>) {
          {
            auto ret = key_elem.decode(key_ctx);
            if (!ret) {
              reader_state.rollback(std::move(checkpoint));
              return false;
            }
          }
        } else {
          key_elem.decode(key_ctx);
        }

        if (!reader.next_token(reader_state)) [[unlikely]] {
          return false;
        }

        auto& value_elem = entry.value();
        auto value_ctx = make_decode_context<value_elem_type, TryPhase::first>(
            value_elem, reader, reader_state);
        {
          if constexpr (std::same_as<value_elem_raw_result_type, bool>) {
            auto ret = value_elem.decode(value_ctx);
            if (!ret) {
              reader_state.rollback(std::move(checkpoint));
              return false;
            }
          } else {
            value_elem.decode(value_ctx);
          }
        }
        return true;
      }
    }() || ...);
  }

  template <detail::AsTryEntryTupleT T>
  [[nodiscard]]
  static auto decode_impl(T& t, reader_type& reader,
                          reader_state_type& reader_state) -> Error {
    const auto& tok = reader.current_token(reader_state);
    if (tok.type != TokenType::ObjectBegin) [[unlikely]] {
      return Error{
          .code = ErrorCode::type_mismatch,
          .offset = tok.offset,
          .detail = "expected object while decoding try entry tuple",
      };
    }

    auto tuple = detail::as_tuple(t);

    auto [const_key_entries, entries] =
        decompose_const_key_entry_tuple_and_entry_tuple(tuple);

    constexpr auto const_key_tuple_size =
        std::tuple_size_v<std::remove_cvref_t<decltype(const_key_entries)>>;

    constexpr auto entry_tuple_size =
        std::tuple_size_v<std::remove_cvref_t<decltype(entries)>>;

    bool matched = true;

    while (matched) [[likely]] {
      if (!reader.next_token(reader_state)) [[unlikely]] {
        return reader.error(reader_state);
      }

      const auto& tok = reader.current_token(reader_state);
      if (tok.type == TokenType::ObjectEnd) {
        return Error::ok();
      }

      matched = false;

      if constexpr (const_key_tuple_size > 0) {
        matched = match_try_constkey_entry_tuple(
            const_key_entries, reader, reader_state,
            std::make_index_sequence<const_key_tuple_size>{});
      }

      if (!matched) {
        if constexpr (entry_tuple_size > 0) {
          matched = match_try_entry_tuple(
              entries, reader, reader_state,
              std::make_index_sequence<entry_tuple_size>{});
        }
      }
    }

    return Error{
        .code = ErrorCode::unknown_member,
        .offset = reader.current_token(reader_state).offset,
        .detail = "no try entry candidate matched object member",
    };
  }

 private:
  reader_type reader_{};
  reader_state_type reader_state_{};
};

/// Free-function convenience wrapper.
template <ReaderConcept R, class T>
  requires detail::DecoderAcceptableT<T>
[[nodiscard]]
auto decode_into(T& t, typename R::input_type input) -> Error {
  Decoder<R> decoder;
  return decoder.decode(t, input);
}

}  // namespace hosq

#undef HOSQ_ALWAYS_INLINE_ATTR
#undef HOSQ_FUNCTION_ALWAYS_INLINE
