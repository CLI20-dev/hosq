#pragma once

#include <charconv>
#include <cmath>
#include <concepts>
#include <cstdint>
#include <limits>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>

#include "hosq/detail/native_reader.hh"
#include "hosq/pipeline/decode.hh"
#include "hosq/value_types.hh"

namespace hosq::decode::detail {
template <class Context>
void typed_error(const Context& ctx, ErrorCode code, std::string_view why) {
  ctx.reader().report_error(
      ctx.reader_state(),
      {code, ctx.reader().current_token(ctx.reader_state()).offset, why});
}
template <class Context>
auto scalar_bytes(const Context& ctx) -> std::string_view {
  const auto& token = ctx.reader().current_token(ctx.reader_state());
  return ctx.input().as_string_view().substr(token.offset, token.length);
}
template <class Context>
auto json_literal(const Context& ctx) -> bool {
  const auto& token = ctx.reader().current_token(ctx.reader_state());
  return token.type == TokenType::Scalar &&
         token.context == static_cast<std::uint8_t>(JsonTokenContext::literal);
}
inline auto valid_json_number(std::string_view raw) -> bool {
  std::size_t pos = 0;
  if (pos < raw.size() && raw[pos] == '-') ++pos;
  if (pos == raw.size()) return false;
  if (raw[pos] == '0')
    ++pos;
  else {
    if (raw[pos] < '1' || raw[pos] > '9') return false;
    do {
      ++pos;
    } while (pos < raw.size() && raw[pos] >= '0' && raw[pos] <= '9');
  }
  auto digits = [&]() {
    const auto start = pos;
    while (pos < raw.size() && raw[pos] >= '0' && raw[pos] <= '9') ++pos;
    return start != pos;
  };
  if (pos < raw.size() && raw[pos] == '.') {
    ++pos;
    if (!digits()) return false;
  }
  if (pos < raw.size() && (raw[pos] == 'e' || raw[pos] == 'E')) {
    ++pos;
    if (pos < raw.size() && (raw[pos] == '+' || raw[pos] == '-')) ++pos;
    if (!digits()) return false;
  }
  return pos == raw.size();
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
  auto operator()(const ScalarView&, const Context& ctx) const -> T {
    T out{};
    if (ctx.reader().error(ctx.reader_state())) return out;
    if constexpr (requires { ctx.reader_state().scalar; }) {
      const auto& s = ctx.reader_state();
      if (s.token.type != TokenType::Scalar ||
          s.token.context !=
              static_cast<std::uint8_t>(hosq::detail::NativeContext::number)) {
        typed_error(ctx, ErrorCode::type_mismatch, "expected numeric scalar");
        return out;
      }
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
              if (fits) out = static_cast<T>(value);
            }
          },
          s.scalar);
      if (!category)
        typed_error(ctx, ErrorCode::type_mismatch,
                    Floating ? "expected floating-point category"
                             : "expected integer category");
      else if (!fits)
        typed_error(ctx, ErrorCode::out_of_range,
                    "numeric conversion would lose range or precision");
      return out;
    } else {
      if (!json_literal(ctx)) {
        typed_error(ctx, ErrorCode::type_mismatch, "expected numeric literal");
        return out;
      }
      const auto raw = scalar_bytes(ctx);
      const bool floating = raw.find_first_of(".eE") != std::string_view::npos;
      if (raw.empty() ||
          !(raw.front() == '-' || (raw.front() >= '0' && raw.front() <= '9')) ||
          floating != Floating) {
        typed_error(ctx, ErrorCode::type_mismatch,
                    Floating ? "expected floating-point category"
                             : "expected integer category");
        return out;
      }
      if (!valid_json_number(raw)) {
        typed_error(ctx, ErrorCode::invalid_number,
                    "invalid JSON number grammar");
        return out;
      }
      if constexpr (Floating) {
        double parsed{};
        const auto result = hosq::detail::number_from_chars(
            raw.data(), raw.data() + raw.size(), parsed);
        if (result.ec == std::errc::result_out_of_range ||
            (result.ec == std::errc{} && !lossless_float(parsed, out)))
          typed_error(ctx, ErrorCode::out_of_range,
                      "floating conversion would lose range or precision");
        else if (result.ec != std::errc{} ||
                 result.ptr != raw.data() + raw.size())
          typed_error(ctx, ErrorCode::invalid_number,
                      "invalid floating literal");
      } else {
        if constexpr (std::unsigned_integral<T>)
          if (raw.front() == '-') {
            typed_error(ctx, ErrorCode::out_of_range,
                        "negative unsigned integer");
            return out;
          }
        const auto result =
            std::from_chars(raw.data(), raw.data() + raw.size(), out);
        if (result.ec == std::errc::result_out_of_range)
          typed_error(ctx, ErrorCode::out_of_range, "integer out of range");
        else if (result.ec != std::errc{} ||
                 result.ptr != raw.data() + raw.size())
          typed_error(ctx, ErrorCode::invalid_number, "invalid integer literal");
      }
      return out;
    }
  }
};

struct TextImpl {
  using view_step = TextImpl;
  template <class>
  using input_type = ScalarView;
  template <class Context>
  auto operator()(const ScalarView& view, const Context& ctx) const
      -> std::string {
    std::string out;
    if (ctx.reader().error(ctx.reader_state())) return out;
    if constexpr (requires {
                    ctx.reader().read_string(ctx.reader_state(), out);
                  }) {
      (void)ctx.reader().read_string(ctx.reader_state(), out);
    } else {
      const auto& tok = ctx.reader().current_token(ctx.reader_state());
      if (tok.type != TokenType::Scalar ||
          tok.context != static_cast<std::uint8_t>(JsonTokenContext::string)) {
        typed_error(ctx, ErrorCode::type_mismatch, "expected string");
        return out;
      }
      const auto raw =
          ext::DecodeStringView<typename Context::reader_type>{}(view, ctx);
      if (!hosq::detail::unescape_json_string(raw, out))
        typed_error(ctx, ErrorCode::invalid_escape, "invalid JSON string body");
    }
    return out;
  }
};
struct RawTextImpl {
  using view_step = RawTextImpl;
  template <class>
  using input_type = ScalarView;
  template <class Context>
  auto operator()(const ScalarView& view, const Context& ctx) const
      -> RawTextValue {
    RawTextValue out;
    if (ctx.reader().error(ctx.reader_state())) return out;
    if constexpr (requires {
                    ctx.reader().read_raw_text(ctx.reader_state(), out);
                  }) {
      (void)ctx.reader().read_raw_text(ctx.reader_state(), out);
    } else {
      const auto& tok = ctx.reader().current_token(ctx.reader_state());
      if (tok.type != TokenType::Scalar ||
          tok.context != static_cast<std::uint8_t>(JsonTokenContext::string)) {
        typed_error(ctx, ErrorCode::type_mismatch, "expected string");
        return out;
      }
      out = {ext::DecodeStringView<typename Context::reader_type>{}(view, ctx),
             Format::json};
      std::string validation;
      if (!hosq::detail::unescape_json_string(out.value, validation))
        typed_error(ctx, ErrorCode::invalid_escape, "invalid JSON string body");
    }
    return out;
  }
};
struct NullImpl {
  using view_step = NullImpl;
  template <class>
  using input_type = ScalarView;
  template <class Context>
  auto operator()(const ScalarView&, const Context& ctx) const
      -> std::nullptr_t {
    if (ctx.reader().error(ctx.reader_state())) return nullptr;
    bool valid;
    if constexpr (requires { ctx.reader_state().scalar; }) {
      const auto& tok = ctx.reader().current_token(ctx.reader_state());
      valid = tok.type == TokenType::Scalar &&
              tok.context ==
                  static_cast<std::uint8_t>(hosq::detail::NativeContext::null);
    } else
      valid = json_literal(ctx) && scalar_bytes(ctx) == "null";
    if (!valid) typed_error(ctx, ErrorCode::type_mismatch, "expected null");
    return nullptr;
  }
};
struct BytesImpl {
  using view_step = BytesImpl;
  template <class>
  using input_type = ScalarView;
  template <class Context>
  auto operator()(const ScalarView&, const Context& ctx) const -> BytesValue {
    BytesValue out;
    if (ctx.reader().error(ctx.reader_state())) return out;
    if constexpr (requires { ctx.reader().read_bytes(ctx.reader_state(), out); })
      (void)ctx.reader().read_bytes(ctx.reader_state(), out);
    else
      typed_error(ctx, ErrorCode::unsupported_type,
                  "this format has no binary scalar");
    return out;
  }
};
struct TypedBooleanImpl {
  using view_step = TypedBooleanImpl;
  template <class>
  using input_type = ScalarView;
  template <class Context>
  auto operator()(const ScalarView&, const Context& ctx) const -> bool {
    if (ctx.reader().error(ctx.reader_state())) return false;
    if constexpr (requires { ctx.reader_state().scalar; }) {
      const auto& s = ctx.reader_state();
      if (s.token.type == TokenType::Scalar &&
          s.token.context ==
              static_cast<std::uint8_t>(hosq::detail::NativeContext::boolean))
        if (const auto* value = std::get_if<bool>(&s.scalar)) return *value;
    } else if (json_literal(ctx)) {
      const auto raw = scalar_bytes(ctx);
      if (raw == "true") return true;
      if (raw == "false") return false;
    }
    typed_error(ctx, ErrorCode::type_mismatch, "expected boolean literal");
    return false;
  }
};
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
inline constexpr auto text = step<detail::TextImpl>;
inline constexpr auto raw_text = step<detail::RawTextImpl>;
inline constexpr auto null = step<detail::NullImpl>;
inline constexpr auto bytes = step<detail::BytesImpl>;
inline constexpr auto typed_boolean = step<detail::TypedBooleanImpl>;
}  // namespace hosq::decode
