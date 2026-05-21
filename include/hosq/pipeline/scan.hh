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
  auto operator()(const DecodeContext<void, R, ScanPhase::normal>& ctx) const
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
  auto operator()(const DecodeContext<void, R, ScanPhase::normal>& ctx) const
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
  auto operator()(const DecodeContext<void, R, ScanPhase::normal>& ctx) const
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
  auto operator()(const DecodeContext<void, R, ScanPhase::normal>& ctx) const
      -> Result<ObjectView, Error>;
};
template <ReaderConcept R>
inline auto ScanObject<R>::operator()(
    const DecodeContext<void, R, ScanPhase::normal>& ctx) const
    -> Result<ObjectView, Error> {
  const R& reader = ctx.reader();
  const auto& state = ctx.reader_state();
  const auto& tok = reader.current_token(state);

  if (tok.type != TokenType::ObjectBegin) {
    return Result<ObjectView, Error>{unexpected(Error{
        .code = ErrorCode::type_mismatch,
        .offset = tok.offset,
    })};
  }

  return ObjectView{};
}

template <ReaderConcept R>
struct ScanArray {
  auto operator()(const DecodeContext<void, R, ScanPhase::normal>& ctx) const
      -> Result<ArrayView, Error> {
    const R& reader = ctx.reader();
    const auto& state = ctx.reader_state();
    const auto& tok = reader.current_token(state);

    if (tok.type != TokenType::ArrayBegin) {
      return Result<ArrayView, Error>{unexpected(Error{
          .code = ErrorCode::type_mismatch,
          .offset = tok.offset,
      })};
    }

    return ArrayView{};
  }
};

};  // namespace ext

namespace scan {

namespace detail {

struct ScanScalar {
  template <ReaderConcept R>
  auto operator()(const DecodeContext<void, R, ScanPhase::normal>& ctx) const
      -> Result<std::string_view, Error> {
    return ext::ScanScalar<R>{}(ctx);
  }
};

struct ScanString {
  template <ReaderConcept R>
  auto operator()(const DecodeContext<void, R, ScanPhase::normal>& ctx) const
      -> Result<std::string_view, Error> {
    return ext::ScanString<R>{}(ctx);
  }
};

struct ScanLiteral {
  template <ReaderConcept R>
  auto operator()(const DecodeContext<void, R, ScanPhase::normal>& ctx) const
      -> Result<std::string_view, Error> {
    return ext::ScanLiteral<R>{}(ctx);
  }
};

struct ScanObject {
  template <ReaderConcept R>
  auto operator()(const DecodeContext<void, R, ScanPhase::normal>& ctx) const
      -> Result<ObjectView, Error> {
    return ext::ScanObject<R>{}(ctx);
  }
};

struct ScanArray {
  template <ReaderConcept R>
  auto operator()(const DecodeContext<void, R, ScanPhase::normal>& ctx) const
      -> Result<ArrayView, Error> {
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
