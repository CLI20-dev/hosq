#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace hosq {

/// Owned binary data. BSON subtype is part of its meaning; subtype zero is the
/// generic binary payload also representable by MessagePack's bin type.
struct BytesValue {
  std::vector<std::byte> value{};
  std::uint8_t subtype = 0;
  friend auto operator==(const BytesValue&, const BytesValue&) -> bool = default;
};

}  // namespace hosq
