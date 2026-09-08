#pragma once

#include "hosq/context.hh"
#include "hosq/error.hh"
#include "hosq/pipeline.hh"
#include "hosq/reader_base.hh"
#include "hosq/view.hh"

namespace hosq::ext {

template <ReaderConcept R>
struct ScanScalar {
  template <class T, TryPhase Phase>
  auto operator()(const DecodeContext<T, R, Phase>& ctx) const -> ScalarView {
    const R& reader = ctx.reader();
    const auto& state = ctx.reader_state();
    const auto& tok = reader.current_token(state);
    if (tok.type != TokenType::Scalar) [[unlikely]] {
      reader.report_error(state, Error{
                                     .code = ErrorCode::type_mismatch,
                                     .offset = tok.offset,
                                     .detail = "scan::scalar expected scalar",
                                 });
    }
    return {};
  }
};

template <ReaderConcept R>
struct ScanObject {
  template <class T, TryPhase Phase>
  auto operator()(DecodeContext<T, R, Phase>& ctx) const -> ObjectView {
    auto& reader = ctx.reader();
    auto& state = ctx.reader_state();
    const auto& tok = reader.current_token(state);
    if (tok.type != TokenType::ObjectBegin) [[unlikely]] {
      reader.report_error(state, Error{
                                     .code = ErrorCode::type_mismatch,
                                     .offset = tok.offset,
                                     .detail = "scan::object expected object",
                                 });
    }
    return {};
  }
};

template <ReaderConcept R>
struct ScanArray {
  template <class T, TryPhase Phase>
  auto operator()(DecodeContext<T, R, Phase>& ctx) const -> ArrayView {
    auto& reader = ctx.reader();
    auto& state = ctx.reader_state();
    const auto& tok = reader.current_token(state);

    if (tok.type != TokenType::ArrayBegin) [[unlikely]] {
      reader.report_error(state, Error{
                                     .code = ErrorCode::type_mismatch,
                                     .offset = tok.offset,
                                     .detail = "scan::array expected array",
                                 });
    }
    return {};
  }
};

}  // namespace hosq::ext

namespace hosq::scan::detail {

template <class Ext, class Ctx>
concept CanScanScalarCtx = requires(const Ext& ext, Ctx&& ctx) {
  { ext(static_cast<Ctx&&>(ctx)) };
};

struct ScanScalar {
  template <class Input, class Context>
    requires requires(const Input& input, Context& ctx) {
      ext::ScanScalar<typename std::remove_cvref_t<Context>::reader_type>{}(
          input, ctx);
    }
  HOSQ_ALWAYS_INLINE auto operator()(const Input& input, Context& ctx) const {
    return ext::ScanScalar<typename std::remove_cvref_t<Context>::reader_type>{}(
        input, ctx);
  }

  template <class T, ReaderConcept R, TryPhase Phase>
  auto operator()(DecodeContext<T, R, Phase>& ctx) const
    requires CanScanScalarCtx<ext::ScanScalar<R>, DecodeContext<T, R, Phase>&>
  {
    return ext::ScanScalar<R>{}(ctx);
  }

  template <class T, ReaderConcept R, TryPhase Phase>
  auto operator()(const DecodeContext<T, R, Phase>& ctx) const
    requires CanScanScalarCtx<ext::ScanScalar<R>,
                              const DecodeContext<T, R, Phase>&>
  {
    return ext::ScanScalar<R>{}(ctx);
  }
};

struct ScanObject {
  template <class T, ReaderConcept R, TryPhase Phase>
  auto operator()(DecodeContext<T, R, Phase>& ctx) const -> ObjectView {
    return ext::ScanObject<R>{}(ctx);
  }
};

struct ScanArray {
  template <class T, ReaderConcept R, TryPhase Phase>
  auto operator()(DecodeContext<T, R, Phase>& ctx) const {
    return ext::ScanArray<R>{}(ctx);
  }
};

}  // namespace hosq::scan::detail

namespace hosq::scan {

inline constexpr auto scalar = hosq::step<detail::ScanScalar>;

inline constexpr auto object = hosq::step<detail::ScanObject>;

inline constexpr auto array = hosq::step<detail::ScanArray>;

}  // namespace hosq::scan
