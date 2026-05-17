#pragma once

#include "ser20/context.hh"
#include "ser20/error.hh"
#include "ser20/pipeline.hh"
#include "ser20/result.hh"

namespace ser20::scan {

namespace detail {

struct ScalarImpl {
  template <class Input>
  static constexpr bool accepts_input = std::same_as<Input, void>;

  template <class Input>
  using after_type = std::string_view;

  template <DecodeContextLike Context>
  auto operator()(Context ctx, Result<void, Error> input) const
      -> Result<std::string_view, Error> {
    if (!input) [[unlikely]] {
      return Unexpected<Error>{input.error()};
    }
    Result<Token, Error> token_result = ctx.reader->next_token();
    if (token_result->type != TokenType::SCALAR) [[unlikely]] {
      return Unexpected<Error>{Error{
          .code = ErrorCode::unexpected_token,
          .offset = token_result->offset,
      }};
    }
    if (!token_result) [[unlikely]] {
      return Unexpected<Error>{token_result.error()};
    }
    return ctx.input.substr(token_result->offset, token_result->length);
  };
};

};  // namespace detail

static constexpr auto scalar = step<detail::ScalarImpl>;

};  // namespace ser20::scan
