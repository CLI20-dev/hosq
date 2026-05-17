#pragma once

#include <string_view>
#include <tuple>
#include <variant>

#include "ser20/element.hh"
#include "ser20/matcher.hh"
#include "ser20/parse_context.hh"
#include "ser20/pipeline.hh"
#include "ser20/reader_base.hh"
#include "ser20/string_literal.hh"
#include "ser20/to_tuple.hh"

namespace ser20 {

template <class T, Reader R>
class Decoder {
 public:
  Decoder(const Decoder&) = delete;
  Decoder(Decoder&&) = delete;
  auto operator=(const Decoder&) -> Decoder& = delete;
  auto operator=(Decoder&&) -> Decoder& = delete;
  ~Decoder() = default;

  [[nodiscard]]
  auto decode(std::string_view input) -> T {
    R reader_(input);






  }
};

}  // namespace ser20
