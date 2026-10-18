#pragma once

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <limits>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>

#include "hosq/bytes_view.hh"
#include "hosq/detail/raw_text.hh"
#include "hosq/detail/type_traits.hh"
#include "hosq/detail/utf8.hh"
#include "hosq/extended_value_types.hh"
#include "hosq/fwd.hh"
#include "hosq/map.hh"
#include "hosq/writer_base.hh"

namespace hosq::detail {
template <class T>
struct IsEncodeOptional : std::false_type {};
template <class T>
struct IsEncodeOptional<std::optional<T>> : std::true_type {};
template <class T>
struct IsEncodeVariant : std::false_type {};
template <class... T>
struct IsEncodeVariant<std::variant<T...>> : std::true_type {};
template <class T>
struct IsEncodeMap : std::false_type {};
template <class K, class V>
struct IsEncodeMap<Map<K, V>> : std::true_type {};

template <class W>
auto encode_error(W& writer, ErrorCode code, std::string_view why) -> bool {
  if constexpr (requires { writer.report_error(Error{}); })
    writer.report_error({code, writer.output().size(), why});
  else {
    // Preserve the original WriterConcept; legacy writers need no new method.
    (void)writer.write({0, TokenType::Error, 0});
  }
  return false;
}
template <class W>
auto encode_token(W& writer, TokenType type) -> bool {
  (void)writer.write({0, type, 0});
  return writer.error().is_ok();
}
// Legacy JSON writers accept encoded bytes through WriteRequest. This fallback
// leaves encode::number/boolean/string_view and custom pipelines unchanged.
template <class W>
auto encode_json_scalar(W& writer, std::string_view value, bool string) -> bool {
  const auto buffer = writer.write(
      {value.size(), TokenType::Scalar,
       static_cast<std::uint8_t>(string ? JsonTokenContext::string
                                        : JsonTokenContext::literal)});
  if (writer.error()) return false;
  std::copy(value.begin(), value.end(), buffer.data());
  return writer.commit(value.size());
}
inline auto escaped_string_body(std::string_view value) -> std::string {
  constexpr char hex[] = "0123456789abcdef";
  std::string out;
  for (unsigned char c : value) {
    switch (c) {
      case '"':
        out += "\\\"";
        break;
      case '\\':
        out += "\\\\";
        break;
      case '\b':
        out += "\\b";
        break;
      case '\t':
        out += "\\t";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\f':
        out += "\\f";
        break;
      case '\r':
        out += "\\r";
        break;
      default:
        if (c < 0x20 || c == 0x7f) {
          out += "\\u00";
          out += hex[c >> 4];
          out += hex[c & 15];
        } else
          out += static_cast<char>(c);
    }
  }
  return out;
}

template <class T, class W>
auto encode_native_value(const T& value, W& writer) -> bool {
  using V = std::remove_cvref_t<T>;
  if (writer.error()) return false;
  if constexpr (IsEncodeOptional<V>::value) {
    if (!value)
      return encode_error(
          writer, ErrorCode::invalid_object_state,
          "empty optional is not an encoded value; use nullptr for null");
    return encode_native_value(*value, writer);
  } else if constexpr (IsEncodeVariant<V>::value) {
    if (value.valueless_by_exception())
      return encode_error(writer, ErrorCode::invalid_object_state,
                          "valueless variant cannot be encoded");
    return std::visit(
        [&](const auto& item) { return encode_native_value(item, writer); },
        value);
  } else if constexpr (std::same_as<V, std::nullptr_t>) {
    if constexpr (requires { writer.write_null(); })
      return writer.write_null();
    else if constexpr (writer_traits_t<W>::format == Format::json)
      return encode_json_scalar(writer, "null", false);
  } else if constexpr (std::same_as<V, bool>) {
    if constexpr (requires { writer.write_boolean(value); })
      return writer.write_boolean(value);
    else if constexpr (writer_traits_t<W>::format == Format::json)
      return encode_json_scalar(writer, value ? "true" : "false", false);
  } else if constexpr (is_extended_value<V>) {
    if constexpr (requires { writer.write_semantic(value); })
      return writer.write_semantic(value);
    return encode_error(writer, ErrorCode::unsupported_type,
                        "this format has no codec for this semantic type");
  } else if constexpr (std::same_as<V, RawTextValue>) {
    std::string text;
    if (!resolve_raw_text(value, text))
      return encode_error(writer, ErrorCode::invalid_escape,
                          "invalid raw string representation");
    return encode_native_value(text, writer);
  } else if constexpr (std::same_as<V, BytesValue>) {
    const BytesView bytes(
        reinterpret_cast<const std::uint8_t*>(value.value.data()),
        value.value.size());
    if constexpr (requires { writer.write_binary(bytes, value.subtype); })
      return writer.write_binary(bytes, value.subtype);
    else if constexpr (requires { writer.write_binary(bytes); }) {
      if (value.subtype != 0)
        return encode_error(writer, ErrorCode::unsupported_type,
                            "writer cannot represent BSON binary subtype");
      return writer.write_binary(bytes);
    }
  } else if constexpr (std::integral<V> || std::floating_point<V>) {
    if constexpr (requires { writer.write_number(value); })
      return writer.write_number(value);
    else if constexpr (writer_traits_t<W>::format == Format::json) {
      if constexpr (std::floating_point<V>) {
        if (!std::isfinite(value))
          return encode_error(writer, ErrorCode::unsupported_type,
                              "JSON has no non-finite numbers");
        if (value < -std::numeric_limits<double>::max() ||
            value > std::numeric_limits<double>::max())
          return encode_error(writer, ErrorCode::out_of_range,
                              "floating value exceeds binary64 range");
        if constexpr (std::numeric_limits<V>::digits >
                      std::numeric_limits<double>::digits)
          if (static_cast<V>(static_cast<double>(value)) != value)
            return encode_error(
                writer, ErrorCode::out_of_range,
                "floating value is not representable as binary64");
      }
      std::array<char, 128> buffer{};
      const auto result = [&] {
        // Common floating schemas decode JSON's numeric value as binary64.
        // Promote float before formatting to retain its exact value there.
        if constexpr (std::floating_point<V>)
          return std::to_chars(buffer.data(), buffer.data() + buffer.size(),
                               static_cast<double>(value));
        else
          return std::to_chars(buffer.data(), buffer.data() + buffer.size(),
                               value);
      }();
      if (result.ec != std::errc{})
        return encode_error(writer, ErrorCode::conversion_error,
                            "cannot format number");
      std::string number(buffer.data(), result.ptr);
      if constexpr (std::floating_point<V>)
        if (number.find_first_of(".eE") == std::string::npos) number += ".0";
      return encode_json_scalar(writer, number, false);
    }
  } else if constexpr (std::convertible_to<const T&, std::string_view>) {
    if constexpr (std::is_pointer_v<V>)
      if (!value)
        return encode_error(writer, ErrorCode::invalid_object_state,
                            "null C string; use nullptr for a null value");
    const std::string_view text = value;
    if constexpr (requires { writer.write_string(text); })
      return writer.write_string(text);
    else if constexpr (writer_traits_t<W>::format == Format::json) {
      if (invalid_utf8_offset(text) != text.size())
        return encode_error(writer, ErrorCode::invalid_utf8,
                            "string must be valid UTF-8");
      const auto escaped = escaped_string_body(text);
      return encode_json_scalar(writer, escaped, true);
    }
  } else if constexpr (std::same_as<V, BytesView>) {
    if constexpr (requires { writer.write_binary(value); })
      return writer.write_binary(value);
  } else if constexpr (IsEncodeMap<V>::value) {
    if constexpr (std::ranges::sized_range<decltype(value.keys())> &&
                  std::ranges::sized_range<decltype(value.values())>) {
      if (std::ranges::size(value.keys()) != std::ranges::size(value.values()))
        return encode_error(writer, ErrorCode::size_mismatch,
                            "map key/value lengths differ");
      if (!encode_token(writer, TokenType::ObjectBegin)) return false;
      auto key = std::ranges::begin(value.keys());
      auto item = std::ranges::begin(value.values());
      for (; key != std::ranges::end(value.keys()); ++key, ++item)
        if (!encode_native_value(*key, writer) ||
            !encode_native_value(*item, writer))
          return false;
      return encode_token(writer, TokenType::ObjectEnd);
    } else
      return encode_error(writer, ErrorCode::unsupported_type,
                          "map encoding requires range key/value storages");
  } else if constexpr (ElemT<V> || AsEntryTupleT<V> || AsTryEntryTupleT<V> ||
                       AsElemTupleT<V> || AsTryElemTupleT<V>) {
    return Encoder<W>::encode_to_writer(value, writer);
  } else if constexpr (std::ranges::input_range<const V>) {
    if (!encode_token(writer, TokenType::ArrayBegin)) return false;
    for (const auto& item : value)
      if (!encode_native_value(item, writer)) return false;
    return encode_token(writer, TokenType::ArrayEnd);
  }
  return encode_error(writer, ErrorCode::unsupported_type,
                      "no encoding for this value type or writer");
}
}  // namespace hosq::detail

namespace hosq::ext {
// Writer-specific value conversion is an optional extension, mirroring the
// reader's scalar conversion extensions without changing WriterConcept.
template <WriterConcept W>
struct EncodeValue {
  template <class T>
  auto operator()(const T& value, W& writer) const -> bool {
    return detail::encode_native_value(value, writer);
  }
};
}  // namespace hosq::ext
