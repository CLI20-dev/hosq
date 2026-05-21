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
  auto decode(T& t, input_type input) -> Result<void, Error> {
    reader_.reset(reader_state_, input);
    return decode_impl(t);
  }

  template <detail::ElemT T>
  using first_decode_context_t =
      DecodeContext<typename T::storage_type, R, ScanPhase::first>;

  template <detail::ElemT T>
  using elem_decode_raw_result_t =
      decltype(std::declval<T&>().decode(
          std::declval<first_decode_context_t<T>&>()));

  template <detail::TryT T, ScanPhase Phase>
    requires detail::ElemT<typename std::remove_cvref_t<T>::base_type>
  using try_elem_context_t = DecodeContext<typename T::storage_type, R, Phase>;

  template <detail::TryT T, ScanPhase Phase>
    requires detail::ElemT<typename std::remove_cvref_t<T>::base_type>
  using try_elem_raw_result_t =
      decltype(std::declval<T&>().decode(
          std::declval<try_elem_context_t<T, Phase>&>()));

  template <detail::TryT T, ScanPhase Phase>
    requires detail::EntryT<typename std::remove_cvref_t<T>::base_type>
  using try_entry_key_void_context_t = DecodeContext<void, R, Phase>;

  template <detail::TryT T, ScanPhase Phase>
    requires detail::EntryT<typename std::remove_cvref_t<T>::base_type>
  using try_entry_key_raw_result_t = decltype(std::declval<T&>().decode_key(
      std::declval<try_entry_key_void_context_t<T, Phase>&>()));

  template <detail::TryT T, ScanPhase Phase>
    requires detail::EntryT<typename std::remove_cvref_t<T>::base_type>
  using try_entry_value_raw_result_t = decltype(std::declval<T&>().decode_value(
      std::declval<DecodeContext<void, R, Phase>&>()));

  template <detail::EntryT T, ScanPhase Phase>
  using entry_key_raw_result_t = decltype(std::declval<T&>().decode_key(
      std::declval<DecodeContext<void, R, Phase>&>()));

  template <detail::EntryT T, ScanPhase Phase>
  using entry_value_raw_result_t = decltype(std::declval<T&>().decode_value(
      std::declval<DecodeContext<void, R, Phase>&>()));

 private:
  [[nodiscard]]
  auto make_reader_checkpoint() {
    if constexpr (CheckpointableStateConcept<reader_state_type>) {
      return reader_state_.checkpoint();
    } else {
      static_assert(std::is_copy_constructible_v<reader_state_type> &&
                    std::is_copy_assignable_v<reader_state_type>);
      return reader_state_;
    }
  }

  template <class Checkpoint>
  auto rollback_reader_checkpoint(Checkpoint&& checkpoint) -> void {
    if constexpr (CheckpointableStateConcept<reader_state_type>) {
      reader_state_.rollback(std::forward<Checkpoint>(checkpoint));
    } else {
      static_assert(std::is_copy_constructible_v<reader_state_type> &&
                    std::is_copy_assignable_v<reader_state_type>);
      reader_state_ = checkpoint;
    }
  }

  template <class Ret>
  [[nodiscard]]
  auto hard_decode_result_to_void(Ret&& ret) -> Result<void, Error> {  // NOLINT
    using ret_type = std::remove_cvref_t<Ret>;

    if constexpr (detail::HosqResultType<ret_type>) {
      if (!ret) [[unlikely]] {
        return unexpected(ret.error());
      }
      return {};
    } else if constexpr (std::same_as<ret_type, bool>) {
      if (!ret) [[unlikely]] {
        return unexpected(Error{
            .code = ErrorCode::validation_failed,
            .offset = reader_.error(reader_state_).offset,
        });
      }
      return {};
    } else {
      return {};
    }
  }

  [[nodiscard]]
  auto hard_decode_result_to_void() -> Result<void, Error> {
    return {};
  }

  template <detail::ElemT T>
  [[nodiscard]]
  auto decode_impl(T& t) -> Result<void, Error> {
    if (!reader_.next_token(reader_state_)) [[unlikely]] {
      return unexpected(reader_.error(reader_state_));
    }
    first_decode_context_t<T> ctx{t.value(), reader_, reader_state_};
    using ret_type = elem_decode_raw_result_t<T>;
    if constexpr (std::same_as<ret_type, void>) {
      t.decode(ctx);
      return {};
    } else {
      auto ret = t.decode(ctx);
      if constexpr (detail::HosqResultType<std::remove_cvref_t<ret_type>>) {
        if (!ret) [[unlikely]] {
          return unexpected(ret.error());
        }
      } else if constexpr (std::same_as<std::remove_cvref_t<ret_type>, bool>) {
        if (!ret) [[unlikely]] {
          return unexpected(Error{
              .code = ErrorCode::candidate_rejected,
              .offset = reader_.error(reader_state_).offset,
          });
        }
      }

      return {};
    }
  }

  template <detail::EntryT T>
  [[nodiscard]]
  auto decode_impl(T& entry) -> Result<void, Error> {
    constexpr auto phase = ScanPhase::normal;

    DecodeContext<T, R, phase> entry_ctx{entry, reader_, reader_state_};
    DecodeContext<void, R, phase> ctx{entry_ctx};

    using key_ret_type = entry_key_raw_result_t<T, phase>;
    if constexpr (std::same_as<key_ret_type, void>) {
      entry.decode_key(ctx);
    } else {
      auto ret = entry.decode_key(ctx);
      auto lifted = hard_decode_result_to_void(ret);
      if (!lifted) [[unlikely]] {
        return unexpected(lifted.error());
      }
    }

    using value_ret_type = entry_value_raw_result_t<T, phase>;
    if constexpr (std::same_as<value_ret_type, void>) {
      entry.decode_value(ctx);
    } else {
      auto ret = entry.decode_value(ctx);
      auto lifted = hard_decode_result_to_void(ret);
      if (!lifted) [[unlikely]] {
        return unexpected(lifted.error());
      }
    }

    return {};
  }

  template <detail::AsElemTupleT T>
  [[nodiscard]]
  auto decode_impl(T& t) -> Result<void, Error> {
    if (!reader_.next_token(reader_state_)) [[unlikely]] {
      return unexpected(reader_.error(reader_state_));
    }
    if (reader_.current_token(reader_state_).type != TokenType::ArrayBegin)
        [[unlikely]] {
      return unexpected(Error{
          .code = ErrorCode::type_mismatch,
          .offset = reader_.current_token(reader_state_).offset,
      });
    }
    auto ret = std::apply(
        [&](auto&... elem) HOSQ_ALWAYS_INLINE_ATTR -> Result<void, Error> {
          Result<void, Error> result{};
          ([&]() HOSQ_ALWAYS_INLINE_ATTR -> bool {
            auto decode_result = decode_impl(elem);
            if (!decode_result) [[unlikely]] {
              result = unexpected(decode_result.error());
              return false;
            }
            return true;
          }() && ...);
          return result;
        },
        detail::as_tuple(t));
    if (!ret) [[unlikely]] {
      return unexpected(ret.error());
    }
    if (!reader_.next_token(reader_state_)) [[unlikely]] {
      return unexpected(reader_.error(reader_state_));
    }
    const auto& tok = reader_.current_token(reader_state_);
    if (tok.type != TokenType::ArrayEnd) [[unlikely]] {
      return unexpected(Error{
          .code = ErrorCode::size_mismatch,
          .offset = tok.offset,
      });
    }
    return {};
  }

  template <detail::AsEntryTupleT T>
  [[nodiscard]]
  auto decode_impl(T& t) -> Result<void, Error> {
    if (!reader_.next_token(reader_state_)) [[unlikely]] {
      return unexpected(reader_.error(reader_state_));
    }

    if (reader_.current_token(reader_state_).type != TokenType::ObjectBegin)
        [[unlikely]] {
      return unexpected(Error{
          .code = ErrorCode::type_mismatch,
          .offset = reader_.current_token(reader_state_).offset,
      });
    }

    auto ret = std::apply(
        [&](auto&... entry) HOSQ_ALWAYS_INLINE_ATTR -> Result<void, Error> {
          Result<void, Error> result{};

          ([&]() HOSQ_ALWAYS_INLINE_ATTR -> bool {
            auto decode_result = decode_impl(entry);
            if (!decode_result) [[unlikely]] {
              result = unexpected(decode_result.error());
              return false;
            }
            return true;
          }() && ...);

          return result;
        },
        detail::as_tuple(t));

    if (!ret) [[unlikely]] {
      return unexpected(ret.error());
    }

    if (!reader_.next_token(reader_state_)) [[unlikely]] {
      return unexpected(reader_.error(reader_state_));
    }

    if (reader_.current_token(reader_state_).type != TokenType::ObjectEnd)
        [[unlikely]] {
      return unexpected(Error{
          .code = ErrorCode::size_mismatch,
          .offset = reader_.current_token(reader_state_).offset,
      });
    }

    return {};
  }

  template <ScanPhase Phase, detail::TryT T>
    requires detail::ElemT<typename std::remove_cvref_t<T>::base_type>
  [[nodiscard]]
  auto try_decode_elem_candidate(T& elem) -> Result<bool, Error> {
    using elem_type = std::remove_cvref_t<T>;
    using context_type = try_elem_context_t<T, Phase>;
    using ret_type = try_elem_raw_result_t<T, Phase>;

    if constexpr (elem_type::template decoder_const_invokable<context_type>) {
      auto ctx = context_type{elem.value(), reader_, reader_state_};
      const auto& cctx = ctx;

      if constexpr (detail::HosqResultType<std::remove_cvref_t<ret_type>>) {
        auto ret = elem.decode(cctx);
        if (!ret) [[unlikely]] {
          if (ret.error().code == ErrorCode::candidate_rejected) {
            return false;
          }
          return unexpected(ret.error());
        }
        return *ret;
      } else {
        return elem.decode(cctx);
      }
    } else {
      auto checkpoint = make_reader_checkpoint();

      auto ctx = context_type{elem.value(), reader_, reader_state_};

      if constexpr (detail::HosqResultType<std::remove_cvref_t<ret_type>>) {
        auto ret = elem.decode(ctx);
        if (!ret) [[unlikely]] {
          if (ret.error().code == ErrorCode::candidate_rejected) {
            rollback_reader_checkpoint(std::move(checkpoint));
            return false;
          }
          rollback_reader_checkpoint(std::move(checkpoint));
          return unexpected(ret.error());
        }

        if (!*ret) {
          rollback_reader_checkpoint(std::move(checkpoint));
          return false;
        }

        return true;
      } else {
        auto ret = elem.decode(ctx);
        if (!ret) {
          rollback_reader_checkpoint(std::move(checkpoint));
          return false;
        }

        return true;
      }
    }
  }

  template <class Tuple, std::size_t... I>
  [[nodiscard]]
  auto try_decode_elem_candidates_impl(Tuple& tuple, std::index_sequence<I...>)
      -> Result<bool, Error> {
    Result<bool, Error> result{false};
    ([&]() HOSQ_ALWAYS_INLINE_ATTR -> bool {
      auto& elem = std::get<I>(tuple);
      auto candidate =
          try_decode_elem_candidate<I == 0 ? ScanPhase::first
                                           : ScanPhase::normal>(elem);
      if (!candidate) [[unlikely]] {
        result = unexpected(candidate.error());
        return true;
      }
      if (*candidate) {
        result = true;
        return true;
      }
      return false;
    }() || ...);
    return result;
  }

  template <detail::AsTryElemTupleT T>
  [[nodiscard]]
  auto decode_impl(T& t) -> Result<void, Error> {
    if (!reader_.next_token(reader_state_)) [[unlikely]] {
      return unexpected(reader_.error(reader_state_));
    }
    const auto& tok = reader_.current_token(reader_state_);
    if (tok.type != TokenType::ArrayBegin) [[unlikely]] {
      return unexpected(Error{
          .code = ErrorCode::type_mismatch,
          .offset = tok.offset,
      });
    }
    auto tuple = detail::as_tuple(t);
    constexpr auto tuple_size =
        std::tuple_size_v<std::remove_cvref_t<decltype(tuple)>>;
    while (true) {
      if (!reader_.next_token(reader_state_)) [[unlikely]] {
        return unexpected(reader_.error(reader_state_));
      }
      const auto& tok = reader_.current_token(reader_state_);
      if (tok.type == TokenType::ArrayEnd) {
        return {};
      }
      auto ret = try_decode_elem_candidates_impl(
          tuple, std::make_index_sequence<tuple_size>{});
      if (!ret) [[unlikely]] {
        return unexpected(ret.error());
      }
      if (!*ret) [[unlikely]] {
        return unexpected(Error{
            .code = ErrorCode::invalid_choice,
            .offset = tok.offset,
        });
      }
    }
  }

  template <detail::TryT T>
    requires detail::EntryT<typename std::remove_cvref_t<T>::base_type>
  [[nodiscard]]
  auto try_decode_entry_value_candidate(T& entry) -> Result<bool, Error> {
    using entry_type = std::remove_cvref_t<T>;
    using value_type = typename entry_type::value_type;

    static_assert(detail::TryT<value_type>,
                  "try_decode_entry_value_candidate requires Try value");

    constexpr auto phase = ScanPhase::first;

    using entry_context_type = DecodeContext<T, R, phase>;
    using void_context_type = DecodeContext<void, R, phase>;
    using ret_type = try_entry_value_raw_result_t<T, phase>;

    auto checkpoint = make_reader_checkpoint();

    if (!reader_.next_token(reader_state_)) [[unlikely]] {
      rollback_reader_checkpoint(std::move(checkpoint));
      return unexpected(reader_.error(reader_state_));
    }

    auto entry_ctx = entry_context_type{entry, reader_, reader_state_};
    auto ctx = void_context_type{entry_ctx};

    if constexpr (entry_type::template value_decoder_const_invokable<R, phase>) {
      const auto& cctx = ctx;

      if constexpr (detail::HosqResultType<std::remove_cvref_t<ret_type>>) {
        auto ret = entry.decode_value(cctx);
        if (!ret) [[unlikely]] {
          rollback_reader_checkpoint(std::move(checkpoint));
          return unexpected(ret.error());
        }

        if (!*ret) {
          rollback_reader_checkpoint(std::move(checkpoint));
          return false;
        }

        return true;
      } else {
        auto ret = entry.decode_value(cctx);
        if (!ret) {
          rollback_reader_checkpoint(std::move(checkpoint));
          return false;
        }

        return true;
      }
    } else {
      if constexpr (detail::HosqResultType<std::remove_cvref_t<ret_type>>) {
        auto ret = entry.decode_value(ctx);
        if (!ret) [[unlikely]] {
          rollback_reader_checkpoint(std::move(checkpoint));
          return unexpected(ret.error());
        }

        if (!*ret) {
          rollback_reader_checkpoint(std::move(checkpoint));
          return false;
        }

        return true;
      } else {
        auto ret = entry.decode_value(ctx);
        if (!ret) {
          rollback_reader_checkpoint(std::move(checkpoint));
          return false;
        }

        return true;
      }
    }
  }

  template <ScanPhase Phase, detail::TryT T>
    requires detail::EntryT<typename std::remove_cvref_t<T>::base_type>
  [[nodiscard]]
  auto try_decode_entry_key_candidate(T& entry) -> Result<bool, Error> {
    using entry_type = std::remove_cvref_t<T>;
    using entry_context_type = DecodeContext<T, R, Phase>;
    using void_context_type = DecodeContext<void, R, Phase>;
    using ret_type = try_entry_key_raw_result_t<T, Phase>;

    auto entry_ctx = entry_context_type{entry, reader_, reader_state_};
    auto ctx = void_context_type{entry_ctx};

    if constexpr (entry_type::template key_decoder_const_invokable<R, Phase>) {
      const auto& cctx = ctx;

      if constexpr (detail::HosqResultType<std::remove_cvref_t<ret_type>>) {
        auto ret = entry.decode_key(cctx);
        if (!ret) [[unlikely]] {
          if (ret.error().code == ErrorCode::candidate_rejected) {
            return false;
          }
          return unexpected(ret.error());
        }
        return *ret;
      } else {
        return entry.decode_key(cctx);
      }
    } else {
      auto checkpoint = make_reader_checkpoint();

      if constexpr (detail::HosqResultType<std::remove_cvref_t<ret_type>>) {
        auto ret = entry.decode_key(ctx);
        if (!ret) [[unlikely]] {
          if (ret.error().code == ErrorCode::candidate_rejected) {
            rollback_reader_checkpoint(std::move(checkpoint));
            return false;
          }
          rollback_reader_checkpoint(std::move(checkpoint));
          return unexpected(ret.error());
        }

        if (!*ret) {
          rollback_reader_checkpoint(std::move(checkpoint));
          return false;
        }

        return true;
      } else {
        auto ret = entry.decode_key(ctx);
        if (!ret) {
          rollback_reader_checkpoint(std::move(checkpoint));
          return false;
        }

        return true;
      }
    }
  }

  template <detail::TryT T>
    requires detail::EntryT<typename std::remove_cvref_t<T>::base_type>
  [[nodiscard]]
  auto decode_entry_value_hard(T& entry) -> Result<void, Error> {
    constexpr auto phase = ScanPhase::first;

    using entry_context_type = DecodeContext<T, R, phase>;
    using void_context_type = DecodeContext<void, R, phase>;
    using ret_type = try_entry_value_raw_result_t<T, phase>;

    if (!reader_.next_token(reader_state_)) [[unlikely]] {
      return unexpected(reader_.error(reader_state_));
    }

    auto entry_ctx = entry_context_type{entry, reader_, reader_state_};
    auto ctx = void_context_type{entry_ctx};

    if constexpr (std::same_as<ret_type, void>) {
      entry.decode_value(ctx);
      return {};
    } else {
      auto ret = entry.decode_value(ctx);

      if constexpr (detail::HosqResultType<std::remove_cvref_t<ret_type>>) {
        if (!ret) [[unlikely]] {
          return unexpected(ret.error());
        }

        return {};
      } else if constexpr (std::same_as<std::remove_cvref_t<ret_type>, bool>) {
        if (!ret) [[unlikely]] {
          return unexpected(Error{
              .code = ErrorCode::validation_failed,
              .offset = reader_.error(reader_state_).offset,
          });
        }

        return {};
      } else {
        return {};
      }
    }
  }

  template <ScanPhase Phase, detail::TryT T>
    requires detail::EntryT<typename std::remove_cvref_t<T>::base_type>
  [[nodiscard]]
  auto try_decode_entry_candidate(T& entry) -> Result<bool, Error> {
    using entry_type = std::remove_cvref_t<T>;
    using value_type = typename entry_type::value_type;

    auto key_ret = try_decode_entry_key_candidate<Phase>(entry);
    if (!key_ret) [[unlikely]] {
      return unexpected(key_ret.error());
    }

    if (!*key_ret) {
      return false;
    }

    if constexpr (detail::TryT<value_type>) {
      return try_decode_entry_value_candidate(entry);
    } else {
      auto value_ret = decode_entry_value_hard(entry);
      if (!value_ret) [[unlikely]] {
        return unexpected(value_ret.error());
      }

      return true;
    }
  }

  template <class Tuple, std::size_t... I>
  [[nodiscard]]
  auto try_decode_entry_candidates_impl(Tuple& tuple, std::index_sequence<I...>)
      -> Result<bool, Error> {
    Result<bool, Error> result{false};

    ([&]() HOSQ_ALWAYS_INLINE_ATTR -> bool {
      auto& entry = std::get<I>(tuple);

      auto candidate =
          try_decode_entry_candidate<I == 0 ? ScanPhase::first
                                            : ScanPhase::normal>(entry);

      if (!candidate) [[unlikely]] {
        result = unexpected(candidate.error());
        return true;
      }

      if (*candidate) {
        result = true;
        return true;
      }

      return false;
    }() || ...);

    return result;
  }

  template <detail::AsTryEntryTupleT T>
  [[nodiscard]]
  auto decode_impl(T& t) -> Result<void, Error> {
    if (!reader_.next_token(reader_state_)) [[unlikely]] {
      return unexpected(reader_.error(reader_state_));
    }

    {
      const auto& tok = reader_.current_token(reader_state_);
      if (tok.type != TokenType::ObjectBegin) [[unlikely]] {
        return unexpected(Error{
            .code = ErrorCode::type_mismatch,
            .offset = tok.offset,
        });
      }
    }

    auto tuple = detail::as_tuple(t);
    constexpr auto tuple_size =
        std::tuple_size_v<std::remove_cvref_t<decltype(tuple)>>;

    while (true) {
      if (!reader_.next_token(reader_state_)) [[unlikely]] {
        return unexpected(reader_.error(reader_state_));
      }

      const auto& tok = reader_.current_token(reader_state_);
      if (tok.type == TokenType::ObjectEnd) {
        return {};
      }

      auto ret = try_decode_entry_candidates_impl(
          tuple, std::make_index_sequence<tuple_size>{});

      if (!ret) [[unlikely]] {
        return unexpected(ret.error());
      }

      if (!*ret) [[unlikely]] {
        return unexpected(Error{
            .code = ErrorCode::unknown_member,
            .offset = tok.offset,
        });
      }
    }
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
auto decode_into(T& t, typename R::input_type input) -> Result<void, Error> {
  Decoder<R> decoder;
  return decoder.decode(t, input);
}

}  // namespace hosq

#undef HOSQ_ALWAYS_INLINE_ATTR
#undef HOSQ_FUNCTION_ALWAYS_INLINE
