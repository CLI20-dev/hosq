#pragma once

#include "hosq/reader_base.hh"

namespace hosq {

namespace ext {

template <ReaderConcept R>
struct ScanObject;

template <ReaderConcept R>
struct ScanArray;

}  // namespace ext

namespace scan::detail {

struct ScanObject;

struct ScanArray;

}  // namespace scan::detail

template <auto... Fns>
  requires(sizeof...(Fns) > 0)
struct Pipeline;

struct ObjectView final {
  ~ObjectView() = default;

 protected:
  ObjectView() = default;

  ObjectView(const ObjectView&) = default;
  ObjectView(ObjectView&&) = default;
  auto operator=(const ObjectView&) -> ObjectView& = default;
  auto operator=(ObjectView&&) -> ObjectView& = default;

  template <ReaderConcept R>
  friend struct ::hosq::ext::ScanObject;
  friend struct ::hosq::scan::detail::ScanObject;

  template <auto... Fns>
    requires(sizeof...(Fns) > 0)
  friend struct Pipeline;
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
  friend struct ::hosq::scan::detail::ScanArray;

  template <auto... Fns>
    requires(sizeof...(Fns) > 0)
  friend struct Pipeline;
};

}  // namespace hosq
