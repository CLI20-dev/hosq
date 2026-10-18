#pragma once

#include <charconv>

#include "hosq/detail/native_reader.hh"
#include "hosq/detail/temporal.hh"

namespace hosq::bson {
template <std::size_t MaxDepth = 64>
class Reader : public ReaderBase<Reader<MaxDepth>>,
               public detail::NativeReaderAccess {
 public:
  auto next_token_impl(state_type& s) const -> bool {
    if (s.failure) return false;
    if (!s.started) {
      s.started = true;
      return document(s, false, s.input.size());
    }
    if (s.frames.empty()) {
      if (s.pos != s.input.size())
        return fail(s, ErrorCode::trailing_data, "data after BSON document");
      s.token = {s.pos, 0, TokenType::End, 0};
      return true;
    }
    auto& f = s.frames.back();
    if (!f.key) {
      f.key = true;
      return value(s, f.marker, f.limit - 1);
    }
    if (!need(s, 1, f.limit)) return false;
    if (s.pos == f.limit - 1) {
      if (s.input[s.pos++] != '\0')
        return fail(s, ErrorCode::invalid_length,
                    "missing BSON document terminator");
      const auto end = f.end;
      s.frames.pop_back();
      s.token = {s.pos - 1, 1, end, 0};
      return true;
    }
    const auto marker = static_cast<unsigned char>(s.input[s.pos++]);
    if (marker == 0)
      return fail(s, ErrorCode::invalid_length, "early BSON terminator");
    const auto start = s.pos;
    if (!cstring(s, f.limit - 1)) return false;
    f.marker = marker;
    if (f.end == TokenType::ObjectEnd) {
      f.key = false;
      s.token = {start, s.pos - start - 1, TokenType::Scalar,
                 static_cast<std::uint8_t>(detail::NativeContext::string)};
      s.scalar = {};
      s.scalar_marker = 0;
      return true;
    }
    char digits[32];
    const auto result =
        std::to_chars(digits, digits + sizeof(digits), f.index++);
    if (s.input.substr(start, s.pos - start - 1) !=
        std::string_view(digits, static_cast<std::size_t>(result.ptr - digits)))
      return fail(s, ErrorCode::invalid_token,
                  "BSON array keys must be sequential indices");
    return value(s, marker, f.limit - 1);
  }

  auto read_semantic(const state_type& s, InstantValue& output) const -> bool {
    output = {};
    if (!semantic_payload(s, 0x09, 8)) return false;
    output = detail::instant_from_milliseconds(
        std::bit_cast<std::int64_t>(semantic_uint(s, 0, 8)));
    return true;
  }
  auto read_semantic(const state_type& s, Decimal128Value& output) const
      -> bool {
    output = {};
    if (!semantic_payload(s, 0x13, output.bytes.size())) return false;
    for (std::size_t i = 0; i < output.bytes.size(); ++i)
      output.bytes[i] = static_cast<std::byte>(
          static_cast<unsigned char>(s.input[s.token.offset + i]));
    return true;
  }
  auto read_semantic(const state_type& s, ObjectIdValue& output) const -> bool {
    output = {};
    if (!semantic_payload(s, 0x07, output.bytes.size())) return false;
    for (std::size_t i = 0; i < output.bytes.size(); ++i)
      output.bytes[i] = static_cast<std::byte>(
          static_cast<unsigned char>(s.input[s.token.offset + i]));
    return true;
  }
  auto read_semantic(const state_type& s, BsonTimestampValue& output) const
      -> bool {
    output = {};
    if (!semantic_payload(s, 0x11, 8)) return false;
    output.increment = static_cast<std::uint32_t>(semantic_uint(s, 0, 4));
    output.seconds = static_cast<std::uint32_t>(semantic_uint(s, 4, 4));
    return true;
  }

 private:
  auto semantic_payload(const state_type& s, unsigned marker,
                        std::size_t width) const -> bool {
    if (s.failure) return false;
    if (s.token.type != TokenType::Scalar ||
        s.token.context !=
            static_cast<std::uint8_t>(detail::NativeContext::extension) ||
        s.scalar_marker != marker) {
      report_error(s, {ErrorCode::type_mismatch, s.token.offset,
                       "expected matching BSON semantic type"});
      return false;
    }
    if (s.token.length != width) {
      report_error(s, {ErrorCode::invalid_length, s.token.offset,
                       "invalid BSON semantic payload length"});
      return false;
    }
    if (s.token.offset > s.input.size() ||
        width > s.input.size() - s.token.offset) {
      report_error(s, {ErrorCode::unexpected_eof, s.token.offset,
                       "truncated BSON semantic payload"});
      return false;
    }
    return true;
  }
  static auto semantic_uint(const state_type& s, std::size_t offset,
                            unsigned width) -> std::uint64_t {
    std::uint64_t result = 0;
    for (unsigned i = 0; i < width; ++i)
      result |= std::uint64_t(static_cast<unsigned char>(
                    s.input[s.token.offset + offset + i]))
                << (8 * i);
    return result;
  }
  static auto cstring(state_type& s, std::size_t limit) -> bool {
    const auto start = s.pos;
    while (s.pos < limit) {
      if (s.input[s.pos++] == '\0') {
        const auto text = s.input.substr(start, s.pos - start - 1);
        if (detail::invalid_utf8_offset(text) != text.size())
          return fail(s, ErrorCode::invalid_utf8, "invalid BSON cstring UTF-8");
        return true;
      }
    }
    return fail(s, ErrorCode::unexpected_eof, "unterminated BSON cstring");
  }
  static auto length(state_type& s, std::size_t limit, std::uint64_t& n)
      -> bool {
    if (!need(s, 4, limit)) return false;
    n = uint_bytes(s, 4, true);
    if (n > 0x7fffffff)
      return fail(s, ErrorCode::invalid_length, "negative BSON length");
    return true;
  }
  static auto document(state_type& s, bool array, std::size_t limit) -> bool {
    const auto start = s.pos;
    std::uint64_t n = 0;
    if (!length(s, limit, n)) return false;
    if (n < 5)
      return fail(s, ErrorCode::invalid_length,
                  "BSON document shorter than five bytes");
    if (!need(s, static_cast<std::size_t>(n - 4), limit)) return false;
    if (s.frames.size() >= MaxDepth)
      return fail(s, ErrorCode::depth_limit_exceeded, "BSON container depth");
    s.frames.push_back({array ? TokenType::ArrayEnd : TokenType::ObjectEnd, 0,
                        start + static_cast<std::size_t>(n)});
    s.token = {start, 4, array ? TokenType::ArrayBegin : TokenType::ObjectBegin,
               0};
    return true;
  }
  static auto value(state_type& s, unsigned m, std::size_t limit) -> bool {
    using C = detail::NativeContext;
    s.scalar = {};
    s.binary_subtype = 0;
    s.scalar_marker = static_cast<std::uint8_t>(m);
    const auto start = s.pos;
    auto bytes = [&](std::size_t n, C context) {
      if (!need(s, n, limit)) return false;
      s.pos += n;
      scalar_token(s, start, context);
      return true;
    };
    auto string = [&](C context) {
      std::uint64_t n = 0;
      if (!length(s, limit, n)) return false;
      if (n == 0)
        return fail(s, ErrorCode::invalid_length, "zero BSON string length");
      if (!need(s, static_cast<std::size_t>(n), limit)) return false;
      const auto offset = s.pos;
      s.pos += static_cast<std::size_t>(n);
      if (s.input[s.pos - 1] != '\0')
        return fail(s, ErrorCode::invalid_token,
                    "missing BSON string terminator");
      const auto text = s.input.substr(offset, static_cast<std::size_t>(n - 1));
      if (detail::invalid_utf8_offset(text) != text.size())
        return fail(s, ErrorCode::invalid_utf8, "invalid BSON string UTF-8");
      s.token = {offset, static_cast<std::size_t>(n - 1), TokenType::Scalar,
                 static_cast<std::uint8_t>(context)};
      return true;
    };
    switch (m) {
      case 0x01: {
        if (!need(s, 8, limit)) return false;
        s.scalar = std::bit_cast<double>(uint_bytes(s, 8, true));
        scalar_token(s, start, C::number);
        return true;
      }
      case 0x02:
        return string(C::string);
      case 0x03:
        return document(s, false, limit);
      case 0x04:
        return document(s, true, limit);
      case 0x05: {
        std::uint64_t n = 0;
        if (!length(s, limit, n) || !need(s, 1, limit)) return false;
        const unsigned subtype = static_cast<unsigned char>(s.input[s.pos++]);
        if (subtype > 9 && subtype < 128)
          return fail(s, ErrorCode::invalid_type_marker,
                      "reserved BSON binary subtype");
        if (!need(s, static_cast<std::size_t>(n), limit)) return false;
        if (subtype == 2) {
          if (n < 4)
            return fail(s, ErrorCode::invalid_length, "short old BSON binary");
          const auto old = uint_bytes(s, 4, true);
          if (old != n - 4)
            return fail(s, ErrorCode::invalid_length,
                        "old BSON binary length mismatch");
          n -= 4;
        }
        const auto offset = s.pos;
        s.pos += static_cast<std::size_t>(n);
        s.binary_subtype = static_cast<std::uint8_t>(subtype);
        scalar_token(s, offset, C::binary);
        return true;
      }
      case 0x08:
        if (!need(s, 1, limit)) return false;
        if (static_cast<unsigned char>(s.input[s.pos]) > 1)
          return fail(s, ErrorCode::invalid_token, "invalid BSON boolean");
        s.scalar = s.input[s.pos++] != 0;
        scalar_token(s, start, C::boolean);
        return true;
      case 0x10:
      case 0x12: {
        const unsigned n = m == 0x10 ? 4 : 8;
        if (!need(s, n, limit)) return false;
        s.scalar = signed_bits(uint_bytes(s, n, true), n);
        scalar_token(s, start, C::number);
        return true;
      }
      case 0x0a:
        return bytes(0, C::null);
      case 0x06:
      case 0x7f:
      case 0xff:
        return bytes(0, C::extension);
      case 0x07:
        return bytes(12, C::extension);
      case 0x09:
      case 0x11:
        return bytes(8, C::extension);
      case 0x13:
        return bytes(16, C::extension);
      case 0x0b: {
        if (!cstring(s, limit)) return false;
        const auto options = s.pos;
        if (!cstring(s, limit)) return false;
        char previous = 0;
        for (char option : s.input.substr(options, s.pos - options - 1)) {
          if (std::string_view("imsux").find(option) == std::string_view::npos ||
              option < previous)
            return fail(s, ErrorCode::invalid_token,
                        "invalid or unsorted BSON regex options");
          previous = option;
        }
        scalar_token(s, start, C::extension);
        return true;
      }
      case 0x0c:
        if (!string(C::extension) || !need(s, 12, limit)) return false;
        s.pos += 12;
        scalar_token(s, start, C::extension);
        return true;
      case 0x0d:
      case 0x0e:
        return string(C::extension);
      case 0x0f: {
        std::uint64_t total = 0;
        if (!length(s, limit, total)) return false;
        if (total < 14)
          return fail(s, ErrorCode::invalid_length,
                      "BSON code-with-scope shorter than fourteen bytes");
        if (!need(s, static_cast<std::size_t>(total - 4), limit)) return false;
        const auto end = start + static_cast<std::size_t>(total);
        std::uint64_t code_length = 0;
        if (!length(s, end, code_length)) return false;
        if (code_length == 0)
          return fail(s, ErrorCode::invalid_length,
                      "zero BSON code string length");
        if (!need(s, static_cast<std::size_t>(code_length), end)) return false;
        const auto text =
            s.input.substr(s.pos, static_cast<std::size_t>(code_length - 1));
        s.pos += static_cast<std::size_t>(code_length);
        if (s.input[s.pos - 1] != '\0')
          return fail(s, ErrorCode::invalid_token,
                      "missing BSON code terminator");
        if (detail::invalid_utf8_offset(text) != text.size())
          return fail(s, ErrorCode::invalid_utf8, "invalid BSON code UTF-8");
        // Validate the scope with the same traversal and enclosing depth limit.
        // It remains an opaque scalar to application schemas and decode::skip.
        state_type scope;
        scope.input = s.input;
        scope.pos = s.pos;
        scope.started = true;
        scope.frames = s.frames;
        const auto enclosing_depth = scope.frames.size();
        if (!document(scope, false, end)) {
          s.pos = scope.pos;
          return fail(s, scope.failure.code, scope.failure.detail);
        }
        while (scope.frames.size() > enclosing_depth) {
          if (!Reader{}.next_token_impl(scope)) {
            s.pos = scope.pos;
            return fail(s, scope.failure.code, scope.failure.detail);
          }
        }
        s.pos = scope.pos;
        if (s.pos != end)
          return fail(s, ErrorCode::invalid_length,
                      "BSON code-with-scope size mismatch");
        scalar_token(s, start, C::extension);
        return true;
      }
      default:
        return fail(s, ErrorCode::invalid_type_marker,
                    "unknown BSON element marker");
    }
  }
};
}  // namespace hosq::bson
namespace hosq {
template <std::size_t N>
struct ReaderTraits<bson::Reader<N>> {
  static constexpr Format format = Format::bson;
  static constexpr bool is_binary_format = true;
};
namespace ext {
template <std::size_t N>
struct DecodeNumber<bson::Reader<N>> : detail::NativeDecodeNumber {};
template <std::size_t N>
struct DecodeNumberInto<bson::Reader<N>> : detail::NativeDecodeNumberInto {};
template <std::size_t N>
struct DecodeBoolean<bson::Reader<N>> : detail::NativeDecodeBoolean {};
template <std::size_t N>
struct DecodeStringView<bson::Reader<N>> : detail::NativeDecodeStringView {};
}  // namespace ext
static_assert(ReaderConcept<bson::Reader<>>);
}  // namespace hosq
