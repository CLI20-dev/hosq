#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <vector>

namespace hosq {

/// Unix seconds plus a nonnegative subsecond remainder; nanoseconds < 1e9.
struct InstantValue {
  std::int64_t seconds = 0;
  std::uint32_t nanoseconds = 0;
  friend auto operator==(const InstantValue&, const InstantValue&)
      -> bool = default;
};
/// Proleptic Gregorian calendar, four-digit years 0000 through 9999.
struct LocalDateValue {
  std::int32_t year = 1970;
  std::uint8_t month = 1;
  std::uint8_t day = 1;
  friend auto operator==(const LocalDateValue&, const LocalDateValue&)
      -> bool = default;
};
/// A wall-clock time without a zone. Second 60 is retained as a leap second.
struct LocalTimeValue {
  std::uint8_t hour = 0;
  std::uint8_t minute = 0;
  std::uint8_t second = 0;
  std::uint32_t nanoseconds = 0;
  friend auto operator==(const LocalTimeValue&, const LocalTimeValue&)
      -> bool = default;
};
struct LocalDateTimeValue {
  LocalDateValue date{};
  LocalTimeValue time{};
  friend auto operator==(const LocalDateTimeValue&, const LocalDateTimeValue&)
      -> bool = default;
};
struct OffsetDateTimeValue {
  LocalDateTimeValue local{};
  std::int16_t offset_minutes = 0;
  /// Preserve RFC3339 -00:00 separately from known UTC (+00:00 or Z).
  bool negative_zero_offset = false;
  friend auto operator==(const OffsetDateTimeValue&, const OffsetDateTimeValue&)
      -> bool = default;
};
/// Opaque IEEE decimal128 BSON wire bytes, without conversion or arithmetic.
struct Decimal128Value {
  std::array<std::byte, 16> bytes{};
  friend auto operator==(const Decimal128Value&, const Decimal128Value&)
      -> bool = default;
};
struct ObjectIdValue {
  std::array<std::byte, 12> bytes{};
  friend auto operator==(const ObjectIdValue&, const ObjectIdValue&)
      -> bool = default;
};
/// MongoDB logical timestamp; deliberately distinct from an Instant.
struct BsonTimestampValue {
  std::uint32_t seconds = 0;
  std::uint32_t increment = 0;
  friend auto operator==(const BsonTimestampValue&, const BsonTimestampValue&)
      -> bool = default;
};
/// MessagePack's signed extension tag and owned opaque payload.
struct ExtensionValue {
  std::int8_t type = 0;
  std::vector<std::byte> payload{};
  friend auto operator==(const ExtensionValue&, const ExtensionValue&)
      -> bool = default;
};

namespace detail {
template <class T>
inline constexpr bool is_extended_value =
    std::is_same_v<T, InstantValue> || std::is_same_v<T, LocalDateValue> ||
    std::is_same_v<T, LocalTimeValue> || std::is_same_v<T, LocalDateTimeValue> ||
    std::is_same_v<T, OffsetDateTimeValue> ||
    std::is_same_v<T, Decimal128Value> || std::is_same_v<T, ObjectIdValue> ||
    std::is_same_v<T, BsonTimestampValue> || std::is_same_v<T, ExtensionValue>;
}  // namespace detail
}  // namespace hosq
