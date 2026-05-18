#pragma once

#include <cstddef>
#include <string_view>

#include "ser20/error.hh"
#include "ser20/reader_base.hh"
#include "ser20/result.hh"

namespace ser20 {

template <class T>
concept WriterConcept =
    requires(T t, const T ct, TokenType type, std::string_view sv) {
      { t.write(type, sv) } -> std::same_as<Result<void, Error>>;
      { ct.output() } -> std::same_as<std::string_view>;
    };

};  // namespace ser20
