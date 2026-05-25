#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace hosq {

struct BytesView {
  constexpr BytesView(const std::uint8_t* data, std::size_t size) noexcept
      : data_(data), size_(size) {}

  [[nodiscard]] constexpr auto begin() const noexcept -> const std::uint8_t* {
    return data_;
  }

  [[nodiscard]] constexpr auto end() const noexcept -> const std::uint8_t* {
    return data_ + size_;
  }

  [[nodiscard]] constexpr auto empty() const noexcept -> bool {
    return size_ == 0;
  }

  [[nodiscard]] constexpr auto subview(std::size_t offset,
                                       std::size_t count) const noexcept
      -> BytesView {
    return {data_ + offset, count};
  }

  [[nodiscard]] auto as_string_view() const noexcept -> std::string_view {
    return {
        reinterpret_cast<const char*>(data_),  // NOLINT
        size_,
    };
  }

  [[nodiscard]] static auto from_string_view(std::string_view view) noexcept
      -> BytesView {
    return {
        reinterpret_cast<const std::uint8_t*>(view.data()),  // NOLINT
        view.size(),
    };
  }

  [[nodiscard]] auto size() const noexcept -> std::size_t { return size_; }

  [[nodiscard]] auto data() const noexcept -> const std::uint8_t* {
    return data_;
  }

 private:
  const std::uint8_t* data_{};
  std::size_t size_{};
};

}  // namespace hosq
