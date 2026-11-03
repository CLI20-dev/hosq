// SPDX-License-Identifier: MIT
//
// Copyright (c) 2026 gen
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

// Adapted from gen740/meta_match at fbc01a0. hosq retains its StringLiteral,
// inlining policy and commit-before-handler terminated-key entry point.
#pragma once

#include <array>
#include <cstddef>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

#include "hosq/detail/compiler.hh"
#include "hosq/string_literal.hh"

namespace hosq::meta {
// ============================================================================
// Handler<Key, Fn> — a key–callable pair usable as an element of match()
// ============================================================================

/**
 * @brief Associates a compile-time string key with a callable.
 *
 * `Handler` is the primary building block for meta_match.  Each instance
 * pairs a `StringLiteral` key (embedded in the template parameter) with an
 * arbitrary callable `Fn`.  The call operator forwards to `fn`.
 *
 * Prefer the `make_handler<Key>(fn)` factory over direct construction.
 *
 * @tparam Key  Compile-time string key.
 * @tparam Fn   Callable type (function, lambda, functor, ...).
 */
template <StringLiteral Key, class Fn>
struct Handler {
  /** The callable invoked on a successful match. */
  Fn fn;

  /** Returns the compile-time `StringLiteral` key. */
  [[nodiscard]] static constexpr auto literal() noexcept { return Key; }

  /** Returns a `std::string_view` of the key. */
  [[nodiscard]] static constexpr auto view() noexcept { return Key.view(); }

  /** Invokes the stored callable. */
  HOSQ_ALWAYS_INLINE auto operator()() noexcept(noexcept(fn())) -> void { fn(); }
};

/**
 * @brief Factory function for constructing a `Handler<Key, Fn>`.
 *
 * @tparam Key  Compile-time string literal, e.g. `"help"`.
 * @param  fn   Callable to invoke on a match.
 * @return `Handler<Key, std::decay_t<Fn>>` wrapping `fn`.
 *
 * @code
 * auto h = make_handler<"help">([] { std::puts("help!"); });
 * @endcode
 */
template <StringLiteral Key, class Fn>
[[nodiscard]] constexpr auto make_handler(Fn&& fn)
    -> Handler<Key, std::decay_t<Fn>> {
  return {std::forward<Fn>(fn)};
}

// ============================================================================
// Key-only planning. Callable types do not participate in this computation.
// ============================================================================

namespace detail {

inline constexpr std::size_t no_index = static_cast<std::size_t>(-1);

template <StringLiteral... Keys>
struct key_metadata {
  static constexpr std::size_t key_count = sizeof...(Keys);
  static constexpr std::array<std::string_view, key_count> keys{Keys.view()...};

  // Bounded generic PREFIX fallback: no corpus names and no scan/storage
  // proportional to an unbounded maximum key length.
  static constexpr bool use_prefix = []() {
    if constexpr (key_count > 16) {
      return false;
    } else {
      std::array<bool, 256> seen{};
      std::size_t diversity = 0;
      unsigned int minimum = 255, maximum = 0;
      for (auto key : keys) {
        if (key.size() > 16) {
          return false;
        }
        if (!key.empty()) {
          const auto byte = static_cast<unsigned char>(key[0]);
          if (!seen[byte]) {
            seen[byte] = true;
            ++diversity;
          }
          if (byte < minimum) minimum = byte;
          if (byte > maximum) maximum = byte;
        }
      }
      if (diversity < 2 || diversity * 5 >= (maximum - minimum + 1) * 2) {
        return false;
      }
      // Each pair must separate or terminate within its first four actual
      // bytes. At most 120 pairs * 4 comparisons, independent of key length.
      for (std::size_t i = 0; i < key_count; ++i) {
        for (std::size_t j = i + 1; j < key_count; ++j) {
          if (keys[i].size() < 4 || keys[j].size() < 4) continue;
          bool shared_four = true;
          for (std::size_t depth = 0; depth < 4; ++depth) {
            if (keys[i][depth] != keys[j][depth]) {
              shared_four = false;
              break;
            }
          }
          if (shared_four) return false;
        }
      }
      return true;
    }
  }();

  static constexpr auto prefix_indices = []() {
    std::array<std::size_t, key_count> result{};
    for (std::size_t i = 0; i < key_count; ++i) result[i] = i;
    return result;
  }();

  struct node {
    std::size_t begin{};
    std::size_t count{};
    std::size_t position{};
    std::size_t edge_begin{};
    std::size_t edge_count{};
  };
  struct edge {
    unsigned char byte{};
    std::size_t child{};
  };
  struct length_root {
    std::size_t length{};
    std::size_t root{};
  };
  struct plan {
    // Every internal node has at least two children. A forest with K leaves
    // therefore needs fewer than 2*K nodes/edges, regardless of key length.
    std::array<node, 2 * key_count> nodes{};
    std::array<edge, 2 * key_count> edges{};
    std::array<length_root, key_count> lengths{};
    std::array<std::size_t, key_count> order{};
    std::array<bool, 256> first_bytes{};
    std::size_t node_count{};
    std::size_t edge_count{};
    std::size_t length_count{};
    bool unique = true;
  };

  static consteval auto build() -> plan {
    plan result{};
    std::array<std::size_t, key_count> scratch{};
    for (std::size_t i = 0; i < key_count; ++i) {
      result.order[i] = i;
      if (!keys[i].empty()) {
        result.first_bytes[static_cast<unsigned char>(keys[i][0])] = true;
      }
    }

    // Stable bottom-up merge sort by length, without constexpr std::algorithm
    // or pairwise full-string duplicate comparisons.
    for (std::size_t width = 1; width < key_count;) {
      for (std::size_t begin = 0; begin < key_count; begin += 2 * width) {
        const auto middle =
            begin + width < key_count ? begin + width : key_count;
        const auto end =
            begin + 2 * width < key_count ? begin + 2 * width : key_count;
        auto left = begin;
        auto right = middle;
        for (auto out = begin; out < end; ++out) {
          if (left < middle &&
              (right == end || keys[result.order[left]].size() <=
                                   keys[result.order[right]].size())) {
            scratch[out] = result.order[left++];
          } else {
            scratch[out] = result.order[right++];
          }
        }
      }
      result.order = scratch;
      if (width > key_count / 2) {
        break;
      }
      width *= 2;
    }

    for (std::size_t begin = 0; begin < key_count;) {
      const auto length = keys[result.order[begin]].size();
      auto end = begin + 1;
      while (end < key_count && keys[result.order[end]].size() == length) {
        ++end;
      }
      const auto root = result.node_count++;
      result.nodes[root] = {begin, end - begin, 0, 0, 0};
      result.lengths[result.length_count++] = {length, root};
      begin = end;
    }

    // Reuse a histogram and clear only present bytes while scoring positions.
    // Iterative construction avoids constexpr recursion along long prefixes.
    std::array<std::size_t, 256> histogram{};
    std::array<unsigned char, 256> present{};
    for (std::size_t current = 0; current < result.node_count; ++current) {
      auto& n = result.nodes[current];
      if (n.count == 1) {
        continue;
      }
      const auto length = keys[result.order[n.begin]].size();
      std::size_t best_diversity = 1;
      std::size_t best_square = no_index;
      std::size_t best_max = no_index;
      for (std::size_t position = 0; position < length; ++position) {
        std::size_t diversity = 0;
        std::size_t square = 0;
        std::size_t maximum = 0;
        for (std::size_t i = n.begin; i < n.begin + n.count; ++i) {
          const auto byte =
              static_cast<unsigned char>(keys[result.order[i]][position]);
          auto& count = histogram[byte];
          if (count == 0) {
            present[diversity++] = byte;
          }
          square += 2 * count + 1;
          ++count;
          if (count > maximum) {
            maximum = count;
          }
        }
        if (diversity > best_diversity ||
            (diversity == best_diversity && diversity > 1 &&
             (square < best_square ||
              (square == best_square && maximum < best_max)))) {
          best_diversity = diversity;
          best_square = square;
          best_max = maximum;
          n.position = position;
        }
        for (std::size_t i = 0; i < diversity; ++i) {
          histogram[present[i]] = 0;
        }
      }
      if (best_diversity == 1) {
        // Equal lengths and no distinguishing actual byte means duplicates.
        // This includes empty, embedded-NUL, control and high-byte keys.
        result.unique = false;
        return result;
      }

      std::size_t diversity = 0;
      for (std::size_t i = n.begin; i < n.begin + n.count; ++i) {
        const auto byte =
            static_cast<unsigned char>(keys[result.order[i]][n.position]);
        if (histogram[byte]++ == 0) {
          present[diversity++] = byte;
        }
      }
      // Counting partition: one pass over present buckets, then a stable
      // scatter of candidates. No per-byte rescans of the candidate set.
      std::array<std::size_t, 256> next{};
      auto offset = n.begin;
      n.edge_begin = result.edge_count;
      n.edge_count = diversity;
      for (std::size_t i = 0; i < diversity; ++i) {
        const auto byte = present[i];
        const auto count = histogram[byte];
        next[byte] = offset;
        const auto child = result.node_count++;
        result.nodes[child] = {offset, count, 0, 0, 0};
        result.edges[result.edge_count++] = {byte, child};
        offset += count;
      }
      for (std::size_t i = n.begin; i < n.begin + n.count; ++i) {
        const auto index = result.order[i];
        const auto byte = static_cast<unsigned char>(keys[index][n.position]);
        scratch[next[byte]++] = index;
      }
      for (std::size_t i = n.begin; i < n.begin + n.count; ++i) {
        result.order[i] = scratch[i];
      }
      for (std::size_t i = 0; i < diversity; ++i) {
        histogram[present[i]] = 0;
      }
    }
    return result;
  }

  static constexpr auto data = build();
  static_assert(data.unique, "duplicated meta::match key");

  static constexpr auto common_lengths = []() {
    std::array<std::size_t, 65> roots{};
    for (auto& root : roots) {
      root = no_index;
    }
    for (std::size_t i = 0; i < data.length_count; ++i) {
      if (data.lengths[i].length < roots.size()) {
        roots[data.lengths[i].length] = data.lengths[i].root;
      }
    }
    return roots;
  }();
};

// Cache child indexing once per key schema/node, not once per callable tuple.
// These tables are used only in constant expressions in the switch cases.
template <class Metadata, std::size_t Node>
inline constexpr auto children = []() {
  std::array<std::size_t, 256> result{};
  for (auto& child : result) {
    child = no_index;
  }
  constexpr auto n = Metadata::data.nodes[Node];
  for (std::size_t i = n.edge_begin; i < n.edge_begin + n.edge_count; ++i) {
    const auto edge = Metadata::data.edges[i];
    result[edge.byte] = edge.child;
  }
  return result;
}();

// Full unsigned-byte source switches let the optimizer choose jump tables,
// bit tests or comparisons. Absent branches never instantiate child traversal.
// Keep the generated label lists aligned; clang-format is not idempotent here.
// clang-format off
#define HOSQ_META_DETAIL_16(F, B) \
  F(B) F(B + 1) F(B + 2) F(B + 3) F(B + 4) F(B + 5) F(B + 6) F(B + 7) \
  F(B + 8) F(B + 9) F(B + 10) F(B + 11) F(B + 12) F(B + 13) F(B + 14) F(B + 15)
#define HOSQ_META_DETAIL_BYTES(F) \
  HOSQ_META_DETAIL_16(F, 0x00) HOSQ_META_DETAIL_16(F, 0x10) \
  HOSQ_META_DETAIL_16(F, 0x20) HOSQ_META_DETAIL_16(F, 0x30) \
  HOSQ_META_DETAIL_16(F, 0x40) HOSQ_META_DETAIL_16(F, 0x50) \
  HOSQ_META_DETAIL_16(F, 0x60) HOSQ_META_DETAIL_16(F, 0x70) \
  HOSQ_META_DETAIL_16(F, 0x80) HOSQ_META_DETAIL_16(F, 0x90) \
  HOSQ_META_DETAIL_16(F, 0xa0) HOSQ_META_DETAIL_16(F, 0xb0) \
  HOSQ_META_DETAIL_16(F, 0xc0) HOSQ_META_DETAIL_16(F, 0xd0) \
  HOSQ_META_DETAIL_16(F, 0xe0) HOSQ_META_DETAIL_16(F, 0xf0)
// clang-format on

template <class Metadata, std::size_t Node, class Tuple>
HOSQ_ALWAYS_INLINE auto dispatch_node(std::string_view c, Tuple& a) -> bool {
  constexpr auto n = Metadata::data.nodes[Node];
  if constexpr (n.count == 1) {
    constexpr auto index = Metadata::data.order[n.begin];
    constexpr auto expected = Metadata::keys[index];
    // Discriminating bytes only select a candidate; they never prove equality.
    if (c != expected) {
      return false;
    }
    std::get<index>(a)();
    return true;
  } else {
    // Entry is reachable only through an exact-length partition. n.position
    // is strictly below that length; no terminator or out-of-bounds load.
#define HOSQ_META_DETAIL_CHILD(C)                       \
  case C: {                                             \
    constexpr auto child = children<Metadata, Node>[C]; \
    if constexpr (child != no_index) {                  \
      return dispatch_node<Metadata, child>(c, a);      \
    } else {                                            \
      return false;                                     \
    }                                                   \
  }
    switch (static_cast<unsigned char>(c[n.position])) {
      HOSQ_META_DETAIL_BYTES(HOSQ_META_DETAIL_CHILD)
    }
#undef HOSQ_META_DETAIL_CHILD
    return false;
  }
}

template <class Metadata, class Tuple, std::size_t... I>
HOSQ_ALWAYS_INLINE auto dispatch_long_length(std::string_view c, Tuple& a,
                                             std::index_sequence<I...>) -> bool {
  // Only real lengths >64 generate code. There is no maximum key length and
  // no runtime metadata traversal, including for the uncommon-length path.
  return ([&]() -> bool {
    if constexpr (Metadata::data.lengths[I].length > 64) {
      if (c.size() == Metadata::data.lengths[I].length) {
        return dispatch_node<Metadata, Metadata::data.lengths[I].root>(c, a);
      }
    }
    return false;
  }() || ... || false);
}

template <class Metadata, class Tuple>
HOSQ_ALWAYS_INLINE auto dispatch_length(std::string_view c, Tuple& a) -> bool {
#define HOSQ_META_DETAIL_LENGTH(L)                     \
  case L: {                                            \
    constexpr auto root = Metadata::common_lengths[L]; \
    if constexpr (root != no_index) {                  \
      return dispatch_node<Metadata, root>(c, a);      \
    } else {                                           \
      return false;                                    \
    }                                                  \
  }
  switch (c.size()) {
    HOSQ_META_DETAIL_16(HOSQ_META_DETAIL_LENGTH, 0)
    HOSQ_META_DETAIL_16(HOSQ_META_DETAIL_LENGTH, 16)
    HOSQ_META_DETAIL_16(HOSQ_META_DETAIL_LENGTH, 32)
    HOSQ_META_DETAIL_16(HOSQ_META_DETAIL_LENGTH, 48)
    HOSQ_META_DETAIL_LENGTH(64)
    default:
      return dispatch_long_length<Metadata>(
          c, a, std::make_index_sequence<Metadata::data.length_count>{});
  }
#undef HOSQ_META_DETAIL_LENGTH
}

// The documented Handler invariant needs no view comparison per callable type.
// Other entries can use the fast planner only when their literal and view agree.
template <class T>
struct is_string_literal : std::false_type {};
template <std::size_t N>
struct is_string_literal<StringLiteral<N>> : std::true_type {};

template <class Entry>
struct canonical_entry : std::bool_constant<[]() {
  if constexpr (is_string_literal<
                    std::remove_cvref_t<decltype(Entry::literal())>>::value) {
    return Entry::view() == Entry::literal().view();
  } else {
    return false;
  }
}()> {};

template <StringLiteral Key, class Fn>
struct canonical_entry<Handler<Key, Fn>> : std::true_type {};

// Foreign literals need not be StringLiteral NTTPs. Use their declared storage
// bound when available; unsized indexable literals retain the old view-sized
// contract. All literal indexing below is forced into constant evaluation.
template <class Entry>
consteval auto literal_bound() -> std::size_t {
  using literal_type = std::remove_reference_t<decltype(Entry::literal())>;
  if constexpr (requires { Entry::literal().size(); }) {
    return Entry::literal().size();
  } else if constexpr (std::is_array_v<literal_type>) {
    return std::extent_v<literal_type>;
  } else {
    return Entry::view().size();
  }
}

template <class Entry, std::size_t N>
consteval auto has_literal_byte() -> bool {
  return N < Entry::view().size() && N < literal_bound<Entry>();
}

// Reject duplicate authoritative views globally, without pairwise full-string
// comparisons. This work is instantiated only for compatibility tuples.
template <class... Entries>
consteval auto compatibility_unique() -> bool {
  constexpr std::size_t count = sizeof...(Entries);
  constexpr std::array<std::string_view, count> views{
      std::remove_cvref_t<Entries>::view()...};
  std::array<std::size_t, count> order{};
  std::array<std::size_t, count> scratch{};
  for (std::size_t i = 0; i < count; ++i) {
    order[i] = i;
  }
  for (std::size_t width = 1; width < count;) {
    for (std::size_t begin = 0; begin < count; begin += 2 * width) {
      const auto middle = begin + width < count ? begin + width : count;
      const auto end = begin + 2 * width < count ? begin + 2 * width : count;
      auto left = begin;
      auto right = middle;
      for (auto out = begin; out < end; ++out) {
        if (left < middle &&
            (right == end || views[order[left]] <= views[order[right]])) {
          scratch[out] = order[left++];
        } else {
          scratch[out] = order[right++];
        }
      }
    }
    order = scratch;
    if (width > count / 2) {
      break;
    }
    width *= 2;
  }
  for (std::size_t i = 1; i < count; ++i) {
    if (views[order[i - 1]] == views[order[i]]) {
      return false;
    }
  }
  return true;
}

template <class Tuple, std::size_t Index>
using compatibility_entry =
    std::remove_cvref_t<std::tuple_element_t<Index, Tuple>>;

template <std::size_t Index, class Tuple>
HOSQ_ALWAYS_INLINE auto compatibility_leaf(std::string_view c, Tuple& a)
    -> bool {
  constexpr auto expected = compatibility_entry<Tuple, Index>::view();
  if (c != expected) {
    return false;
  }
  std::get<Index>(a)();
  return true;
}

template <class Tuple, std::size_t N, auto Indices>
struct compatibility_node {
  static constexpr auto counts = []() {
    std::array<std::size_t, 256> result{};
    [&]<std::size_t... J>(std::index_sequence<J...>) {
      (
          [&]() {
            using entry = compatibility_entry<Tuple, Indices[J]>;
            if constexpr (has_literal_byte<entry, N>()) {
              ++result[static_cast<unsigned char>(entry::literal()[N])];
            }
          }(),
          ...);
    }(std::make_index_sequence<Indices.size()>{});
    return result;
  }();

  template <unsigned char Byte>
  static consteval auto candidates() {
    std::array<std::size_t, counts[Byte]> result{};
    std::size_t out = 0;
    [&]<std::size_t... J>(std::index_sequence<J...>) {
      (
          [&]() {
            using entry = compatibility_entry<Tuple, Indices[J]>;
            if constexpr (has_literal_byte<entry, N>()) {
              if constexpr (static_cast<unsigned char>(entry::literal()[N]) ==
                            Byte) {
                result[out++] = Indices[J];
              }
            }
          }(),
          ...);
    }(std::make_index_sequence<Indices.size()>{});
    return result;
  }
};

template <std::size_t N, auto Indices, class Tuple>
HOSQ_ALWAYS_INLINE auto dispatch_compatibility(std::string_view c, Tuple& a)
    -> bool {
  if (c.size() == N) {
    // The old terminal length test alone could accept an unchecked prefix.
    // A terminal must also pass complete equality against Entry::view().
    return [&]<std::size_t... J>(std::index_sequence<J...>) {
      return ([&]() -> bool {
        using entry = compatibility_entry<Tuple, Indices[J]>;
        if constexpr (entry::view().size() == N) {
          return compatibility_leaf<Indices[J]>(c, a);
        }
        return false;
      }() || ... || false);
    }(std::make_index_sequence<Indices.size()>{});
  }
  if (c.size() < N) {
    return false;
  }
  using node = compatibility_node<Tuple, N, Indices>;
#define HOSQ_META_DETAIL_COMPAT(C)                          \
  case C: {                                                 \
    if constexpr (node::counts[C] != 0) {                   \
      constexpr auto next = node::template candidates<C>(); \
      if constexpr (next.size() == 1) {                     \
        return compatibility_leaf<next[0]>(c, a);           \
      } else {                                              \
        return dispatch_compatibility<N + 1, next>(c, a);   \
      }                                                     \
    } else {                                                \
      return false;                                         \
    }                                                       \
  }
  // Sequential literal routing preserves the old custom-entry semantics;
  // neither view lengths nor view bytes are substituted as routing keys.
  switch (static_cast<unsigned char>(c[N])) {
    HOSQ_META_DETAIL_BYTES(HOSQ_META_DETAIL_COMPAT)
  }
#undef HOSQ_META_DETAIL_COMPAT
  return false;
}

// Cached original-index candidates depend only on keys, never callable types.
// Instantiated only for schemas bounded by use_prefix (<=16 keys/bytes,
// pairwise common prefix <=3). At most four sequential byte loads per route.
template <class Metadata, std::size_t N, auto Indices>
struct prefix_node {
  static_assert(N < 4, "PREFIX isolation depth exceeds policy bound");
  static constexpr auto counts = []() {
    std::array<std::size_t, 256> result{};
    for (auto index : Indices) {
      const auto key = Metadata::keys[index];
      if (N < key.size()) ++result[static_cast<unsigned char>(key[N])];
    }
    return result;
  }();
  static constexpr auto terminal = []() {
    for (auto index : Indices) {
      if (Metadata::keys[index].size() == N) return index;
    }
    return no_index;
  }();
  // Exactly 16 source labels: absent slots have distinct unreachable labels
  // above the unsigned-byte domain, so they do not distort switch lowering.
  static constexpr auto bytes = []() {
    std::array<unsigned int, 16> result{};
    for (std::size_t i = 0; i < result.size(); ++i) result[i] = 256 + i;
    std::size_t out = 0;
    for (unsigned int byte = 0; byte < 256; ++byte) {
      if (counts[byte]) result[out++] = byte;
    }
    return result;
  }();
  template <unsigned char Byte>
  static constexpr auto candidates = []() {
    std::array<std::size_t, counts[Byte]> result{};
    std::size_t out = 0;
    for (auto index : Indices) {
      const auto key = Metadata::keys[index];
      if (N < key.size() && static_cast<unsigned char>(key[N]) == Byte) {
        result[out++] = index;
      }
    }
    return result;
  }();
};

template <class Metadata, std::size_t Index, class Tuple>
HOSQ_ALWAYS_INLINE auto prefix_leaf(std::string_view c, Tuple& a) -> bool {
  // Ordinary leaves do not trust their routing path as proof of full equality.
  if (c != Metadata::keys[Index]) return false;
  std::get<Index>(a)();
  return true;
}

template <class Metadata, std::size_t N, auto Indices, class Tuple>
HOSQ_ALWAYS_INLINE auto dispatch_prefix(std::string_view c, Tuple& a) -> bool {
  using node = prefix_node<Metadata, N, Indices>;
  // The only entry is N=0; recursive calls follow a nonterminal byte load.
  // Thus c.size() >= N is already established, without an extra lower test.
  if (c.size() == N) {
    if constexpr (node::terminal != no_index) {
      // Proof terminal: this traversal starts only at depth zero; each of
      // the N actual bytes has matched. Legacy match<N> resets to zero too.
      std::get<node::terminal>(a)();
      return true;
    }
    return false;
  }
#define HOSQ_META_DETAIL_PREFIX(C)                                     \
  case node::bytes[C]: {                                               \
    if constexpr (node::bytes[C] < 256) {                              \
      constexpr auto next = node::template candidates<node::bytes[C]>; \
      if constexpr (next.size() == 1) {                                \
        return prefix_leaf<Metadata, next[0]>(c, a);                   \
      } else {                                                         \
        return dispatch_prefix<Metadata, N + 1, next>(c, a);           \
      }                                                                \
    } else {                                                           \
      return false;                                                    \
    }                                                                  \
  }
  // Promote the switch domain so padded labels >255 cannot trigger an
  // out-of-range-case warning; the optimizer still knows the value is a byte.
  const unsigned int byte = static_cast<unsigned char>(c[N]);
  switch (byte) { HOSQ_META_DETAIL_16(HOSQ_META_DETAIL_PREFIX, 0) }
#undef HOSQ_META_DETAIL_PREFIX
  return false;
}

template <class... Entries>
HOSQ_ALWAYS_INLINE auto dispatch_canonical(std::string_view c,
                                           std::tuple<Entries...>& a) -> bool {
  using metadata = key_metadata<std::remove_cvref_t<Entries>::literal()...>;
  // Force the global duplicate diagnostic even for an empty runtime input.
  static_assert(metadata::data.unique, "duplicated meta::match key");
  if constexpr (sizeof...(Entries) == 0) {
    return false;
  } else if constexpr (metadata::use_prefix) {
    // Route from depth zero without an upfront length-membership guard.
    // Each node checks termination before reading its actual byte; ordinary
    // leaves compare the complete view and terminals prove every prior byte.
    return dispatch_prefix<metadata, 0, metadata::prefix_indices>(c, a);
  } else {
#define HOSQ_META_DETAIL_FIRST(C)                   \
  case C: {                                         \
    if constexpr (!metadata::data.first_bytes[C]) { \
      return false;                                 \
    }                                               \
    break;                                          \
  }
    // Preserve cheap first-byte rejection without duplicating length dispatch
    // in each allowed case. Empty input bypasses the prefilter without a load.
    if (!c.empty()) {
      switch (static_cast<unsigned char>(c[0])) {
        HOSQ_META_DETAIL_BYTES(HOSQ_META_DETAIL_FIRST)
      }
    }
#undef HOSQ_META_DETAIL_FIRST
    return dispatch_length<metadata>(c, a);
  }
}

template <class... Entries>
HOSQ_ALWAYS_INLINE auto dispatch(std::string_view c, std::tuple<Entries...>& a)
    -> bool {
  if constexpr ((canonical_entry<std::remove_cvref_t<Entries>>::value && ...)) {
    return dispatch_canonical(c, a);
  } else {
    static_assert(compatibility_unique<Entries...>(),
                  "duplicated meta::match key");
    constexpr auto indices = []() {
      std::array<std::size_t, sizeof...(Entries)> result{};
      for (std::size_t i = 0; i < result.size(); ++i) {
        result[i] = i;
      }
      return result;
    }();
    return dispatch_compatibility<0, indices>(c, a);
  }
}

// Sequential-trie tables for the keys in Indices at depth N.
template <class Metadata, std::size_t N, auto Indices>
struct terminated_node {
  static constexpr auto counts = [] {
    std::array<std::size_t, 256> result{};
    for (auto index : Indices) {
      const auto key = Metadata::keys[index];
      if (N < key.size()) ++result[static_cast<unsigned char>(key[N])];
    }
    return result;
  }();
  static constexpr auto terminal = [] {
    for (auto index : Indices) {
      if (Metadata::keys[index].size() == N) return index;
    }
    return no_index;
  }();
  template <unsigned char Byte>
  static constexpr auto candidates = [] {
    std::array<std::size_t, counts[Byte]> result{};
    std::size_t out = 0;
    for (auto index : Indices) {
      const auto key = Metadata::keys[index];
      if (N < key.size() && static_cast<unsigned char>(key[N]) == Byte) {
        result[out++] = index;
      }
    }
    return result;
  }();
};

template <int Terminator, class Metadata, std::size_t Index, class Commit,
          class Tuple>
HOSQ_ALWAYS_INLINE auto terminated_leaf(std::string_view c, Tuple& a,
                                        Commit& commit) -> bool {
  constexpr auto expected = Metadata::keys[Index];
  if (c.size() <= expected.size() || c.substr(0, expected.size()) != expected ||
      static_cast<unsigned char>(c[expected.size()]) != Terminator) {
    return false;
  }
  commit(expected.size());
  std::get<Index>(a)();
  return true;
}

template <int Terminator, class Metadata, std::size_t N, auto Indices,
          class Commit, class Tuple>
HOSQ_ALWAYS_INLINE auto dispatch_terminated(std::string_view c, Tuple& a,
                                            Commit& commit) -> bool {
  using node = terminated_node<Metadata, N, Indices>;
  if (N >= c.size()) return false;
  const auto byte = static_cast<unsigned char>(c[N]);
  if (byte == Terminator) {
    if constexpr (node::terminal != no_index) {
      commit(N);
      std::get<node::terminal>(a)();
      return true;
    } else {
      return false;
    }
  }
#define HOSQ_META_TERMINATED(C)                                                \
  case C: {                                                                    \
    if constexpr (node::counts[C] != 0) {                                      \
      constexpr auto next = node::template candidates<C>;                      \
      if constexpr (next.size() == 1) {                                        \
        return terminated_leaf<Terminator, Metadata, next[0]>(c, a, commit);   \
      } else {                                                                 \
        return dispatch_terminated<Terminator, Metadata, N + 1, next>(c, a,    \
                                                                      commit); \
      }                                                                        \
    } else {                                                                   \
      return false;                                                            \
    }                                                                          \
  }
  switch (byte) { HOSQ_META_DETAIL_BYTES(HOSQ_META_TERMINATED) }
#undef HOSQ_META_TERMINATED
  return false;
}

// Custom entries may have a foreign literal type or an authoritative view that
// differs from literal(). Keep them on the ordinary compatibility dispatcher.
template <class Entry, class Commit>
struct committing_handler {
  Entry& entry;
  Commit& commit;
  std::size_t length;

  static constexpr auto literal() { return Entry::literal(); }
  static constexpr auto view() { return Entry::view(); }
  HOSQ_ALWAYS_INLINE auto operator()() -> void {
    commit(length);
    entry();
  }
};

#undef HOSQ_META_DETAIL_BYTES
#undef HOSQ_META_DETAIL_16

}  // namespace detail

// ============================================================================
// match() — public entry points and legacy ref-tuple helpers
// ============================================================================

/**
 * Dispatches a complete string against compile-time keys; calls exactly one
 * stored handler on equality and returns true, or returns false on a miss.
 * The original tuple is traversed by reference: handlers are never copied or
 * moved, and exceptions from a handler propagate unchanged.
 */
template <class... Entries>
HOSQ_ALWAYS_INLINE auto match(std::string_view c, std::tuple<Entries...>& a)
    -> bool {
  return detail::dispatch(c, a);
}

template <class... Entries>
HOSQ_ALWAYS_INLINE auto match(std::string_view c, std::tuple<Entries...>&& a)
    -> bool {
  // As in the lvalue overload, invoke the stored handler as an lvalue.
  return detail::dispatch(c, a);
}

/**
 * Legacy ref-tuple/depth entry point. N remains a lower bound on input length
 * (the old traversal's starting offset), not permission to trust an unchecked
 * prefix. All accepted inputs must equal a complete key, including bytes <N.
 * Inputs shorter than N safely miss. New code should use match(c, tuple).
 */
template <std::size_t N, class... Entries>
HOSQ_ALWAYS_INLINE auto match(std::string_view c, std::tuple<Entries&...> a)
    -> bool {
  if (c.size() < N) {
    return false;
  }
  return detail::dispatch(c, a);
}

// Retained for source compatibility; production traversal does not make
// candidate subtuples. Ended keys are not actual byte-zero candidates.
template <std::size_t N, char C, class... Entries>
consteval auto get_matched_array() {
  constexpr std::size_t matched_count =
      ((detail::has_literal_byte<std::remove_cvref_t<Entries>, N>() &&
        (std::remove_cvref_t<Entries>::literal()[N] == C)) +
       ... + 0);
  std::array<std::size_t, matched_count> result{};
  std::size_t index = 0;
  [&]<std::size_t... I>(std::index_sequence<I...>) {
    (((detail::has_literal_byte<std::remove_cvref_t<Entries>, N>() &&
       std::remove_cvref_t<Entries>::literal()[N] == C)
          ? (result[index++] = I, void())
          : void()),
     ...);
  }(std::index_sequence_for<Entries...>{});
  return result;
}

template <std::size_t Count, std::array<std::size_t, Count> Indices,
          class... Entries>
constexpr auto get_next_candidates(std::tuple<Entries&...> a) {
  return [&]<std::size_t... I>(std::index_sequence<I...>) {
    return std::forward_as_tuple(std::get<Indices[I]>(a)...);
  }(std::make_index_sequence<Count>{});
}

// Match a delimited prefix without scanning for its length first.
// Commit receives the key length and runs before the selected handler.
template <char Terminator, class Commit, class... Entries>
HOSQ_ALWAYS_INLINE auto match_terminated(std::string_view remaining,
                                         std::tuple<Entries...>& handlers,
                                         Commit commit) -> bool {
  if constexpr ((detail::canonical_entry<std::remove_cvref_t<Entries>>::value &&
                 ...)) {
    using metadata =
        detail::key_metadata<std::remove_cvref_t<Entries>::literal()...>;
    static_assert(metadata::data.unique, "duplicated meta::match key");
    return detail::dispatch_terminated<static_cast<unsigned char>(Terminator),
                                       metadata, 0, metadata::prefix_indices>(
        remaining, handlers, commit);
  } else {
    const auto length = remaining.find(Terminator);
    if (length == std::string_view::npos) return false;
    auto committing = std::apply(
        [&](auto&... entry) {
          return std::tuple{detail::committing_handler<
              std::remove_reference_t<decltype(entry)>, Commit>{entry, commit,
                                                                length}...};
        },
        handlers);
    return detail::dispatch(remaining.substr(0, length), committing);
  }
}

}  // namespace hosq::meta
