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

// Internal key dispatch. Key-only indexing is adapted from gen740/meta_match
// at fbc01a0; hosq uses one sequential trie for both reader paths.
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

template <StringLiteral Key, class Fn>
struct Handler {
  Fn fn;
  [[nodiscard]] static constexpr auto view() noexcept { return Key.view(); }
  HOSQ_ALWAYS_INLINE auto operator()() noexcept(noexcept(fn())) -> void { fn(); }
};

template <StringLiteral Key, class Fn>
[[nodiscard]] constexpr auto make_handler(Fn&& fn)
    -> Handler<Key, std::decay_t<Fn>> {
  return {std::forward<Fn>(fn)};
}

namespace detail {
inline constexpr std::size_t no_index = static_cast<std::size_t>(-1);

struct no_commit {
  HOSQ_ALWAYS_INLINE auto operator()(std::size_t) const noexcept -> void {}
};

// Neither metadata nor candidate sets depend on callable types.
template <StringLiteral... Keys>
struct key_metadata {
  static constexpr std::array<std::string_view, sizeof...(Keys)> keys{
      Keys.view()...};
  static constexpr auto indices = [] {
    std::array<std::size_t, sizeof...(Keys)> result{};
    for (std::size_t i = 0; i < result.size(); ++i) result[i] = i;
    return result;
  }();
  static constexpr bool unique = [] {
    for (std::size_t i = 0; i < keys.size(); ++i) {
      for (std::size_t j = i + 1; j < keys.size(); ++j) {
        if (keys[i] == keys[j]) return false;
      }
    }
    return true;
  }();
  static_assert(unique, "duplicated meta::match key");
};

// Sequential-trie tables for the keys in Indices at depth N.
template <class Metadata, std::size_t N, auto Indices>
struct trie_node {
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

// Terminator == -1 matches the complete view. Otherwise a successful match
// must be followed by that byte, and commits the reader before the handler.
template <int Terminator, class Metadata, std::size_t Index, class Commit,
          class Tuple>
HOSQ_ALWAYS_INLINE auto match_leaf(std::string_view input, Tuple& handlers,
                                   Commit& commit) -> bool {
  constexpr auto key = Metadata::keys[Index];
  if constexpr (Terminator < 0) {
    if (input != key) return false;
  } else {
    if (input.size() <= key.size() || input.substr(0, key.size()) != key ||
        static_cast<unsigned char>(input[key.size()]) != Terminator) {
      return false;
    }
  }
  commit(key.size());
  std::get<Index>(handlers)();
  return true;
}

template <int Terminator, class Metadata, std::size_t N, auto Indices,
          class Commit, class Tuple>
HOSQ_ALWAYS_INLINE auto dispatch(std::string_view input, Tuple& handlers,
                                 Commit& commit) -> bool {
  using node = trie_node<Metadata, N, Indices>;
  const bool at_end = [&] {
    if constexpr (Terminator < 0) {
      return input.size() == N;
    } else {
      return input.size() > N &&
             static_cast<unsigned char>(input[N]) == Terminator;
    }
  }();
  if (at_end) {
    if constexpr (node::terminal != no_index) {
      commit(N);
      std::get<node::terminal>(handlers)();
      return true;
    }
    return false;
  }
  if (N >= input.size()) return false;
#define HOSQ_META_BRANCH(C)                                                 \
  case C: {                                                                 \
    if constexpr (node::counts[C] != 0) {                                   \
      constexpr auto next = node::template candidates<C>;                   \
      if constexpr (next.size() == 1) {                                     \
        return match_leaf<Terminator, Metadata, next[0]>(input, handlers,   \
                                                         commit);           \
      } else {                                                              \
        return dispatch<Terminator, Metadata, N + 1, next>(input, handlers, \
                                                           commit);         \
      }                                                                     \
    }                                                                       \
    return false;                                                           \
  }
  switch (static_cast<unsigned char>(input[N])) {
    HOSQ_META_DETAIL_BYTES(HOSQ_META_BRANCH)
  }
#undef HOSQ_META_BRANCH
  return false;
}

#undef HOSQ_META_DETAIL_BYTES
#undef HOSQ_META_DETAIL_16
}  // namespace detail

// These entry points accept only the Handler tuples constructed by the decoder.
template <StringLiteral... Keys, class... Fns>
HOSQ_ALWAYS_INLINE auto match(std::string_view input,
                              std::tuple<Handler<Keys, Fns>...>& handlers)
    -> bool {
  using metadata = detail::key_metadata<Keys...>;
  detail::no_commit commit;
  return detail::dispatch<-1, metadata, 0, metadata::indices>(input, handlers,
                                                              commit);
}

template <char Terminator, class Commit, StringLiteral... Keys, class... Fns>
HOSQ_ALWAYS_INLINE auto match_terminated(
    std::string_view input, std::tuple<Handler<Keys, Fns>...>& handlers,
    Commit commit) -> bool {
  using metadata = detail::key_metadata<Keys...>;
  return detail::dispatch<static_cast<unsigned char>(Terminator), metadata, 0,
                          metadata::indices>(input, handlers, commit);
}

}  // namespace hosq::meta
