#pragma once

#include "hosq/context.hh"
#include "hosq/extended_value_types.hh"
#include "hosq/pipeline.hh"
#include "hosq/view.hh"

namespace hosq::decode::detail {
template <class Value>
struct ExtendedValueImpl {
  using view_step = ExtendedValueImpl;
  template <class>
  using input_type = ScalarView;

  template <class Context>
  auto operator()(const ScalarView&, const Context& ctx) const -> Value {
    Value output{};
    if (ctx.reader().error(ctx.reader_state())) return output;
    if constexpr (requires {
                    ctx.reader().read_semantic(ctx.reader_state(), output);
                  }) {
      (void)ctx.reader().read_semantic(ctx.reader_state(), output);
    } else {
      ctx.reader().report_error(
          ctx.reader_state(),
          Error{ErrorCode::unsupported_type,
                ctx.reader().current_token(ctx.reader_state()).offset,
                "this format has no codec for the requested semantic type"});
    }
    return output;
  }
};
}  // namespace hosq::decode::detail

namespace hosq::decode {
inline constexpr auto instant = step<detail::ExtendedValueImpl<InstantValue>>;
inline constexpr auto offset_date_time =
    step<detail::ExtendedValueImpl<OffsetDateTimeValue>>;
inline constexpr auto local_date_time =
    step<detail::ExtendedValueImpl<LocalDateTimeValue>>;
inline constexpr auto local_date =
    step<detail::ExtendedValueImpl<LocalDateValue>>;
inline constexpr auto local_time =
    step<detail::ExtendedValueImpl<LocalTimeValue>>;
inline constexpr auto decimal128 =
    step<detail::ExtendedValueImpl<Decimal128Value>>;
inline constexpr auto object_id = step<detail::ExtendedValueImpl<ObjectIdValue>>;
inline constexpr auto bson_timestamp =
    step<detail::ExtendedValueImpl<BsonTimestampValue>>;
inline constexpr auto msgpack_extension =
    step<detail::ExtendedValueImpl<ExtensionValue>>;
}  // namespace hosq::decode
