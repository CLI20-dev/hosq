#pragma once
#include <cstdint>
#include <functional>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

namespace ser20 {

// ── CRC32C (compile-time, used by matcher_neon.hh) ───────────────────────────

constexpr auto crc32c(std::string_view s) -> uint32_t {
  uint32_t crc = 0xFFFFFFFF;
  for (auto c : s) {
    crc ^= static_cast<uint8_t>(c);
    for (int i = 0; i < 8; ++i) crc = (crc >> 1) ^ (0x82F63B78u & -(crc & 1));
  }
  return crc ^ 0xFFFFFFFF;
}

// ── Matcher ───────────────────────────────────────────────────────────────────
//
// Plain if-chain over the tuple via if constexpr recursion.
// Short-circuits on first match; no hashing overhead.

struct Matcher {
  template <class Callback, class... Args>
  static constexpr auto find(std::string_view str, std::tuple<Args...>& args,
                             Callback&& cb) -> bool {
    return find_impl<0>(str, args, std::forward<Callback>(cb));
  }

 private:
  template <std::size_t I, class Callback, class... Args>
  static constexpr auto find_impl(std::string_view str,
                                  std::tuple<Args...>& args,
                                  Callback&& cb) -> bool {
    if constexpr (I < sizeof...(Args)) {
      using Arg = std::remove_cvref_t<std::tuple_element_t<I, std::tuple<Args...>>>;
      if (str == Arg::name) {
        std::invoke(std::forward<Callback>(cb), std::get<I>(args));
        return true;
      }
      return find_impl<I + 1>(str, args, std::forward<Callback>(cb));
    } else {
      return false;
    }
  }
};

}  // namespace ser20
