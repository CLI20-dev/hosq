#include <benchmark/benchmark.h>

#include <fstream>
#include <iostream>
#include <string>

#include "allocation_counter.hh"
#include "ser20/json/simple_reader.hh"

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
// Fully drains a Reader, advancing past every token so next_token() sees a
// consistent state. Without advance() after ScalarBegin the reader position
// is undefined and token iteration breaks.

static inline uint64_t load_u64(const char* p) noexcept {
  uint64_t v;
  std::memcpy(&v, p, sizeof(v));
  return v;
}

static inline uint64_t repeat_byte(unsigned char c) noexcept {
  return 0x0101010101010101ull * static_cast<uint64_t>(c);
}

static inline bool has_zero_byte(uint64_t x) noexcept {
  return ((x - 0x0101010101010101ull) & ~x & 0x8080808080808080ull) != 0;
}

static inline bool has_byte(uint64_t x, unsigned char c) noexcept {
  return has_zero_byte(x ^ repeat_byte(c));
}

static inline bool has_string_stop(uint64_t x) noexcept {
  return has_byte(x, '"') || has_byte(x, '\\');
}

static inline bool is_literal_stop(char c) noexcept {
  return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == ',' ||
         c == ']' || c == '}';
}

static inline bool has_literal_stop(uint64_t x) noexcept {
  return has_byte(x, ' ') || has_byte(x, '\t') || has_byte(x, '\r') ||
         has_byte(x, '\n') || has_byte(x, ',') || has_byte(x, ']') ||
         has_byte(x, '}');
}

static inline size_t scan_string_len_u64(std::string_view input,
                                         size_t quote_offset) noexcept {
  const char* const base = input.data();
  const char* p = base + quote_offset + 1;
  const char* const end = base + input.size();

  // Full 8-byte blocks. Safe: only load while at least 8 bytes remain.
  while (static_cast<size_t>(end - p) >= 8) {
    const uint64_t x = load_u64(p);

    if (!has_string_stop(x)) {
      p += 8;
      continue;
    }

    // Preserve exact escape behavior from the original loop.
    for (;;) {
      const char c = *p++;

      if (c == '\\') {
        if (p < end) {
          ++p;
          continue;
        }
        return static_cast<size_t>(p - (base + quote_offset));
      }

      if (c == '"') {
        return static_cast<size_t>(p - (base + quote_offset));
      }
    }
  }

  // Tail.
  while (p < end) {
    const char c = *p++;

    if (c == '\\') {
      if (p < end) {
        ++p;
      }
      continue;
    }

    if (c == '"') {
      return static_cast<size_t>(p - (base + quote_offset));
    }
  }

  return static_cast<size_t>(end - (base + quote_offset));
}

static inline size_t scan_literal_len_u64(std::string_view input,
                                          size_t offset) noexcept {
  const char* const base = input.data();
  const char* p = base + offset;
  const char* const end = base + input.size();

  while (static_cast<size_t>(end - p) >= 8) {
    const uint64_t x = load_u64(p);

    if (!has_literal_stop(x)) {
      p += 8;
      continue;
    }

    while (p < end && !is_literal_stop(*p)) {
      ++p;
    }

    return static_cast<size_t>(p - (base + offset));
  }

  while (p < end && !is_literal_stop(*p)) {
    ++p;
  }

  return static_cast<size_t>(p - (base + offset));
}

static size_t drain(json::SimpleReader::state_type& s,
                    json::SimpleReader& reader, std::string_view input) {
  size_t acc = 0;

  while (true) {
    if (!reader.next_token(s)) break;

    const auto& tok = reader.current_token(s);

    switch (tok.type) {
      case TokenType::End:
      case TokenType::Scalar:
      case TokenType::ObjectBegin:
      case TokenType::ArrayBegin:
      case TokenType::ObjectEnd:
      case TokenType::ArrayEnd:
        acc += tok.offset;
        break;
    }
  }

  return acc;
}

static void report_allocs(benchmark::State& state) {
  const double iters = static_cast<double>(state.iterations());
  state.counters["allocs"] =
      std::round(static_cast<double>(bench::AllocationCounter::count()) / iters);
  state.counters["alloc_bytes"] =
      std::round(static_cast<double>(bench::AllocationCounter::bytes()) / iters);
}

// ─── Benchmark definitions ────────────────────────────────────────────────────

#define DEFINE_BM(Name, maker)                                            \
  static void BM_##Name(benchmark::State& state) {                        \
    const std::string input = maker(static_cast<size_t>(state.range(0))); \
    bench::AllocationCounter::reset();                                    \
    json::SimpleReader::state_type s;                                     \
    for (auto _ : state) {                                                \
      json::SimpleReader reader;                                          \
      reader.reset(s, input);                                             \
      benchmark::DoNotOptimize(input.data());                             \
      size_t acc = drain(s, reader, input);                               \
      benchmark::DoNotOptimize(acc);                                      \
      benchmark::ClobberMemory();                                         \
    }                                                                     \
    state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) *    \
                            static_cast<int64_t>(input.size()));          \
    report_allocs(state);                                                 \
  }

DEFINE_BM(StructuralSparse, make_structural_sparse)
DEFINE_BM(FlatCompactObject, make_flat_object)
DEFINE_BM(PrettyObject, make_pretty_object)
DEFINE_BM(LongStringNoEscape, make_long_string_array)
DEFINE_BM(EscapedStringHeavy, make_escaped_string_array)
DEFINE_BM(MixedRealistic, make_mixed_realistic)
DEFINE_BM(TwitterLikeRandom, make_twitter_like_random)
DEFINE_BM(TwitterLikeLongRandom, make_twitter_like_long_random)
DEFINE_BM(TwitterLikeEscapedRandom, make_twitter_like_escaped_random)

#undef DEFINE_BM

// ─── twitter.json real-file benchmark ────────────────────────────────────────

static const std::string kTwitterJson = []() {
  std::ifstream f(TWITTER_JSON_PATH);
  if (!f) {
    std::cerr << "cannot open " TWITTER_JSON_PATH "\n";
    std::abort();
  }
  return std::string{std::istreambuf_iterator<char>(f), {}};
}();

static void BM_TwitterJson(benchmark::State& state) {
  bench::AllocationCounter::reset();
  json::SimpleReader::state_type s;
  for (auto _ : state) {
    json::SimpleReader reader;
    reader.reset(s, kTwitterJson);
    benchmark::DoNotOptimize(kTwitterJson.data());
    size_t acc = drain(s, reader, kTwitterJson);
    benchmark::DoNotOptimize(acc);
    benchmark::ClobberMemory();
  }
  state.SetBytesProcessed(state.iterations() *
                          static_cast<int64_t>(kTwitterJson.size()));
  report_allocs(state);
}

// ─── Registration ─────────────────────────────────────────────────────────────

BENCHMARK(BM_TwitterJson);

#define REGISTER(BM) BENCHMARK(BM)->Arg(1 << 20)->Arg(8 << 20)->Arg(64 << 20)

REGISTER(BM_PrettyObject);
REGISTER(BM_LongStringNoEscape);
REGISTER(BM_EscapedStringHeavy);
REGISTER(BM_MixedRealistic);
REGISTER(BM_TwitterLikeRandom);
REGISTER(BM_TwitterLikeLongRandom);
REGISTER(BM_TwitterLikeEscapedRandom);

#undef REGISTER

}  // namespace

BENCHMARK_MAIN();
