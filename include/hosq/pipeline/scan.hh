#pragma once

#include <cstdint>
#include <string_view>

#include "hosq/context.hh"
#include "hosq/error.hh"
#include "hosq/pipeline.hh"
#include "hosq/reader_base.hh"
#include "hosq/view.hh"

namespace hosq {

namespace ext {

template <ReaderConcept R>
  requires(!ReaderTraits<R>::is_binary_format)
struct ScanScalar {
  template <class T, TryPhase Phase>
  auto operator()(const DecodeContext<T, R, Phase>& ctx) const
      -> std::string_view {
    const R& reader = ctx.reader();
    const auto& state = ctx.reader_state();
    const auto& tok = reader.current_token(state);

    if (tok.type != TokenType::Scalar) {
      reader.report_error(state, Error{
                                     .code = ErrorCode::type_mismatch,
                                     .offset = tok.offset,
                                 });
      return {};
    }

    return reader.input(state).substr(tok.offset, tok.length);
  }
};

template <ReaderConcept R>
  requires(!ReaderTraits<R>::is_binary_format &&
           ReaderTraits<R>::format == Format::json)
struct ScanString {
  template <class T, TryPhase Phase>
  auto operator()(const DecodeContext<T, R, Phase>& ctx) const
      -> std::string_view {
    const R& reader = ctx.reader();
    const auto& state = ctx.reader_state();
    const auto& tok = reader.current_token(state);

    if (tok.type != TokenType::Scalar ||
        tok.context != static_cast<std::uint8_t>(JsonTokenContext::string)) {
      reader.report_error(state, Error{
                                     .code = ErrorCode::type_mismatch,
                                     .offset = tok.offset,
                                 });
      return {};
    }

    return reader.input(state).substr(tok.offset, tok.length);
  }
};

template <ReaderConcept R>
  requires(!ReaderTraits<R>::is_binary_format &&
           ReaderTraits<R>::format == Format::json)
struct ScanLiteral {
  template <class T, TryPhase Phase>
  auto operator()(const DecodeContext<T, R, Phase>& ctx) const
      -> std::string_view {
    const R& reader = ctx.reader();
    const auto& state = ctx.reader_state();
    const auto& tok = reader.current_token(state);

    if (tok.type != TokenType::Scalar ||
        tok.context != static_cast<std::uint8_t>(JsonTokenContext::literal)) {
      reader.report_error(state, Error{
                                     .code = ErrorCode::type_mismatch,
                                     .offset = tok.offset,
                                 });
      return {};
    }
    return reader.input(state).substr(tok.offset, tok.length);
  }
};

template <ReaderConcept R>
struct ScanObject {
  template <class T>
  auto operator()(DecodeContext<T, R>& ctx) const -> ObjectView {
    auto& reader = ctx.reader();
    auto& state = ctx.reader_state();
    if (!reader.next_token(state)) [[unlikely]] {
      return {};
    }
    const auto& tok = reader.current_token(state);
    if (tok.type != TokenType::ObjectBegin) [[unlikely]] {
      reader.report_error(state, Error{
                                     .code = ErrorCode::type_mismatch,
                                     .offset = tok.offset,
                                 });
    }
    return {};
  }
};

template <ReaderConcept R>
struct ScanArray {
  template <class T>
  auto operator()(DecodeContext<T, R>& ctx) const -> ArrayView {
    auto& reader = ctx.reader();
    auto& state = ctx.reader_state();

    if (!reader.next_token(state)) [[unlikely]] {
      return {};
    }
    const auto& tok = reader.current_token(state);

    if (tok.type != TokenType::ArrayBegin) {
      reader.report_error(state, Error{
                                     .code = ErrorCode::type_mismatch,
                                     .offset = tok.offset,
                                 });
    }
    return {};
  }
};

}  // namespace ext

namespace scan {

namespace detail {

struct ScanScalar {
  template <class T, ReaderConcept R, TryPhase Phase>
  auto operator()(const DecodeContext<T, R, Phase>& ctx) const
      -> std::string_view {
    return ext::ScanScalar<R>{}(ctx);
  }
};

struct ScanString {
  template <class T, ReaderConcept R, TryPhase Phase>
  auto operator()(const DecodeContext<T, R, Phase>& ctx) const
      -> std::string_view {
    return ext::ScanString<R>{}(ctx);
  }
};

struct ScanLiteral {
  template <class T, ReaderConcept R, TryPhase Phase>
  auto operator()(const DecodeContext<T, R, Phase>& ctx) const
      -> std::string_view {
    return ext::ScanLiteral<R>{}(ctx);
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
  auto operator()(DecodeContext<T, R, Phase>& ctx) const -> ArrayView {
    return ext::ScanArray<R>{}(ctx);
  }
};

}  // namespace detail

inline constexpr auto scalar = hosq::step<detail::ScanScalar>;

inline constexpr auto string = hosq::step<detail::ScanString>;

inline constexpr auto literal = hosq::step<detail::ScanLiteral>;

inline constexpr auto object = hosq::step<detail::ScanObject>;

inline constexpr auto array = hosq::step<detail::ScanArray>;

}  // namespace scan

}  // namespace hosq
