#pragma once

#include <array>
#include <concepts>
#include <cstddef>
#include <optional>
#include <string_view>
#include <tuple>
#include <type_traits>

#include "hosq/string_literal.hh"

namespace hosq {

/// One enum value, its canonical output name, and optional input-only aliases.
/// Names are semantic text, not escaped wire representations.
template <auto Value, StringLiteral Canonical, StringLiteral... Aliases>
  requires std::is_enum_v<decltype(Value)>
struct EnumName {
  using enum_type = decltype(Value);
  static constexpr enum_type value = Value;
  static constexpr auto names =
      std::array<std::string_view, 1 + sizeof...(Aliases)>{Canonical.view(),
                                                           Aliases.view()...};
};

namespace detail {
template <class T>
struct IsEnumName : std::false_type {};
template <auto V, StringLiteral C, StringLiteral... A>
struct IsEnumName<EnumName<V, C, A...>> : std::true_type {};

template <class... Names>
consteval auto valid_enum_mapping() -> bool {
  if constexpr (sizeof...(Names) == 0 || !(IsEnumName<Names>::value && ...)) {
    return false;
  } else {
    using E = typename std::tuple_element_t<0, std::tuple<Names...>>::enum_type;
    if constexpr (!(std::same_as<E, typename Names::enum_type> && ...)) {
      return false;
    } else {
      constexpr std::array<E, sizeof...(Names)> values{Names::value...};
      for (std::size_t i = 0; i < values.size(); ++i)
        for (std::size_t j = i + 1; j < values.size(); ++j)
          if (values[i] == values[j]) return false;
      constexpr auto count = (Names::names.size() + ...);
      std::array<std::string_view, count> names{};
      std::size_t position = 0;
      (
          [&] {
            for (auto name : Names::names) names[position++] = name;
          }(),
          ...);
      for (std::size_t i = 0; i < names.size(); ++i)
        for (std::size_t j = i + 1; j < names.size(); ++j)
          if (names[i] == names[j]) return false;
      return true;
    }
  }
}
}  // namespace detail

/// A nonempty, unambiguous mapping for one enum type. Each enum value has
/// exactly one canonical name; every canonical name and alias is unique.
template <class... Names>
  requires(detail::valid_enum_mapping<Names...>())
struct EnumMapping {
  using enum_type =
      typename std::tuple_element_t<0, std::tuple<Names...>>::enum_type;

  [[nodiscard]]
  static constexpr auto from_string(std::string_view input) noexcept
      -> std::optional<enum_type> {
    std::optional<enum_type> result;
    (
        [&] {
          for (auto name : Names::names)
            if (input == name) result = Names::value;
        }(),
        ...);
    return result;
  }

  [[nodiscard]]
  static constexpr auto to_string(enum_type input) noexcept
      -> std::optional<std::string_view> {
    std::optional<std::string_view> result;
    (
        [&] {
          if (input == Names::value) result = Names::names[0];
        }(),
        ...);
    return result;
  }
};

namespace detail {
template <class T>
struct IsEnumMapping : std::false_type {};
template <class... Names>
struct IsEnumMapping<EnumMapping<Names...>> : std::true_type {};
template <class T>
concept EnumMappingT = IsEnumMapping<T>::value;
}  // namespace detail

}  // namespace hosq
