#pragma once

#include <cstddef>

namespace hosq {

/// Per-object/map storage shared by key and value pipelines.
///
/// This is not a key-value container. `keys` and `values` are the storages
/// declared by the key pipeline and value pipeline respectively. They may be
/// scalar scratch values or aggregate storages such as vectors.
template <class Keys, class Values>
struct Map {
  using keys_type = Keys;
  using values_type = Values;

  Keys keys{};
  Values values{};
};

}  // namespace hosq
