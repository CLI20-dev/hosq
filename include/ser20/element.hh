#pragma once

#include <cstddef>
#include <optional>
#include <string_view>
#include <type_traits>
#include <utility>

#include "ser20/parse_context.hh"
#include "ser20/pipeline.hh"
#include "ser20/string_literal.hh"

namespace ser20 {

#ifndef SER20_ALWAYS_INLINE
#if defined(__GNUC__) || defined(__clang__)
#define SER20_ALWAYS_INLINE inline __attribute__((always_inline))
#elif defined(_MSC_VER)
#define SER20_ALWAYS_INLINE __forceinline
#else
#define SER20_ALWAYS_INLINE inline
#endif
#endif

struct ElementTag {};

/**
 * @brief Controls whether an element is required or optional in the command
 * line.
 */
enum class Presence { required, optional };

/**
 * @brief Parameter bag used when constructing `ElemImpl` or `PositionalImpl`
 * with named fields.
 *
 * Allows brace-initialisation with named members, e.g.:
 * @code
 *   cli::Option<int, "count"> count{cli::ArgParameter<int>{
 *       .help = "Number of repetitions",
 *       .presence = cli::required,
 *       .default_value = 1,
 *   }};
 * @endcode
 *
 * @tparam T The storage value type of the argument.
 */
template <class T>
struct ArgParameter {};

template <StringLiteral Name, auto Pipe>
struct ElemImpl : public ElementTag {
  using pipe_type = std::remove_cvref_t<decltype(Pipe)>;
  using storage_type = parse_storage_t<Pipe>;
  using value_type = storage_type;

  ElemImpl() = default;
  explicit ElemImpl(ArgParameter<value_type>) {}

  static constexpr std::string_view name{Name.data(), Name.size()};
  static constexpr bool is_unknown_handler = name.empty();

  [[nodiscard]] auto value() const -> const storage_type& { return value_; }
  [[nodiscard]] auto value() -> storage_type& { return value_; }
  [[nodiscard]] auto occurrences() const -> std::size_t { return occurrences_; }
  [[nodiscard]] auto operator*() -> storage_type& { return value_; }
  [[nodiscard]] auto operator*() const -> const storage_type& { return value_; }
  [[nodiscard]] auto operator->() -> storage_type* { return &value_; }
  [[nodiscard]] auto operator->() const -> const storage_type* {
    return &value_;
  }

  operator const storage_type&() const noexcept { return value_; }
  operator storage_type&() noexcept { return value_; }

  template <class U>
  [[nodiscard]] auto value_or(U&& fallback) const -> storage_type {
    return static_cast<storage_type>(std::forward<U>(fallback));
  }

  [[nodiscard]] explicit operator std::optional<storage_type>() const {
    return std::nullopt;
  }

  SER20_ALWAYS_INLINE auto parse(ParseCtxBase& base) -> PipelineResult<void> {
    ParseCtx<storage_type> ctx{base, value_};
    ctx.occurrences = occurrences_;
    auto result = pipe_type::invoke(ctx, PipelineResult<void>::ok());
    base.next_is_pending = ctx.next_is_pending;
    base.pending_tok = ctx.pending_tok;
    if (result.have_value()) [[likely]]
      ++occurrences_;
    return result;
  }

 private:
  storage_type value_{};
  std::size_t occurrences_{};
};

#undef SER20_ALWAYS_INLINE
}  // namespace ser20
