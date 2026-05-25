#pragma once

#include "hosq/reader_base.hh"

namespace hosq::ext {

template <ReaderConcept R>
struct ScanObject;

template <ReaderConcept R>
struct ScanArray;

template <ReaderConcept R>
struct ScanScalar;

}  // namespace hosq::ext
