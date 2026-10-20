#pragma once

#include <memory>
#include <utility>
#include <variant>

#include "hosq/error.hh"

namespace hosq {

template <class R>
class Decoder;

/// A decode_document outcome, preserving its pipeline semantics. T is the
/// requested decodable type, including any Elem wrapper; no storage or schema
/// type is inferred. Borrowed values in T still depend on the caller's input
/// lifetime.
template <class T>
class ParseResult {
 public:
  using value_type = T;

  [[nodiscard]] auto has_value() const noexcept -> bool {
    return result_.index() == 0;
  }
  [[nodiscard]] explicit operator bool() const noexcept { return has_value(); }

  /// Checked access: throws std::bad_variant_access on failure. Moving the
  /// value out leaves a successful result containing a moved-from T.
  [[nodiscard]] auto value() & -> T& { return std::get<0>(result_); }
  [[nodiscard]] auto value() const& -> const T& { return std::get<0>(result_); }
  [[nodiscard]] auto value() && -> T&& {
    return std::get<0>(std::move(result_));
  }
  [[nodiscard]] auto value() const&& -> const T&& {
    return std::get<0>(std::move(result_));
  }

  [[nodiscard]] auto operator*() & -> T& { return value(); }
  [[nodiscard]] auto operator*() const& -> const T& { return value(); }
  [[nodiscard]] auto operator*() && -> T&& { return std::move(*this).value(); }
  [[nodiscard]] auto operator*() const&& -> const T&& {
    return std::move(*this).value();
  }
  [[nodiscard]] auto operator->() -> T* { return std::addressof(value()); }
  [[nodiscard]] auto operator->() const -> const T* {
    return std::addressof(value());
  }

  /// Returns the preserved decoding error, or Error::ok() on success.
  [[nodiscard]] auto error() const -> Error {
    if (has_value()) return Error::ok();
    if (const auto* error = std::get_if<1>(&result_)) return *error;
    // A throwing T assignment can leave std::variant valueless.
    return {ErrorCode::invalid_state, 0,
            "parse result lost its value during assignment"};
  }

 private:
  template <class R>
  friend class Decoder;

  explicit ParseResult(T&& value)
      : result_(std::in_place_index<0>, std::move(value)) {}
  explicit ParseResult(Error error) : result_(std::in_place_index<1>, error) {}

  std::variant<T, Error> result_;
};

}  // namespace hosq
