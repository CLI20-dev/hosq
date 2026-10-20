#pragma once

#include <cstdint>
#include <string>
#include <type_traits>
#include <variant>

#include "hosq/reader_base.hh"
#include "hosq/value_types.hh"

namespace hosq {
/// A codec converts its wire representation to these lossless numeric values.
/// The requested category lets textual codecs reject a category mismatch before
/// interpreting the literal. Common decoding still checks the returned category.
struct SemanticNumber {
  enum class Category { integer, floating };
  Category requested = Category::integer;
  std::variant<std::int64_t, std::uint64_t, double> value{std::int64_t{0}};
  // Preserve a textual minus on integer zero for strict unsigned JSON schemas.
  bool negative_integer = false;
};
}  // namespace hosq

namespace hosq::ext {
/// Optional scalar codec boundary. Readers can provide read_semantic(state, T&)
/// or specialize this adapter without exposing their state representation.
/// Existing dedicated string/binary reader hooks remain supported.
/// A failed conversion must report an error through the Reader.
template <ReaderConcept R>
struct DecodeSemanticScalar {
  template <class View, class Context, class T>
  auto operator()(const View&, const Context& ctx, T& output) const -> bool {
    auto& reader = ctx.reader();
    const auto& state = ctx.reader_state();
    if constexpr (requires { reader.read_semantic(state, output); })
      return reader.read_semantic(state, output);
    else if constexpr (std::same_as<T, std::string> &&
                       requires { reader.read_string(state, output); })
      return reader.read_string(state, output);
    else if constexpr (std::same_as<T, BytesValue> &&
                       requires { reader.read_bytes(state, output); })
      return reader.read_bytes(state, output);
    else {
      ::hosq::detail::report_reader_error(
          reader, state,
          {ErrorCode::unsupported_type, reader.current_token(state).offset,
           "this format has no codec for this scalar type"});
      return false;
    }
  }
};
}  // namespace hosq::ext
