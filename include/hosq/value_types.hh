#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "hosq/reader_base.hh"

namespace hosq {

/// Borrowed text. The caller's input must outlive this value. `unknown` holds
/// semantic UTF-8, `json` an escaped string body, and `toml` a complete quoted
/// string literal. Encoding resolves the source syntax before escaping it.
struct RawTextValue {
  std::string_view value{};
  Format format = Format::unknown;
  friend auto operator==(const RawTextValue&, const RawTextValue&)
      -> bool = default;
};

/// Owned binary data. BSON subtype is part of its meaning; subtype zero is the
/// generic binary payload also representable by MessagePack's bin type.
struct BytesValue {
  std::vector<std::byte> value{};
  std::uint8_t subtype = 0;
  friend auto operator==(const BytesValue&, const BytesValue&) -> bool = default;
};

}  // namespace hosq
