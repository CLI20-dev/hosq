#include <benchmark/benchmark.h>
#include <yyjson.h>

#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <variant>

#include "hosq/decoder.hh"
#include "hosq/element.hh"
#include "hosq/entry.hh"
#include "hosq/json/fast_reader.hh"
#include "hosq/json/simple_reader.hh"
#include "hosq/pipeline/decode.hh"
#include "hosq/pipeline/match.hh"
#include "hosq/pipeline/scan.hh"
#include "hosq/pipeline/store.hh"
#include "hosq/pipeline_util.hh"
#include "hosq/string_literal.hh"
#include "hosq/try.hh"

using namespace hosq;
using namespace std::string_view_literals;

namespace {

using Reader = hosq::json::SimpleReader;

// Expected sums — computed from twitter.json with uint64 wrapping
constexpr uint64_t kStatusIdSum = 13693999927316377398ULL;
constexpr uint64_t kUserIdSum = 221361100704ULL;
constexpr int kRetweetSum = 7122;
constexpr int kFavoriteSum = 0;

// ─── Custom pipeline steps ────────────────────────────────────────────────────

// Matches any string key, returning bool=true (required for Try<Entry<Key,...>>)
struct AnyBoolImpl {
  template <class Input>
  using storage_type = std::monostate;

  auto operator()(std::string_view) const -> bool { return true; }
};

constexpr auto any_bool = step<AnyBoolImpl>;

// ─── Schema aliases ───────────────────────────────────────────────────────────

template <StringLiteral Name>
using Key = Elem<scan::string | match::literal<Name>, noop_encoder>;

// Catch-all entry: matches any key, skips the associated value.
using SkipKey = Elem<scan::string | any_bool, noop_encoder>;
using SkipVal = Elem<decode::skip, noop_encoder>;
using SkipEntry = Try<Entry<SkipKey, SkipVal>>;

template <class T>
using NumberField =
    Elem<scan::literal | decode::number<T> | store::assign, noop_encoder>;

// ─── Schema structs ───────────────────────────────────────────────────────────

struct UserSchema {
  Try<Entry<Key<"id"_literal>, NumberField<uint64_t>>> id;
  SkipEntry skip;
};

using UserField =
    Elem<scan::object | decode::to<UserSchema> | store::assign, noop_encoder>;

struct StatusSchema {
  Try<Entry<Key<"id"_literal>, NumberField<uint64_t>>> id;
  Try<Entry<Key<"retweet_count"_literal>, NumberField<int>>> retweet_count;
  Try<Entry<Key<"favorite_count"_literal>, NumberField<int>>> favorite_count;
  Try<Entry<Key<"user"_literal>, UserField>> user;
  SkipEntry skip;
};

using StatusesField =
    Elem<scan::array | decode::collect<scan::object | decode::to<StatusSchema>> |
             store::assign,
         noop_encoder>;

struct TwitterSchema {
  Try<Entry<Key<"statuses"_literal>, StatusesField>> statuses;
  SkipEntry skip;
};

// ─── Shared JSON data ─────────────────────────────────────────────────────────

std::string g_json;  // NOLINT

void load_json() {
  std::ifstream f(TWITTER_JSON_PATH);  // NOLINT
  g_json.assign(std::istreambuf_iterator<char>(f), {});
}

// ─── Verification helpers ─────────────────────────────────────────────────────

struct Sums {
  uint64_t status_id{0};
  uint64_t user_id{0};
  int retweet{0};
  int favorite{0};
};

auto collect_hosq() -> Sums {
  TwitterSchema schema;
  const auto result = decode_into<Reader>(schema, std::string_view{g_json});
  if (!result.is_ok()) {
    std::cerr << "hosq parse error: code=" << static_cast<int>(result.code)
              << " offset=" << result.offset << "\n";
    return {};
  }
  Sums s;
  // statuses.value() -> StatusesField (Elem), .value() -> vector<StatusSchema>
  for (const auto& st : schema.statuses.value().value()) {
    // id.value() -> NumberField Elem, .value() -> uint64_t
    s.status_id += st.id.value().value();
    // user.value() -> UserField Elem, .value() -> UserSchema
    s.user_id += st.user.value().value().id.value().value();
    s.retweet += st.retweet_count.value().value();
    s.favorite += st.favorite_count.value().value();
  }
  return s;
}

auto collect_hosq_fast() -> Sums {
  using FastReader = hosq::json::FastReader<>;
  TwitterSchema schema;
  const auto result = decode_into<FastReader>(schema, std::string_view{g_json});
  if (!result.is_ok()) {
    std::cerr << "hosq fast parse error: code=" << static_cast<int>(result.code)
              << " offset=" << result.offset << "\n";
    return {};
  }
  Sums s;
  for (const auto& st : schema.statuses.value().value()) {
    s.status_id += st.id.value().value();
    s.user_id += st.user.value().value().id.value().value();
    s.retweet += st.retweet_count.value().value();
    s.favorite += st.favorite_count.value().value();
  }
  return s;
}

auto collect_yyjson() -> Sums {
  yyjson_doc* doc = yyjson_read(g_json.data(), g_json.size(), 0);
  if (!doc) {
    std::cerr << "yyjson parse failed\n";
    return {};
  }
  Sums s;
  yyjson_val* root = yyjson_doc_get_root(doc);
  yyjson_val* arr = yyjson_obj_get(root, "statuses");
  size_t idx = 0, max = 0;
  yyjson_val* st = nullptr;
  yyjson_arr_foreach(arr, idx, max, st) {
    s.status_id += yyjson_get_uint(yyjson_obj_get(st, "id"));
    s.user_id +=
        yyjson_get_uint(yyjson_obj_get(yyjson_obj_get(st, "user"), "id"));
    s.retweet +=
        static_cast<int>(yyjson_get_int(yyjson_obj_get(st, "retweet_count")));
    s.favorite +=
        static_cast<int>(yyjson_get_int(yyjson_obj_get(st, "favorite_count")));
  }
  yyjson_doc_free(doc);
  return s;
}

}  // namespace

// ─── Benchmarks ───────────────────────────────────────────────────────────────

static void bm_hosq_twitter(benchmark::State& state) {
  for (auto _ : state) {
    TwitterSchema schema;
    const auto result = decode_into<Reader>(schema, std::string_view{g_json});
    benchmark::DoNotOptimize(schema);
    if (!result.is_ok()) {
      state.SkipWithError("hosq parse failed");
      break;
    }
  }
  state.SetBytesProcessed(state.iterations() *
                          static_cast<int64_t>(g_json.size()));
}

static void bm_hosq_twitter_fast(benchmark::State& state) {
  using FastReader = hosq::json::FastReader<>;
  for (auto _ : state) {
    TwitterSchema schema;
    const auto result =
        decode_into<FastReader>(schema, std::string_view{g_json});
    benchmark::DoNotOptimize(schema);
    if (!result.is_ok()) {
      state.SkipWithError("hosq fast parse failed");
      break;
    }
  }
  state.SetBytesProcessed(state.iterations() *
                          static_cast<int64_t>(g_json.size()));
}

static void bm_yyjson_twitter(benchmark::State& state) {
  for (auto _ : state) {
    Sums s;
    yyjson_doc* doc = yyjson_read(g_json.data(), g_json.size(), 0);
    if (!doc) {
      state.SkipWithError("yyjson parse failed");
      break;
    }
    yyjson_val* root = yyjson_doc_get_root(doc);
    yyjson_val* arr = yyjson_obj_get(root, "statuses");
    size_t idx = 0, max = 0;
    yyjson_val* st = nullptr;
    yyjson_arr_foreach(arr, idx, max, st) {
      bool has_id = false, has_retweet = false, has_favorite = false,
           has_user = false;
      yyjson_val* key = nullptr;
      yyjson_val* val = nullptr;
      yyjson_obj_iter iter;
      yyjson_obj_iter_init(st, &iter);
      while ((key = yyjson_obj_iter_next(&iter))) {
        val = yyjson_obj_iter_get_val(key);
        const char* k = yyjson_get_str(key);
        if (!has_id && strcmp(k, "id") == 0) {
          s.status_id += yyjson_get_uint(val);
          has_id = true;
        } else if (!has_retweet && strcmp(k, "retweet_count") == 0) {
          s.retweet += static_cast<int>(yyjson_get_int(val));
          has_retweet = true;
        } else if (!has_favorite && strcmp(k, "favorite_count") == 0) {
          s.favorite += static_cast<int>(yyjson_get_int(val));
          has_favorite = true;
        } else if (!has_user && strcmp(k, "user") == 0) {
          s.user_id += yyjson_get_uint(yyjson_obj_get(val, "id"));
          has_user = true;
        }
      }
    }
    benchmark::DoNotOptimize(s);
    yyjson_doc_free(doc);
  }
  state.SetBytesProcessed(state.iterations() *
                          static_cast<int64_t>(g_json.size()));
}

BENCHMARK(bm_yyjson_twitter);
BENCHMARK(bm_hosq_twitter);
BENCHMARK(bm_hosq_twitter_fast);

auto main(int argc, char** argv) -> int {
  load_json();

  auto s1 = collect_hosq();
  std::cout << "[hosq]  status_id_sum=" << s1.status_id
            << "  user_id_sum=" << s1.user_id << "  retweet_sum=" << s1.retweet
            << "  favorite_sum=" << s1.favorite << "\n";
  if (s1.status_id != kStatusIdSum || s1.user_id != kUserIdSum ||
      s1.retweet != kRetweetSum || s1.favorite != kFavoriteSum) {
    std::cerr << "[hosq]  verification FAILED\n";
    return 1;
  }
  std::cout << "[hosq]  verification PASSED\n";

  auto sf = collect_hosq_fast();
  std::cout << "[hosq/fast]  status_id_sum=" << sf.status_id
            << "  user_id_sum=" << sf.user_id << "  retweet_sum=" << sf.retweet
            << "  favorite_sum=" << sf.favorite << "\n";
  if (sf.status_id != kStatusIdSum || sf.user_id != kUserIdSum ||
      sf.retweet != kRetweetSum || sf.favorite != kFavoriteSum) {
    std::cerr << "[hosq/fast]  verification FAILED\n";
    return 1;
  }
  std::cout << "[hosq/fast]  verification PASSED\n";

  auto s2 = collect_yyjson();
  std::cout << "[yyjson] status_id_sum=" << s2.status_id
            << "  user_id_sum=" << s2.user_id << "  retweet_sum=" << s2.retweet
            << "  favorite_sum=" << s2.favorite << "\n";
  if (s2.status_id != kStatusIdSum || s2.user_id != kUserIdSum ||
      s2.retweet != kRetweetSum || s2.favorite != kFavoriteSum) {
    std::cerr << "[yyjson] verification FAILED\n";
    return 1;
  }
  std::cout << "[yyjson] verification PASSED\n\n";

  ::benchmark::Initialize(&argc, argv);
  ::benchmark::RunSpecifiedBenchmarks();
  ::benchmark::Shutdown();
  return 0;
}
