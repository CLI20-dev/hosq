#pragma once
#include <arm_acle.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <functional>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

#include "ser20/matcher.hh"

namespace ser20 {

// ── ARM CRC32C (runtime) ──────────────────────────────────────────────────────

inline auto crc32c_arm(std::string_view s) noexcept -> uint32_t {
  uint32_t crc = 0xFFFFFFFFu;
  const auto* p = reinterpret_cast<const uint8_t*>(s.data());
  std::size_t n = s.size();
  while (n >= 8) {
    uint64_t v = 0;
    std::memcpy(&v, p, sizeof(v));
    crc = __crc32cd(crc, v);
    p += 8;
    n -= 8;
  }
  if (n >= 4) {
    uint32_t v = 0;
    std::memcpy(&v, p, sizeof(v));
    crc = __crc32cw(crc, v);
    p += 4;
    n -= 4;
  }
  if (n >= 2) {
    uint16_t v = 0;
    std::memcpy(&v, p, sizeof(v));
    crc = __crc32ch(crc, v);
    p += 2;
    n -= 2;
  }
  if (n >= 1) crc = __crc32cb(crc, *p);
  return crc ^ 0xFFFFFFFFu;
}

// ── MatcherNeon ───────────────────────────────────────────────────────────────
//
// Fixed 256-slot open-addressing table keyed by the lower 8 bits of CRC32C.
//
//   slot  = crc32c_arm(str) & 0xFF          — single AND, no division
//   probe = linear within the 256-slot ring  — O(1) expected
//
// Dispatch:
//   invoke_at(idx, …) uses fold (… || Is == idx && …) over Is = 0…N-1.
//   Because N ≤ 128, idx is bounded to [0, 127] — LLVM emits a jump table.
//
// Constraint: N ≤ 128 (load factor ≤ 0.5 in 256 slots).

struct MatcherNeon {
 private:
  static constexpr std::size_t kTableSize = 256;
  static constexpr uint8_t     kMask      = 0xFF;

  template <class... Args>
  struct Table {
    static constexpr std::size_t kN   = sizeof...(Args);
    static constexpr uint32_t    kEmpty = static_cast<uint32_t>(kN);

    struct Slot {
      uint32_t hash{0};
      uint32_t idx{kEmpty};
    };

    static constexpr auto build() noexcept -> std::array<Slot, kTableSize> {
      std::array<Slot, kTableSize> t{};
      const std::array<std::string_view, kN> names{Args::name...};
      for (uint32_t i = 0; i < static_cast<uint32_t>(kN); ++i) {
        const uint32_t h = crc32c(names[i]);
        uint8_t s = static_cast<uint8_t>(h);          // lower 8 bits
        while (t[s].idx != kEmpty) s = static_cast<uint8_t>(s + 1);
        t[s] = {h, i};
      }
      return t;
    }

    static constexpr std::array<Slot, kTableSize> kSlots = build();

    // fold (… || Is == idx && …): idx ∈ [0, N-1] → compiler emits jump table.
    template <class Callback, std::size_t... Is>
    static auto invoke_at(uint32_t idx, std::string_view str,
                          std::tuple<Args...>& args, Callback& cb,
                          std::index_sequence<Is...>) noexcept -> bool {
      return (... ||
              (static_cast<uint32_t>(Is) == idx &&
               str == std::tuple_element_t<Is, std::tuple<Args...>>::name &&
               (std::invoke(cb, std::get<Is>(args)), true)));
    }

    template <class Callback>
    static auto lookup(uint32_t h, std::string_view str,
                       std::tuple<Args...>& args, Callback&& cb) -> bool {
      uint8_t s = static_cast<uint8_t>(h);             // lower 8 bits
      while (kSlots[s].idx != kEmpty) {
        if (kSlots[s].hash == h) {
          if (invoke_at(kSlots[s].idx, str, args, cb,
                        std::index_sequence_for<Args...>{}))
            return true;
        }
        s = static_cast<uint8_t>(s + 1);
      }
      return false;
    }
  };

 public:
  template <class Callback, class... Args>
  static auto find(std::string_view str, std::tuple<Args...>& args,
                   Callback&& cb) -> bool {
    static_assert(sizeof...(Args) <= 128,
                  "MatcherNeon: tuple size must be <= 128 (256-slot table, load <= 0.5)");
    const uint32_t h = crc32c_arm(str);
    return Table<Args...>::lookup(h, str, args, std::forward<Callback>(cb));
  }
};

}  // namespace ser20
