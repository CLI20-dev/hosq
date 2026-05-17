#include <cstdlib>
#include <new>

#include "allocation_counter.hh"

namespace {

auto allocate(std::size_t size) -> void* {
  if (void* ptr = std::malloc(size == 0 ? 1 : size)) {
    bench::AllocationCounter::record(size);
    return ptr;
  }
  throw std::bad_alloc{};
}

auto allocate_aligned(std::size_t size, std::align_val_t alignment) -> void* {
  void* ptr = nullptr;
  const auto align = static_cast<std::size_t>(alignment);
  if (posix_memalign(&ptr, align, size == 0 ? align : size) == 0) {
    bench::AllocationCounter::record(size);
    return ptr;
  }
  throw std::bad_alloc{};
}

}  // namespace

auto operator new(std::size_t size) -> void* { return allocate(size); }

auto operator new[](std::size_t size) -> void* { return allocate(size); }

auto operator new(std::size_t size, const std::nothrow_t&) noexcept -> void* {
  try {
    return allocate(size);
  } catch (...) {
    return nullptr;
  }
}

auto operator new[](std::size_t size, const std::nothrow_t&) noexcept -> void* {
  try {
    return allocate(size);
  } catch (...) {
    return nullptr;
  }
}

auto operator new(std::size_t size, std::align_val_t alignment) -> void* {
  return allocate_aligned(size, alignment);
}

auto operator new[](std::size_t size, std::align_val_t alignment) -> void* {
  return allocate_aligned(size, alignment);
}

auto operator new(std::size_t size, std::align_val_t alignment,
                  const std::nothrow_t&) noexcept -> void* {
  try {
    return allocate_aligned(size, alignment);
  } catch (...) {
    return nullptr;
  }
}

auto operator new[](std::size_t size, std::align_val_t alignment,
                    const std::nothrow_t&) noexcept -> void* {
  try {
    return allocate_aligned(size, alignment);
  } catch (...) {
    return nullptr;
  }
}

void operator delete(void* ptr) noexcept { std::free(ptr); }

void operator delete[](void* ptr) noexcept { std::free(ptr); }

void operator delete(void* ptr, std::size_t) noexcept { std::free(ptr); }

void operator delete[](void* ptr, std::size_t) noexcept { std::free(ptr); }

void operator delete(void* ptr, const std::nothrow_t&) noexcept {
  std::free(ptr);
}

void operator delete[](void* ptr, const std::nothrow_t&) noexcept {
  std::free(ptr);
}

void operator delete(void* ptr, std::align_val_t) noexcept { std::free(ptr); }

void operator delete[](void* ptr, std::align_val_t) noexcept { std::free(ptr); }

void operator delete(void* ptr, std::size_t, std::align_val_t) noexcept {
  std::free(ptr);
}

void operator delete[](void* ptr, std::size_t, std::align_val_t) noexcept {
  std::free(ptr);
}
