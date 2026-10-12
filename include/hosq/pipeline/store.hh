#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "hosq/context.hh"
#include "hosq/error.hh"
#include "hosq/pipeline.hh"

namespace hosq::detail {
template <class T>
struct ConstructFn;
}  // namespace hosq::detail

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
  // Called after the first step's input adapter. Unknown constructors,
  // contexts and result adapters retain ordinary execution.
  template <class Store, auto Maker, class Input, class Context>
    requires std::same_as<Store, OnceImpl> &&
             std::same_as<std::remove_cvref_t<decltype(Maker)>,
                          hosq::detail::ConstructFn<std::string>> &&
             std::same_as<Input, std::string_view> &&
             std::same_as<
                 std::remove_cv_t<Context>,
                 DecodeContext<std::optional<std::string>,
                               typename std::remove_cv_t<Context>::reader_type,
                               std::remove_cv_t<Context>::scan_phase>> &&
             std::same_as<
                 typename ext::InputView<std::invoke_result_t<
                     decltype(Maker), Input&&>>::identity_view,
                 ext::InputView<std::invoke_result_t<decltype(Maker), Input&&>>>
  HOSQ_ALWAYS_INLINE static constexpr auto try_construct(Input& input,
                                                         Context& ctx) -> bool {
    // Preserve ordinary construction before duplicate reporting.
    if (ctx.value().has_value()) return false;
    ctx.value().emplace(input);
    return true;
  }

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
              .detail = "store::once already has a value",
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
[[deprecated]] inline constexpr auto push = step<detail::PushImpl>;

/// Discards the decoded value; storage_type<T> = ignored.
[[deprecated]] inline constexpr auto ignore = step<detail::IgnoreImpl>;

/// Stores the decoded value once; errors with duplicate_member on subsequent
/// stores.
inline constexpr auto once = step<detail::OnceImpl>;

/// Stores the decoded value once; later occurrences are consumed and ignored.
inline constexpr auto first_wins = step<detail::FirstWinsImpl>;

}  // namespace hosq::store
