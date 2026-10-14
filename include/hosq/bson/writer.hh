#pragma once

#include <vector>

#include "hosq/bytes_view.hh"
#include "hosq/detail/semantic_writer.hh"

namespace hosq::bson {
template <std::size_t MaxDepth = 64, std::size_t MaxOutput = 0x7fffffffULL>
class Writer
    : public detail::SemanticWriter<Writer<MaxDepth, MaxOutput>, MaxOutput> {
  using Base = detail::SemanticWriter<Writer, MaxOutput>;
  struct Frame {
    bool array;
    bool key = true;
    std::size_t start;
    std::size_t count = 0;
    std::string name;
  };

 public:
  explicit Writer(std::string& output) : Base(output) {}
  auto check_string_size(std::size_t n) -> bool {
    if (n >= 0x7fffffffULL || n > MaxOutput)
      return this->fail(ErrorCode::output_too_large,
                        "BSON string length exceeds int32/output limit");
    if (frames_.empty())
      return this->fail(ErrorCode::unsupported_type,
                        "BSON root must be a document");
    return true;
  }
  auto write_string(std::string_view value) -> bool {
    if (!this->ready() || !this->valid_string(value)) return false;
    const std::string copy(value);
    if (!frames_.empty() && !frames_.back().array && frames_.back().key) {
      if (copy.find('\0') != std::string::npos)
        return this->fail(ErrorCode::invalid_token,
                          "BSON key cannot contain NUL");
      frames_.back().name = copy;
      frames_.back().key = false;
      return true;
    }
    if (!prefix(2) || !this->integer_bytes(copy.size() + 1, 4, true) ||
        !this->append(copy) || !this->byte(0))
      return false;
    return after_value();
  }
  template <std::integral T>
    requires(!std::same_as<T, bool>)
  auto write_number(T value) -> bool {
    if (!this->ready()) return false;
    if constexpr (sizeof(T) > 8)
      return this->fail(ErrorCode::unsupported_type,
                        "BSON integers wider than 64 bits are unsupported");
    if constexpr (!std::is_signed_v<T>) {
      if (static_cast<std::uint64_t>(value) >
          static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
        return this->fail(ErrorCode::out_of_range,
                          "BSON has no uint64 number representation");
    }
    const auto v = static_cast<std::int64_t>(value);
    const bool wide =
        sizeof(T) > 4 || v > std::numeric_limits<std::int32_t>::max();
    return prefix(wide ? 0x12 : 0x10) &&
           this->integer_bytes(static_cast<std::uint64_t>(v), wide ? 8 : 4,
                               true) &&
           after_value();
  }
  template <std::floating_point T>
  auto write_number(T value) -> bool {
    double converted;
    return this->ready() && this->binary_double(value, converted) && prefix(1) &&
           this->integer_bytes(std::bit_cast<std::uint64_t>(converted), 8,
                               true) &&
           after_value();
  }
  auto write_boolean(bool value) -> bool {
    return this->ready() && prefix(8) && this->byte(value ? 1 : 0) &&
           after_value();
  }
  auto write_null() -> bool {
    return this->ready() && prefix(10) && after_value();
  }
  auto write_binary(BytesView value) -> bool {
    if (!this->ready()) return false;
    if (value.size() > 0x7fffffffULL || value.size() > MaxOutput)
      return this->fail(ErrorCode::output_too_large,
                        "BSON binary length exceeds int32/output limit");
    const std::string copy(value.as_string_view());
    return prefix(5) && this->integer_bytes(copy.size(), 4, true) &&
           this->byte(0) && this->append(copy) && after_value();
  }

 private:
  friend Base;
  void write_structure(TokenType type) {
    if (type == TokenType::ObjectBegin || type == TokenType::ArrayBegin) {
      if (frames_.size() >= MaxDepth) {
        this->fail(ErrorCode::depth_limit_exceeded, "BSON writer depth");
        return;
      }
      const bool array = type == TokenType::ArrayBegin;
      if (frames_.empty()) {
        if (root_ || array) {
          this->fail(ErrorCode::unsupported_type,
                     "BSON root must be one document");
          return;
        }
      } else if (!prefix(array ? 4 : 3))
        return;
      const auto start = this->output_.size();
      if (!this->integer_bytes(0, 4, true)) return;
      frames_.push_back({array, true, start, 0, {}});
      return;
    }
    if (type == TokenType::ObjectEnd || type == TokenType::ArrayEnd) {
      const bool array = type == TokenType::ArrayEnd;
      if (frames_.empty() || frames_.back().array != array ||
          (!array && !frames_.back().key)) {
        this->fail(ErrorCode::invalid_state,
                   "mismatched or incomplete BSON container");
        return;
      }
      if (!this->byte(0)) return;
      const auto length = this->output_.size() - frames_.back().start;
      if (length > 0x7fffffffULL) {
        this->fail(ErrorCode::output_too_large,
                   "BSON document length exceeds int32");
        return;
      }
      for (unsigned i = 0; i < 4; ++i)
        this->output_[frames_.back().start + i] =
            static_cast<char>(length >> (8 * i));
      frames_.pop_back();
      (void)after_value();
      return;
    }
    if (type == TokenType::End) {
      if (!root_ || !frames_.empty()) {
        this->fail(ErrorCode::invalid_state, "unfinished BSON document");
        return;
      }
      this->finished_ = true;
      return;
    }
    this->fail(ErrorCode::invalid_token, "unexpected BSON writer token");
  }

 private:
  auto prefix(unsigned marker) -> bool {
    if (frames_.empty())
      return this->fail(ErrorCode::unsupported_type,
                        "BSON root must be a document");
    auto& f = frames_.back();
    if (!f.array && f.key)
      return this->fail(ErrorCode::type_mismatch,
                        "BSON object key must be a string");
    const std::string key = f.array ? std::to_string(f.count) : f.name;
    return this->byte(marker) && this->append(key) && this->byte(0);
  }
  auto after_value() -> bool {
    if (frames_.empty()) {
      root_ = true;
      return true;
    }
    auto& f = frames_.back();
    f.key = true;
    f.name.clear();
    ++f.count;
    return true;
  }
  std::vector<Frame> frames_;
  bool root_ = false;
};
}  // namespace hosq::bson
namespace hosq {
template <std::size_t D, std::size_t O>
struct WriterTraits<bson::Writer<D, O>> {
  static constexpr Format format = Format::bson;
  static constexpr bool is_binary_format = true;
};
static_assert(WriterConcept<bson::Writer<>>);
}  // namespace hosq
