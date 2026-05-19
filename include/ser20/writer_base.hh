#pragma once

#include <cstddef>

#include "ser20/error.hh"
#include "ser20/reader_base.hh"
#include "ser20/result.hh"

namespace ser20 {

struct WriteRequest {
  size_t length;
  TokenType type;
  uint8_t context;
};

template <class T>
concept WriterConcept =
    requires(T t, const T ct, const WriteRequest& request, std::size_t written) {
      typename T::output_type;
      typename T::buffer_type;
      {
        t.write(request)
      } -> std::same_as<Result<typename T::buffer_type, Error>>;
      { t.commit(written) } -> std::same_as<Result<void, Error>>;

      { ct.output() } -> std::same_as<typename T::output_type>;
    };

template <class W>
struct WriterTraits {
  static constexpr Format format = Format::unknown;
  static constexpr bool is_binary_format = false;
};

template <class W>
using writer_traits_t = WriterTraits<std::remove_cvref_t<W>>;

};  // namespace ser20
