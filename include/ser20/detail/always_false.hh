#pragma once

namespace ser20::detail {

/// Always-false helper for static_assert inside unreachable `constexpr if`
/// branches.
template <class Head>
static constexpr bool always_false = false;

}  // namespace ser20::detail
