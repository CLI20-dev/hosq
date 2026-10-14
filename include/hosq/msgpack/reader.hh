#pragma once

#include "hosq/detail/native_reader.hh"

namespace hosq::msgpack {
// All map key types remain traversable for decode::skip. Constant object
// schemas require string keys. Binary and extension values are opaque scalars.
template <std::size_t MaxDepth = 64>
class Reader : public ReaderBase<Reader<MaxDepth>>,
               public detail::NativeReaderAccess {
 public:
  auto next_token_impl(state_type& s) const -> bool {
    using C = detail::NativeContext;
    if (s.failure) return false;
    if (!s.frames.empty() && s.frames.back().remaining == 0) {
      const auto end = s.frames.back().end;
      s.frames.pop_back();
      s.token = {s.pos, 0, end, 0};
      return true;
    }
    if (s.started && s.frames.empty()) {
      if (s.pos != s.input.size())
        return fail(s, ErrorCode::trailing_data, "data after MessagePack root");
      s.token = {s.pos, 0, TokenType::End, 0};
      return true;
    }
    if (!need(s, 1, s.input.size())) return false;
    const auto start = s.pos;
    const unsigned m = static_cast<unsigned char>(s.input[s.pos++]);
    if (!s.frames.empty()) --s.frames.back().remaining;
    s.started = true;
    s.scalar = {};
    auto count = [&](unsigned n, std::uint64_t& result) {
      if (!need(s, n, s.input.size())) return false;
      result = uint_bytes(s, n);
      return true;
    };
    auto payload = [&](std::uint64_t n, C context) {
      if (n > std::numeric_limits<std::size_t>::max())
        return fail(s, ErrorCode::invalid_length,
                    "MessagePack payload exceeds address space");
      if (!need(s, static_cast<std::size_t>(n), s.input.size())) return false;
      const auto offset = s.pos;
      s.pos += static_cast<std::size_t>(n);
      scalar_token(s, offset, context);
      return true;
    };
    auto container = [&](std::uint64_t n, bool map) {
      if (s.frames.size() >= MaxDepth)
        return fail(s, ErrorCode::depth_limit_exceeded,
                    "MessagePack container depth");
      s.frames.push_back(
          {map ? TokenType::ObjectEnd : TokenType::ArrayEnd, map ? n * 2 : n});
      s.token = {start, s.pos - start,
                 map ? TokenType::ObjectBegin : TokenType::ArrayBegin, 0};
      return true;
    };
    if (m <= 0x7f) {
      s.scalar = std::uint64_t{m};
      scalar_token(s, start, C::number);
      return true;
    }
    if (m >= 0xe0) {
      s.scalar = signed_bits(m, 1);
      scalar_token(s, start, C::number);
      return true;
    }
    if (m >= 0xa0 && m <= 0xbf) return payload(m & 31, C::string);
    if (m >= 0x90 && m <= 0x9f) return container(m & 15, false);
    if (m >= 0x80 && m <= 0x8f) return container(m & 15, true);
    std::uint64_t n = 0;
    if (m == 0xc0) {
      scalar_token(s, start, C::null);
      return true;
    }
    if (m == 0xc2 || m == 0xc3) {
      s.scalar = m == 0xc3;
      scalar_token(s, start, C::boolean);
      return true;
    }
    if (m >= 0xcc && m <= 0xd3) {
      const bool sign = m >= 0xd0;
      const unsigned width = 1u << (m - (sign ? 0xd0 : 0xcc));
      if (!count(width, n)) return false;
      if (sign)
        s.scalar = signed_bits(n, width);
      else
        s.scalar = n;
      scalar_token(s, start, C::number);
      return true;
    }
    if (m == 0xca || m == 0xcb) {
      if (!count(m == 0xca ? 4 : 8, n)) return false;
      s.scalar = m == 0xca ? static_cast<double>(std::bit_cast<float>(
                                 static_cast<std::uint32_t>(n)))
                           : std::bit_cast<double>(n);
      scalar_token(s, start, C::number);
      return true;
    }
    if (m >= 0xd9 && m <= 0xdb) {
      if (!count(1u << (m - 0xd9), n)) return false;
      return payload(n, C::string);
    }
    if (m >= 0xc4 && m <= 0xc6) {
      if (!count(1u << (m - 0xc4), n)) return false;
      return payload(n, C::binary);
    }
    if (m >= 0xdc && m <= 0xdf) {
      if (!count((m & 1) ? 4 : 2, n)) return false;
      return container(n, m >= 0xde);
    }
    if ((m >= 0xc7 && m <= 0xc9) || (m >= 0xd4 && m <= 0xd8)) {
      if (m <= 0xc9) {
        if (!count(1u << (m - 0xc7), n)) return false;
      } else
        n = std::uint64_t{1} << (m - 0xd4);
      // Include the signed extension type byte in the opaque token.
      return payload(n + 1, C::extension);
    }
    return fail(s, ErrorCode::invalid_type_marker,
                "reserved MessagePack marker");
  }
};
}  // namespace hosq::msgpack

namespace hosq {
template <std::size_t N>
struct ReaderTraits<msgpack::Reader<N>> {
  static constexpr Format format = Format::msgpack;
  static constexpr bool is_binary_format = true;
};
namespace ext {
template <std::size_t N>
struct DecodeNumber<msgpack::Reader<N>> : detail::NativeDecodeNumber {};
template <std::size_t N>
struct DecodeNumberInto<msgpack::Reader<N>> : detail::NativeDecodeNumberInto {};
template <std::size_t N>
struct DecodeBoolean<msgpack::Reader<N>> : detail::NativeDecodeBoolean {};
template <std::size_t N>
struct DecodeStringView<msgpack::Reader<N>> : detail::NativeDecodeStringView {};
}  // namespace ext
static_assert(ReaderConcept<msgpack::Reader<>>);
}  // namespace hosq
