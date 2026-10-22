#pragma once

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "hosq/error.hh"
#include "hosq/reader_base.hh"

namespace hosq {

struct WriteRequest {
  std::size_t length;
  TokenType type;
  std::uint8_t context;
};

template <class T>
concept WriterConcept =
    requires(T t, const T ct, const WriteRequest& request, std::size_t written) {
      typename T::output_type;
      typename T::buffer_type;

      { t.write(request) } -> std::same_as<typename T::buffer_type>;
      { t.commit(written) } -> std::same_as<bool>;
      { t.error() } -> std::same_as<Error>;
      { ct.output() } -> std::same_as<typename T::output_type>;
    };

namespace detail {
template <class W>
struct WriterErrorAccess {
  static auto get(const W& writer) -> ErrorState {
    return {const_cast<W&>(writer).error().code};
  }
};
template <class W>
HOSQ_ALWAYS_INLINE auto writer_error(const W& writer) -> ErrorState {
  return WriterErrorAccess<std::remove_cvref_t<W>>::get(writer);
}
template <class W>
void report_writer_error(W& writer, ErrorInfo error) {
  using Access = WriterErrorAccess<std::remove_cvref_t<W>>;
  if constexpr (requires { Access::report(writer, error); })
    Access::report(writer, error);
  else if constexpr (requires {
                       writer.report_error(static_cast<hosq::Error>(error));
                     })
    writer.report_error(static_cast<hosq::Error>(error));
  else
    (void)writer.write({0, TokenType::Error, 0});
}
}  // namespace detail

template <class W>
struct WriterTraits {
  static constexpr Format format = Format::unknown;
  static constexpr bool is_binary_format = false;
};

template <class W>
using writer_traits_t = WriterTraits<std::remove_cvref_t<W>>;

}  // namespace hosq
