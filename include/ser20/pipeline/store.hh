#pragma once

#include <vector>

#include "ser20/pipeline.hh"

namespace ser20::store {

/// Marker type representing a deliberately ignored (not stored) decoder output.
struct ignored {};

namespace detail {

struct AssignImpl {
  template <class Input>
  using storage_type = std::remove_cvref_t<Input>;

  template <class Input, class Context>
  auto operator()(Input value, Context& ctx) const -> void {
    ctx.elem() = std::move(value);
  }
};

struct PushImpl {
  template <class Input>
  using storage_type = std::vector<std::remove_cvref_t<Input>>;

  template <class Input, class Context>
  auto operator()(Input value, Context& ctx) const -> void {
    ctx.elem().push_back(std::move(value));
  }
};

struct EmplaceImpl {
  template <class Input>
  using storage_type = std::vector<std::remove_cvref_t<Input>>;

  template <class Input, class Context>
  auto operator()(Input value, Context& ctx) const -> void {
    ctx.elem().emplace_back(std::move(value));
  }
};

struct IgnoreImpl {
  template <class Input>
  using storage_type = ignored;

  template <class T>
  auto operator()(T) const -> void {}
};

}  // namespace detail

/// Assigns the decoded value into ctx.elem(); storage_type<T> = T.
inline constexpr auto assign = step<detail::AssignImpl>;

/// Appends the decoded value via push_back; storage_type<T> = std::vector<T>.
inline constexpr auto push = step<detail::PushImpl>;

/// Appends the decoded value via emplace_back; storage_type<T> = std::vector<T>.
inline constexpr auto emplace = step<detail::EmplaceImpl>;

/// Discards the decoded value; storage_type<T> = ignored.
inline constexpr auto ignore = step<detail::IgnoreImpl>;

}  // namespace ser20::store
