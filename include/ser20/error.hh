#pragma once

#include <string>

namespace ser20 {

/**
 * @brief Identifies the specific failure that occurred during parsing or
 * validation.
 *
 * Codes are grouped by category:
 * - **Parse errors** (`unknown_option` … `duplicate_argument`): problems
 *   tokenising or dispatching command-line tokens.
 * - **Conversion/validation errors** (`conversion_error` … `validation_failed`):
 *   problems converting a raw string to the target type or failing a constraint.
 * - **Informational codes** (`help_requested`, `exit_success`): not true errors;
 *   the parser uses these to signal that the program should print help and exit
 * 0.
 */
enum class ErrorCode {
  unknown_option,       ///< An unrecognised option name was encountered.
  unknown_command,      ///< An unrecognised subcommand name was encountered.
  unexpected_argument,  ///< A positional argument was supplied when none was
                        ///< expected.
  missing_value,        ///< An option that requires a value was given none.
  invalid_value,  ///< A value could not be interpreted as the required type.
  duplicate_argument,  ///< An option or positional that forbids repetition was
                       ///< seen more than once.

  conversion_error,  ///< Generic failure during string-to-type conversion.
  out_of_range,  ///< A numeric value fell outside the representable range of the
                 ///< target type.
  missing_required,    ///< A required option or positional was not provided.
  mutually_exclusive,  ///< Two mutually-exclusive options were both supplied.
  dependency_missing,  ///< An option that depends on another option was given
                       ///< without it.
  invalid_choice,      ///< A value was not among the allowed choices.
  validation_failed,   ///< A user-defined or built-in validation predicate
                       ///< rejected the value.
  help_requested,  ///< The `--help` flag (or equivalent) was seen; print help
                   ///< and exit 0.
  exit_success,    ///< The action pipeline requested a clean exit with code 0.
  unknown_error,   ///< Sentinel/default; indicates no error has been set.
};

/**
 * @brief Broad classification of an error for structured error handling.
 *
 * - `parse`      : The error arose during tokenisation or dispatch.
 * - `conversion` : The error arose while converting a string to a typed value.
 * - `validation` : The error arose while validating a converted value.
 */
enum class ErrorKind {
  parse,
  conversion,
  validation,
};

/**
 * @brief Returns a human-readable description of an `ErrorCode`.
 *
 * @param code The error code to describe.
 * @return A non-owning string view of a static description string.
 */
[[nodiscard]] constexpr auto to_string(ErrorCode code) noexcept
    -> std::string_view {
  switch (code) {
    case ErrorCode::unknown_option:
      return "unknown option";
    case ErrorCode::unknown_command:
      return "unknown command";
    case ErrorCode::unexpected_argument:
      return "unexpected argument";
    case ErrorCode::missing_value:
      return "missing value";
    case ErrorCode::invalid_value:
      return "invalid value";
    case ErrorCode::duplicate_argument:
      return "duplicate argument";
    case ErrorCode::conversion_error:
      return "conversion error";
    case ErrorCode::out_of_range:
      return "out of range";
    case ErrorCode::missing_required:
      return "missing required argument";
    case ErrorCode::mutually_exclusive:
      return "mutually exclusive argument";
    case ErrorCode::dependency_missing:
      return "dependency missing";
    case ErrorCode::invalid_choice:
      return "invalid choice";
    case ErrorCode::validation_failed:
      return "validation failed";
    case ErrorCode::help_requested:
      return "help requested";
    case ErrorCode::exit_success:
      return "exit success";
    case ErrorCode::unknown_error:
      return "unknown error";
  }

  return "unknown error";
}

/**
 * @brief Carries all context about a single parse/validation failure.
 *
 * A `ParseError` is embedded in both `ParseResult<T>` and `ActionResult<T>`.
 * When `code == ErrorCode::unknown_error` the struct represents the *absence*
 * of an error (i.e. success).
 *
 * Typical usage after parsing:
 * @code
 *   auto result = ser20::parse<MyArgs>(argc, argv);
 *   if (!result) {
 *     auto& e = result.error;
 *     auto& out = e.use_stdout() ? std::cout : std::cerr;
 *     out << e.message() << '\n';
 *     std::exit(e.exit_code());
 *   }
 * @endcode
 */
struct ParseError {
  /** @brief The specific error code; `unknown_error` means no error. */
  ErrorCode code = ErrorCode::unknown_error;

  /** @brief Broad category of the error. */
  ErrorKind kind = ErrorKind::parse;

  /** @brief Zero-based index into `argv` where the error was detected, or -1 if
   * unknown. */
  int position = -1;

  /** @brief The token or option name that caused the error (may be empty). */
  std::string subject{};

  /** @brief Additional human-readable context about the failure (may be empty).
   */
  std::string detail{};

  /**
   * @brief Returns true when `position` contains a valid `argv` index.
   *
   * @return `true` if `position >= 0`.
   */
  [[nodiscard]] constexpr auto has_position() const noexcept -> bool {
    return position >= 0;
  }

  /**
   * @brief Returns the process exit code appropriate for this error.
   *
   * @return 0 for `help_requested` or `exit_success`, 1 for all other codes.
   */
  [[nodiscard]] constexpr auto exit_code() const noexcept -> int {
    return code == ErrorCode::help_requested || code == ErrorCode::exit_success
               ? 0
               : 1;
  }

  /**
   * @brief Returns true when the error message should be written to stdout.
   *
   * Help text and exit-success messages go to stdout; all other errors go to
   * stderr.
   *
   * @return `true` for `help_requested` or `exit_success`.
   */
  [[nodiscard]] constexpr auto use_stdout() const noexcept -> bool {
    return code == ErrorCode::help_requested || code == ErrorCode::exit_success;
  }

  /**
   * @brief Formats a human-readable error message.
   *
   * - For `help_requested` or `exit_success`: returns `detail` verbatim.
   * - Otherwise: combines `to_string(code)`, `subject`, `detail`, and
   *   (if available) the `argv` position into a single string.
   *
   * @return A formatted error string.
   */
  [[nodiscard]] constexpr auto message() const -> std::string {
    if (code == ErrorCode::help_requested) {
      return detail;
    }

    if (code == ErrorCode::exit_success) {
      return detail;
    }

    std::string out{to_string(code)};
    if (!subject.empty()) {
      out += ": ";
      out += subject;
    }

    if (!detail.empty()) {
      out += " (";
      out += detail;
      out += ")";
    }

    if (has_position()) {
      out += " at argv[";
      out += std::to_string(position);
      out += "]";
    }

    return out;
  }

  /**
   * @brief Returns true when an actual error is present.
   *
   * @return `true` if `code != unknown_error`.
   */
  [[nodiscard]] constexpr auto has_error() const noexcept -> bool {
    return code != ErrorCode::unknown_error;
  }
};

}  // namespace ser20
