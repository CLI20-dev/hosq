#pragma once

#include <concepts>
#include <cstddef>
#include <tuple>
#include <type_traits>
#include <utility>

#include "hosq/context.hh"
#include "hosq/detail/as_tuple.hh"
#include "hosq/detail/compiler.hh"
#include "hosq/detail/type_traits.hh"
#include "hosq/meta_match.hh"
#include "hosq/parse_result.hh"
#include "hosq/pipeline/decode.hh"
#include "hosq/pipeline/scan.hh"
#include "hosq/reader_base.hh"

namespace hosq {

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
    if (!hosq::next_token<ReadPosition::value>(reader_, reader_state_))
        [[unlikely]] {
      return reader_.error(reader_state_);
    }
    return decode_nested(t, reader_, reader_state_);
  }

  /// Decode one complete document, including validation of the input suffix.
  /// decode() retains its legacy contract for cursor-observing pipelines.
  template <class T>
    requires detail::DecoderAcceptableT<T>
  [[nodiscard]]
  auto decode_document(T& t, input_type input) -> Error {
    const auto result = decode(t, input);
    if (result) return result;
    // Nested decoding stops on the decoded value's last token.
    if (!hosq::next_token<Expect::document_end>(reader_, reader_state_))
      return reader_.error(reader_state_);
    const auto& token = reader_.current_token(reader_state_);
    if (token.type != TokenType::End)
      return Error{ErrorCode::trailing_data, token.offset,
                   "expected end of document after decoded value"};
    return reader_.error(reader_state_);
  }

  /// Decode into a fresh T and expose it only after decode_document succeeds.
  /// T is an explicit decodable type, not its storage. Pipeline semantics are
  /// unchanged: root/legacy aggregate match::* soft false is not a hard error;
  /// use validate::* or typed containers for validation failures.
  /// External callback effects are not rolled back. Input views (including
  /// RawText) remain borrowed; the result does not extend input lifetime.
  /// Construction, pipeline and move exceptions propagate to the caller.
  template <class T>
    requires detail::DecoderAcceptableT<T> &&
             std::same_as<T, std::remove_cvref_t<T>> &&
             std::default_initializable<T> && std::move_constructible<T>
  [[nodiscard]]
  auto parse(input_type input) -> ParseResult<T> {
    T value{};
    const auto error = decode_document(value, input);
    if (error) return ParseResult<T>{error};
    return ParseResult<T>{std::move(value)};
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
  static HOSQ_ALWAYS_INLINE auto decode_nested(T& t, reader_type& reader,
                                               reader_state_type& reader_state)
      -> Error {
    return ext::ReaderDispatch<R>{}(reader, reader_state, [&](auto* active) {
      using Active = std::remove_pointer_t<decltype(active)>;
      if constexpr (std::same_as<Active, R>) {
        return decode_impl(t, *active, reader_state);
      } else {
        return Decoder<Active>::decode_nested(t, *active, reader_state);
      }
    });
  }

 private:
  template <detail::ElemT T>
  [[nodiscard]]
  static HOSQ_ALWAYS_INLINE auto decode_impl(T& t, reader_type& reader,
                                             reader_state_type& reader_state)
      -> Error {
    auto ctx = make_decode_context(t, reader, reader_state);
    t.decode(ctx);
    return reader.error(reader_state);
  }

  template <detail::AsElemTupleT T>
  [[nodiscard]]
  static HOSQ_ALWAYS_INLINE auto decode_impl(T& t, reader_type& reader,
                                             reader_state_type& reader_state)
      -> Error {
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
            if (!hosq::next_token<Expect::next_element_or_end>(
                    reader, reader_state)) [[unlikely]] {
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
    if (!hosq::next_token<Expect::next_element_or_end>(reader, reader_state))
        [[unlikely]] {
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
  static HOSQ_ALWAYS_INLINE auto read_entry_key(T& t, ReaderT& reader,
                                                StateT& reader_state) -> bool {
    if (!hosq::next_token<Expect::next_member_or_end>(reader, reader_state))
        [[unlikely]] {
      return false;
    }
    if constexpr (detail::ConstKeyEntryT<T>) {
      const DecodeContext<void, ReaderT> key_ctx{reader, reader_state};
      auto scan_key = [&]() -> std::string_view {
        return (scan::scalar | decode::string_view).invoke(key_ctx);
      };
      auto handlers = std::tuple{meta::make_handler<T::key()>([] {})};
      bool matched;
      if constexpr (requires {
                      reader.match_const_key(reader_state, handlers, scan_key,
                                             std::false_type{});
                    }) {
        matched = reader.match_const_key(reader_state, handlers, scan_key,
                                         std::false_type{});
      } else {
        matched = meta::match(scan_key(), handlers);
      }
      if (!matched) {
        if (reader.error(reader_state).is_ok()) {
          reader.report_error(
              reader_state,
              Error{.code = ErrorCode::unknown_member,
                    .offset = reader.current_token(reader_state).offset,
                    .detail = "expected declared constant key"});
        }
        return false;
      }
      if (!hosq::next_token<ReadPosition::member_value>(reader, reader_state))
          [[unlikely]]
        return false;
    } else {
      auto key_ctx = make_decode_context(t.key(), reader, reader_state);
      t.key().decode(key_ctx);
      if (!hosq::next_token<Expect::member_value>(reader, reader_state))
          [[unlikely]]
        return false;
    }
    return true;
  }

  template <detail::EntryT T, class ReaderT, class StateT>
  [[nodiscard]]
  static HOSQ_ALWAYS_INLINE auto decode_entry(T& t, ReaderT& reader,
                                              StateT& reader_state) -> bool {
    if constexpr (detail::ConstKeyEntryT<T> && requires {
                    ext::EntryInput<ReaderT>{}.template decode<T::key()>(
                        t.value(), reader, reader_state);
                  }) {
      if (ext::EntryInput<ReaderT>{}.template decode<T::key()>(t.value(), reader,
                                                               reader_state))
        return true;
    }
    bool ready = false;
    if constexpr (detail::ConstKeyEntryT<T>) {
      if constexpr (requires {
                      reader.template try_read_const_member<T::key()>(
                          reader_state);
                    }) {
        ready = reader.template try_read_const_member<T::key()>(reader_state);
      }
    }
    if (!ready && !read_entry_key(t, reader, reader_state)) return false;
    auto value_ctx = make_decode_context(t.value(), reader, reader_state);
    t.value().decode(value_ctx);
    return true;
  }

  template <detail::AsEntryTupleT T>
  [[nodiscard]]
  static HOSQ_ALWAYS_INLINE auto decode_impl(T& t, reader_type& reader,
                                             reader_state_type& reader_state)
      -> Error {
    if (reader.current_token(reader_state).type != TokenType::ObjectBegin)
        [[unlikely]] {
      return Error{
          .code = ErrorCode::type_mismatch,
          .offset = reader.current_token(reader_state).offset,
          .detail = "expected object while decoding entry tuple",
      };
    }
    auto tuple = detail::as_tuple(t);
    [&]<std::size_t... I>(std::index_sequence<I...>) HOSQ_ALWAYS_INLINE_ATTR
        -> void {
      // The && fold evaluates entries left-to-right and stops at the first
      // failed decode. If decode puts the reader into an error state,
      // subsequent next_token() is a noop and the error is reported below.
      (void)(decode_entry(std::get<I>(tuple), reader, reader_state) && ...);
    }(std::make_index_sequence<
            std::tuple_size_v<std::remove_cvref_t<decltype(tuple)>>>{});
    if (!hosq::next_token<Expect::next_member_or_end>(reader, reader_state))
        [[unlikely]] {
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
  static HOSQ_ALWAYS_INLINE auto decode_impl(T& t, reader_type& reader,
                                             reader_state_type& reader_state)
      -> Error {
    if (reader.error(reader_state)) return reader.error(reader_state);
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
      if (!hosq::next_token<Expect::next_element_or_end>(reader, reader_state))
          [[unlikely]] {
        return reader.error(reader_state);
      }
      const auto& tok = reader.current_token(reader_state);
      if (tok.type == TokenType::ArrayEnd) {
        return Error::ok();
      }
      matched = [&]<size_t... I>(auto& tuple, std::index_sequence<I...>)
                    HOSQ_ALWAYS_INLINE_ATTR -> bool {
        // A hard reader error stops dispatch even when the terminal rejects.
        return (
            ([&]() HOSQ_ALWAYS_INLINE_ATTR -> bool {
              auto& elem = std::get<I>(tuple);
              using elem_type = std::remove_cvref_t<decltype(elem)>;
              using base_type = detail::try_base_t<elem_type>;
              auto ctx =
                  make_decode_context<base_type, I == 0 ? TryPhase::first
                                                        : TryPhase::normal>(
                      elem, reader, reader_state);
              using context_type = decltype(ctx);
              constexpr bool const_decodable =
                  elem_type::template decoder_const_invokable<context_type>;
              using result_type =
                  decode_raw_result_t<const_decodable, elem_type, context_type>;
              if constexpr (std::same_as<result_type, void>) {
                // A void terminal claims this element, even if it reports an
                // error.
                if constexpr (const_decodable) {
                  const auto& const_ctx = ctx;
                  elem.decode(const_ctx);
                } else {
                  elem.decode(ctx);
                }
                return true;
              } else if constexpr (const_decodable) {
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
            }() || reader.error(reader_state).has_error()) ||
            ...);
      }(tuple, std::make_index_sequence<tuple_size>{});
      if (reader.error(reader_state)) return reader.error(reader_state);
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

  // Entry candidates cross from the key token to its value. Some readers keep
  // the current token outside their checkpoint to cache lazy scalar scans.
  // Restore the key on a soft rejection, retaining cached lengths when the
  // candidate never left that token. Storage and hard errors are not restored.
  static auto rollback_try_entry_candidate(reader_type& reader,
                                           reader_state_type& reader_state,
                                           auto checkpoint,
                                           const Token& key_token) -> void {
    reader_state.rollback(std::move(checkpoint));
    if constexpr (requires {
                    {
                      reader.current_token(reader_state)
                    } -> std::same_as<Token&>;
                  }) {
      if (reader.error(reader_state).is_ok()) {
        auto& token = reader.current_token(reader_state);
        if (token.offset != key_token.offset || token.type != key_token.type ||
            token.context != key_token.context)
          token = key_token;
      }
    }
  }

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

  template <bool DeferredKey = false, std::size_t... I>
  static auto match_try_constkey_entry_tuple(auto& tuple, reader_type& reader,
                                             reader_state_type& reader_state,
                                             std::index_sequence<I...>) -> bool {
    const auto key_ctx =
        DecodeContext<void, reader_type, TryPhase::first>{reader, reader_state};

    auto scan_key = [&]() -> std::string_view {
      return (::hosq::scan::scalar | ::hosq::decode::string_view)
          .invoke(key_ctx);
    };

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
            const auto key_token = reader.current_token(reader_state);

            if (!hosq::next_token<ReadPosition::member_value>(
                    reader, reader_state)) [[unlikely]] {
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
              rollback_try_entry_candidate(reader, reader_state,
                                           std::move(checkpoint), key_token);
              decoded = false;
              return;
            }

            decoded = true;
            return;
          } else {
            if constexpr (requires {
                            ext::EntryInput<R>{}
                                .template decode_matched<entry_type::key()>(
                                    entry.value(), reader, reader_state);
                          }) {
              if (ext::EntryInput<R>{}
                      .template decode_matched<entry_type::key()>(
                          entry.value(), reader, reader_state)) {
                decoded = true;
                return;
              }
            }
            if (!hosq::next_token<ReadPosition::member_value>(
                    reader, reader_state)) [[unlikely]] {
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

    bool matched;
    if constexpr (requires {
                    reader.match_const_key(reader_state, handlers, scan_key,
                                           std::bool_constant<DeferredKey>{});
                  }) {
      matched = reader.match_const_key(reader_state, handlers, scan_key,
                                       std::bool_constant<DeferredKey>{});
    } else {
      matched = hosq::meta::match(scan_key(), handlers);
    }
    if (!matched) return false;

    return decoded;
  }

  template <size_t... I>
  static auto match_try_entry_tuple(auto& tuple, reader_type& reader,
                                    reader_state_type& reader_state,
                                    std::index_sequence<I...>) -> bool {
    return (
        ([&]() HOSQ_ALWAYS_INLINE_ATTR -> bool {
          auto& entry = std::get<I>(tuple);

          static constexpr TryPhase phase =
              I == 0 ? TryPhase::first : TryPhase::normal;
          using entry_type = std::remove_cvref_t<decltype(entry)>;

          using key_elem_type = entry_type::key_type;
          using key_elem_value_type = typename key_elem_type::storage_type;
          using key_elem_context_type =
              DecodeContext<key_elem_value_type, R, phase>;
          static constexpr bool key_is_const_decodable =
              key_elem_type::template decoder_const_invokable<
                  key_elem_context_type>;
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
          auto key_ctx = make_decode_context<key_elem_type, phase>(
              key_elem, reader, reader_state);
          if constexpr (key_is_const_decodable) {
            if constexpr (std::same_as<key_elem_raw_result_type, bool>) {
              const auto& const_key_ctx = key_ctx;
              if (!key_elem.decode(const_key_ctx)) {
                return false;
              }
            } else {
              key_elem.decode(key_ctx);
            }
            if (reader.error(reader_state)) return false;

            if constexpr (std::same_as<value_elem_raw_result_type, bool>) {
              auto checkpoint = reader_state.checkpoint();
              const auto key_token = reader.current_token(reader_state);
              if (!hosq::next_token<ReadPosition::member_value>(
                      reader, reader_state)) [[unlikely]] {
                return false;
              }

              auto& value_elem = entry.value();
              auto value_ctx =
                  make_decode_context<value_elem_type, TryPhase::first>(
                      value_elem, reader, reader_state);
              auto ret = value_elem.decode(value_ctx);
              if (!ret) {
                rollback_try_entry_candidate(reader, reader_state,
                                             std::move(checkpoint), key_token);
                return false;
              }
              return true;
            } else {
              if (!hosq::next_token<ReadPosition::member_value>(
                      reader, reader_state)) [[unlikely]] {
                return false;
              }
              auto& value_elem = entry.value();
              auto value_ctx =
                  make_decode_context<value_elem_type, TryPhase::first>(
                      value_elem, reader, reader_state);
              value_elem.decode(value_ctx);
              return true;
            }

          } else {
            auto checkpoint = reader_state.checkpoint();
            const auto key_token = reader.current_token(reader_state);
            if constexpr (std::same_as<key_elem_raw_result_type, bool>) {
              {
                auto ret = key_elem.decode(key_ctx);
                if (!ret) {
                  rollback_try_entry_candidate(reader, reader_state,
                                               std::move(checkpoint), key_token);
                  return false;
                }
              }
            } else {
              key_elem.decode(key_ctx);
            }
            if (reader.error(reader_state)) return false;

            if (!hosq::next_token<Expect::member_value>(reader, reader_state))
                [[unlikely]] {
              return false;
            }

            auto& value_elem = entry.value();
            auto value_ctx =
                make_decode_context<value_elem_type, TryPhase::first>(
                    value_elem, reader, reader_state);
            {
              if constexpr (std::same_as<value_elem_raw_result_type, bool>) {
                auto ret = value_elem.decode(value_ctx);
                if (!ret) {
                  rollback_try_entry_candidate(reader, reader_state,
                                               std::move(checkpoint), key_token);
                  return false;
                }
              } else {
                value_elem.decode(value_ctx);
              }
            }
            return true;
          }
        }() || reader.error(reader_state).has_error()) ||
        ...);
  }

  template <detail::AsTryEntryTupleT T>
  [[nodiscard]]
  static HOSQ_ALWAYS_INLINE auto decode_impl(T& t, reader_type& reader,
                                             reader_state_type& reader_state)
      -> Error {
    if (reader.error(reader_state)) return reader.error(reader_state);
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

    // Only the standard skip pair has no observable work before resolving
    // its key. Custom key pipelines keep the original resolved-key contract.
    constexpr bool deferred_key = [] {
      if constexpr (entry_tuple_size == 1) {
        using Entry = std::remove_cvref_t<
            std::tuple_element_t<0, std::remove_cvref_t<decltype(entries)>>>;
        if constexpr (requires {
                        typename Entry::key_type::decoder_type;
                        typename Entry::value_type::decoder_type;
                      }) {
          return std::same_as<typename Entry::key_type::decoder_type,
                              std::remove_cvref_t<decltype(scan::scalar |
                                                           decode::skip)>> &&
                 std::same_as<typename Entry::value_type::decoder_type,
                              std::remove_cvref_t<decltype(decode::skip)>>;
        }
      }
      return false;
    }();
    bool matched = true;

    while (matched) [[likely]] {
      if (!hosq::next_token<Expect::next_member_or_end>(reader, reader_state))
          [[unlikely]] {
        return reader.error(reader_state);
      }

      const auto& tok = reader.current_token(reader_state);
      if (tok.type == TokenType::ObjectEnd) {
        return Error::ok();
      }

      matched = false;

      if constexpr (const_key_tuple_size > 0) {
        matched = match_try_constkey_entry_tuple<deferred_key>(
            const_key_entries, reader, reader_state,
            std::make_index_sequence<const_key_tuple_size>{});
      }
      if (reader.error(reader_state)) return reader.error(reader_state);

      if (!matched) {
        if constexpr (entry_tuple_size > 0) {
          matched = match_try_entry_tuple(
              entries, reader, reader_state,
              std::make_index_sequence<entry_tuple_size>{});
        }
      }
      if (reader.error(reader_state)) return reader.error(reader_state);
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

/// Strict document convenience wrapper. Unlike decode_into, also requires the
/// reader to reach End after decoding the root value.
template <ReaderConcept R, class T>
  requires detail::DecoderAcceptableT<T>
[[nodiscard]]
auto decode_document_into(T& t, typename R::input_type input) -> Error {
  Decoder<R> decoder;
  return decoder.decode_document(t, input);
}

/// Value-returning complete-document parse. Select T explicitly, for example
/// parse<json::FastReader<>, types::value::Record<Schema>>(input).
template <ReaderConcept R, class T>
  requires detail::DecoderAcceptableT<T> &&
           std::same_as<T, std::remove_cvref_t<T>> &&
           std::default_initializable<T> && std::move_constructible<T>
[[nodiscard]]
auto parse(typename R::input_type input) -> ParseResult<T> {
  Decoder<R> decoder;
  return decoder.template parse<T>(input);
}

}  // namespace hosq
