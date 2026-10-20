#pragma once

#include <cmath>
#include <concepts>
#include <cstdint>
#include <limits>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>

#include "hosq/pipeline/decode.hh"
#include "hosq/scalar_codec.hh"

namespace hosq::decode::detail {
template <class Context>
void typed_error(const Context& ctx, ErrorCode code, std::string_view why) {
  ctx.reader().report_error(
      ctx.reader_state(),
      {code, ctx.reader().current_token(ctx.reader_state()).offset, why});
}
template <class T>
auto lossless_float(double value, T& out) -> bool {
  if (std::isnan(value)) {
    out = static_cast<T>(value);
    return true;
  }
  if (std::isinf(value)) {
    if constexpr (std::numeric_limits<T>::has_infinity) {
      out = static_cast<T>(value);
      return true;
    }
    return false;
  }
  if (static_cast<long double>(value) > std::numeric_limits<T>::max() ||
      static_cast<long double>(value) < -std::numeric_limits<T>::max())
    return false;
  const T narrowed = static_cast<T>(value);
  if (static_cast<double>(narrowed) != value ||
      (value == 0 && std::signbit(narrowed) != std::signbit(value)))
    return false;
  out = narrowed;
  return true;
}

template <class T, bool Floating>
struct TypedNumberImpl {
  using view_step = TypedNumberImpl;
  template <class>
  using input_type = ScalarView;

  template <class Context>
  auto operator()(const ScalarView& view, const Context& ctx) const -> T {
    T out{};
    if (ctx.reader().error(ctx.reader_state())) return out;
    SemanticNumber number;
    number.requested = Floating ? SemanticNumber::Category::floating
                                : SemanticNumber::Category::integer;
    if (!ext::DecodeSemanticScalar<typename Context::reader_type>{}(view, ctx,
                                                                    number))
      return out;
    bool category = false, fits = false;
    std::visit(
        [&](auto value) {
          using V = decltype(value);
          if constexpr (Floating && std::same_as<V, double>) {
            category = true;
            fits = lossless_float(value, out);
          } else if constexpr (!Floating && (std::same_as<V, std::int64_t> ||
                                             std::same_as<V, std::uint64_t>)) {
            category = true;
            fits = std::in_range<T>(value);
            if constexpr (std::unsigned_integral<T>)
              fits = fits && !number.negative_integer;
            if (fits) out = static_cast<T>(value);
          }
        },
        number.value);
    if (!category)
      typed_error(ctx, ErrorCode::type_mismatch,
                  Floating ? "expected floating-point category"
                           : "expected integer category");
    else if (!fits)
      typed_error(ctx, ErrorCode::out_of_range,
                  "numeric conversion would lose range or precision");
    return out;
  }
};

template <class Value>
struct ScalarCodecImpl {
  using view_step = ScalarCodecImpl;
  template <class>
  using input_type = ScalarView;
  template <class Context>
  auto operator()(const ScalarView& view, const Context& ctx) const -> Value {
    Value output{};
    if (!ctx.reader().error(ctx.reader_state()))
      (void)ext::DecodeSemanticScalar<typename Context::reader_type>{}(view, ctx,
                                                                       output);
    return output;
  }
};
using NullImpl = ScalarCodecImpl<std::nullptr_t>;
using BytesImpl = ScalarCodecImpl<BytesValue>;
using TypedBooleanImpl = ScalarCodecImpl<bool>;
}  // namespace hosq::decode::detail

namespace hosq::decode {
template <std::signed_integral T = std::int64_t>
inline constexpr auto integer = step<detail::TypedNumberImpl<T, false>>;
template <std::unsigned_integral T = std::uint64_t>
  requires(!std::same_as<T, bool>)
inline constexpr auto unsigned_integer = step<detail::TypedNumberImpl<T, false>>;
/// Floating inputs are decoded as binary64; narrowing must preserve their
/// binary value and signed zero. NaN payload bits are not preserved.
template <std::floating_point T = double>
inline constexpr auto floating = step<detail::TypedNumberImpl<T, true>>;
inline constexpr auto null = step<detail::NullImpl>;
inline constexpr auto bytes = step<detail::BytesImpl>;
inline constexpr auto typed_boolean = step<detail::TypedBooleanImpl>;
}  // namespace hosq::decode
