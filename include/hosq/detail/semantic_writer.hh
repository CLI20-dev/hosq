#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>

#include "hosq/detail/native_encode.hh"
#include "hosq/detail/utf8.hh"
#include "hosq/writer_base.hh"

namespace hosq::detail {
// Shared reservation/error/size mechanics; syntax and scalar representations
// stay with the concrete writer. Low-level scalar reservations contain raw
// semantic strings (context 1), not text or pre-encoded binary numbers.
template <class Derived, std::size_t MaxOutput>
class SemanticWriter {
 public:
  using output_type = std::string_view;
  using buffer_type = std::span<char>;
  explicit SemanticWriter(std::string& output) : output_(output) {
    (void)room(0);
  }
  SemanticWriter(const SemanticWriter&) = delete;
  auto operator=(const SemanticWriter&) -> SemanticWriter& = delete;
  SemanticWriter(SemanticWriter&&) = delete;
  auto operator=(SemanticWriter&&) -> SemanticWriter& = delete;
  auto output() const noexcept -> output_type { return output_; }
  auto error() const noexcept -> Error { return error_; }
  void report_error(Error error) noexcept {
    if (!error_) error_ = error;
  }
  auto write(const WriteRequest& request) -> buffer_type {
    if (!ready()) return {};
    if (request.type != TokenType::Scalar) {
      if (request.context || request.length) {
        fail(ErrorCode::invalid_token,
             "structural tokens require zero length/context");
        return {};
      }
      static_cast<Derived&>(*this).write_structure(request.type);
      return {};
    }
    if (request.context != 1 || request.length == unresolved_length) {
      fail(ErrorCode::invalid_token,
           "reserve strings with context 1; use typed scalar methods");
      return {};
    }
    if (request.length > MaxOutput || request.length > scratch_.max_size()) {
      fail(ErrorCode::output_too_large,
           "string reservation exceeds output limit");
      return {};
    }
    if (!static_cast<Derived&>(*this).check_string_size(request.length) ||
        !room(request.length))
      return {};
    scratch_.resize(request.length);
    pending_ = true;
    return {scratch_.data(), scratch_.size()};
  }
  auto commit(std::size_t written) -> bool {
    if (error_) return false;
    if (!pending_ || written > scratch_.size())
      return fail(ErrorCode::invalid_state,
                  "commit without matching string reservation");
    scratch_.resize(written);
    pending_ = false;
    return static_cast<Derived&>(*this).write_string(scratch_);
  }

 protected:
  auto fail(ErrorCode code, std::string_view why) -> bool {
    report_error({code, output_.size(), why});
    return false;
  }
  auto ready() -> bool {
    if (error_) return false;
    if (finished_ || pending_)
      return fail(ErrorCode::invalid_state,
                  "writer has finished or has an uncommitted string");
    return true;
  }
  auto room(std::size_t count) -> bool {
    const auto limit = std::min(MaxOutput, output_.max_size());
    if (output_.size() > limit || count > limit - output_.size())
      return fail(ErrorCode::output_too_large, "writer output limit");
    return true;
  }
  auto append(std::string_view bytes) -> bool {
    if (!room(bytes.size())) return false;
    output_.append(bytes);
    return true;
  }
  auto byte(unsigned value) -> bool {
    const char c = static_cast<char>(value);
    return append({&c, 1});
  }
  auto integer_bytes(std::uint64_t value, unsigned n, bool little = false)
      -> bool {
    std::array<char, 8> data{};
    for (unsigned i = 0; i < n; ++i)
      data[i] = static_cast<char>(value >> (8 * (little ? i : n - i - 1)));
    return append({data.data(), n});
  }
  auto valid_string(std::string_view text) -> bool {
    if (!static_cast<Derived&>(*this).check_string_size(text.size()))
      return false;
    if (invalid_utf8_offset(text) != text.size())
      return fail(ErrorCode::invalid_utf8, "writer string must be valid UTF-8");
    return true;
  }
  template <class T>
  auto binary_double(T value, double& out) -> bool {
    if (std::isfinite(value) && (value < -std::numeric_limits<double>::max() ||
                                 value > std::numeric_limits<double>::max()))
      return fail(ErrorCode::out_of_range,
                  "floating value exceeds binary64 range");
    out = static_cast<double>(value);
    if constexpr (sizeof(T) > sizeof(double)) {
      if (std::isfinite(value) && static_cast<T>(out) != value)
        return fail(ErrorCode::out_of_range,
                    "floating value is not representable as binary64");
    }
    return true;
  }
  std::string& output_;
  bool finished_ = false;

 private:
  std::string scratch_;
  bool pending_ = false;
  Error error_ = Error::ok();
};

// JSON and TOML basic strings share these escapes. Escaping DEL also keeps
// the result valid in TOML 1.0, which forbids a literal U+007F.
inline auto quoted_string(std::string_view input) -> std::string {
  return "\"" + escaped_string_body(input) + "\"";
}
}  // namespace hosq::detail
