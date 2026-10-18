#pragma once

#include <string_view>

#include "hosq/enum_mapping.hh"
#include "hosq/match_result.hh"
#include "hosq/pipeline/encode.hh"
#include "hosq/pipeline/typed_decode.hh"

namespace hosq::decode {
namespace detail {
template <hosq::detail::EnumMappingT Mapping>
struct EnumValueImpl {
  template <class Context>
  auto operator()(std::string_view input, const Context& ctx) const
      -> MatchResult<typename Mapping::enum_type> {
    using E = typename Mapping::enum_type;
    if (ctx.reader().error(ctx.reader_state())) return {false, E{}};
    if (const auto value = Mapping::from_string(input)) return {true, *value};
    typed_error(ctx, ErrorCode::invalid_choice, "unknown enum name");
    return {false, E{}};
  }
};
}  // namespace detail

/// Convert semantic text into a mapped enum. Unknown names are hard errors.
template <hosq::detail::EnumMappingT Mapping>
inline constexpr auto enum_value = step<detail::EnumValueImpl<Mapping>>;
}  // namespace hosq::decode

namespace hosq::encode {
namespace detail {
template <hosq::detail::EnumMappingT Mapping>
struct EnumValueImpl {
  template <class Context>
  auto operator()(typename Mapping::enum_type input, Context& ctx) const
      -> void {
    if (ctx.writer().error()) return;
    if (const auto name = Mapping::to_string(input))
      ValueImpl{}(*name, ctx);
    else
      (void)hosq::detail::encode_error(
          ctx.writer(), ErrorCode::invalid_object_state, "unmapped enum value");
  }
};
}  // namespace detail

/// Encode only the canonical semantic name through the concrete text codec.
template <hosq::detail::EnumMappingT Mapping>
inline constexpr auto enum_value = step<detail::EnumValueImpl<Mapping>>;
}  // namespace hosq::encode
