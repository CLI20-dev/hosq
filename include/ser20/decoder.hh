#pragma once

#include <tuple>
#include <type_traits>

#include "ser20/as_tuple.hh"
#include "ser20/element.hh"
#include "ser20/json/reader.hh"
#include "ser20/reader_base.hh"

namespace ser20 {

#if defined(__GNUC__) || defined(__clang__)
#define SER20_ALWAYS_INLINE_ATTR __attribute__((always_inline))
#define SER20_FUNCTION_ALWAYS_INLINE inline __attribute__((always_inline))
#elif defined(_MSC_VER)
#define SER20_ALWAYS_INLINE_ATTR
#define SER20_FUNCTION_ALWAYS_INLINE __forceinline
#else
#define SER20_ALWAYS_INLINE_ATTR
#define SER20_FUNCTION_ALWAYS_INLINE inline
#endif

namespace detail {}  // namespace detail

template <ReaderConcept R>
class Decoder {
 public:
  Decoder(const Decoder&) = delete;
  Decoder(Decoder&&) = delete;
  auto operator=(const Decoder&) -> Decoder& = delete;
  auto operator=(Decoder&&) -> Decoder& = delete;
  Decoder() = default;
  ~Decoder() = default;

  template <class T>
    requires(detail::ElemT<T> || detail::AsEntryTupleT<T>)
  [[nodiscard]]
  auto decode(T& t, typename R::input_type input) -> Result<void, Error> {
    reader_.reset(input);
    auto next = reader_.next_token();
    if (!next) {
      return unexpected(next.error());
    }
    if constexpr (detail::ElemT<T>) {
      return decode_element(t);
    } else if constexpr (detail::AsEntryTupleT<T>) {
      return decode_as_entry_tuple(t);
    } else {
      static_assert(detail::ElemT<T> || detail::AsEntryTupleT<T>,
                    "T must be an Elem or have an as_tuple() of Entries");
    }
  }

 private:
  template <detail::ElemT T>
  [[nodiscard]]
  SER20_FUNCTION_ALWAYS_INLINE constexpr auto decode_element(T& t)
      -> Result<void, Error> {
    if constexpr (std::remove_cvref_t<T>::template decoder_const_invokable<R>) {
      const auto& r = static_cast<const R&>(reader_);
      return t.decode(r);
    } else {
      return t.decode(reader_);
    }
  }

  template <detail::AsEntryTupleT T>
  [[nodiscard]]
  SER20_FUNCTION_ALWAYS_INLINE auto decode_as_entry_tuple(T& t)
      -> Result<void, Error> {
    if (reader_.current_token().type != TokenType::ObjectBegin) {
      return unexpected(Error{
          .code = ErrorCode::type_mismatch,
          .offset = reader_.current_token().offset,
      });
    }

    auto advanced = reader_.advance(1);
    if (!advanced) {
      return unexpected(advanced.error());
    }

    while (true) {
      auto next = reader_.next_token();
      if (!next) {
        return unexpected(next.error());
      }

      if (reader_.current_token().type == TokenType::ObjectEnd) {
        return {};
      }

      auto result = decode_one_member(t);
      if (!result) {
        return unexpected(result.error());
      }
    }
  }

  template <detail::AsEntryTupleT T>
  [[nodiscard]]
  SER20_FUNCTION_ALWAYS_INLINE auto decode_one_member(T& t)
      -> Result<void, Error> {
    return std::apply(
        [&](auto&... entry) SER20_ALWAYS_INLINE_ATTR -> Result<void, Error> {
          Result<void, Error> result{
              unexpected(Error{.code = ErrorCode::candidate_rejected})};

          ([&]() SER20_ALWAYS_INLINE_ATTR -> bool {
            using Entry = std::remove_cvref_t<decltype(entry)>;

            if constexpr (entry_key_const_invokable<Entry>()) {
              auto key_result = decode_entry_key_const(entry);

              if (is_candidate_rejected(key_result)) {
                return false;
              }

              if (!key_result) {
                result = unexpected(key_result.error());
                return true;
              }
            } else {
              auto snapshot = reader_;
              auto key_result = decode_entry_key(entry);

              if (is_candidate_rejected(key_result)) {
                reader_ = snapshot;
                return false;
              }

              if (!key_result) {
                reader_ = snapshot;
                result = unexpected(key_result.error());
                return true;
              }
            }

            auto next = reader_.next_token();
            if (!next) {
              result = unexpected(next.error());
              return true;
            }

            if constexpr (entry_value_const_invokable<Entry>()) {
              auto value_result = decode_entry_value_const(entry);

              if (!value_result) {
                result = unexpected(value_result.error());
                return true;
              }

              return true;
            } else {
              auto snapshot = reader_;
              auto value_result = decode_entry_value(entry);

              if (is_candidate_rejected(value_result)) {
                reader_ = snapshot;
                return false;
              }

              if (!value_result) {
                reader_ = snapshot;
                result = unexpected(value_result.error());
                return true;
              }

              return true;
            }
          }() || ...);

          return result;
        },
        ser20::as_tuple(t));
  }

  template <class Entry>
  static consteval auto entry_key_const_invokable() -> bool {
    using E = std::remove_cvref_t<Entry>;
    if constexpr (E::category == detail::EntryCategory::ElemElem ||
                  E::category == detail::EntryCategory::ElemSchema) {
      return E::template key_decoder_const_invokable<R>;
    } else {
      return false;
    }
  }

  template <class Entry>
  static consteval auto entry_value_const_invokable() -> bool {
    using E = std::remove_cvref_t<Entry>;
    if constexpr (E::category == detail::EntryCategory::ElemElem ||
                  E::category == detail::EntryCategory::SchemaElem) {
      return E::template value_decoder_const_invokable<R>;
    } else {
      return false;
    }
  }

  template <class Entry>
  SER20_FUNCTION_ALWAYS_INLINE auto decode_entry_key(Entry& entry)
      -> Result<void, Error> {
    using E = std::remove_cvref_t<Entry>;
    if constexpr (E::category == detail::EntryCategory::ElemElem ||
                  E::category == detail::EntryCategory::ElemSchema) {
      return entry.decode_key(reader_);
    } else {
      return decode_as_entry_tuple(entry.key());
    }
  }

  template <class Entry>
  SER20_FUNCTION_ALWAYS_INLINE auto decode_entry_value(Entry& entry)
      -> Result<void, Error> {
    using E = std::remove_cvref_t<Entry>;

    if constexpr (E::category == detail::EntryCategory::ElemElem ||
                  E::category == detail::EntryCategory::SchemaElem) {
      return entry.decode_value(reader_);
    } else if constexpr (E::category == detail::EntryCategory::ElemSchema ||
                         E::category == detail::EntryCategory::SchemaSchema) {
      return decode_as_entry_tuple(entry.value());
    } else {
      static_assert(detail::always_false<E>, "Unsupported Entry category");
    }
  }

  template <class Entry>
  SER20_FUNCTION_ALWAYS_INLINE auto decode_entry_key_const(Entry& entry)
      -> Result<void, Error> {
    using E = std::remove_cvref_t<Entry>;

    if constexpr (E::category == detail::EntryCategory::ElemElem ||
                  E::category == detail::EntryCategory::ElemSchema) {
      const auto& r = static_cast<const R&>(reader_);
      return entry.decode_key(r);
    } else {
      static_assert(detail::always_false<E>,
                    "Schema key is not const-invokable");
    }
  }

  template <class Entry>
  SER20_FUNCTION_ALWAYS_INLINE auto decode_entry_value_const(Entry& entry)
      -> Result<void, Error> {
    using E = std::remove_cvref_t<Entry>;

    if constexpr (E::category == detail::EntryCategory::ElemElem ||
                  E::category == detail::EntryCategory::SchemaElem) {
      const auto& r = static_cast<const R&>(reader_);
      return entry.decode_value(r);
    } else {
      static_assert(detail::always_false<E>,
                    "Schema value is not const-invokable");
    }
  }

  template <class T>
  [[nodiscard]]
  SER20_FUNCTION_ALWAYS_INLINE static constexpr auto is_candidate_rejected(
      const Result<T, Error>& result) -> bool {
    return !result && result.error().code == ErrorCode::candidate_rejected;
  }

  R reader_;
};

#undef SER20_ALWAYS_INLINE_ATTR
#undef SER20_FUNCTION_ALWAYS_INLINE

namespace detail {

template <ReaderConcept R>
auto skip_json_scalar(R& reader) -> Result<void, Error> {
  const auto& tok = reader.current_token();
  if (tok.type != TokenType::ScalarBegin) {
    return unexpected(Error{.code = ErrorCode::unknown_member});
  }
  const auto sv = reader.input();
  std::size_t end = tok.offset;
  if (static_cast<JsonTokenContext>(tok.context) == JsonTokenContext::string) {
    ++end;
    while (end < sv.size()) {
      if (sv[end] == '\\') {
        end += 2;
        continue;
      }
      if (sv[end] == '"') {
        ++end;
        break;
      }
      ++end;
    }
  } else {
    while (end < sv.size()) {
      char c = sv[end];
      if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == ',' ||
          c == ']' || c == '}')
        break;
      ++end;
    }
  }
  return reader.advance(end - tok.offset);
}

}  // namespace detail

/// Free-function convenience wrapper: decodes a JSON object into a struct whose
/// members are ser20::Entry fields. Reader type R defaults to json::Reader<>.
/// Free-function convenience wrapper.
template <ReaderConcept R = json::Reader<>, class T>
  requires(detail::ElemT<T> || detail::AsEntryTupleT<T>)
[[nodiscard]]
auto decode_into(T& t, typename R::input_type input) -> Result<void, Error> {
  Decoder<R> decoder;
  return decoder.decode(t, input);
}

}  // namespace ser20
