#include <benchmark/benchmark.h>
#include <yyjson.h>

#include <fstream>
#include <iostream>
#include <string>
#include <string_view>

#include "ser20/decoder.hh"
#include "ser20/element.hh"
#include "ser20/pipeline/decode.hh"
#include "ser20/pipeline/match.hh"
#include "ser20/pipeline/scan.hh"
#include "ser20/pipeline/store.hh"
#include "ser20/pipeline_util.hh"
#include "ser20/string_literal.hh"

using namespace ser20;
using namespace std::string_view_literals;

namespace {

// Expected sums — computed from twitter.json with uint64 wrapping
constexpr uint64_t kStatusIdSum = 13693999927316377626ULL;
constexpr uint64_t kUserIdSum   = 221361100704ULL;
constexpr int      kRetweetSum  = 7122;
constexpr int      kFavoriteSum = 0;

// ─── skip_any_value ─────────────────────────────────────────────────────────
// A single decoder step that consumes any JSON value (scalar, object, array)
// and stores nothing. Used as the catch-all value sink for unknown fields.
struct SkipAnyValueImpl {
    template <class>
    using storage_type = std::monostate;

    template <class Context>
    requires(!std::is_const_v<std::remove_reference_t<Context>>)
    auto operator()(Context& ctx) const -> Result<void, Error> {
        auto& reader = ctx.reader();
        const auto& tok = reader.current_token();
        if (tok.type == TokenType::ScalarBegin) {
            auto r = scan::detail::scan_scalar_for(reader, tok);
            if (!r) return unexpected(r.error());
            return {};
        }
        if (tok.type == TokenType::ObjectBegin) {
            auto r = scan::detail::scan_object_for(reader, tok);
            if (!r) return unexpected(r.error());
            return {};
        }
        if (tok.type == TokenType::ArrayBegin) {
            auto r = scan::detail::scan_array_for(reader, tok);
            if (!r) return unexpected(r.error());
            return {};
        }
        return unexpected(Error{.code = ErrorCode::type_mismatch});
    }
};

constexpr auto skip_any_value = step<SkipAnyValueImpl>;

// ─── Schema ──────────────────────────────────────────────────────────────────
// Each struct must have specific entries first; the _skip catch-all must be
// last so match::any only fires after all named keys have been tried.

using SkipEntry = Entry<
    Elem<scan::string | match::any | store::ignore, noop_encoder>,
    Elem<skip_any_value, noop_encoder>>;

struct UserSchema {
    Entry<
        Elem<scan::string | match::literal<"id"_literal>, noop_encoder>,
        Elem<scan::literal | decode::number<uint64_t> | store::once, noop_encoder>>
        id;
    SkipEntry _skip;
};

struct StatusSchema {
    Entry<
        Elem<scan::string | match::literal<"id"_literal>, noop_encoder>,
        Elem<scan::literal | decode::number<uint64_t> | store::once, noop_encoder>>
        id;
    Entry<
        Elem<scan::string | match::literal<"retweet_count"_literal>, noop_encoder>,
        Elem<scan::literal | decode::number<int> | store::once, noop_encoder>>
        retweet_count;
    Entry<
        Elem<scan::string | match::literal<"favorite_count"_literal>, noop_encoder>,
        Elem<scan::literal | decode::number<int> | store::once, noop_encoder>>
        favorite_count;
    Entry<
        Elem<scan::string | match::literal<"user"_literal>, noop_encoder>,
        UserSchema>
        user;
    SkipEntry _skip;
};

struct TwitterSchema {
    Entry<
        Elem<scan::string | match::literal<"statuses"_literal>, noop_encoder>,
        Elem<decode::vector<StatusSchema> | store::once, noop_encoder>>
        statuses;
    SkipEntry _skip;
};

// ─── Shared JSON data ────────────────────────────────────────────────────────

std::string g_json;

void load_json() {
    std::ifstream f(TWITTER_JSON_PATH);
    g_json.assign(std::istreambuf_iterator<char>(f), {});
}

// ─── Verification helpers ────────────────────────────────────────────────────

struct Sums {
    uint64_t status_id{0};
    uint64_t user_id{0};
    int      retweet{0};
    int      favorite{0};
};

Sums collect_ser20() {
    TwitterSchema schema;
    auto result = decode_into(schema, std::string_view{g_json});
    if (!result) {
        std::cerr << "ser20 parse error: code=" << static_cast<int>(result.error().code)
                  << " offset=" << result.error().offset << "\n";
        return {};
    }
    Sums s;
    for (const auto& st : schema.statuses.value().value()) {
        s.status_id += st.id.value().value();
        s.user_id   += st.user.value().id.value().value();
        s.retweet   += st.retweet_count.value().value();
        s.favorite  += st.favorite_count.value().value();
    }
    return s;
}

Sums collect_yyjson() {
    yyjson_doc* doc = yyjson_read(g_json.data(), g_json.size(), 0);
    if (!doc) {
        std::cerr << "yyjson parse failed\n";
        return {};
    }
    Sums s;
    yyjson_val* root = yyjson_doc_get_root(doc);
    yyjson_val* arr  = yyjson_obj_get(root, "statuses");
    size_t idx, max;
    yyjson_val* st;
    yyjson_arr_foreach(arr, idx, max, st) {
        s.status_id += yyjson_get_uint(yyjson_obj_get(st, "id"));
        s.user_id   += yyjson_get_uint(yyjson_obj_get(yyjson_obj_get(st, "user"), "id"));
        s.retweet   += static_cast<int>(yyjson_get_int(yyjson_obj_get(st, "retweet_count")));
        s.favorite  += static_cast<int>(yyjson_get_int(yyjson_obj_get(st, "favorite_count")));
    }
    yyjson_doc_free(doc);
    return s;
}

}  // namespace

// ─── Benchmarks ──────────────────────────────────────────────────────────────

static void BM_ser20_twitter(benchmark::State& state) {
    for (auto _ : state) {
        TwitterSchema schema;
        auto result = decode_into(schema, std::string_view{g_json});
        benchmark::DoNotOptimize(schema);
        if (!result) {
            state.SkipWithError("ser20 parse failed");
            break;
        }
    }
    state.SetBytesProcessed(state.iterations() * static_cast<int64_t>(g_json.size()));
}

static void BM_yyjson_twitter(benchmark::State& state) {
    for (auto _ : state) {
        yyjson_doc* doc = yyjson_read(g_json.data(), g_json.size(), 0);
        benchmark::DoNotOptimize(doc);
        yyjson_doc_free(doc);
        if (!doc) {
            state.SkipWithError("yyjson parse failed");
            break;
        }
    }
    state.SetBytesProcessed(state.iterations() * static_cast<int64_t>(g_json.size()));
}

BENCHMARK(BM_ser20_twitter);
BENCHMARK(BM_yyjson_twitter);

int main(int argc, char** argv) {
    load_json();

    // Verify ser20 correctness by comparing extracted sums
    auto s1 = collect_ser20();
    std::cout << "[ser20]  status_id_sum=" << s1.status_id
              << "  user_id_sum=" << s1.user_id
              << "  retweet_sum=" << s1.retweet
              << "  favorite_sum=" << s1.favorite << "\n";
    if (s1.status_id != kStatusIdSum || s1.user_id != kUserIdSum ||
        s1.retweet != kRetweetSum || s1.favorite != kFavoriteSum) {
        std::cerr << "[ser20]  verification FAILED\n";
        return 1;
    }
    std::cout << "[ser20]  verification PASSED\n";

    // Verify yyjson correctness with the same sums
    auto s2 = collect_yyjson();
    std::cout << "[yyjson] status_id_sum=" << s2.status_id
              << "  user_id_sum=" << s2.user_id
              << "  retweet_sum=" << s2.retweet
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
