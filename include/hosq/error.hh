#pragma once

#include <string_view>

namespace hosq {

enum class ErrorCode {
  unknown_error,
  // Input / output
  unexpected_eof,
  trailing_data,
  depth_limit_exceeded,
  input_too_large,
  output_too_large,
  write_failed,

  // Reader/Writer state
  invalid_state,

  // Syntax / representation
  invalid_token,
  unexpected_token,
  invalid_escape,
  invalid_utf8,
  invalid_number,
  invalid_type_marker,
  invalid_length,
  size_mismatch,
  unsupported_feature,
  // Schema / mapping
  unknown_member,
  missing_required,
  duplicate_member,
  type_mismatch,
  unsupported_type,
  // Conversion / validation
  conversion_error,
  out_of_range,
  invalid_choice,
  validation_failed,
  // Encode-side value problem
  invalid_object_state,

  // Candidate
  candidate_rejected,

  ok,
};

enum class ErrorKind {
  input,       ///< The input itself or configured input limits caused failure.
  output,      ///< Writing output or configured output limits caused failure.
  limit,       ///< A configured size/depth limit was exceeded.
  state,       ///< The reader/writer was used in an invalid state.
  syntax,      ///< Text/binary representation is malformed.
  schema,      ///< The value structure did not match the declared schema.
  conversion,  ///< A value could not be converted to the requested C++ type.
  validation,  ///< A value failed a validation rule.
  matching,    ///< No matching candidate was found for the input.
  ok,          ///< No error.
};

[[nodiscard]] constexpr auto kind_of(ErrorCode code) noexcept -> ErrorKind {
  switch (code) {
    case ErrorCode::unexpected_eof:
    case ErrorCode::trailing_data:
      return ErrorKind::input;
    case ErrorCode::write_failed:
      return ErrorKind::output;
    case ErrorCode::depth_limit_exceeded:
    case ErrorCode::input_too_large:
    case ErrorCode::output_too_large:
      return ErrorKind::limit;
    case ErrorCode::invalid_state:
      return ErrorKind::state;
    case ErrorCode::invalid_token:
    case ErrorCode::unexpected_token:
    case ErrorCode::invalid_escape:
    case ErrorCode::invalid_utf8:
    case ErrorCode::invalid_number:
    case ErrorCode::invalid_type_marker:
    case ErrorCode::unsupported_feature:
      return ErrorKind::syntax;
    case ErrorCode::unknown_member:
    case ErrorCode::missing_required:
    case ErrorCode::duplicate_member:
    case ErrorCode::type_mismatch:
    case ErrorCode::unsupported_type:
    case ErrorCode::invalid_length:
    case ErrorCode::size_mismatch:
      return ErrorKind::schema;
    case ErrorCode::conversion_error:
    case ErrorCode::out_of_range:
      return ErrorKind::conversion;
    case ErrorCode::invalid_choice:
    case ErrorCode::validation_failed:
    case ErrorCode::invalid_object_state:
      return ErrorKind::validation;
    case ErrorCode::unknown_error:
      return ErrorKind::syntax;
    case ErrorCode::candidate_rejected:
      return ErrorKind::matching;
    case ErrorCode::ok:
      return ErrorKind::ok;
  }
  return ErrorKind::syntax;
}

[[nodiscard]] constexpr auto to_string(ErrorCode code) noexcept
    -> std::string_view {
  switch (code) {
    case ErrorCode::unknown_error:
      return "unknown error";
    case ErrorCode::unexpected_eof:
      return "unexpected end of input";
    case ErrorCode::trailing_data:
      return "trailing data";
    case ErrorCode::depth_limit_exceeded:
      return "depth limit exceeded";
    case ErrorCode::input_too_large:
      return "input too large";
    case ErrorCode::output_too_large:
      return "output too large";
    case ErrorCode::write_failed:
      return "write failed";
    case ErrorCode::invalid_state:
      return "invalid state";
    case ErrorCode::invalid_token:
      return "invalid token";
    case ErrorCode::unexpected_token:
      return "unexpected token";
    case ErrorCode::invalid_escape:
      return "invalid escape sequence";
    case ErrorCode::invalid_utf8:
      return "invalid UTF-8";
    case ErrorCode::invalid_number:
      return "invalid number";
    case ErrorCode::invalid_type_marker:
      return "invalid type marker";
    case ErrorCode::invalid_length:
      return "invalid length";
    case ErrorCode::size_mismatch:
      return "size mismatch";
    case ErrorCode::unsupported_feature:
      return "unsupported feature";
    case ErrorCode::unknown_member:
      return "unknown member";
    case ErrorCode::missing_required:
      return "missing required member";
    case ErrorCode::duplicate_member:
      return "duplicate member";
    case ErrorCode::type_mismatch:
      return "type mismatch";
    case ErrorCode::unsupported_type:
      return "unsupported type";
    case ErrorCode::conversion_error:
      return "conversion error";
    case ErrorCode::out_of_range:
      return "out of range";
    case ErrorCode::invalid_choice:
      return "invalid choice";
    case ErrorCode::validation_failed:
      return "validation failed";
    case ErrorCode::invalid_object_state:
      return "invalid object state";
    case ErrorCode::candidate_rejected:
      return "candidate rejected";
    case ErrorCode::ok:
      return "ok";
  }
  return "unknown error";
}

struct Error {
  ErrorCode code{ErrorCode::unknown_error};
  size_t offset{0};
  std::string_view detail{};

  [[nodiscard]] static constexpr auto ok() noexcept -> Error {
    return Error{.code = ErrorCode::ok, .offset = 0, .detail = ""};
  }

  [[nodiscard]] constexpr auto is_ok() const noexcept -> bool {
    return code == ErrorCode::ok;
  }

  [[nodiscard]] constexpr auto has_error() const noexcept -> bool {
    return code != ErrorCode::ok;
  }

  [[nodiscard]] constexpr explicit operator bool() const noexcept {
    return has_error();
  }

  [[nodiscard]] constexpr auto kind() const noexcept -> ErrorKind {
    return kind_of(code);
  }
};

}  // namespace hosq
