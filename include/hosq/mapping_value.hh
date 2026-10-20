#pragma once

#include <cstddef>
#include <utility>
#include <vector>

namespace hosq {

/// Ordered mapping entries, independent of a reader or wire format.
///
/// Each key and value is stored in one pair, so insertion, removal, and
/// reordering cannot leave unequal key/value counts. Keys need not be hashable
/// or comparable. Duplicate keys are retained; codecs validate them using
/// their key semantics. Unlike Map, this is a high-level value container.
template <class K, class V>
class MappingValue {
 public:
  using key_type = K;
  using mapped_type = V;
  using entry_type = std::pair<K, V>;
  using storage_type = std::vector<entry_type>;
  using iterator = typename storage_type::iterator;
  using const_iterator = typename storage_type::const_iterator;

  auto entries() noexcept -> storage_type& { return entries_; }
  auto entries() const noexcept -> const storage_type& { return entries_; }

  auto begin() noexcept -> iterator { return entries_.begin(); }
  auto begin() const noexcept -> const_iterator { return entries_.begin(); }
  auto end() noexcept -> iterator { return entries_.end(); }
  auto end() const noexcept -> const_iterator { return entries_.end(); }

  auto size() const noexcept -> std::size_t { return entries_.size(); }
  auto empty() const noexcept -> bool { return entries_.empty(); }
  auto clear() noexcept -> void { entries_.clear(); }

  template <class... Args>
  auto emplace_back(Args&&... args) -> entry_type& {
    return entries_.emplace_back(std::forward<Args>(args)...);
  }

 private:
  storage_type entries_{};
};

}  // namespace hosq
