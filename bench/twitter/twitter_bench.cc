#include <benchmark/benchmark.h>
#include <simdjson.h>
#include <yyjson.h>
#include <rapidjson/document.h>
#include <nlohmann/json.hpp>
#include <glaze/glaze.hpp>

#include <cassert>
#include <cstdint>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include "ser20/parser.hh"

// ── glaze schema ──────────────────────────────────────────────────────────────

namespace glz_tw {

struct User {
  int64_t id{};
  std::string name{};
  std::string screen_name{};
  int32_t followers_count{};
  int32_t friends_count{};
};

struct Status {
  int64_t id{};
  std::string text{};
  int32_t retweet_count{};
  int32_t favorite_count{};
  std::string lang{};
  User user{};
};

struct Twitter {
  std::vector<Status> statuses{};
};

static int64_t checksum(const Twitter& doc) {
  int64_t sum = 0;
  for (const auto& s : doc.statuses) {
    sum += s.id;
    sum += s.retweet_count;
    sum += s.favorite_count;
    sum += s.user.id;
    sum += s.user.followers_count;
  }
  return sum;
}

}  // namespace glz_tw

// ── Schema ────────────────────────────────────────────────────────────────────

namespace tw {

struct User {
  ser20::ElemImpl<"id", ser20::scan::scalar | ser20::decode::integer<int64_t> |
                            ser20::pack::once>
      id;
  ser20::ElemImpl<"name", ser20::scan::string | ser20::decode::string_view |
                              ser20::pack::once>
      name;
  ser20::ElemImpl<"screen_name", ser20::scan::string |
                                     ser20::decode::string_view |
                                     ser20::pack::once>
      screen_name;
  ser20::ElemImpl<"followers_count", ser20::scan::scalar |
                                         ser20::decode::integer<int32_t> |
                                         ser20::pack::once>
      followers_count;
  ser20::ElemImpl<"friends_count", ser20::scan::scalar |
                                       ser20::decode::integer<int32_t> |
                                       ser20::pack::once>
      friends_count;
  ser20::ElemImpl<"", ser20::decode::member<ser20::pack::ignore> |
                          ser20::pack::ignore>
      unknown;
};

struct Status {
  ser20::ElemImpl<"id", ser20::scan::scalar | ser20::decode::integer<int64_t> |
                            ser20::pack::once>
      id;
  ser20::ElemImpl<"text", ser20::scan::string | ser20::decode::string_view |
                              ser20::pack::once>
      text;
  ser20::ElemImpl<"retweet_count", ser20::scan::scalar |
                                       ser20::decode::integer<int32_t> |
                                       ser20::pack::once>
      retweet_count;
  ser20::ElemImpl<"favorite_count", ser20::scan::scalar |
                                        ser20::decode::integer<int32_t> |
                                        ser20::pack::once>
      favorite_count;
  ser20::ElemImpl<"lang", ser20::scan::string | ser20::decode::string_view |
                              ser20::pack::once>
      lang;
  ser20::ElemImpl<"user", ser20::decode::parse_to<User> | ser20::pack::once>
      user;
  ser20::ElemImpl<"", ser20::decode::member<ser20::pack::ignore> |
                          ser20::pack::ignore>
      unknown;
};

struct Twitter {
  ser20::ElemImpl<"statuses",
                  ser20::decode::vector<ser20::decode::parse_to<Status> |
                                        ser20::pack::assign> |
                      ser20::pack::once>
      statuses;
  ser20::ElemImpl<"", ser20::decode::member<ser20::pack::ignore> |
                          ser20::pack::ignore>
      unknown;
};

struct TwitterArray {
  ser20::ElemImpl<"statuses",
                  ser20::decode::array<128, ser20::decode::parse_to<Status> |
                                                ser20::pack::assign> |
                      ser20::pack::once>
      statuses;
  ser20::ElemImpl<"", ser20::decode::member<ser20::pack::ignore> |
                          ser20::pack::ignore>
      unknown;
};

// ── Plain C++ structs for simdjson materialize ────────────────────────────────

struct UserPlain {
  int64_t id{};
  int32_t followers_count{};
};
struct StatusPlain {
  int64_t id{};
  int32_t retweet_count{};
  int32_t favorite_count{};
  UserPlain user{};
};

// ── Checksum helpers ──────────────────────────────────────────────────────────

static int64_t checksum_plain(const std::vector<StatusPlain>& statuses) {
  int64_t sum = 0;
  for (const auto& s : statuses) {
    sum += s.id;
    sum += s.retweet_count;
    sum += s.favorite_count;
    sum += s.user.id;
    sum += s.user.followers_count;
  }
  return sum;
}

static int64_t checksum_ser20(const std::vector<Status>& statuses) {
  int64_t sum = 0;
  for (const auto& s : statuses) {
    sum += static_cast<int64_t>(s.id);
    sum += static_cast<int32_t>(s.retweet_count);
    sum += static_cast<int32_t>(s.favorite_count);
    const User& u = s.user;
    sum += static_cast<int64_t>(u.id);
    sum += static_cast<int32_t>(u.followers_count);
  }
  return sum;
}

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

// ── Checksum verification (runs once at startup) ──────────────────────────────

static const bool kChecksumOk = []() -> bool {
  // ser20 sum
  auto doc = ser20::parse<tw::Twitter>(kJson);
  const std::vector<tw::Status>& ser20_statuses = doc.statuses;
  int64_t ser20_sum = tw::checksum_ser20(ser20_statuses);

  // simdjson sum
  simdjson::ondemand::parser parser;
  auto padded = simdjson::padded_string(kJson);
  auto sdoc = parser.iterate(padded);
  int64_t sj_sum = 0;
  for (auto tweet : sdoc["statuses"]) {
    sj_sum += static_cast<int64_t>(tweet["id"].get_int64());
    sj_sum += static_cast<int64_t>(tweet["retweet_count"].get_int64());
    sj_sum += static_cast<int64_t>(tweet["favorite_count"].get_int64());
    auto user = tweet["user"].get_object();
    sj_sum += static_cast<int64_t>(user["id"].get_int64());
    sj_sum += static_cast<int64_t>(user["followers_count"].get_int64());
  }

  if (ser20_sum != sj_sum) {
    std::cerr << "CHECKSUM MISMATCH: ser20=" << ser20_sum
              << " simdjson=" << sj_sum << "\n";
    std::abort();
  }
  return true;
}();

// ── simdjson (ondemand, sum only) ─────────────────────────────────────────────

static void BM_simdjson_ondemand(benchmark::State& state) {
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

// ── simdjson (ondemand → materialize to vector) ───────────────────────────────

static void BM_simdjson_materialize(benchmark::State& state) {
  simdjson::ondemand::parser parser;
  auto padded = simdjson::padded_string(kJson);

  for (auto _ : state) {
    auto doc = parser.iterate(padded);
    std::vector<tw::StatusPlain> statuses;
    statuses.reserve(128);
    for (auto tweet : doc["statuses"]) {
      tw::StatusPlain s{};
      s.id = static_cast<int64_t>(tweet["id"].get_int64());
      s.retweet_count = static_cast<int32_t>(tweet["retweet_count"].get_int64());
      s.favorite_count =
          static_cast<int32_t>(tweet["favorite_count"].get_int64());
      auto user = tweet["user"].get_object();
      s.user.id = static_cast<int64_t>(user["id"].get_int64());
      s.user.followers_count =
          static_cast<int32_t>(user["followers_count"].get_int64());
      statuses.push_back(s);
    }
    const auto sum = tw::checksum_plain(statuses);
    benchmark::DoNotOptimize(sum);
    benchmark::DoNotOptimize(statuses.data());
    benchmark::ClobberMemory();
  }
  state.SetBytesProcessed(state.iterations() *
                          static_cast<int64_t>(kJson.size()));
}

// ── ser20 ─────────────────────────────────────────────────────────────────────

static void BM_ser20(benchmark::State& state) {
  for (auto _ : state) {
    auto doc = ser20::parse<tw::Twitter>(kJson);
    const std::vector<tw::Status>& statuses = doc.statuses;
    const auto sum = tw::checksum_ser20(statuses);
    benchmark::DoNotOptimize(sum);
    benchmark::DoNotOptimize(statuses.data());
    benchmark::ClobberMemory();
  }
  state.SetBytesProcessed(state.iterations() *
                          static_cast<int64_t>(kJson.size()));
}

// ── ser20 (array, no heap alloc for statuses) ─────────────────────────────────

static void BM_ser20_array(benchmark::State& state) {
  for (auto _ : state) {
    auto doc = ser20::parse<tw::TwitterArray>(kJson);
    const auto& arr =
        static_cast<const std::array<tw::Status, 128>&>(doc.statuses);
    int64_t sum = 0;
    for (const auto& s : arr) {
      sum += static_cast<int64_t>(s.id);
      sum += static_cast<int32_t>(s.retweet_count);
      sum += static_cast<int32_t>(s.favorite_count);
      const tw::User& u = s.user;
      sum += static_cast<int64_t>(u.id);
      sum += static_cast<int32_t>(u.followers_count);
    }
    benchmark::DoNotOptimize(sum);
    benchmark::DoNotOptimize(&doc);
    benchmark::ClobberMemory();
  }
  state.SetBytesProcessed(state.iterations() *
                          static_cast<int64_t>(kJson.size()));
}

// ── yyjson ────────────────────────────────────────────────────────────────────

static void BM_yyjson(benchmark::State& state) {
  for (auto _ : state) {
    yyjson_doc* doc = yyjson_read(kJson.data(), kJson.size(), 0);
    yyjson_val* root = yyjson_doc_get_root(doc);
    yyjson_val* statuses = yyjson_obj_get(root, "statuses");
    int64_t sum = 0;
    yyjson_val* tweet;
    yyjson_arr_iter iter;
    yyjson_arr_iter_init(statuses, &iter);
    while ((tweet = yyjson_arr_iter_next(&iter))) {
      sum += yyjson_get_sint(yyjson_obj_get(tweet, "id"));
      sum += yyjson_get_sint(yyjson_obj_get(tweet, "retweet_count"));
      sum += yyjson_get_sint(yyjson_obj_get(tweet, "favorite_count"));
      yyjson_val* user = yyjson_obj_get(tweet, "user");
      sum += yyjson_get_sint(yyjson_obj_get(user, "id"));
      sum += yyjson_get_sint(yyjson_obj_get(user, "followers_count"));
    }
    benchmark::DoNotOptimize(sum);
    yyjson_doc_free(doc);
    benchmark::ClobberMemory();
  }
  state.SetBytesProcessed(state.iterations() *
                          static_cast<int64_t>(kJson.size()));
}

// ── RapidJSON (DOM) ───────────────────────────────────────────────────────────

static void BM_rapidjson(benchmark::State& state) {
  for (auto _ : state) {
    rapidjson::Document doc;
    doc.Parse(kJson.data(), kJson.size());
    int64_t sum = 0;
    for (auto& tweet : doc["statuses"].GetArray()) {
      sum += tweet["id"].GetInt64();
      sum += tweet["retweet_count"].GetInt();
      sum += tweet["favorite_count"].GetInt();
      const auto& user = tweet["user"];
      sum += user["id"].GetInt64();
      sum += user["followers_count"].GetInt();
    }
    benchmark::DoNotOptimize(sum);
    benchmark::ClobberMemory();
  }
  state.SetBytesProcessed(state.iterations() *
                          static_cast<int64_t>(kJson.size()));
}

// ── nlohmann/json (DOM) ───────────────────────────────────────────────────────

static void BM_nlohmann(benchmark::State& state) {
  for (auto _ : state) {
    auto doc = nlohmann::json::parse(kJson);
    int64_t sum = 0;
    for (const auto& tweet : doc["statuses"]) {
      sum += tweet["id"].get<int64_t>();
      sum += tweet["retweet_count"].get<int32_t>();
      sum += tweet["favorite_count"].get<int32_t>();
      const auto& user = tweet["user"];
      sum += user["id"].get<int64_t>();
      sum += user["followers_count"].get<int32_t>();
    }
    benchmark::DoNotOptimize(sum);
    benchmark::ClobberMemory();
  }
  state.SetBytesProcessed(state.iterations() *
                          static_cast<int64_t>(kJson.size()));
}

// ── glaze (struct-based) ──────────────────────────────────────────────────────

static void BM_glaze(benchmark::State& state) {
  for (auto _ : state) {
    glz_tw::Twitter doc{};
    (void)glz::read<glz::opts{.error_on_unknown_keys = false}>(doc, kJson);
    const auto sum = glz_tw::checksum(doc);
    benchmark::DoNotOptimize(sum);
    benchmark::DoNotOptimize(&doc);
    benchmark::ClobberMemory();
  }
  state.SetBytesProcessed(state.iterations() *
                          static_cast<int64_t>(kJson.size()));
}

BENCHMARK(BM_simdjson_ondemand)->MinTime(2.0);
BENCHMARK(BM_simdjson_materialize)->MinTime(2.0);
BENCHMARK(BM_yyjson)->MinTime(2.0);
BENCHMARK(BM_rapidjson)->MinTime(2.0);
BENCHMARK(BM_nlohmann)->MinTime(2.0);
BENCHMARK(BM_glaze)->MinTime(2.0);
BENCHMARK(BM_ser20)->MinTime(2.0);
BENCHMARK(BM_ser20_array)->MinTime(2.0);
