#include <benchmark/benchmark.h>
#include <simdjson.h>

#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "ser20/parser.hh"

// ── Schema ────────────────────────────────────────────────────────────────────

namespace tw {

struct User {
  ser20::ElemImpl<"id",
      ser20::scan::scalar | ser20::decode::integer<int64_t> |
      ser20::pack::once> id;
  ser20::ElemImpl<"name",
      ser20::scan::string | ser20::decode::string_view |
      ser20::pack::once> name;
  ser20::ElemImpl<"screen_name",
      ser20::scan::string | ser20::decode::string_view |
      ser20::pack::once> screen_name;
  ser20::ElemImpl<"followers_count",
      ser20::scan::scalar | ser20::decode::integer<int32_t> |
      ser20::pack::once> followers_count;
  ser20::ElemImpl<"friends_count",
      ser20::scan::scalar | ser20::decode::integer<int32_t> |
      ser20::pack::once> friends_count;
  ser20::ElemImpl<"",
      ser20::decode::member<ser20::pack::ignore> |
      ser20::pack::ignore> unknown;
};

struct Status {
  ser20::ElemImpl<"id",
      ser20::scan::scalar | ser20::decode::integer<int64_t> |
      ser20::pack::once> id;
  ser20::ElemImpl<"text",
      ser20::scan::string | ser20::decode::string_view |
      ser20::pack::once> text;
  ser20::ElemImpl<"retweet_count",
      ser20::scan::scalar | ser20::decode::integer<int32_t> |
      ser20::pack::once> retweet_count;
  ser20::ElemImpl<"favorite_count",
      ser20::scan::scalar | ser20::decode::integer<int32_t> |
      ser20::pack::once> favorite_count;
  ser20::ElemImpl<"lang",
      ser20::scan::string | ser20::decode::string_view |
      ser20::pack::once> lang;
  ser20::ElemImpl<"user",
      ser20::decode::parse_to<User> |
      ser20::pack::once> user;
  ser20::ElemImpl<"",
      ser20::decode::member<ser20::pack::ignore> |
      ser20::pack::ignore> unknown;
};

struct Twitter {
  ser20::ElemImpl<"statuses",
      ser20::decode::vector<
          ser20::decode::parse_to<Status> |
          ser20::pack::assign> |
      ser20::pack::once> statuses;
  ser20::ElemImpl<"",
      ser20::decode::member<ser20::pack::ignore> |
      ser20::pack::ignore> unknown;
};

}  // namespace tw

// ── Fixture: load JSON once ───────────────────────────────────────────────────

static std::string load_twitter_json() {
  std::ifstream f(TWITTER_JSON_PATH);
  if (!f) {
    std::cerr << "cannot open " TWITTER_JSON_PATH "\n";
    std::abort();
  }
  return {std::istreambuf_iterator<char>(f), {}};
}

static const std::string kJson = load_twitter_json();

// ── ser20 benchmark ───────────────────────────────────────────────────────────

static void BM_ser20(benchmark::State& state) {
  for (auto _ : state) {
    auto doc = ser20::parse<tw::Twitter>(kJson);
    const std::vector<tw::Status>& statuses = doc.statuses;
    int64_t sum = 0;
    for (const auto& s : statuses) {
      sum += static_cast<int64_t>(s.id);
      sum += static_cast<int32_t>(s.retweet_count);
      sum += static_cast<int32_t>(s.favorite_count);
      const tw::User& u = s.user;
      sum += static_cast<int64_t>(u.id);
      sum += static_cast<int32_t>(u.followers_count);
    }
    benchmark::DoNotOptimize(sum);
  }
  state.SetBytesProcessed(state.iterations() *
                          static_cast<int64_t>(kJson.size()));
}

// ── simdjson (ondemand) benchmark ─────────────────────────────────────────────

static void BM_simdjson_ondemand(benchmark::State& state) {
  // ondemand requires padded input
  simdjson::ondemand::parser parser;
  auto padded = simdjson::padded_string(kJson);

  for (auto _ : state) {
    auto doc = parser.iterate(padded);
    int64_t sum = 0;
    for (auto tweet : doc["statuses"]) {
      sum += static_cast<int64_t>(tweet["id"].get_int64());
      sum += static_cast<int64_t>(tweet["retweet_count"].get_int64());
      sum += static_cast<int64_t>(tweet["favorite_count"].get_int64());
      auto user = tweet["user"].get_object();
      sum += static_cast<int64_t>(user["id"].get_int64());
      sum += static_cast<int64_t>(user["followers_count"].get_int64());
    }
    benchmark::DoNotOptimize(sum);
  }
  state.SetBytesProcessed(state.iterations() *
                          static_cast<int64_t>(kJson.size()));
}

BENCHMARK(BM_simdjson_ondemand)->MinTime(2.0);
BENCHMARK(BM_ser20)->MinTime(2.0);
