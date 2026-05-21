#pragma once

#include <array>
#include <charconv>
#include <concepts>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>
#include <vector>

#include "hosq/decoder.hh"
#include "hosq/error.hh"
#include "hosq/pipeline.hh"
#include "hosq/pipeline_util.hh"
#include "hosq/result.hh"

namespace hosq::decode {

namespace detail {

template <class T>
concept Number = (std::integral<std::remove_cvref_t<T>> &&
                  !std::same_as<std::remove_cvref_t<T>, bool>) ||
                 std::floating_point<std::remove_cvref_t<T>>;

template <Number T>
struct NumberImpl {
  auto operator()(std::string_view input) const
      -> Result<std::remove_cvref_t<T>, Error> {
    using Value = std::remove_cvref_t<T>;
    Value value{};
    const char* first = input.data();
    const char* last = input.data() + input.size();
    const auto result = std::from_chars(first, last, value);
    if (result.ec == std::errc::result_out_of_range) [[unlikely]] {
      return unexpected(Error{.code = ErrorCode::out_of_range});
    }
    if (result.ec != std::errc{} || result.ptr != last) [[unlikely]] {
      return unexpected(Error{.code = ErrorCode::invalid_number});
    }
    return value;
  }
};

struct IdentityImpl {
  template <class T>
  auto operator()(T value) const -> T {
    return value;
  }
};

template <auto F>
struct CustomImpl {
  template <class T>
  auto operator()(T value) const -> std::invoke_result_t<decltype(F), T> {
    return F(std::move(value));
  }
};

}  // namespace detail

/// Parses a numeric string_view into arithmetic type T using std::from_chars.
template <detail::Number T>
inline constexpr auto number = step<detail::NumberImpl<T>>;

namespace detail {

struct BooleanImpl {
  auto operator()(std::string_view sv) const -> Result<bool, Error> {
    if (sv == "true") return true;
    if (sv == "false") return false;
    return unexpected(Error{.code = ErrorCode::type_mismatch});
  }
};

struct StringViewImpl {
  auto operator()(std::string_view sv) const -> std::string_view { return sv; }
};

template <class T>
struct DecodedResult;

template <::hosq::detail::ElemT T>
struct DecodedResult<T> {
  using type = typename std::remove_cvref_t<T>::storage_type;
};

template <::hosq::detail::AsEntryTupleT T>
struct DecodedResult<T> {
  using type = std::remove_cvref_t<T>;
};

template <class T>
using decoded_result_t = typename DecodedResult<std::remove_cvref_t<T>>::type;

template <class T, class Reader>
auto decode_current(T& element, Reader& reader) -> Result<void, Error> {
  return ::hosq::Decoder<std::remove_cvref_t<Reader>>::decode_from_reader(
      element, reader);
}

template <class T>
auto decoded_value(T& element) -> decoded_result_t<T> {
  if constexpr (::hosq::detail::ElemT<T>) {
    return std::move(element.value());
  } else {
    return std::move(element);
  }
}

template <class T>
  requires(::hosq::detail::ElemT<T> || ::hosq::detail::AsEntryTupleT<T>)
struct VectorImpl {
  template <class Context>
    requires(!std::is_const_v<std::remove_reference_t<Context>>)
  auto operator()(Context& ctx) const
      -> Result<std::vector<decoded_result_t<T>>, Error> {
    auto& reader = ctx.reader();
    if (reader.current_token().type != TokenType::ArrayBegin) {
      return unexpected(Error{
          .code = ErrorCode::type_mismatch,
          .offset = reader.current_token().offset,
      });
    }

    auto advanced = reader.advance(1);
    if (!advanced) {
      return unexpected(advanced.error());
    }

    std::vector<decoded_result_t<T>> values;
    while (true) {
      auto next = reader.next_token();
      if (!next) {
        return unexpected(next.error());
      }

      if (reader.current_token().type == TokenType::ArrayEnd) {
        return values;
      }

      T element{};
      auto decoded = decode_current(element, reader);
      if (!decoded) {
        return unexpected(decoded.error());
      }
      values.push_back(decoded_value(element));
    }
  }
};

template <class T, std::size_t N>
  requires(::hosq::detail::ElemT<T> || ::hosq::detail::AsEntryTupleT<T>)
struct FixedArrayImpl {
  template <class Context>
    requires(!std::is_const_v<std::remove_reference_t<Context>>)
  auto operator()(Context& ctx) const
      -> Result<std::array<decoded_result_t<T>, N>, Error> {
    auto& reader = ctx.reader();
    if (reader.current_token().type != TokenType::ArrayBegin) {
      return unexpected(Error{
          .code = ErrorCode::type_mismatch,
          .offset = reader.current_token().offset,
      });
    }

    auto advanced = reader.advance(1);
    if (!advanced) {
      return unexpected(advanced.error());
    }

    std::array<decoded_result_t<T>, N> values{};
    for (std::size_t i = 0; i < N; ++i) {
      auto next = reader.next_token();
      if (!next) {
        return unexpected(next.error());
      }
      if (reader.current_token().type == TokenType::ArrayEnd) {
        return unexpected(Error{.code = ErrorCode::size_mismatch,
                                .offset = reader.current_token().offset});
      }

      T element{};
      auto decoded = decode_current(element, reader);
      if (!decoded) {
        return unexpected(decoded.error());
      }
      values[i] = decoded_value(element);
    }

    auto next = reader.next_token();
    if (!next) {
      return unexpected(next.error());
    }
    if (reader.current_token().type != TokenType::ArrayEnd) {
      return unexpected(Error{.code = ErrorCode::size_mismatch,
                              .offset = reader.current_token().offset});
    }

    return values;
  }
};

}  // namespace detail

/// Converts "true"/"false" to bool; returns type_mismatch for anything else.
inline constexpr auto boolean = step<detail::BooleanImpl>;

/// Passes string_view through unchanged; escape resolution is left to the
/// caller.
inline constexpr auto string_view = step<detail::StringViewImpl>;

/// Decodes the current JSON array as std::vector<decoded_result_t<T>>.
template <class T>
  requires(::hosq::detail::ElemT<T> || ::hosq::detail::AsEntryTupleT<T>)
inline constexpr auto vector = step<detail::VectorImpl<T>>;

/// Decodes the current JSON array as std::array<decoded_result_t<T>, N>.
template <class T, std::size_t N>
  requires(::hosq::detail::ElemT<T> || ::hosq::detail::AsEntryTupleT<T>)
inline constexpr auto fixed_array = step<detail::FixedArrayImpl<T, N>>;

/// Passes any value through unchanged.
inline constexpr auto identity = step<detail::IdentityImpl>;

/// Applies user-defined conversion F to the input; F may return a plain value or
/// Result<U, Error>.
template <auto F>
inline constexpr auto custom = step<detail::CustomImpl<F>>;

}  // namespace hosq::decode
