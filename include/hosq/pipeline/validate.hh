#pragma once

#include <concepts>
#include <cstddef>
#include <string_view>
#include <type_traits>
#include <utility>

#include "hosq/detail/utf8.hh"
#include "hosq/error.hh"
#include "hosq/pipeline.hh"

namespace hosq::validate {

namespace detail {

template <class Context>
void report_validation_error(Context& ctx, ErrorCode code,
                             std::string_view detail) {
  ::hosq::detail::report_reader_error(
      ctx.reader(), ctx.reader_state(),
      ::hosq::detail::ErrorInfo{
          .code = code,
          .offset = ctx.reader().current_token(ctx.reader_state()).offset,
          .detail = detail,
      });
}

template <auto V>
struct MatchImpl {
  template <class T, class Context>
  auto operator()(T&& value, Context& ctx) const -> decltype(auto) {
    if (value != V) {
      report_validation_error(ctx, ErrorCode::invalid_choice,
                              "validate::match value mismatch");
    }
    return std::forward<T>(value);
  }
};

template <StringLiteral V>
struct MatchImpl<V> {
  template <class T, class Context>
  auto operator()(T&& value, Context& ctx) const -> decltype(auto) {
    if (value != V.view()) {
      report_validation_error(ctx, ErrorCode::invalid_choice,
                              "validate::match string mismatch");
    }
    return std::forward<T>(value);
  }
};

template <auto... Vs>
struct OneOfImpl {
  template <class T, class Context>
  auto operator()(T&& value, Context& ctx) const -> decltype(auto) {
    if (!((value == Vs) || ...)) {
      report_validation_error(ctx, ErrorCode::invalid_choice,
                              "validate::one_of value not allowed");
    }
    return std::forward<T>(value);
  }
};

template <auto Min, auto Max>
struct RangeImpl {
  template <class T, class Context>
  auto operator()(T&& value, Context& ctx) const -> decltype(auto) {
    if (value < Min || value > Max) {
      report_validation_error(ctx, ErrorCode::out_of_range,
                              "validate::range value outside bounds");
    }
    return std::forward<T>(value);
  }
};

template <auto Min>
struct MinImpl {
  template <class T, class Context>
  auto operator()(T&& value, Context& ctx) const -> decltype(auto) {
    if (value < Min) {
      report_validation_error(ctx, ErrorCode::out_of_range,
                              "validate::min value below minimum");
    }
    return std::forward<T>(value);
  }
};

template <auto Max>
struct MaxImpl {
  template <class T, class Context>
  auto operator()(T&& value, Context& ctx) const -> decltype(auto) {
    if (value > Max) {
      report_validation_error(ctx, ErrorCode::out_of_range,
                              "validate::max value above maximum");
    }
    return std::forward<T>(value);
  }
};

struct NonEmptyImpl {
  template <class T, class Context>
  auto operator()(T&& value, Context& ctx) const -> decltype(auto) {
    if (value.empty()) {
      report_validation_error(ctx, ErrorCode::validation_failed,
                              "validate::non_empty value is empty");
    }
    return std::forward<T>(value);
  }
};

template <std::size_t N>
struct SizeImpl {
  template <class T, class Context>
  auto operator()(T&& value, Context& ctx) const -> decltype(auto) {
    if (value.size() != N) {
      report_validation_error(ctx, ErrorCode::validation_failed,
                              "validate::size value has wrong size");
    }
    return std::forward<T>(value);
  }
};

template <std::size_t Min, std::size_t Max>
struct SizeRangeImpl {
  template <class T, class Context>
  auto operator()(T&& value, Context& ctx) const -> decltype(auto) {
    if (value.size() < Min || value.size() > Max) {
      report_validation_error(ctx, ErrorCode::validation_failed,
                              "validate::size_range value outside bounds");
    }
    return std::forward<T>(value);
  }
};

template <auto F>
struct PredicateImpl {
  template <class T, class Context>
  auto operator()(T&& value, Context& ctx) const -> decltype(auto) {
    if (!F(value)) {
      report_validation_error(ctx, ErrorCode::validation_failed,
                              "validate::predicate returned false");
    }
    return std::forward<T>(value);
  }
};

template <auto F>
struct CustomImpl {
  template <class T, class Context>
  auto operator()(T&& value, Context& ctx) const -> decltype(auto) {
    if constexpr (requires { F(std::forward<T>(value), ctx); }) {
      using R = std::remove_cvref_t<decltype(F(std::forward<T>(value), ctx))>;
      if constexpr (std::same_as<R, bool>) {
        if (!F(value, ctx)) {
          report_validation_error(ctx, ErrorCode::validation_failed,
                                  "validate::custom(ctx) returned false");
        }
        return std::forward<T>(value);
      } else if constexpr (std::same_as<R, void>) {
        F(value, ctx);
        return std::forward<T>(value);
      } else {
        return F(std::forward<T>(value), ctx);
      }
    } else {
      using R = std::remove_cvref_t<decltype(F(std::forward<T>(value)))>;
      if constexpr (std::same_as<R, bool>) {
        if (!F(value)) {
          report_validation_error(ctx, ErrorCode::validation_failed,
                                  "validate::custom returned false");
        }
        return std::forward<T>(value);
      } else if constexpr (std::same_as<R, void>) {
        F(value);
        return std::forward<T>(value);
      } else {
        return F(std::forward<T>(value));
      }
    }
  }
};

struct Utf8Impl {
  template <class T, class Context>
    requires std::convertible_to<const T&, std::string_view>
  auto operator()(T&& value, Context& ctx) const -> decltype(auto) {
    if (!::hosq::detail::reader_error(ctx.reader(), ctx.reader_state())) {
      const std::string_view bytes = value;
      const auto invalid = hosq::detail::invalid_utf8_offset(bytes);
      if (invalid != bytes.size())
        ::hosq::detail::report_reader_error(
            ctx.reader(), ctx.reader_state(),
            {ErrorCode::invalid_utf8,
             ctx.reader().current_token(ctx.reader_state()).offset + invalid,
             "invalid string UTF-8"});
    }
    return std::forward<T>(value);
  }
};

struct IdentityImpl {
  template <class T>
  auto operator()(T&& value) const -> decltype(auto) {
    return std::forward<T>(value);
  }
};

}  // namespace detail

/// Checks that the decoded value equals V; reports invalid_choice on mismatch.
template <auto V>
inline constexpr auto match = step<detail::MatchImpl<V>>;

/// Checks that the decoded value equals one of Vs...; reports invalid_choice if
/// none match.
template <auto... Vs>
inline constexpr auto one_of = step<detail::OneOfImpl<Vs...>>;

/// Checks that the decoded value is within [Min, Max] (closed); reports
/// out_of_range otherwise.
template <auto Min, auto Max>
inline constexpr auto range = step<detail::RangeImpl<Min, Max>>;

/// Checks that the decoded value is >= Min; reports out_of_range otherwise.
template <auto Min>
inline constexpr auto min = step<detail::MinImpl<Min>>;

/// Checks that the decoded value is <= Max; reports out_of_range otherwise.
template <auto Max>
inline constexpr auto max = step<detail::MaxImpl<Max>>;

/// Checks that the decoded container/string is not empty; reports
/// validation_failed otherwise.
inline constexpr auto non_empty = step<detail::NonEmptyImpl>;

/// Checks that the decoded container/string has exactly size N; reports
/// validation_failed otherwise.
template <std::size_t N>
inline constexpr auto size = step<detail::SizeImpl<N>>;

/// Checks that the decoded container/string has size in [Min, Max]; reports
/// validation_failed otherwise.
template <std::size_t Min, std::size_t Max>
inline constexpr auto size_range = step<detail::SizeRangeImpl<Min, Max>>;

/// Applies predicate F(value) -> bool; reports validation_failed if F returns
/// false.
template <auto F>
inline constexpr auto predicate = step<detail::PredicateImpl<F>>;

/// Applies F to the value. F(value, ctx) is preferred over F(value).
/// If F returns bool, reports validation_failed when false and forwards the
/// original value. If F returns void, assumes F reports errors itself and
/// forwards the original value. Otherwise F's return value is forwarded to the
/// next pipeline step.
template <auto F>
inline constexpr auto custom = step<detail::CustomImpl<F>>;

/// Validate UTF-8 bytes and forward the original value without copying it.
inline constexpr auto utf8 = step<detail::Utf8Impl>;

/// Passes any value through unchanged.
inline constexpr auto identity = step<detail::IdentityImpl>;

}  // namespace hosq::validate
