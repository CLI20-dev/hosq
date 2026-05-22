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

template <class W>
struct WriterTraits {
  static constexpr Format format = Format::unknown;
  static constexpr bool is_binary_format = false;
};

template <class W>
using writer_traits_t = WriterTraits<std::remove_cvref_t<W>>;

}  // namespace hosq
