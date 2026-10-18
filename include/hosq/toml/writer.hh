#pragma once

#include <unordered_set>
#include <vector>

#include "hosq/bytes_view.hh"
#include "hosq/detail/semantic_writer.hh"
#include "hosq/detail/temporal.hh"

namespace hosq::toml {
// Canonical TOML 1.0: one assignment per root member; nested tables and arrays
// of tables use their equivalent inline forms. Keys are always quoted.
template <std::size_t MaxDepth = 64,
          std::size_t MaxOutput = std::numeric_limits<std::size_t>::max()>
class Writer
    : public detail::SemanticWriter<Writer<MaxDepth, MaxOutput>, MaxOutput> {
  using Base = detail::SemanticWriter<Writer, MaxOutput>;
  struct Frame {
    bool array;
    bool root;
    bool key = true;
    std::size_t count = 0;
    std::unordered_set<std::string> keys;
  };

 public:
  explicit Writer(std::string& output) : Base(output) {}
  auto check_string_size(std::size_t n) -> bool {
    if (n > MaxOutput)
      return this->fail(ErrorCode::output_too_large,
                        "TOML string exceeds output limit");
    return true;
  }
  auto write_string(std::string_view value) -> bool {
    if (!this->ready() || !this->valid_string(value)) return false;
    const auto escaped = detail::quoted_string(value);
    if (!frames_.empty() && !frames_.back().array && frames_.back().key) {
      auto& f = frames_.back();
      if (!f.keys.emplace(value).second)
        return this->fail(ErrorCode::duplicate_member,
                          "duplicate TOML output key");
      if (!f.root && f.count && !this->append(", ")) return false;
      if (!this->append(escaped) || !this->append(" = ")) return false;
      f.key = false;
      return true;
    }
    return before_value() && this->append(escaped) && after_value();
  }
  template <std::integral T>
    requires(!std::same_as<T, bool>)
  auto write_number(T value) -> bool {
    if (!this->ready()) return false;
    if constexpr (sizeof(T) > 8)
      return this->fail(ErrorCode::unsupported_type,
                        "TOML integers wider than 64 bits are unsupported");
    if constexpr (!std::is_signed_v<T>) {
      if (static_cast<std::uint64_t>(value) >
          static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
        return this->fail(ErrorCode::out_of_range,
                          "TOML integers must fit int64");
    }
    std::array<char, 32> data{};
    const auto result = std::to_chars(data.data(), data.data() + data.size(),
                                      static_cast<std::int64_t>(value));
    return before_value() &&
           this->append({data.data(),
                         static_cast<std::size_t>(result.ptr - data.data())}) &&
           after_value();
  }
  template <std::floating_point T>
  auto write_number(T value) -> bool {
    if (!this->ready()) return false;
    double converted;
    if (!this->binary_double(value, converted)) return false;
    std::array<char, 128> data{};
    const auto result =
        std::to_chars(data.data(), data.data() + data.size(), converted);
    if (result.ec != std::errc{})
      return this->fail(ErrorCode::conversion_error, "cannot format TOML float");
    std::string text(data.data(), result.ptr);
    if (std::isfinite(converted) &&
        text.find_first_of(".eE") == std::string::npos)
      text += ".0";
    return before_value() && this->append(text) && after_value();
  }
  auto write_boolean(bool value) -> bool {
    return this->ready() && before_value() &&
           this->append(value ? "true" : "false") && after_value();
  }
  auto write_null() -> bool {
    if (!this->ready()) return false;
    return this->fail(ErrorCode::unsupported_type, "TOML 1.0 has no null value");
  }
  auto write_binary(BytesView) -> bool {
    if (!this->ready()) return false;
    return this->fail(ErrorCode::unsupported_type,
                      "TOML 1.0 has no binary value");
  }
  auto write_semantic(LocalDateValue value) -> bool {
    if (!this->ready()) return false;
    if (!detail::valid_date(value)) return invalid_temporal();
    return write_temporal(detail::format_local_date(value));
  }
  auto write_semantic(LocalTimeValue value) -> bool {
    if (!this->ready()) return false;
    if (!detail::valid_time(value)) return invalid_temporal();
    return write_temporal(detail::format_local_time(value));
  }
  auto write_semantic(LocalDateTimeValue value) -> bool {
    if (!this->ready()) return false;
    if (!detail::valid_local_datetime(value)) return invalid_temporal();
    return write_temporal(detail::format_local_datetime(value));
  }
  auto write_semantic(OffsetDateTimeValue value) -> bool {
    if (!this->ready()) return false;
    if (!detail::valid_offset_datetime(value)) return invalid_temporal();
    return write_temporal(detail::format_offset_datetime(value));
  }
  auto write_semantic(InstantValue value) -> bool {
    if (!this->ready()) return false;
    OffsetDateTimeValue offset;
    if (!detail::valid_instant(value) ||
        !detail::instant_to_offset(value, offset))
      return invalid_temporal();
    return write_temporal(detail::format_offset_datetime(offset));
  }

 private:
  auto invalid_temporal() -> bool {
    return this->fail(ErrorCode::out_of_range,
                      "invalid or unrepresentable TOML date/time");
  }
  auto write_temporal(std::string_view text) -> bool {
    return before_value() && this->append(text) && after_value();
  }
  friend Base;
  void write_structure(TokenType type) {
    if (type == TokenType::ObjectBegin || type == TokenType::ArrayBegin) {
      if (frames_.size() >= MaxDepth) {
        this->fail(ErrorCode::depth_limit_exceeded, "TOML writer depth");
        return;
      }
      const bool array = type == TokenType::ArrayBegin;
      const bool root = frames_.empty();
      if (root) {
        if (root_ || array) {
          this->fail(ErrorCode::unsupported_type, "TOML root must be one table");
          return;
        }
      } else if (!before_value() || !this->append(array ? "[" : "{"))
        return;
      frames_.push_back({array, root, true, 0, {}});
      return;
    }
    if (type == TokenType::ObjectEnd || type == TokenType::ArrayEnd) {
      const bool array = type == TokenType::ArrayEnd;
      if (frames_.empty() || frames_.back().array != array ||
          (!array && !frames_.back().key)) {
        this->fail(ErrorCode::invalid_state,
                   "mismatched or incomplete TOML container");
        return;
      }
      const bool root = frames_.back().root;
      if (!root && !this->append(array ? "]" : "}")) return;
      frames_.pop_back();
      (void)after_value();
      return;
    }
    if (type == TokenType::End) {
      if (!root_ || !frames_.empty()) {
        this->fail(ErrorCode::invalid_state, "unfinished TOML document");
        return;
      }
      this->finished_ = true;
      return;
    }
    this->fail(ErrorCode::invalid_token, "unexpected TOML writer token");
  }

 private:
  auto before_value() -> bool {
    if (frames_.empty())
      return this->fail(ErrorCode::unsupported_type,
                        "TOML root must be a table");
    const auto& f = frames_.back();
    if (!f.array && f.key)
      return this->fail(ErrorCode::type_mismatch,
                        "TOML object key must be a string");
    if (f.array && f.count) return this->append(", ");
    return true;
  }
  auto after_value() -> bool {
    if (frames_.empty()) {
      root_ = true;
      return true;
    }
    auto& f = frames_.back();
    f.key = true;
    ++f.count;
    if (f.root) return this->append("\n");
    return true;
  }
  std::vector<Frame> frames_;
  bool root_ = false;
};
}  // namespace hosq::toml
namespace hosq {
template <std::size_t D, std::size_t O>
struct WriterTraits<toml::Writer<D, O>> {
  static constexpr Format format = Format::toml;
  static constexpr bool is_binary_format = false;
};
static_assert(WriterConcept<toml::Writer<>>);
}  // namespace hosq
