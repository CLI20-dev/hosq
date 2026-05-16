#include <benchmark/benchmark.h>

#include <string>

#include "ser20/scanner.hh"
#ifdef __ARM_NEON
#include "ser20/scanner_neon.hh"
#endif

namespace {

using namespace ser20;

// ─── Input generators ────────────────────────────────────────────────────────

static std::string make_whitespace_sparse(size_t bytes) {
  std::string s;
  s.reserve(bytes + 256);
  while (s.size() < bytes) {
    s.append(255, ' ');
    s.push_back('{');
  }
  return s;
}

static std::string make_structural_sparse(size_t bytes) {
  std::string s;
  s.reserve(bytes + 256);
  while (s.size() < bytes) {
    s.append(255, 'a');
    s.push_back(',');
  }
  return s;
}

static std::string make_flat_object(size_t bytes) {
  std::string s = "{";
  for (size_t i = 0; s.size() < bytes; ++i) {
    if (i != 0) s.push_back(',');
    s += "\"key";
    s += std::to_string(i);
    s += "\":\"value";
    s += std::to_string(i);
    s.push_back('"');
  }
  s.push_back('}');
  return s;
}

static std::string make_pretty_object(size_t bytes) {
  std::string s = "{\n";
  for (size_t i = 0; s.size() < bytes; ++i) {
    s += "    \"key";
    s += std::to_string(i);
    s += "\": \"value";
    s += std::to_string(i);
    s += "\",\n";
  }
  s += "\"end\":\"end\"\n}";
  return s;
}

static std::string make_long_string_array(size_t bytes) {
  std::string s = "[";
  for (size_t i = 0; s.size() < bytes; ++i) {
    if (i != 0) s.push_back(',');
    s.push_back('"');
    s.append(1024, 'a');
    s.push_back('"');
  }
  s.push_back(']');
  return s;
}

static std::string make_escaped_string_array(size_t bytes) {
  std::string s = "[";
  for (size_t i = 0; s.size() < bytes; ++i) {
    if (i != 0) s.push_back(',');
    s.push_back('"');
    for (int j = 0; j < 64; ++j) s += R"(abc\"def)";
    s.push_back('"');
  }
  s.push_back(']');
  return s;
}

static std::string make_mixed_realistic(size_t bytes) {
  std::string s = "[";
  for (size_t i = 0; s.size() < bytes; ++i) {
    if (i != 0) s.push_back(',');
    s += R"({"id":)";
    s += std::to_string(i);
    s += R"(,"name":"name)";
    s += std::to_string(i);
    s += R"(","ok":true,"value":null})";
  }
  s.push_back(']');
  return s;
}

static std::string make_random_ascii_string(size_t n, uint64_t& seed) {
  std::string s;
  s.resize(n);

  uint64_t x = seed;
  for (size_t i = 0; i < n; ++i) {
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    s[i] = static_cast<char>('a' + (x % 26));
  }
  seed = x;
  return s;
}

static std::string make_twitter_like_random(size_t bytes) {
  std::string s = R"({"statuses":[)";
  uint64_t seed = 1;

  for (size_t i = 0; s.size() < bytes; ++i) {
    if (i != 0) s.push_back(',');

    s += R"({"created_at":"Sun Aug 31 00:29:15 +0000 2014")";
    s += R"(,"id":)";
    s += std::to_string(505874924095815681ULL + i);
    s += R"(,"text":")";
    s += make_random_ascii_string(140, seed);
    s.push_back('"');
    s += R"(,"retweet_count":)";
    s += std::to_string(i % 100);
    s += R"(,"favorite_count":)";
    s += std::to_string(i % 1000);
    s += R"(,"in_reply_to_status_id":null)";
    s += R"(,"user":{"id":)";
    s += std::to_string(1000000 + i);
    s += R"(,"screen_name":")";
    s += make_random_ascii_string(12, seed);
    s += R"(","name":")";
    s += make_random_ascii_string(20, seed);
    s += R"(","verified":false}})";
  }

  s += R"(]})";
  return s;
}

static std::string make_twitter_like_long_random(size_t bytes) {
  std::string s = R"({"statuses":[)";
  uint64_t seed = 7;

  for (size_t i = 0; s.size() < bytes; ++i) {
    if (i != 0) s.push_back(',');

    s += R"({"id":)";
    s += std::to_string(i);
    s += R"(,"text":")";
    s += make_random_ascii_string(512, seed);
    s.push_back('"');
    s += R"(,"user":{"id":)";
    s += std::to_string(i + 1000);
    s += R"(,"screen_name":")";
    s += make_random_ascii_string(16, seed);
    s += R"("}})";
  }

  s += R"(]})";
  return s;
}

static std::string make_twitter_like_escaped_random(size_t bytes) {
  std::string s = R"({"statuses":[)";
  uint64_t seed = 11;

  for (size_t i = 0; s.size() < bytes; ++i) {
    if (i != 0) s.push_back(',');

    s += R"({"id":)";
    s += std::to_string(i);
    s += R"(,"text":")";

    for (int j = 0; j < 32; ++j) {
      s += make_random_ascii_string(8, seed);
      s += R"(\")";
    }

    s += R"(end")";
    s += R"(,"user":{"screen_name":")";
    s += make_random_ascii_string(12, seed);
    s += R"("}})";
  }

  s += R"(]})";
  return s;
}

// ─── Benchmark kernel ─────────────────────────────────────────────────────────

template <typename Scanner>
static size_t drain(Scanner& scanner) {
  size_t acc = 0;
  while (true) {
    const auto tok = scanner.consume_next_token();
    if (tok.is_end()) break;
    acc += tok.pos;
  }
  return acc;
}

// ─── Benchmark definitions ────────────────────────────────────────────────────

template <typename Scanner>
static void BM_WhitespaceSparse(benchmark::State& state) {
  const std::string input =
      make_whitespace_sparse(static_cast<size_t>(state.range(0)));
  for (auto _ : state) {
    Scanner scanner(input);
    benchmark::DoNotOptimize(input.data());
    size_t acc = drain(scanner);
    benchmark::DoNotOptimize(acc);
    benchmark::ClobberMemory();
  }
  state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) *
                          static_cast<int64_t>(input.size()));
}

template <typename Scanner>
static void BM_StructuralSparse(benchmark::State& state) {
  const std::string input =
      make_structural_sparse(static_cast<size_t>(state.range(0)));
  for (auto _ : state) {
    Scanner scanner(input);
    benchmark::DoNotOptimize(input.data());
    size_t acc = drain(scanner);
    benchmark::DoNotOptimize(acc);
    benchmark::ClobberMemory();
  }
  state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) *
                          static_cast<int64_t>(input.size()));
}

template <typename Scanner>
static void BM_FlatCompactObject(benchmark::State& state) {
  const std::string input =
      make_flat_object(static_cast<size_t>(state.range(0)));
  for (auto _ : state) {
    Scanner scanner(input);
    benchmark::DoNotOptimize(input.data());
    size_t acc = drain(scanner);
    benchmark::DoNotOptimize(acc);
    benchmark::ClobberMemory();
  }
  state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) *
                          static_cast<int64_t>(input.size()));
}

template <typename Scanner>
static void BM_PrettyObject(benchmark::State& state) {
  const std::string input =
      make_pretty_object(static_cast<size_t>(state.range(0)));
  for (auto _ : state) {
    Scanner scanner(input);
    benchmark::DoNotOptimize(input.data());
    size_t acc = drain(scanner);
    benchmark::DoNotOptimize(acc);
    benchmark::ClobberMemory();
  }
  state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) *
                          static_cast<int64_t>(input.size()));
}

template <typename Scanner>
static void BM_LongStringNoEscape(benchmark::State& state) {
  const std::string input =
      make_long_string_array(static_cast<size_t>(state.range(0)));
  for (auto _ : state) {
    Scanner scanner(input);
    benchmark::DoNotOptimize(input.data());
    size_t acc = drain(scanner);
    benchmark::DoNotOptimize(acc);
    benchmark::ClobberMemory();
  }
  state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) *
                          static_cast<int64_t>(input.size()));
}

template <typename Scanner>
static void BM_EscapedStringHeavy(benchmark::State& state) {
  const std::string input =
      make_escaped_string_array(static_cast<size_t>(state.range(0)));
  for (auto _ : state) {
    Scanner scanner(input);
    benchmark::DoNotOptimize(input.data());
    size_t acc = drain(scanner);
    benchmark::DoNotOptimize(acc);
    benchmark::ClobberMemory();
  }
  state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) *
                          static_cast<int64_t>(input.size()));
}

template <typename Scanner>
static void BM_MixedRealistic(benchmark::State& state) {
  const std::string input =
      make_mixed_realistic(static_cast<size_t>(state.range(0)));
  for (auto _ : state) {
    Scanner scanner(input);
    benchmark::DoNotOptimize(input.data());
    size_t acc = drain(scanner);
    benchmark::DoNotOptimize(acc);
    benchmark::ClobberMemory();
  }
  state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) *
                          static_cast<int64_t>(input.size()));
}

template <typename Scanner>
static void BM_TwitterLikeRandom(benchmark::State& state) {
  const std::string input =
      make_twitter_like_random(static_cast<size_t>(state.range(0)));

  for (auto _ : state) {
    Scanner scanner(input);
    benchmark::DoNotOptimize(input.data());
    size_t acc = drain(scanner);
    benchmark::DoNotOptimize(acc);
    benchmark::ClobberMemory();
  }

  state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) *
                          static_cast<int64_t>(input.size()));
}

template <typename Scanner>
static void BM_TwitterLikeLongRandom(benchmark::State& state) {
  const std::string input =
      make_twitter_like_long_random(static_cast<size_t>(state.range(0)));

  for (auto _ : state) {
    Scanner scanner(input);
    benchmark::DoNotOptimize(input.data());
    size_t acc = drain(scanner);
    benchmark::DoNotOptimize(acc);
    benchmark::ClobberMemory();
  }

  state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) *
                          static_cast<int64_t>(input.size()));
}

template <typename Scanner>
static void BM_TwitterLikeEscapedRandom(benchmark::State& state) {
  const std::string input =
      make_twitter_like_escaped_random(static_cast<size_t>(state.range(0)));

  for (auto _ : state) {
    Scanner scanner(input);
    benchmark::DoNotOptimize(input.data());
    size_t acc = drain(scanner);
    benchmark::DoNotOptimize(acc);
    benchmark::ClobberMemory();
  }

  state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) *
                          static_cast<int64_t>(input.size()));
}

// ─── Registration ─────────────────────────────────────────────────────────────

#ifdef __ARM_NEON
#define REGISTER(BM)                         \
  BENCHMARK_TEMPLATE(BM, ser20::Scanner)     \
      ->Arg(1 << 20)                         \
      ->Arg(8 << 20)                         \
      ->Arg(64 << 20);                       \
  BENCHMARK_TEMPLATE(BM, ser20::ScannerNeon) \
      ->Arg(1 << 20)                         \
      ->Arg(8 << 20)                         \
      ->Arg(64 << 20)
#else
#define REGISTER(BM)                     \
  BENCHMARK_TEMPLATE(BM, ser20::Scanner) \
      ->Arg(1 << 20)                     \
      ->Arg(8 << 20)                     \
      ->Arg(64 << 20)
#endif

REGISTER(BM_WhitespaceSparse);
REGISTER(BM_StructuralSparse);
REGISTER(BM_FlatCompactObject);
REGISTER(BM_PrettyObject);
REGISTER(BM_LongStringNoEscape);
REGISTER(BM_EscapedStringHeavy);
REGISTER(BM_MixedRealistic);
REGISTER(BM_TwitterLikeRandom);
REGISTER(BM_TwitterLikeLongRandom);
REGISTER(BM_TwitterLikeEscapedRandom);

#undef REGISTER

}  // namespace
