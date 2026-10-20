#pragma once

#include <concepts>

#include "hosq/error.hh"

namespace hosq {

/// Candidate rejection is distinct from a malformed/unrepresentable value.
/// Reader errors take precedence over a pipeline's bool terminal.
enum class DecodeStatus { matched, no_match, error };

struct DecodeOutcome {
  DecodeStatus status = DecodeStatus::matched;
  Error error = Error::ok();

  [[nodiscard]] auto matched() const -> bool {
    return status == DecodeStatus::matched;
  }
};

namespace detail {

/// Invoke exactly once with the actual Reader/Context-dependent terminal type.
/// This adapter does not roll back storage, callbacks or the reader cursor.
template <class Element, class Context>
auto invoke_decode(Element& element, Context& ctx) -> DecodeOutcome {
  auto& reader = ctx.reader();
  auto& state = ctx.reader_state();
  if (const auto error = reader.error(state); error)
    return {DecodeStatus::error, error};
  bool matched = true;
  if constexpr (std::same_as<decltype(element.decode(ctx)), bool>)
    matched = element.decode(ctx);
  else
    element.decode(ctx);
  if (const auto error = reader.error(state); error)
    return {DecodeStatus::error, error};
  return {matched ? DecodeStatus::matched : DecodeStatus::no_match,
          Error::ok()};
}

/// At a position requiring one value, an exhausted candidate is an error.
/// Candidate dispatch itself consumes no_match and tries the next candidate.
template <class Reader, class State>
auto require_decode_match(const DecodeOutcome& outcome, Reader& reader,
                          State& state) -> Error {
  if (outcome.status == DecodeStatus::no_match)
    reader.report_error(
        state, Error{ErrorCode::candidate_rejected,
                     reader.current_token(state).offset,
                     "no decoder candidate accepted value"});
  return reader.error(state);
}

}  // namespace detail
}  // namespace hosq
