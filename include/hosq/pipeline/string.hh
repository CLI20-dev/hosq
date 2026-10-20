#pragma once

#include <string>
#include <string_view>
#include <utility>

#include "hosq/pipeline.hh"
#include "hosq/reader_base.hh"
#include "hosq/view.hh"

namespace hosq::ext {
/// Read a borrowed source representation after checking the string category.
/// JSON returns its escaped body, TOML its complete quoted literal (or bare
/// key), and binary formats their string payload. The input owns these bytes.
template <ReaderConcept R>
struct DecodeStringSource {
  template <class Context>
  auto operator()(const ScalarView&, const Context& ctx,
                  std::string_view& output) const -> bool {
    if constexpr (requires {
                    ctx.reader().read_string_source(ctx.reader_state(), output);
                  })
      return ctx.reader().read_string_source(ctx.reader_state(), output);
    else {
      ctx.reader().report_error(
          ctx.reader_state(),
          {ErrorCode::unsupported_type,
           ctx.reader().current_token(ctx.reader_state()).offset,
           "this reader has no string source codec"});
      return false;
    }
  }
};

/// Format-owned specializations interpret their input, never reread a value.
/// Every branch retains owned std::string output, including no-op formats.
template <ReaderConcept R, Format Source>
struct UnescapeString {
  template <class Context>
  auto operator()(std::string input, const Context& ctx) const -> std::string {
    if constexpr (reader_traits_t<R>::format == Source)
      ctx.reader().report_error(
          ctx.reader_state(),
          {ErrorCode::unsupported_type,
           ctx.reader().current_token(ctx.reader_state()).offset,
           "this reader has no codec for this string conversion"});
    return input;
  }
};
}  // namespace hosq::ext

namespace hosq::decode::detail {
struct StringSourceImpl {
  using view_step = StringSourceImpl;
  template <class>
  using input_type = ScalarView;
  template <class Context>
  auto operator()(const ScalarView& view, const Context& ctx) const
      -> std::string_view {
    std::string_view output;
    if (!ctx.reader().error(ctx.reader_state()))
      (void)ext::DecodeStringSource<typename Context::reader_type>{}(view, ctx,
                                                                     output);
    return output;
  }
};
template <Format Source>
struct UnescapeStringImpl {
  template <class Context>
  auto operator()(std::string input, const Context& ctx) const -> std::string {
    if (ctx.reader().error(ctx.reader_state())) return input;
    return ext::UnescapeString<typename Context::reader_type, Source>{}(
        std::move(input), ctx);
  }
};
}  // namespace hosq::decode::detail

namespace hosq::decode {
/// Borrow the original string representation; its input must remain alive.
/// No escape conversion or ownership transfer happens at this step.
inline constexpr auto string_source = step<detail::StringSourceImpl>;
/// Resolve a JSON string body into an owned string. Other formats pass through.
/// Use construct<std::string> to own a borrowed source before this step.
inline constexpr auto json_unescape =
    step<detail::UnescapeStringImpl<Format::json>>;
/// Resolve a TOML quoted literal, or pass through a bare key. Other formats
/// pass through. This step consumes and returns an owned string.
inline constexpr auto toml_unescape =
    step<detail::UnescapeStringImpl<Format::toml>>;
}  // namespace hosq::decode
