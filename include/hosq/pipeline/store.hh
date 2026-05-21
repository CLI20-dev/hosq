#pragma once

#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

#include "hosq/error.hh"
#include "hosq/pipeline.hh"
#include "hosq/result.hh"

namespace hosq::store {

/// Marker type representing a deliberately ignored (not stored) decoder output.
struct ignored {};

namespace detail {

struct AssignImpl {
  template <class Input>
  using storage_type = std::remove_cvref_t<Input>;

  template <class Input, class Context>
  auto operator()(Input value, Context& ctx) const -> void {
    ctx.value() = std::move(value);
  }
};

struct PushImpl {
  template <class Input>
  using storage_type = std::vector<std::remove_cvref_t<Input>>;

  template <class Input, class Context>
  auto operator()(Input value, Context& ctx) const -> void {
    ctx.value().push_back(std::move(value));
  }
};

struct EmplaceImpl {
  template <class Input>
  using storage_type = std::vector<std::remove_cvref_t<Input>>;

  template <class Input, class Context>
  auto operator()(Input value, Context& ctx) const -> void {
    ctx.value().emplace_back(std::move(value));
  }
};

struct IgnoreImpl {
  template <class Input>
  using storage_type = ignored;

  template <class T>
  auto operator()(T) const -> void {}
};

struct OnceImpl {
  template <class Input>
  using storage_type = std::optional<std::remove_cvref_t<Input>>;

  template <class Input, class Context>
  auto operator()(Input value, Context& ctx) const -> Result<void, Error> {
    if (ctx.value().has_value()) [[unlikely]] {
      return unexpected(Error{.code = ErrorCode::duplicate_member});
    }
    ctx.value() = std::move(value);
    return {};
  }
};

struct FirstWinsImpl {
  template <class Input>
  using storage_type = std::optional<std::remove_cvref_t<Input>>;

  template <class Input, class Context>
  auto operator()(Input value, Context& ctx) const -> void {
    if (ctx.value().has_value()) {
      return;
    }

    ctx.value() = std::move(value);
  }
};

struct OverwriteImpl {
  template <class Input>
  using storage_type = std::remove_cvref_t<Input>;

  template <class Input, class Context>
  auto operator()(Input value, Context& ctx) const -> void {
    ctx.value() = std::move(value);
  }
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

/// Stores the decoded value once; errors with duplicate_member on subsequent
/// stores.
inline constexpr auto once = step<detail::OnceImpl>;

/// Stores the decoded value once; later occurrences are consumed and ignored.
inline constexpr auto first_wins = step<detail::FirstWinsImpl>;

/// Stores the decoded value, allowing later occurrences to overwrite earlier
/// ones.
inline constexpr auto overwrite = step<detail::OverwriteImpl>;

}  // namespace hosq::store
