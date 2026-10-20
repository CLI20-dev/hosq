#pragma once

#include <concepts>

#include "hosq/detail/type_traits.hh"
#include "hosq/error.hh"

namespace hosq {

/// Candidate rejection is distinct from a malformed/unrepresentable value.
/// Reader errors take precedence over a candidate's MatchResult terminal.
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
  static_assert(std::same_as<decltype(element.decode(ctx)), void> ||
                    MatchResultT<decltype(element.decode(ctx))>,
                "decoder terminals must be void or MatchResult; bool is data");
  bool matched = true;
  if constexpr (MatchResultT<decltype(element.decode(ctx))>)
    matched = element.decode(ctx).matched();
  else
    element.decode(ctx);
  if (const auto error = reader.error(state); error)
    return {DecodeStatus::error, error};
  return {matched ? DecodeStatus::matched : DecodeStatus::no_match, Error::ok()};
}

/// Ordinary execution cannot erase the matching effect, even when a concrete
/// reader/context changes a pipeline's terminal from its analysis result.
template <class Element, class Context>
auto invoke_required_decode(Element& element, Context& ctx) -> DecodeOutcome {
  static_assert(std::same_as<decltype(element.decode(ctx)), void>,
                "matching pipelines require Try candidate dispatch");
  return invoke_decode(element, ctx);
}

}  // namespace detail
}  // namespace hosq
