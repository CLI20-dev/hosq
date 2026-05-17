#pragma once

#include <cstddef>

namespace bench {

class AllocationCounter {
 public:
  static void record(std::size_t size) noexcept;
  static void reset() noexcept;
  static std::size_t count() noexcept;
  static std::size_t bytes() noexcept;
};

}  // namespace bench
