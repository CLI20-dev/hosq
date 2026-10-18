#pragma once

#include <cstdint>
#include <limits>
#include <string>
#include <string_view>

#include "hosq/extended_value_types.hh"

namespace hosq::detail {

enum class TemporalKind { invalid, date, time, local_datetime, offset_datetime };

inline constexpr auto valid_instant(const InstantValue& value) -> bool {
  return value.nanoseconds < 1'000'000'000;
}

inline constexpr auto valid_date(const LocalDateValue& value) -> bool {
  if (value.year < 0 || value.year > 9999 || value.month < 1 ||
      value.month > 12 || value.day < 1)
    return false;
  constexpr unsigned lengths[] = {31, 28, 31, 30, 31, 30,
                                  31, 31, 30, 31, 30, 31};
  const bool leap =
      value.year % 4 == 0 && (value.year % 100 != 0 || value.year % 400 == 0);
  return value.day <=
         lengths[value.month - 1] + (value.month == 2 && leap ? 1u : 0u);
}

inline constexpr auto valid_time(const LocalTimeValue& value) -> bool {
  return value.hour < 24 && value.minute < 60 && value.second <= 60 &&
         value.nanoseconds < 1'000'000'000;
}

inline constexpr auto valid_local_datetime(const LocalDateTimeValue& value)
    -> bool {
  return valid_date(value.date) && valid_time(value.time);
}

inline constexpr auto valid_offset_datetime(const OffsetDateTimeValue& value)
    -> bool {
  return valid_local_datetime(value.local) && value.offset_minutes >= -1439 &&
         value.offset_minutes <= 1439 &&
         (!value.negative_zero_offset || value.offset_minutes == 0);
}

inline constexpr auto instant_from_milliseconds(std::int64_t value)
    -> InstantValue {
  auto seconds = value / 1000;
  auto remainder = value % 1000;
  if (remainder < 0) {
    --seconds;
    remainder += 1000;
  }
  return {seconds, static_cast<std::uint32_t>(remainder * 1'000'000)};
}

inline constexpr auto instant_to_milliseconds(const InstantValue& value,
                                              std::int64_t& result) -> bool {
  if (!valid_instant(value) || value.nanoseconds % 1'000'000 != 0) return false;
  constexpr auto minimum = std::numeric_limits<std::int64_t>::min();
  constexpr auto maximum = std::numeric_limits<std::int64_t>::max();
  constexpr auto minimum_seconds = minimum / 1000 - 1;
  constexpr auto minimum_fraction = 1000 + minimum % 1000;
  constexpr auto maximum_seconds = maximum / 1000;
  constexpr auto maximum_fraction = maximum % 1000;
  const auto fraction = value.nanoseconds / 1'000'000;
  if (value.seconds < minimum_seconds || value.seconds > maximum_seconds ||
      (value.seconds == minimum_seconds && fraction < minimum_fraction) ||
      (value.seconds == maximum_seconds && fraction > maximum_fraction))
    return false;
  // The floor-second representation of INT64_MIN milliseconds cannot itself
  // be multiplied by 1000. Add the checked remainder to INT64_MIN instead.
  result = value.seconds == minimum_seconds
               ? minimum + (fraction - minimum_fraction)
               : value.seconds * 1000 + fraction;
  return true;
}

namespace temporal_impl {
// Gregorian civil-date algorithms use March as the beginning of the year.
// All callers supply validated four-digit years; no platform time API is used.
inline constexpr auto days_from_date(const LocalDateValue& date)
    -> std::int64_t {
  const auto year = date.year - (date.month <= 2 ? 1 : 0);
  const auto era = (year >= 0 ? year : year - 399) / 400;
  const auto yoe = static_cast<unsigned>(year - era * 400);
  const auto month = static_cast<unsigned>(date.month);
  const auto doy =
      (153 * (month > 2 ? month - 3 : month + 9) + 2) / 5 + date.day - 1;
  const auto doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return static_cast<std::int64_t>(era) * 146097 + doe - 719468;
}

inline constexpr auto date_from_days(std::int64_t days) -> LocalDateValue {
  days += 719468;
  const auto era = (days >= 0 ? days : days - 146096) / 146097;
  const auto doe = static_cast<unsigned>(days - era * 146097);
  const auto yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  auto year = static_cast<std::int32_t>(yoe + era * 400);
  const auto doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const auto mp = (5 * doy + 2) / 153;
  const auto day = doy - (153 * mp + 2) / 5 + 1;
  const auto month = mp < 10 ? mp + 3 : mp - 9;
  year += month <= 2 ? 1 : 0;
  return {year, static_cast<std::uint8_t>(month),
          static_cast<std::uint8_t>(day)};
}

inline constexpr auto digits(std::string_view text, unsigned& result) -> bool {
  result = 0;
  for (const auto ch : text) {
    if (ch < '0' || ch > '9') return false;
    result = result * 10 + static_cast<unsigned>(ch - '0');
  }
  return true;
}

inline auto append_digits(std::string& text, unsigned value, unsigned width)
    -> void {
  const auto start = text.size();
  text.append(width, '0');
  while (width != 0) {
    text[start + --width] = static_cast<char>('0' + value % 10);
    value /= 10;
  }
}
}  // namespace temporal_impl

inline constexpr auto offset_to_instant(const OffsetDateTimeValue& value,
                                        InstantValue& result) -> bool {
  if (!valid_offset_datetime(value) || value.local.time.second == 60 ||
      value.negative_zero_offset)
    return false;
  const auto& time = value.local.time;
  result = {temporal_impl::days_from_date(value.local.date) * 86400 +
                time.hour * 3600 + time.minute * 60 + time.second -
                static_cast<std::int64_t>(value.offset_minutes) * 60,
            time.nanoseconds};
  return true;
}

inline constexpr auto instant_to_offset(const InstantValue& value,
                                        OffsetDateTimeValue& result) -> bool {
  constexpr auto first_second = temporal_impl::days_from_date({0, 1, 1}) * 86400;
  constexpr auto last_second =
      temporal_impl::days_from_date({9999, 12, 31}) * 86400 + 86399;
  if (!valid_instant(value) || value.seconds < first_second ||
      value.seconds > last_second)
    return false;
  auto days = value.seconds / 86400;
  auto seconds = value.seconds % 86400;
  if (seconds < 0) {
    --days;
    seconds += 86400;
  }
  result = {{temporal_impl::date_from_days(days),
             {static_cast<std::uint8_t>(seconds / 3600),
              static_cast<std::uint8_t>((seconds / 60) % 60),
              static_cast<std::uint8_t>(seconds % 60), value.nanoseconds}},
            0,
            false};
  return true;
}

inline auto parse_local_date(std::string_view text, LocalDateValue& result)
    -> bool {
  if (text.size() != 10 || text[4] != '-' || text[7] != '-') return false;
  unsigned year, month, day;
  if (!temporal_impl::digits(text.substr(0, 4), year) ||
      !temporal_impl::digits(text.substr(5, 2), month) ||
      !temporal_impl::digits(text.substr(8, 2), day))
    return false;
  const LocalDateValue value{static_cast<std::int32_t>(year),
                             static_cast<std::uint8_t>(month),
                             static_cast<std::uint8_t>(day)};
  if (!valid_date(value)) return false;
  result = value;
  return true;
}

inline auto parse_local_time_impl(std::string_view text, LocalTimeValue& result,
                                  bool allow_precision_loss) -> bool {
  if (text.size() < 8 || text[2] != ':' || text[5] != ':') return false;
  unsigned hour, minute, second;
  if (!temporal_impl::digits(text.substr(0, 2), hour) ||
      !temporal_impl::digits(text.substr(3, 2), minute) ||
      !temporal_impl::digits(text.substr(6, 2), second))
    return false;
  std::uint32_t fraction = 0;
  if (text.size() > 8) {
    if (text[8] != '.' || text.size() == 9) return false;
    for (std::size_t i = 9; i < text.size(); ++i) {
      if (text[i] < '0' || text[i] > '9') return false;
      if (i < 18)
        fraction = fraction * 10 + static_cast<unsigned>(text[i] - '0');
      else if (text[i] != '0' && !allow_precision_loss)
        return false;  // Never silently truncate sub-nanosecond precision.
    }
    for (auto digits = text.size() - 9; digits < 9; ++digits) fraction *= 10;
  }
  const LocalTimeValue value{static_cast<std::uint8_t>(hour),
                             static_cast<std::uint8_t>(minute),
                             static_cast<std::uint8_t>(second), fraction};
  if (!valid_time(value)) return false;
  result = value;
  return true;
}

inline auto parse_local_time(std::string_view text, LocalTimeValue& result)
    -> bool {
  return parse_local_time_impl(text, result, false);
}

inline auto parse_local_datetime_impl(std::string_view text,
                                      LocalDateTimeValue& result,
                                      bool allow_precision_loss) -> bool {
  if (text.size() < 19 ||
      (text[10] != 'T' && text[10] != 't' && text[10] != ' '))
    return false;
  LocalDateTimeValue value;
  if (!parse_local_date(text.substr(0, 10), value.date) ||
      !parse_local_time_impl(text.substr(11), value.time, allow_precision_loss))
    return false;
  result = value;
  return true;
}

inline auto parse_local_datetime(std::string_view text,
                                 LocalDateTimeValue& result) -> bool {
  return parse_local_datetime_impl(text, result, false);
}

inline auto parse_offset_datetime_impl(std::string_view text,
                                       OffsetDateTimeValue& result,
                                       bool allow_precision_loss) -> bool {
  if (text.size() < 20) return false;
  OffsetDateTimeValue value;
  if (text.back() == 'Z' || text.back() == 'z') {
    if (!parse_local_datetime_impl(text.substr(0, text.size() - 1), value.local,
                                   allow_precision_loss))
      return false;
  } else {
    if (text.size() < 25) return false;
    const auto start = text.size() - 6;
    const auto sign = text[start];
    if ((sign != '+' && sign != '-') || text[start + 3] != ':') return false;
    unsigned hour, minute;
    if (!temporal_impl::digits(text.substr(start + 1, 2), hour) ||
        !temporal_impl::digits(text.substr(start + 4, 2), minute) || hour > 23 ||
        minute > 59 ||
        !parse_local_datetime_impl(text.substr(0, start), value.local,
                                   allow_precision_loss))
      return false;
    const auto offset = static_cast<std::int16_t>(hour * 60 + minute);
    value.offset_minutes = sign == '-' ? -offset : offset;
    value.negative_zero_offset = sign == '-' && offset == 0;
  }
  result = value;
  return true;
}

inline auto parse_offset_datetime(std::string_view text,
                                  OffsetDateTimeValue& result) -> bool {
  return parse_offset_datetime_impl(text, result, false);
}

// Formatting requires a validated value, and preserves its nanosecond value.
inline auto format_local_date(const LocalDateValue& value) -> std::string {
  std::string result;
  temporal_impl::append_digits(result, static_cast<unsigned>(value.year), 4);
  result += '-';
  temporal_impl::append_digits(result, value.month, 2);
  result += '-';
  temporal_impl::append_digits(result, value.day, 2);
  return result;
}

inline auto format_local_time(const LocalTimeValue& value) -> std::string {
  std::string result;
  temporal_impl::append_digits(result, value.hour, 2);
  result += ':';
  temporal_impl::append_digits(result, value.minute, 2);
  result += ':';
  temporal_impl::append_digits(result, value.second, 2);
  if (value.nanoseconds != 0) {
    result += '.';
    temporal_impl::append_digits(result, value.nanoseconds, 9);
    while (result.back() == '0') result.pop_back();
  }
  return result;
}

inline auto format_local_datetime(const LocalDateTimeValue& value)
    -> std::string {
  return format_local_date(value.date) + 'T' + format_local_time(value.time);
}

inline auto format_offset_datetime(const OffsetDateTimeValue& value)
    -> std::string {
  auto result = format_local_datetime(value.local);
  if (value.offset_minutes == 0 && !value.negative_zero_offset) {
    result += 'Z';
  } else {
    const auto offset =
        value.offset_minutes < 0 ? -value.offset_minutes : value.offset_minutes;
    result += value.offset_minutes < 0 || value.negative_zero_offset ? '-' : '+';
    temporal_impl::append_digits(result, static_cast<unsigned>(offset / 60), 2);
    result += ':';
    temporal_impl::append_digits(result, static_cast<unsigned>(offset % 60), 2);
  }
  return result;
}

inline auto classify_temporal(std::string_view text) -> TemporalKind {
  LocalDateValue date;
  LocalTimeValue time;
  LocalDateTimeValue local;
  OffsetDateTimeValue offset;
  if (parse_local_date(text, date)) return TemporalKind::date;
  // Classification checks lexical/calendar validity separately from the
  // nanosecond storage precision, so a reader can distinguish precision loss
  // from a different semantic category. Parsing never permits precision loss.
  if (parse_local_time_impl(text, time, true)) return TemporalKind::time;
  if (parse_local_datetime_impl(text, local, true))
    return TemporalKind::local_datetime;
  if (parse_offset_datetime_impl(text, offset, true))
    return TemporalKind::offset_datetime;
  return TemporalKind::invalid;
}

}  // namespace hosq::detail
