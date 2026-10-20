#pragma once

#include <string>

#include "hosq/detail/utf8.hh"
#include "hosq/json/string_codec.hh"
#include "hosq/toml/string_codec.hh"
#include "hosq/value_types.hh"

namespace hosq::detail {
/// Resolve source syntax with lightweight format-owned string codecs.
inline auto resolve_raw_text(const RawTextValue& raw, std::string& out) -> bool {
  out.clear();
  if (raw.format == Format::unknown) {
    if (invalid_utf8_offset(raw.value) != raw.value.size()) return false;
    out.assign(raw.value);
    return true;
  }
  if (raw.format == Format::json)
    return json::string_codec::unescape_json_string(raw.value, out);
  if (raw.format == Format::toml)
    return toml::string_codec::resolve(raw.value, out);
  return false;
}
}  // namespace hosq::detail
