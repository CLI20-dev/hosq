#pragma once

#include <charconv>
#include <concepts>
#include <string_view>
#include <system_error>
#include <type_traits>

#include "ser20/context.hh"
#include "ser20/error.hh"
#include "ser20/pipeline.hh"
#include "ser20/result.hh"

namespace ser20::decode::detail {

template <class T>
concept Number = (std::integral<std::remove_cvref_t<T>> &&
                  !std::same_as<std::remove_cvref_t<T>, bool>) ||
                 std::floating_point<std::remove_cvref_t<T>>;

template <Number T>
struct NumberImpl {
  template <DecodeContextLike Context>
  auto operator()(Context& ctx, Result<std::string_view, Error> input) const
      -> Result<std::remove_cvref_t<T>, Error> {
    if (!input) [[unlikely]] {
      return unexpected(input.error());
    }
    using Value = std::remove_cvref_t<T>;
    Value value{};
    const std::string_view view = input.value();
    const char* first = view.data();
    const char* last = view.data() + view.size();
    const auto result = std::from_chars(first, last, value);
    if (result.ec == std::errc::result_out_of_range) [[unlikely]] {
      return unexpected(Error{
          .code = ErrorCode::out_of_range,
          .offset = ctx.reader->current_token()->offset,
      });
    }
    if (result.ec != std::errc{} || result.ptr != last) [[unlikely]] {
      return unexpected(Error{
          .code = ErrorCode::invalid_number,
          .offset = ctx.reader->current_token()->offset,
      });
    }
    return value;
  }
};

}  // namespace ser20::decode::detail

namespace ser20::decode {

template <detail::Number T>
inline constexpr auto number = ser20::step<detail::NumberImpl<T>>;

}  // namespace ser20::decode
