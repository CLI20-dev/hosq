#pragma once

#include "hosq/reader_base.hh"

namespace hosq {

namespace ext {

template <ReaderConcept R>
struct ScanObject;

template <ReaderConcept R>
struct ScanArray;

};  // namespace ext

struct ObjectView final {
 protected:
  ObjectView() = default;
  ~ObjectView() = default;

  ObjectView(const ObjectView&) = default;
  ObjectView(ObjectView&&) = default;
  auto operator=(const ObjectView&) -> ObjectView& = default;
  auto operator=(ObjectView&&) -> ObjectView& = default;

  template <ReaderConcept R>
  friend struct ::hosq::ext::ScanObject;
};

struct ArrayView final {
  ~ArrayView() = default;

 protected:
  ArrayView() = default;

  ArrayView(const ArrayView&) = default;
  ArrayView(ArrayView&&) = default;
  auto operator=(const ArrayView&) -> ArrayView& = default;
  auto operator=(ArrayView&&) -> ArrayView& = default;

  template <ReaderConcept R>
  friend struct ::hosq::ext::ScanArray;
};

}  // namespace hosq
