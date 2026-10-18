#pragma once

#include <vector>

#include "hosq/bytes_view.hh"
#include "hosq/detail/semantic_writer.hh"
#include "hosq/extended_value_types.hh"

namespace hosq::msgpack {
template <std::size_t MaxDepth = 64,
          std::size_t MaxOutput = std::numeric_limits<std::size_t>::max()>
class Writer
    : public detail::SemanticWriter<Writer<MaxDepth, MaxOutput>, MaxOutput> {
  using Base = detail::SemanticWriter<Writer, MaxOutput>;
  struct Frame {
    bool map;
    bool key = true;
    std::uint64_t count = 0;
    std::size_t header;
  };

 public:
  explicit Writer(std::string& output) : Base(output) {}
  auto check_string_size(std::size_t n) -> bool {
    if (n > 0xffffffffULL || n > MaxOutput)
      return this->fail(ErrorCode::output_too_large,
                        "MessagePack string length exceeds uint32/output limit");
    return true;
  }
  auto write_string(std::string_view value) -> bool {
    if (!this->ready() || !this->valid_string(value) || !before_value())
      return false;
    // Snapshot before output growth so a view of writer.output() is safe.
    const std::string copy(value);
    if (value.size() <= 31) {
      if (!this->byte(0xa0 + value.size())) return false;
    } else if (!sized_marker(0xd9, copy.size()))
      return false;
    if (!this->append(copy)) return false;
    return after_value();
  }
  template <std::integral T>
    requires(!std::same_as<T, bool>)
  auto write_number(T value) -> bool {
    if (!this->ready() || !before_value()) return false;
    if constexpr (sizeof(T) > 8)
      return this->fail(
          ErrorCode::unsupported_type,
          "MessagePack integers wider than 64 bits are unsupported");
    if constexpr (std::is_signed_v<T>) {
      if (value < 0) {
        const auto v = static_cast<std::int64_t>(value);
        if (v >= -32) {
          if (!this->byte(static_cast<unsigned char>(v))) return false;
        } else {
          const unsigned width = v >= -128     ? 1
                                 : v >= -32768 ? 2
                                 : v >= std::numeric_limits<std::int32_t>::min()
                                     ? 4
                                     : 8;
          const unsigned marker = width == 1   ? 0xd0
                                  : width == 2 ? 0xd1
                                  : width == 4 ? 0xd2
                                               : 0xd3;
          if (!this->byte(marker) ||
              !this->integer_bytes(static_cast<std::uint64_t>(v), width))
            return false;
        }
        return after_value();
      }
    }
    const auto v = static_cast<std::uint64_t>(value);
    if (v <= 127) {
      if (!this->byte(static_cast<unsigned>(v))) return false;
    } else {
      const unsigned width = v <= 255             ? 1
                             : v <= 65535         ? 2
                             : v <= 0xffffffffULL ? 4
                                                  : 8;
      const unsigned marker = width == 1   ? 0xcc
                              : width == 2 ? 0xcd
                              : width == 4 ? 0xce
                                           : 0xcf;
      if (!this->byte(marker) || !this->integer_bytes(v, width)) return false;
    }
    return after_value();
  }
  template <std::floating_point T>
  auto write_number(T value) -> bool {
    if (!this->ready() || !before_value()) return false;
    if constexpr (std::same_as<T, float>) {
      if (!this->byte(0xca) ||
          !this->integer_bytes(std::bit_cast<std::uint32_t>(value), 4))
        return false;
    } else {
      double converted;
      if (!this->binary_double(value, converted) || !this->byte(0xcb) ||
          !this->integer_bytes(std::bit_cast<std::uint64_t>(converted), 8))
        return false;
    }
    return after_value();
  }
  auto write_boolean(bool value) -> bool {
    return this->ready() && before_value() && this->byte(value ? 0xc3 : 0xc2) &&
           after_value();
  }
  auto write_null() -> bool {
    return this->ready() && before_value() && this->byte(0xc0) && after_value();
  }
  auto write_binary(BytesView value) -> bool {
    if (!this->ready() || !check_string_size(value.size()) || !before_value())
      return false;
    const std::string copy(value.as_string_view());
    return sized_marker(0xc4, copy.size()) && this->append(copy) &&
           after_value();
  }
  auto write_extension(std::int8_t type, BytesView value) -> bool {
    if (!this->ready() || !check_string_size(value.size()) || !before_value())
      return false;
    const std::string copy(value.as_string_view());
    unsigned fixed = 0;
    switch (copy.size()) {
      case 1:
        fixed = 0xd4;
        break;
      case 2:
        fixed = 0xd5;
        break;
      case 4:
        fixed = 0xd6;
        break;
      case 8:
        fixed = 0xd7;
        break;
      case 16:
        fixed = 0xd8;
        break;
      default:
        break;
    }
    if (fixed) {
      if (!this->byte(fixed)) return false;
    } else if (!sized_marker(0xc7, copy.size()))
      return false;
    return this->byte(static_cast<unsigned char>(type)) && this->append(copy) &&
           after_value();
  }
  auto write_semantic(const InstantValue& value) -> bool {
    if (!this->ready()) return false;
    if (value.nanoseconds >= 1000000000)
      return this->fail(ErrorCode::out_of_range,
                        "MessagePack timestamp nanoseconds exceed 999999999");
    std::array<std::uint8_t, 12> payload{};
    auto put = [&](std::uint64_t bits, unsigned start, unsigned width) {
      for (unsigned i = 0; i < width; ++i)
        payload[start + i] =
            static_cast<std::uint8_t>(bits >> (8 * (width - i - 1)));
    };
    unsigned size;
    if (value.seconds >= 0 && value.seconds <= 0xffffffffLL &&
        value.nanoseconds == 0) {
      size = 4;
      put(static_cast<std::uint64_t>(value.seconds), 0, 4);
    } else if (value.seconds >= 0 && value.seconds <= 0x3ffffffffLL) {
      size = 8;
      put((std::uint64_t{value.nanoseconds} << 34) |
              static_cast<std::uint64_t>(value.seconds),
          0, 8);
    } else {
      size = 12;
      put(value.nanoseconds, 0, 4);
      put(static_cast<std::uint64_t>(value.seconds), 4, 8);
    }
    return write_extension(-1, BytesView(payload.data(), size));
  }
  auto write_semantic(const ExtensionValue& value) -> bool {
    if (!this->ready()) return false;
    if (value.type == -1) {
      const auto size = value.payload.size();
      if (size != 4 && size != 8 && size != 12)
        return this->fail(ErrorCode::invalid_length,
                          "MessagePack timestamp requires 4, 8, or 12 bytes");
      if (size != 4) {
        std::uint32_t nanos = 0;
        for (unsigned i = 0; i < 4; ++i)
          nanos = (nanos << 8) | std::to_integer<std::uint8_t>(value.payload[i]);
        if (size == 8) nanos >>= 2;
        if (nanos >= 1000000000)
          return this->fail(
              ErrorCode::out_of_range,
              "MessagePack timestamp nanoseconds exceed 999999999");
      }
    }
    return write_extension(
        value.type,
        BytesView(reinterpret_cast<const std::uint8_t*>(value.payload.data()),
                  value.payload.size()));
  }

 private:
  friend Base;
  void write_structure(TokenType type) {
    if (type == TokenType::ObjectBegin || type == TokenType::ArrayBegin) {
      if (frames_.size() >= MaxDepth) {
        this->fail(ErrorCode::depth_limit_exceeded, "MessagePack writer depth");
        return;
      }
      if (!before_value()) return;
      const bool map = type == TokenType::ObjectBegin;
      const auto start = this->output_.size();
      if (!this->byte(map ? 0x80 : 0x90)) return;
      frames_.push_back({map, true, 0, start});
      return;
    }
    if (type == TokenType::ObjectEnd || type == TokenType::ArrayEnd) {
      const bool map = type == TokenType::ObjectEnd;
      if (frames_.empty() || frames_.back().map != map ||
          (map && !frames_.back().key)) {
        this->fail(ErrorCode::invalid_state,
                   "mismatched or incomplete MessagePack container");
        return;
      }
      const auto frame = frames_.back();
      frames_.pop_back();
      std::string header;
      if (frame.count <= 15)
        header += static_cast<char>((map ? 0x80 : 0x90) + frame.count);
      else {
        const unsigned n = frame.count <= 65535 ? 2 : 4;
        header += static_cast<char>(map ? (n == 2 ? 0xde : 0xdf)
                                        : (n == 2 ? 0xdc : 0xdd));
        for (unsigned i = 0; i < n; ++i)
          header += static_cast<char>(frame.count >> (8 * (n - i - 1)));
      }
      if (!this->room(header.size() - 1)) return;
      this->output_.replace(frame.header, 1, header);
      (void)after_value();
      return;
    }
    if (type == TokenType::End) {
      if (!frames_.empty() || !root_) {
        this->fail(ErrorCode::invalid_state, "unfinished MessagePack document");
        return;
      }
      this->finished_ = true;
      return;
    }
    this->fail(ErrorCode::invalid_token, "unexpected MessagePack writer token");
  }

 private:
  auto before_value() -> bool {
    if (frames_.empty() && root_)
      return this->fail(ErrorCode::invalid_state,
                        "MessagePack root already written");
    return true;
  }
  auto after_value() -> bool {
    if (frames_.empty()) {
      root_ = true;
      return true;
    }
    auto& f = frames_.back();
    if (f.map) {
      f.key = !f.key;
      if (!f.key) return true;
    }
    if (f.count == 0xffffffffULL)
      return this->fail(ErrorCode::output_too_large,
                        "MessagePack container count exceeds uint32");
    ++f.count;
    return true;
  }
  auto sized_marker(unsigned base, std::size_t n) -> bool {
    const unsigned width = n <= 255 ? 1 : n <= 65535 ? 2 : 4;
    return this->byte(base + (width == 1   ? 0
                              : width == 2 ? 1
                                           : 2)) &&
           this->integer_bytes(n, width);
  }
  std::vector<Frame> frames_;
  bool root_ = false;
};
}  // namespace hosq::msgpack
namespace hosq {
template <std::size_t D, std::size_t O>
struct WriterTraits<msgpack::Writer<D, O>> {
  static constexpr Format format = Format::msgpack;
  static constexpr bool is_binary_format = true;
};
static_assert(WriterConcept<msgpack::Writer<>>);
}  // namespace hosq
