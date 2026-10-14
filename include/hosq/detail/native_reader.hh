#pragma once

#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "hosq/detail/utf8.hh"
#include "hosq/pipeline/decode.hh"
#include "hosq/reader_base.hh"

namespace hosq::detail {
// Scalar contexts for formats whose numbers are already typed on the wire.
enum class NativeContext : std::uint8_t {
  none,
  string,
  number,
  boolean,
  null,
  binary,
  extension,
  datetime
};
using NativeScalar =
    std::variant<std::monostate, std::int64_t, std::uint64_t, double, bool>;
struct NativeFrame {
  TokenType end;
  std::uint64_t remaining = 0;
  std::size_t limit = 0;
  std::size_t index = 0;
  bool key = true;
  std::uint8_t marker = 0;
};
struct NativeState {
  std::string_view input;
  std::size_t pos = 0;
  bool started = false;
  std::vector<NativeFrame> frames;
  Token token{0, 0, TokenType::End, 0};
  NativeScalar scalar;
  mutable Error failure = Error::ok();
  struct Checkpoint {
    std::size_t pos;
    bool started;
    std::vector<NativeFrame> frames;
    Token token;
    NativeScalar scalar;
  };
  auto checkpoint() const -> Checkpoint {
    return {pos, started, frames, token, scalar};
  }
  void rollback(Checkpoint c) {
    pos = c.pos;
    started = c.started;
    frames = std::move(c.frames);
    token = c.token;
    scalar = c.scalar;
  }
};
struct NativeReaderAccess {
  using input_type = std::string_view;
  using state_type = NativeState;
  void reset(state_type& s, input_type in) const {
    s = {};
    s.input = in;
  }
  auto current_token(const state_type& s) const -> const Token& {
    return s.token;
  }
  auto input(const state_type& s) const -> BytesView {
    return BytesView::from_string_view(s.input);
  }
  void report_error(const state_type& s, Error e) const {
    if (s.failure.is_ok()) s.failure = e;
  }
  auto error(const state_type& s) const -> Error { return s.failure; }

 protected:
  static auto fail(state_type& s, ErrorCode code, std::string_view why) -> bool {
    if (s.failure.is_ok()) s.failure = {code, s.pos, why};
    s.token = {s.pos, 0, TokenType::Error, 0};
    return false;
  }
  static auto need(state_type& s, std::size_t count, std::size_t limit) -> bool {
    if (s.pos > limit || count > limit - s.pos)
      return fail(s, ErrorCode::unexpected_eof, "truncated value");
    return true;
  }
  static auto uint_bytes(state_type& s, unsigned n, bool little = false)
      -> std::uint64_t {
    std::uint64_t v = 0;
    for (unsigned i = 0; i < n; ++i) {
      const auto b = static_cast<unsigned char>(s.input[s.pos++]);
      if (little)
        v |= std::uint64_t(b) << (8 * i);
      else
        v = (v << 8) | b;
    }
    return v;
  }
  static auto signed_bits(std::uint64_t bits, unsigned n) -> std::int64_t {
    if (n < 8 && (bits & (std::uint64_t{1} << (n * 8 - 1))))
      bits |= ~std::uint64_t{0} << (n * 8);
    return std::bit_cast<std::int64_t>(bits);
  }
  static void scalar_token(state_type& s, std::size_t start,
                           NativeContext context) {
    s.token = {start, s.pos - start, TokenType::Scalar,
               static_cast<std::uint8_t>(context)};
  }
};

// Checked conversions preserve integer precision and reject fractional floats
// for integer schemas. No floating-to-integer cast happens before range checks.
struct NativeDecodeNumber {
  template <class T, class Context>
  auto operator()(const ScalarView&, const Context& ctx) const -> T {
    auto& s = ctx.reader_state();
    T out{};
    bool numeric = false;
    bool fits = false;
    if (s.token.context == static_cast<std::uint8_t>(NativeContext::number)) {
      std::visit(
          [&](auto v) {
            using V = decltype(v);
            if constexpr (std::same_as<V, std::int64_t> ||
                          std::same_as<V, std::uint64_t> ||
                          std::same_as<V, double>) {
              numeric = true;
              if constexpr (std::integral<T> && std::integral<V>) {
                fits = std::in_range<T>(v);
              } else if constexpr (std::integral<T>) {
                // Powers of two give exact exclusive upper bounds, including
                // uint64.
                const double upper =
                    std::ldexp(1.0, std::numeric_limits<T>::digits);
                const double lower = std::is_signed_v<T> ? -upper : 0.0;
                fits = std::isfinite(v) && std::trunc(v) == v && v >= lower &&
                       v < upper;
              } else {
                fits = !std::isfinite(static_cast<double>(v)) ||
                       (static_cast<long double>(v) >=
                            -std::numeric_limits<T>::max() &&
                        static_cast<long double>(v) <=
                            std::numeric_limits<T>::max());
              }
              if (fits) out = static_cast<T>(v);
            }
          },
          s.scalar);
    }
    if (!numeric || !fits)
      ctx.reader().report_error(
          s, {numeric ? ErrorCode::out_of_range : ErrorCode::type_mismatch,
              s.token.offset, "expected representable number"});
    return out;
  }
};
struct NativeDecodeNumberInto {
  template <class T, class Context>
  void operator()(const ScalarView& view, const Context& ctx) const {
    auto value = NativeDecodeNumber{}.template operator()<T>(view, ctx);
    if (ctx.reader().error(ctx.reader_state()).is_ok()) ctx.value() = value;
  }
};
struct NativeDecodeBoolean {
  template <class Context>
  auto operator()(const ScalarView&, const Context& ctx) const -> bool {
    auto& s = ctx.reader_state();
    if (s.token.context == static_cast<std::uint8_t>(NativeContext::boolean))
      return std::get<bool>(s.scalar);
    ctx.reader().report_error(
        s, {ErrorCode::type_mismatch, s.token.offset, "expected boolean"});
    return false;
  }
};
struct NativeDecodeStringView {
  template <class Context>
  auto operator()(const ScalarView&, const Context& ctx) const
      -> std::string_view {
    auto& s = ctx.reader_state();
    if (s.token.context == static_cast<std::uint8_t>(NativeContext::string))
      return s.input.substr(s.token.offset, s.token.length);
    ctx.reader().report_error(
        s, {ErrorCode::type_mismatch, s.token.offset, "expected string"});
    return {};
  }
};
}  // namespace hosq::detail
