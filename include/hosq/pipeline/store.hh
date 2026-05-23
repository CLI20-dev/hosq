#pragma once

#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

#include "hosq/error.hh"
#include "hosq/pipeline.hh"

namespace hosq::store {

namespace detail {

struct AssignImpl {
  template <class Input>
  using storage_type = std::remove_cvref_t<Input>;

  template <class Input, class Context>
  auto operator()(Input&& value, Context& ctx) const -> void {
    ctx.value() = std::forward<Input>(value);
  }
};

struct PushImpl {
  template <class Input>
  using storage_type = std::vector<std::remove_cvref_t<Input>>;

  template <class Input, class Context>
  auto operator()(Input&& value, Context& ctx) const -> void {
    ctx.value().push_back(std::forward<Input>(value));
  }
};

struct IgnoreImpl {
  template <class Input>
  using storage_type = std::monostate;

  template <class T>
  auto operator()(const T&) const -> void {}
};

struct OnceImpl {
  template <class Input>
  using storage_type = std::optional<std::remove_cvref_t<Input>>;

  template <class Input, class Context>
  auto operator()(Input&& value, Context& ctx) const -> void {
    if (ctx.value().has_value()) [[unlikely]] {
      ctx.reader().report_error(
          ctx.reader_state(),
          Error{
              .code = ErrorCode::duplicate_member,
              .offset = ctx.reader().current_token(ctx.reader_state()).offset,
          });
      return;
    }
    ctx.value() = std::forward<Input>(value);
  }
};

struct FirstWinsImpl {
  template <class Input>
  using storage_type = std::optional<std::remove_cvref_t<Input>>;

  template <class Input, class Context>
  auto operator()(Input&& value, Context& ctx) const -> void {
    if (ctx.value().has_value()) {
      return;
    }
    ctx.value() = std::forward<Input>(value);
  }
};

}  // namespace detail

/// Assigns the decoded value into ctx.elem(); storage_type<T> = T.
inline constexpr auto assign = step<detail::AssignImpl>;

/// Appends the decoded value via push_back; storage_type<T> = std::vector<T>.
inline constexpr auto push = step<detail::PushImpl>;

/// Discards the decoded value; storage_type<T> = ignored.
inline constexpr auto ignore = step<detail::IgnoreImpl>;

/// Stores the decoded value once; errors with duplicate_member on subsequent
/// stores.
inline constexpr auto once = step<detail::OnceImpl>;

/// Stores the decoded value once; later occurrences are consumed and ignored.
inline constexpr auto first_wins = step<detail::FirstWinsImpl>;

}  // namespace hosq::store
