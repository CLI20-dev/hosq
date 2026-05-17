#include "allocation_counter.hh"

namespace bench {

static std::size_t g_count = 0;
static std::size_t g_bytes = 0;

void AllocationCounter::record(std::size_t size) noexcept {
  ++g_count;
  g_bytes += size;
}

void AllocationCounter::reset() noexcept {
  g_count = 0;
  g_bytes = 0;
}

std::size_t AllocationCounter::count() noexcept { return g_count; }
std::size_t AllocationCounter::bytes() noexcept { return g_bytes; }

}  // namespace bench
