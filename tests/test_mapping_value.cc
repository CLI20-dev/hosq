#include <gtest/gtest.h>

#include <algorithm>
#include <concepts>
#include <memory>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "hosq/mapping_value.hh"

namespace {
using hosq::MappingValue;
using TextMapping = MappingValue<std::string, int>;
static_assert(
    std::same_as<TextMapping::entry_type, std::pair<std::string, int>>);
static_assert(std::same_as<TextMapping::storage_type,
                           std::vector<std::pair<std::string, int>>>);
static_assert(std::same_as<decltype(std::declval<TextMapping&>().entries()),
                           TextMapping::storage_type&>);
static_assert(
    std::same_as<decltype(std::declval<const TextMapping&>().entries()),
                 const TextMapping::storage_type&>);
static_assert(std::same_as<decltype(std::declval<TextMapping&>().begin()),
                           TextMapping::iterator>);
static_assert(std::same_as<decltype(std::declval<const TextMapping&>().begin()),
                           TextMapping::const_iterator>);
static_assert(std::same_as<decltype(std::declval<const TextMapping&>().end()),
                           TextMapping::const_iterator>);

TEST(MappingValue, PreservesInsertionOrderAndDuplicateEntries) {
  TextMapping value;
  EXPECT_TRUE(value.empty());
  EXPECT_EQ(value.size(), 0u);
  auto& first = value.emplace_back("b", 2);
  EXPECT_EQ(first.first, "b");
  EXPECT_EQ(first.second, 2);
  value.emplace_back("a", 1);
  value.emplace_back("b", 3);
  EXPECT_EQ(value.entries(),
            (TextMapping::storage_type{{"b", 2}, {"a", 1}, {"b", 3}}));

  const auto& view = value;
  std::vector<std::string> keys;
  for (const auto& [key, mapped] : view) {
    keys.push_back(key);
    EXPECT_GT(mapped, 0);
  }
  EXPECT_EQ(keys, (std::vector<std::string>{"b", "a", "b"}));
  EXPECT_EQ(view.size(), 3u);
  EXPECT_FALSE(view.empty());
}

TEST(MappingValue, VectorMutationsKeepEntriesPaired) {
  TextMapping value;
  value.entries() = {{"c", 3}, {"a", 1}, {"b", 2}};
  std::sort(value.begin(), value.end());
  EXPECT_EQ(value.entries(),
            (TextMapping::storage_type{{"a", 1}, {"b", 2}, {"c", 3}}));
  value.entries().erase(value.begin() + 1);
  value.entries().insert(value.begin(), {"d", 4});
  EXPECT_EQ(value.entries(),
            (TextMapping::storage_type{{"d", 4}, {"a", 1}, {"c", 3}}));
  value.clear();
  EXPECT_TRUE(value.empty());
  EXPECT_EQ(value.begin(), value.end());
  value.emplace_back("next", 5);
  EXPECT_EQ(value.entries(), (TextMapping::storage_type{{"next", 5}}));
}

// Deliberately provides neither hashing nor equality. Interpretation belongs
// to the codec, so even equality is unnecessary for storing mapping entries.
struct SemanticKey {
  std::vector<int> components;
};

TEST(MappingValue, SupportsNonhashableNoncomparableKeysAndMoveOnlyValues) {
  MappingValue<SemanticKey, std::unique_ptr<int>> value;
  auto owned = std::make_unique<int>(7);
  const auto* original = owned.get();
  value.emplace_back(SemanticKey{{1, 2}}, std::move(owned));
  value.emplace_back(SemanticKey{{1, 2}}, std::make_unique<int>(8));
  EXPECT_EQ(owned, nullptr);

  auto moved = std::move(value);
  ASSERT_EQ(moved.size(), 2u);
  EXPECT_EQ(moved.entries()[0].first.components, (std::vector<int>{1, 2}));
  EXPECT_EQ(moved.entries()[0].second.get(), original);
  EXPECT_EQ(*moved.entries()[1].second, 8);
  std::swap(moved.entries()[0], moved.entries()[1]);
  moved.entries().erase(moved.begin());
  ASSERT_EQ(moved.size(), 1u);
  EXPECT_EQ(moved.entries()[0].second.get(), original);
}

struct NonDefaultValue {
  explicit NonDefaultValue(int v) : value(v) {}
  int value;
};

TEST(MappingValue, ConstructsEntriesInPlaceWithoutDefaultConstructibleValues) {
  MappingValue<std::string, NonDefaultValue> value;
  auto& entry = value.emplace_back(std::piecewise_construct,
                                   std::forward_as_tuple(3u, 'k'),
                                   std::forward_as_tuple(42));
  EXPECT_EQ(entry.first, "kkk");
  EXPECT_EQ(entry.second.value, 42);
}

TEST(MappingValue, CopiesIndependentEntriesAndSupportsMoveAssignment) {
  TextMapping original;
  original.emplace_back("x", 1);
  auto copy = original;
  copy.entries()[0].second = 2;
  EXPECT_EQ(original.entries()[0].second, 1);
  TextMapping assigned;
  assigned = std::move(copy);
  EXPECT_EQ(assigned.entries(), (TextMapping::storage_type{{"x", 2}}));
}
}  // namespace
