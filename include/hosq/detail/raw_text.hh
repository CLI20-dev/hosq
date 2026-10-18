#pragma once

#include <string>

#include "hosq/toml/reader.hh"
#include "hosq/value_types.hh"

namespace hosq::detail {
/// Resolves borrowed source syntax without retaining a concrete reader/state.
inline auto resolve_raw_text(const RawTextValue& raw, std::string& out) -> bool {
  out.clear();
  if (raw.format == Format::unknown) {
    if (invalid_utf8_offset(raw.value) != raw.value.size()) return false;
    out.assign(raw.value);
    return true;
  }
  if (raw.format == Format::json) return unescape_json_string(raw.value, out);
  if (raw.format == Format::toml) {
    // Reuse the TOML parser for quote styles, Unicode, and continuations.
    // Demand exactly one string value so caller-constructed raw values cannot
    // inject another member or reinterpret another scalar category as text.
    const std::string document = std::string{"v="} + std::string{raw.value};
    toml::Reader<> reader;
    toml::Reader<>::state_type state;
    reader.reset(state, document);
    if (reader.error(state)) return false;
    for (unsigned i = 0; i < 3; ++i)
      if (!reader.next_token(state)) return false;
    if (!reader.read_string(state, out)) return false;
    if (!reader.next_token(state) ||
        reader.current_token(state).type != TokenType::ObjectEnd ||
        !reader.next_token(state) ||
        reader.current_token(state).type != TokenType::End ||
        reader.error(state)) {
      out.clear();
      return false;
    }
    return true;
  }
  return false;
}
}  // namespace hosq::detail
