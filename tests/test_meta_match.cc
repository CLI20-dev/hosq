#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include "hosq/meta_match.hh"

namespace {
using hosq::StringLiteral;
namespace meta = hosq::meta;

constexpr std::size_t none = static_cast<std::size_t>(-1);

// Keys, their prefixes and extensions, and every single-byte mutation that
// moves a byte into another class (NUL, ASCII, high bit).
auto probes(const std::vector<std::string_view>& keys)
    -> std::vector<std::string> {
  std::vector<std::string> result{"", "\"", std::string(1, '\0'), "\xff"};
  for (auto key : keys) {
    const std::string k(key);
    result.push_back(k);
    for (std::size_t n = 0; n < k.size(); ++n) result.push_back(k.substr(0, n));
    for (char extra : {'\0', 'a', '\x80', '\xff'}) result.push_back(k + extra);
    for (std::size_t i = 0; i < k.size(); ++i) {
      for (unsigned char flip : {0x01, 0x20, 0x80}) {
        auto mutated = k;
        mutated[i] = static_cast<char>(static_cast<unsigned char>(k[i]) ^ flip);
        result.push_back(mutated);
      }
      auto zeroed = k;
      zeroed[i] = '\0';
      result.push_back(zeroed);
    }
  }
  return result;
}

auto oracle(const std::vector<std::string_view>& keys, std::string_view input)
    -> std::size_t {
  for (std::size_t i = 0; i < keys.size(); ++i) {
    if (keys[i] == input) return i;
  }
  return none;
}

template <StringLiteral... Keys>
void check_keys() {
  const std::vector<std::string_view> keys{Keys.view()...};
  std::size_t selected = none;
  int calls = 0;
  auto handlers = [&]<std::size_t... I>(std::index_sequence<I...>) {
    return std::tuple{meta::make_handler<Keys>([&selected, &calls] {
      selected = I;
      ++calls;
    })...};
  }(std::index_sequence_for<decltype(Keys)...>{});

  for (const auto& input : probes(keys)) {
    const auto expected = oracle(keys, input);
    selected = none;
    calls = 0;
    EXPECT_EQ(meta::match(input, handlers), expected != none)
        << testing::PrintToString(input);
    EXPECT_EQ(selected, expected) << testing::PrintToString(input);
    EXPECT_EQ(calls, expected != none ? 1 : 0);

    // The terminated form must agree when the input is followed by '"'.
    if (input.find('"') != std::string::npos) continue;
    std::size_t committed = none;
    auto commit = [&](std::size_t n) { committed = n; };
    const auto delimited = input + "\":0";
    selected = none;
    EXPECT_EQ(meta::match_terminated<'"'>(delimited, handlers, commit),
              expected != none)
        << testing::PrintToString(input);
    EXPECT_EQ(selected, expected) << testing::PrintToString(input);
    EXPECT_EQ(committed, expected != none ? input.size() : none);
    // Without a terminator nothing may match, even if a key is a prefix.
    selected = none;
    EXPECT_FALSE(meta::match_terminated<'"'>(input, handlers, commit));
    EXPECT_EQ(selected, none);
  }
}

template <StringLiteral... Keys>
constexpr bool uses_prefix = meta::detail::key_metadata<Keys...>::use_prefix;
}  // namespace

TEST(MetaMatch, SmallSparseSchemaUsesPrefixRoute) {
  static_assert(uses_prefix<"id", "name", "value", "x", "zeta", "">);
  check_keys<"id", "name", "value", "x", "zeta", "">();
}

TEST(MetaMatch, SharedPrefixesUseLengthRoute) {
  static_assert(!uses_prefix<"user", "user_id", "user_name", "user_mail", "u">);
  check_keys<"user", "user_id", "user_name", "user_mail", "u">();
}

TEST(MetaMatch, DistinguishingBytesOutsidePrintableAscii) {
  // Each pair below differs only in a control or high byte.
  check_keys<"\x01", "\x1f", "\x7f", "\x80", "\xff">();
  check_keys<"\xc3\xa9t\xc3\xa9", "\xc3\xa9", "caf\xc3\xa9", "caf\xc3\xa8">();
  check_keys<"tab\tkey", "tab\nkey", "tab key">();
}

TEST(MetaMatch, EmbeddedNul) { check_keys<"a", "a\0b", "a\0c", "", "\0">(); }

TEST(MetaMatch, LongKeysBeyondLengthSwitch) {
  check_keys<
      "0123456789012345678901234567890123456789012345678901234567890123456789",
      "012345678901234567890123456789012345678901234567890123456789012345678x",
      "0123456789012345678901234567890123456789012345678901234567890123",
      "short">();
}

TEST(MetaMatch, ManyKeys) {
  check_keys<"k0", "k1", "k2", "k3", "k4", "k5", "k6", "k7", "k8", "k9", "k10",
             "k11", "k12", "k13", "k14", "k15", "k16", "k17", "k18", "k19",
             "alpha", "beta", "gamma", "delta", "epsilon", "zeta", "eta",
             "theta", "iota", "kappa", "lambda", "mu", "nu", "xi", "omicron",
             "pi", "rho", "sigma", "tau", "upsilon">();
}

TEST(MetaMatch, EmptyHandlerSet) {
  auto handlers = std::tuple{};
  EXPECT_FALSE(meta::match("", handlers));
  EXPECT_FALSE(meta::match("x", handlers));
}

TEST(MetaMatch, HandlersSharingKeysShareOnePlan) {
  auto a =
      std::tuple{meta::make_handler<"x">([] {}), meta::make_handler<"y">([] {})};
  int hits = 0;
  auto b = std::tuple{meta::make_handler<"x">([&] { ++hits; }),
                      meta::make_handler<"y">([&] { hits += 2; })};
  static_assert(
      std::is_same_v<meta::detail::key_metadata<
                         std::tuple_element_t<0, decltype(a)>::literal(),
                         std::tuple_element_t<1, decltype(a)>::literal()>,
                     meta::detail::key_metadata<
                         std::tuple_element_t<0, decltype(b)>::literal(),
                         std::tuple_element_t<1, decltype(b)>::literal()>>);
  EXPECT_TRUE(meta::match("x", a));
  EXPECT_TRUE(meta::match("y", b));
  EXPECT_EQ(hits, 2);
}

TEST(MetaMatch, TerminatedCommitRunsOnceBeforeHandler) {
  int committed = 0;
  int called = 0;
  auto handlers = std::tuple{meta::make_handler<"prefix">([&] {
                               EXPECT_EQ(committed, 6);
                               ++called;
                             }),
                             meta::make_handler<"prefixLong">([&] {
                               EXPECT_EQ(committed, 10);
                               ++called;
                             })};
  auto commit = [&](std::size_t length) {
    EXPECT_EQ(committed, 0);
    committed = static_cast<int>(length);
  };
  EXPECT_FALSE(meta::match_terminated<'"'>("prefix", handlers, commit));
  EXPECT_FALSE(meta::match_terminated<'"'>("prefixLonger\"", handlers, commit));
  EXPECT_EQ(committed, 0);
  EXPECT_EQ(called, 0);
  EXPECT_TRUE(meta::match_terminated<'"'>("prefix\":0", handlers, commit));
  EXPECT_EQ(called, 1);
  committed = 0;
  EXPECT_TRUE(meta::match_terminated<'"'>("prefixLong\":0", handlers, commit));
  EXPECT_EQ(called, 2);
}

TEST(MetaMatch, StatefulMoveOnlyHandlerIsNotCopiedAndExceptionsPropagate) {
  auto handlers = std::tuple{
      meta::make_handler<"key">([state = std::make_unique<int>(0)]() mutable {
        if (++*state == 2) throw std::runtime_error("second call");
      })};
  EXPECT_TRUE(meta::match("key", handlers));
  EXPECT_THROW(meta::match("key", handlers), std::runtime_error);
}

TEST(MetaMatch, LegacyDepthStillChecksTheWholeKey) {
  int calls = 0;
  auto handler = meta::make_handler<"prefix">([&] { ++calls; });
  auto refs = std::tie(handler);
  EXPECT_FALSE(meta::match<3>("pre", refs));
  EXPECT_FALSE(meta::match<3>("xxxfix", refs));
  EXPECT_TRUE(meta::match<3>("prefix", refs));
  EXPECT_EQ(calls, 1);
}

TEST(MetaMatch, TerminatedCustomLiteralKeepsCompatibilityAndCommitOrder) {
  struct ForeignEntry {
    int& committed;
    int& calls;
    static constexpr auto literal() { return std::array{'k', 'e', 'y', '\0'}; }
    static constexpr auto view() -> std::string_view { return "key"; }
    auto operator()() -> void {
      EXPECT_EQ(committed, 3);
      ++calls;
    }
  };
  int committed = 0;
  int calls = 0;
  auto handlers = std::tuple{ForeignEntry{committed, calls}};
  auto commit = [&](std::size_t size) { committed = static_cast<int>(size); };
  EXPECT_FALSE(meta::match_terminated<'"'>("key", handlers, commit));
  EXPECT_FALSE(meta::match_terminated<'"'>("keys\"", handlers, commit));
  EXPECT_EQ(committed, 0);
  EXPECT_EQ(calls, 0);
  EXPECT_TRUE(meta::match_terminated<'"'>("key\":0", handlers, commit));
  EXPECT_EQ(calls, 1);
}
