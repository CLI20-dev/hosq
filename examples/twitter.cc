#include <fstream>
#include <iostream>
#include <string>
#include <string_view>

#include "hosq/decoder.hh"
#include "hosq/element.hh"
#include "hosq/entry.hh"
#include "hosq/json/fast_reader.hh"
#include "hosq/pipeline/decode.hh"
#include "hosq/pipeline/scan.hh"
#include "hosq/pipeline/store.hh"
#include "hosq/pipeline_util.hh"
#include "hosq/string_literal.hh"
#include "hosq/try.hh"

using namespace hosq;

// ─── Schema ───────────────────────────────────────────────────────────────────

struct UserSchema {
  Try<Entry<Key<"id">,
            Elem<scan::scalar | decode::number<uint64_t> | store::assign,
                 noop_encoder>>>
      id;
  Try<Entry<Elem<scan::scalar | decode::skip, noop_encoder>,
            Elem<decode::skip, noop_encoder>>>
      skip;
};

struct StatusSchema {
  Try<Entry<Key<"id">,
            Elem<scan::scalar | decode::number<uint64_t> | store::assign,
                 noop_encoder>>>
      id;
  Try<Entry<
      Key<"retweet_count">,
      Elem<scan::scalar | decode::number<int> | store::assign, noop_encoder>>>
      retweet_count;
  Try<Entry<
      Key<"favorite_count">,
      Elem<scan::scalar | decode::number<int> | store::assign, noop_encoder>>>
      favorite_count;
  Try<Entry<
      Key<"user">,
      Elem<scan::object | decode::to<UserSchema> | store::assign, noop_encoder>>>
      user;
  Try<Entry<Elem<scan::scalar | decode::skip, noop_encoder>,
            Elem<decode::skip, noop_encoder>>>
      skip;
};

struct TwitterSchema {
  Try<Entry<Key<"statuses">,
            Elem<scan::array |
                     decode::collect<scan::object | decode::to<StatusSchema>> |
                     store::assign,
                 noop_encoder>>>
      statuses;
  Try<Entry<Elem<scan::scalar | decode::skip, noop_encoder>,
            Elem<decode::skip, noop_encoder>>>
      skip;
};

// ─── Main ─────────────────────────────────────────────────────────────────────

auto main(int argc, char** argv) -> int {
  using Reader = hosq::json::FastReader<>;

  const char* json_path = TWITTER_JSON_PATH;
  int iterations = 1000;
  if (argc >= 2) json_path = argv[1];
  if (argc >= 3) iterations = std::stoi(argv[2]);

  std::ifstream f(json_path);
  if (!f) return 1;
  const std::string json{std::istreambuf_iterator<char>(f), {}};

  for (int i = 0; i < iterations; ++i) {
    TwitterSchema s;
    const auto err = decode_into<Reader>(s, std::string_view{json});
    if (!err.is_ok()) {
      return 1;
    }
  }
  return 0;
}
