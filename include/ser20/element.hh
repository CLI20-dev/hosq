#pragma once

#include "ser20/pipeline.hh"
#include "ser20/string_literal.hh"

namespace ser20 {


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
struct ArgParameter {
  // Presence presence{Presence::optional};  ///< Whether the argument is
  // required. std::optional<T>
  //     default_value{};  ///< Default value when the argument is absent.
  // std::function<void(const T&)>
  //     on_parse{};          ///< Callback invoked once after parsing completes.
  // std::string_view env{};  ///< Environment variable name to fall back to when
  //                          ///< the option is absent from the command line.
};

namespace detail {

template <StringLiteral Name, auto A>
struct ElemImpl : public ElementTag {
  using value_type =
      typename std::remove_cvref_t<decltype(std::get<1>(A.validate()))>::type;
  ElemImpl() : ElemImpl(ArgParameter<value_type>{}) {}
  ElemImpl(ArgParameter<value_type> param) : param_(std::move(param)) {}

  // Bind to an external variable after construction.
  // Allows: opt.bind(var)  or  opt = &var
  auto bind(std::remove_pointer_t<value_type>& ref) -> ElemImpl&
    requires std::is_pointer_v<value_type>
  {
    value_ = &ref;
    return *this;
  }
  auto operator=(std::remove_pointer_t<value_type>* ptr) -> ElemImpl&
    requires std::is_pointer_v<value_type>
  {
    value_ = ptr;
    return *this;
  }

  static constexpr auto name = Name;

  [[nodiscard]] auto value() const -> const value_type& { return value_; }
  [[nodiscard]] auto value() -> value_type& { return value_; }
  [[nodiscard]] auto occurrences() const -> std::size_t {
    return occurrence_count_;
  }
  [[nodiscard]] auto operator*() -> value_type& { return value_; }
  [[nodiscard]] auto operator*() const -> const value_type& { return value_; }
  [[nodiscard]] auto operator->() -> value_type* { return &value_; }
  [[nodiscard]] auto operator->() const -> const value_type* { return &value_; }

  template <class U>
  [[nodiscard]] auto value_or(U&& fallback) const -> value_type {
    return static_cast<value_type>(std::forward<U>(fallback));
  }

  [[nodiscard]] explicit operator std::optional<value_type>() const {
    return std::nullopt;
  }

 protected:
  static constexpr bool entry_is_optional =
      std::remove_cvref_t<decltype(A)>::entry_is_optional;

  // Called once when the option token is seen (before processing its values).
  auto notify_option_seen() -> void { ++occurrence_count_; }

  // Called once per value token associated with this option.
  auto invoke_action(std::string_view key, std::string_view text,
                     std::size_t index) -> PipelineResult<void> {
    PipelineCtx<value_type> ctx{
        .index = index,
        .key = key,
        .occurrences = occurrence_count_,
        .arg = std::ref(value_),
    };
    return decltype(A)::invoke(ctx, PipelineResult<std::string_view>::ok(text));
  }

  [[nodiscard]] auto get_param() const -> const ArgParameter<value_type>& {
    return param_;
  }

 private:
  ArgParameter<value_type> param_;
  value_type value_{};
  std::size_t occurrence_count_{};  // times the option token appeared
};

};  // namespace detail

};  // namespace ser20
