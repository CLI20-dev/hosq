#pragma once

#include <concepts>
#include <cstddef>
#include <tuple>
#include <type_traits>
#include <utility>

#include "hosq/detail/as_tuple.hh"
#include "hosq/element.hh"
#include "hosq/entry.hh"
#include "hosq/reader_base.hh"
#include "hosq/try.hh"

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

namespace detail {

template <class T>
struct DecoderAcceptableType : std::false_type {};

template <class T>
  requires(ElemT<T> || AsElemTupleT<T> || AsEntryTupleT<T> ||
           AsTryElemTupleT<T> || AsTryEntryTupleT<T>)
struct DecoderAcceptableType<T> : std::true_type {};

template <class T>
concept DecoderAcceptableT = DecoderAcceptableType<T>::value;

}  // namespace detail

template <ReaderConcept R>
class Decoder {
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
    return decode_impl(t);
  }

  template <detail::ElemT T, ScanPhase Phase = ScanPhase::normal>
  [[nodiscard]]
  static auto make_decode_context(T& t, R& reader, reader_state_type& state) {
    return DecodeContext<typename T::storage_type, R, Phase>{t.value(), reader,
                                                             state};
  }

 private:
  template <detail::ElemT T>
  [[nodiscard]]
  auto decode_impl(T& t) -> Error {
    if (!reader_.next_token(reader_state_)) [[unlikely]] {
      return reader_.error(reader_state_);
    }
    t.decode(make_decode_context(t, reader_, reader_state_));
    return reader_.error(reader_state_);
  }

  template <detail::AsElemTupleT T>
  [[nodiscard]]
  auto decode_impl(T& t) -> Error {
    if (!reader_.next_token(reader_state_)) [[unlikely]] {
      return reader_.error(reader_state_);
    }
    if (reader_.current_token(reader_state_).type != TokenType::ArrayBegin)
        [[unlikely]] {
      return Error{
          .code = ErrorCode::type_mismatch,
          .offset = reader_.current_token(reader_state_).offset,
      };
    }
    std::apply(
        [&](auto&... elem) HOSQ_ALWAYS_INLINE_ATTR -> void {
          ([&]() HOSQ_ALWAYS_INLINE_ATTR -> bool {
            if (!reader_.next_token(reader_state_)) [[unlikely]] {
              return false;
            }
            elem.decode(make_decode_context(elem, reader_, reader_state_));
            // If decode puts the reader into an error state, subsequent
            // next_token() is a noop and the error is reported below.
            return true;
          }() && ...);
        },
        detail::as_tuple(t));
    if (!reader_.next_token(reader_state_)) [[unlikely]] {
      return reader_.error(reader_state_);
    }
    const auto& tok = reader_.current_token(reader_state_);
    if (tok.type != TokenType::ArrayEnd) [[unlikely]] {
      return Error{
          .code = ErrorCode::size_mismatch,
          .offset = tok.offset,
      };
    }
    return Error::ok();
  }

  template <detail::EntryT T>
  [[nodiscard]]
  HOSQ_FUNCTION_ALWAYS_INLINE auto decode_entry(T& t) -> bool {
    if (!reader_.next_token(reader_state_)) [[unlikely]] {
      return false;
    }
    t.key().decode(make_decode_context(t.key(), reader_, reader_state_));
    if (!reader_.next_token(reader_state_)) [[unlikely]] {
      return false;
    }
    t.value().decode(make_decode_context(t.value(), reader_, reader_state_));
    return true;
  }

  template <detail::AsEntryTupleT T>
  [[nodiscard]]
  auto decode_impl(T& t) -> Error {
    if (!reader_.next_token(reader_state_)) [[unlikely]] {
      return reader_.error(reader_state_);
    }
    if (reader_.current_token(reader_state_).type != TokenType::ObjectBegin)
        [[unlikely]] {
      return Error{
          .code = ErrorCode::type_mismatch,
          .offset = reader_.current_token(reader_state_).offset,
      };
    }
    std::apply(
        [&](auto&... entry) HOSQ_ALWAYS_INLINE_ATTR -> void {
          ([&]() HOSQ_ALWAYS_INLINE_ATTR -> bool {
            if (!decode_entry(entry)) [[unlikely]] {
              return false;
            }
            // If decode puts the reader into an error state, subsequent
            // next_token() is a noop and the error is reported below.
            return true;
          }() && ...);
        },
        detail::as_tuple(t));
    if (!reader_.next_token(reader_state_)) [[unlikely]] {
      return reader_.error(reader_state_);
    }
    if (reader_.current_token(reader_state_).type != TokenType::ObjectEnd)
        [[unlikely]] {
      return Error{
          .code = ErrorCode::size_mismatch,
          .offset = reader_.current_token(reader_state_).offset,
      };
    }
    return Error::ok();
  }

  template <detail::AsTryElemTupleT T>
  [[nodiscard]]
  auto decode_impl(T& t) -> Error {
    if (!reader_.next_token(reader_state_)) [[unlikely]] {
      return reader_.error(reader_state_);
    }
    const auto& tok = reader_.current_token(reader_state_);
    if (tok.type != TokenType::ArrayBegin) [[unlikely]] {
      return Error{
          .code = ErrorCode::type_mismatch,
          .offset = tok.offset,
      };
    }
    auto tuple = detail::as_tuple(t);
    constexpr auto tuple_size =
        std::tuple_size_v<std::remove_cvref_t<decltype(tuple)>>;
    bool matched = true;
    while (matched) [[likely]] {
      if (!reader_.next_token(reader_state_)) [[unlikely]] {
        return reader_.error(reader_state_);
      }
      const auto& tok = reader_.current_token(reader_state_);
      if (tok.type == TokenType::ArrayEnd) {
        return Error::ok();
      }
      matched = [&]<size_t... I>(auto& tuple, std::index_sequence<I...>)
                    HOSQ_ALWAYS_INLINE_ATTR -> bool {
        return ([&]() HOSQ_ALWAYS_INLINE_ATTR -> bool {
          auto& elem = std::get<I>(tuple);
          using elem_type = std::remove_cvref_t<decltype(elem)>;
          auto ctx = make_decode_context<elem_type, I == 0 ? ScanPhase::first
                                                           : ScanPhase::normal>(
              elem, reader_, reader_state_);
          using context_type = decltype(ctx);
          if constexpr (elem_type::template decoder_const_invokable<
                            context_type>) {
            const auto& const_ctx = ctx;
            return elem.decode(const_ctx);
          } else {
            auto checkpoint = reader_state_.checkpoint();
            auto ret = elem.decode(ctx);
            if (!ret) {
              reader_state_.rollback(std::move(checkpoint));
              return false;
            }
            return true;
          }
        }() || ...);
      }(tuple, std::make_index_sequence<tuple_size>{});
    }
    return Error{
        .code = ErrorCode::candidate_rejected,
        .offset = reader_.current_token(reader_state_).offset,
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

  template <detail::AsTryEntryTupleT T>
  [[nodiscard]]
  auto decode_impl(T& t) -> Error {
    if (!reader_.next_token(reader_state_)) [[unlikely]] {
      return reader_.error(reader_state_);
    }
    const auto& tok = reader_.current_token(reader_state_);
    if (tok.type != TokenType::ArrayBegin) [[unlikely]] {
      return Error{
          .code = ErrorCode::type_mismatch,
          .offset = tok.offset,
      };
    }
    auto tuple = detail::as_tuple(t);
    constexpr auto tuple_size =
        std::tuple_size_v<std::remove_cvref_t<decltype(tuple)>>;
    bool matched = true;
    while (matched) [[likely]] {
      if (!reader_.next_token(reader_state_)) [[unlikely]] {
        return reader_.error(reader_state_);
      }
      const auto& tok = reader_.current_token(reader_state_);
      if (tok.type == TokenType::ArrayEnd) {
        return Error::ok();
      }
      matched = [&]<size_t... I>(auto& tuple, std::index_sequence<I...>)
                    HOSQ_ALWAYS_INLINE_ATTR -> bool {
        return ([&]() HOSQ_ALWAYS_INLINE_ATTR -> bool {
          auto& entry = std::get<I>(tuple);

          // Some useful type aliases here
          static constexpr ScanPhase phase =
              I == 0 ? ScanPhase::first : ScanPhase::normal;
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

          static_assert(std::same_as<key_elem_raw_result_type, bool>,
                        "Try<Entry> key decoder must return bool");

          using value_elem_type = entry_type::value_type;
          using value_elem_value_type = typename value_elem_type::storage_type;
          using value_elem_context_type =
              DecodeContext<value_elem_value_type, R, ScanPhase::first>;
          static constexpr bool value_is_const_decodable =
              value_elem_type::template decoder_const_invokable<
                  value_elem_context_type>;
          using value_elem_raw_result_type =
              decode_raw_result_t<value_is_const_decodable, value_elem_type,
                                  value_elem_context_type>;

          // Try to decode the key first.
          auto& key_elem = entry.key();
          auto key_ctx = make_decode_context<key_elem_type, phase>(
              key_elem, reader_, reader_state_);
          if constexpr (key_is_const_decodable) {
            const auto& const_key_ctx = key_ctx;
            if (!key_elem.decode(const_key_ctx)) {
              return false;
            }

            if constexpr (std::same_as<value_elem_raw_result_type, bool>) {
              // Save checkpoint before trying to decode the value.
              auto checkpoint = reader_state_.checkpoint();
              if (!reader_.next_token(reader_state_)) [[unlikely]] {
                return false;
              }

              // Then try to decode the value.
              auto& value_elem = entry.value();
              auto value_ctx =
                  make_decode_context<value_elem_type, ScanPhase::first>(
                      value_elem, reader_, reader_state_);
              auto ret = value_elem.decode(value_ctx);
              if (!ret) {
                reader_state_.rollback(std::move(checkpoint));
                return false;
              }
              return true;
            } else {
              if (!reader_.next_token(reader_state_)) [[unlikely]] {
                return false;
              }
              // Then try to decode the value.
              auto& value_elem = entry.value();
              auto value_ctx =
                  make_decode_context<value_elem_type, ScanPhase::first>(
                      value_elem, reader_, reader_state_);
              value_elem.decode(value_ctx);
              return true;
            }

          } else {
            auto checkpoint = reader_state_.checkpoint();
            {
              auto ret = key_elem.decode(key_ctx);
              if (!ret) {
                reader_state_.rollback(std::move(checkpoint));
                return false;
              }
            }

            // Read the next token before trying to decode the value
            if (!reader_.next_token(reader_state_)) [[unlikely]] {
              return false;
            }

            // Then try to decode the value.
            auto& value_elem = entry.value();
            auto value_ctx =
                make_decode_context<value_elem_type, ScanPhase::first>(
                    value_elem, reader_, reader_state_);
            {
              if constexpr (std::same_as<value_elem_raw_result_type, bool>) {
                auto ret = value_elem.decode(value_ctx);
                if (!ret) {
                  reader_state_.rollback(std::move(checkpoint));
                  return false;
                }
              } else {
                value_elem.decode(value_ctx);
              }
            }
            return true;
          }
        }() || ...);
      }(tuple, std::make_index_sequence<tuple_size>{});
    }
    return Error{
        .code = ErrorCode::candidate_rejected,
        .offset = reader_.current_token(reader_state_).offset,
    };
  }

 private:
  reader_type reader_{};
  reader_state_type reader_state_{};
};

/// Free-function convenience wrapper: decodes a JSON object into a struct whose
/// members are hosq::Entry fields. Reader type R defaults to json::Reader<>.
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
