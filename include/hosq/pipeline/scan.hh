#pragma once

#include "hosq/context.hh"
#include "hosq/error.hh"
#include "hosq/pipeline.hh"
#include "hosq/reader_base.hh"
#include "hosq/result.hh"
#include "hosq/view.hh"

namespace hosq {

namespace ext {

template <ReaderConcept R>
  requires(!ReaderTraits<R>::is_binary_format)
struct ScanScalar {
  template <class T, ScanPhase Phase>
  auto operator()(const DecodeContext<T, R, Phase>& ctx) const
      -> Result<std::string_view, Error> {
    const R& reader = ctx.reader();
    const auto& state = ctx.reader_state();
    const auto& tok = reader.current_token(state);

    if (tok.type != TokenType::Scalar) {
      return Result<std::string_view, Error>{unexpected(Error{
          .code = ErrorCode::type_mismatch,
          .offset = tok.offset,
      })};
    }

    return reader.input(state).substr(tok.offset, tok.length);
  }
};

template <ReaderConcept R>
  requires(!ReaderTraits<R>::is_binary_format &&
           ReaderTraits<R>::format == Format::json)
struct ScanString {
  template <class T, ScanPhase Phase>
  auto operator()(const DecodeContext<T, R, Phase>& ctx) const
      -> Result<std::string_view, Error> {
    const R& reader = ctx.reader();
    const auto& state = ctx.reader_state();
    const auto& tok = reader.current_token(state);

    if (tok.type != TokenType::Scalar ||
        tok.context != static_cast<std::uint8_t>(JsonTokenContext::string)) {
      return Result<std::string_view, Error>{unexpected(Error{
          .code = ErrorCode::type_mismatch,
          .offset = tok.offset,
      })};
    }

    return reader.input(state).substr(tok.offset, tok.length);
  }
};

template <ReaderConcept R>
  requires(!ReaderTraits<R>::is_binary_format &&
           ReaderTraits<R>::format == Format::json)
struct ScanLiteral {
  template <class T, ScanPhase Phase>
  auto operator()(const DecodeContext<T, R, Phase>& ctx) const
      -> Result<std::string_view, Error> {
    const R& reader = ctx.reader();
    const auto& state = ctx.reader_state();
    const auto& tok = reader.current_token(state);

    if (tok.type != TokenType::Scalar ||
        tok.context != static_cast<std::uint8_t>(JsonTokenContext::literal)) {
      return Result<std::string_view, Error>{unexpected(Error{
          .code = ErrorCode::type_mismatch,
          .offset = tok.offset,
      })};
    }

    return reader.input(state).substr(tok.offset, tok.length);
  }
};

template <ReaderConcept R>
struct ScanObject {
  template <class T, ScanPhase Phase>
  auto operator()(DecodeContext<T, R, Phase>& ctx) const
      -> Result<ObjectView<R, Phase>, Error>;
};
template <ReaderConcept R>
template <class T, ScanPhase Phase>
inline auto ScanObject<R>::operator()(DecodeContext<T, R, Phase>& ctx) const
    -> Result<ObjectView<R, Phase>, Error> {
  auto& reader = ctx.reader();
  auto& state = ctx.reader_state();

  if constexpr (Phase == ScanPhase::normal) {
    if (!reader.next_token(state)) [[unlikely]] {
      return unexpected(reader.error(state));
    }
  }

  const auto& tok = reader.current_token(state);

  if (tok.type != TokenType::ObjectBegin) {
    return Result<ObjectView<R, Phase>, Error>{unexpected(Error{
        .code = ErrorCode::type_mismatch,
        .offset = tok.offset,
    })};
  }

  return ObjectView<R, Phase>{reader, state};
}

template <ReaderConcept R>
struct ScanArray {
  template <class T, ScanPhase Phase>
  auto operator()(DecodeContext<T, R, Phase>& ctx) const
      -> Result<ArrayView<R, Phase>, Error> {
    auto& reader = ctx.reader();
    auto& state = ctx.reader_state();

    if constexpr (Phase == ScanPhase::normal) {
      if (!reader.next_token(state)) [[unlikely]] {
        return unexpected(reader.error(state));
      }
    }

    const auto& tok = reader.current_token(state);

    if (tok.type != TokenType::ArrayBegin) {
      return Result<ArrayView<R, Phase>, Error>{unexpected(Error{
          .code = ErrorCode::type_mismatch,
          .offset = tok.offset,
      })};
    }

    return ArrayView<R, Phase>{reader, state};
  }
};

};  // namespace ext

namespace scan {

namespace detail {

struct ScanScalar {
  auto operator()(hosq::detail::AnyDecodeContext) const
      -> Result<std::string_view, Error>;

  template <class T, ReaderConcept R, ScanPhase Phase>
  auto operator()(const DecodeContext<T, R, Phase>& ctx) const
      -> Result<std::string_view, Error> {
    return ext::ScanScalar<R>{}(ctx);
  }
};

struct ScanString {
  auto operator()(hosq::detail::AnyDecodeContext) const
      -> Result<std::string_view, Error>;

  template <class T, ReaderConcept R, ScanPhase Phase>
  auto operator()(const DecodeContext<T, R, Phase>& ctx) const
      -> Result<std::string_view, Error> {
    return ext::ScanString<R>{}(ctx);
  }
};

struct ScanLiteral {
  auto operator()(hosq::detail::AnyDecodeContext) const
      -> Result<std::string_view, Error>;

  template <class T, ReaderConcept R, ScanPhase Phase>
  auto operator()(const DecodeContext<T, R, Phase>& ctx) const
      -> Result<std::string_view, Error> {
    return ext::ScanLiteral<R>{}(ctx);
  }
};

struct ScanObject {
  auto operator()(hosq::detail::AnyDecodeContext) const
      -> Result<ObjectView<hosq::detail::PipelineProbeReader, ScanPhase::first>,
                Error>;

  template <class T, ReaderConcept R, ScanPhase Phase>
  auto operator()(DecodeContext<T, R, Phase>& ctx) const
      -> Result<ObjectView<R, Phase>, Error> {
    return ext::ScanObject<R>{}(ctx);
  }
};

struct ScanArray {
  auto operator()(hosq::detail::AnyDecodeContext) const
      -> Result<ArrayView<hosq::detail::PipelineProbeReader, ScanPhase::first>,
                Error>;

  template <class T, ReaderConcept R, ScanPhase Phase>
  auto operator()(DecodeContext<T, R, Phase>& ctx) const
      -> Result<ArrayView<R, Phase>, Error> {
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
