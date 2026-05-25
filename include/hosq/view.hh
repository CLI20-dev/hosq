#pragma once

#include <ranges>

#include "hosq/ext_fwd.hh"

namespace hosq::scan::detail {

struct ScanObject;

struct ScanArray;

struct ScanScalar;

}  // namespace hosq::scan::detail

namespace hosq {

struct ObjectView final : public std::ranges::view_interface<ObjectView> {
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

struct ScalarView final {
  ~ScalarView() = default;

 protected:
  ScalarView() = default;

  ScalarView(const ScalarView&) = default;
  ScalarView(ScalarView&&) = default;
  auto operator=(const ScalarView&) -> ScalarView& = default;
  auto operator=(ScalarView&&) -> ScalarView& = default;

  template <ReaderConcept R>
  friend struct ::hosq::ext::ScanScalar;
  friend struct ::hosq::scan::detail::ScanScalar;

  template <auto... Fns>
    requires(sizeof...(Fns) > 0)
  friend struct Pipeline;
};

}  // namespace hosq
