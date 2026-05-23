#include <fstream>
#include <string>
#include <string_view>
#include <variant>

#include "hosq/decoder.hh"
#include "hosq/element.hh"
#include "hosq/entry.hh"
#include "hosq/json/fast_reader.hh"
#include "hosq/pipeline/decode.hh"
#include "hosq/pipeline/match.hh"
#include "hosq/pipeline/scan.hh"
#include "hosq/pipeline/store.hh"
#include "hosq/pipeline_util.hh"
#include "hosq/string_literal.hh"
#include "hosq/try.hh"

using namespace hosq;

struct AnyBoolImpl {
  template <class Input>
  using storage_type = std::monostate;
  auto operator()(std::string_view) const -> bool { return true; }
};
constexpr auto any_bool = step<AnyBoolImpl>;

struct SkipValueImpl {
  template <class Input>
  using storage_type = std::monostate;

  template <class Storage, ReaderConcept R, TryPhase Phase>
  auto operator()(DecodeContext<Storage, R, Phase>& ctx) const -> void {
    auto& reader = ctx.reader();
    auto& state = ctx.reader_state();
    if constexpr (Phase == TryPhase::normal) {
      if (!reader.next_token(state)) return;
    }
    const auto& tok = reader.current_token(state);
    if (tok.type == TokenType::Scalar) return;
    if (tok.type == TokenType::ObjectBegin ||
        tok.type == TokenType::ArrayBegin) {
      int depth = 1;
      while (depth > 0) {
        if (!reader.next_token(state)) return;
        const auto& t = reader.current_token(state);
        if (t.type == TokenType::ObjectBegin || t.type == TokenType::ArrayBegin)
          ++depth;
        else if (t.type == TokenType::ObjectEnd || t.type == TokenType::ArrayEnd)
          --depth;
      }
      return;
    }
    reader.report_error(
        state, Error{.code = ErrorCode::type_mismatch, .offset = tok.offset});
  }
};
constexpr auto skip_value = step<SkipValueImpl>;

// ─── Schema ───────────────────────────────────────────────────────────────────

struct UserSchema {
  Try<Entry<Elem<scan::string | match::literal<"id"_literal>, noop_encoder>,
            Elem<scan::literal | decode::number<uint64_t> | store::assign,
                 noop_encoder>>>
      id;
  Try<Entry<Elem<scan::string | any_bool, noop_encoder>,
            Elem<skip_value, noop_encoder>>>
      skip;
};

struct StatusSchema {
  Try<Entry<Elem<scan::string | match::literal<"id"_literal>, noop_encoder>,
            Elem<scan::literal | decode::number<uint64_t> | store::assign,
                 noop_encoder>>>
      id;
  Try<Entry<
      Elem<scan::string | match::literal<"retweet_count"_literal>, noop_encoder>,
      Elem<scan::literal | decode::number<int> | store::assign, noop_encoder>>>
      retweet_count;
  Try<Entry<
      Elem<scan::string | match::literal<"favorite_count"_literal>,
           noop_encoder>,
      Elem<scan::literal | decode::number<int> | store::assign, noop_encoder>>>
      favorite_count;
  Try<Entry<
      Elem<scan::string | match::literal<"user"_literal>, noop_encoder>,
      Elem<scan::object | decode::to<UserSchema> | store::assign, noop_encoder>>>
      user;
  Try<Entry<Elem<scan::string | any_bool, noop_encoder>,
            Elem<skip_value, noop_encoder>>>
      _skip;
};

struct TwitterSchema {
  Try<Entry<
      Elem<scan::string | match::literal<"statuses"_literal>, noop_encoder>,
      Elem<scan::array |
               decode::collect<scan::object | decode::to<StatusSchema>> |
               store::assign,
           noop_encoder>>>
      statuses;
  Try<Entry<Elem<scan::string | any_bool, noop_encoder>,
            Elem<skip_value, noop_encoder>>>
      _skip;
};

// ─── Main ─────────────────────────────────────────────────────────────────────

auto main(int argc, char** argv) -> int {
  using Reader = hosq::json::FastReader<>;

  const char* json_path = TWITTER_JSON_PATH;
  int iterations = 1;
  if (argc >= 2) json_path = argv[1];
  if (argc >= 3) iterations = std::stoi(argv[2]);

  std::ifstream f(json_path);
  if (!f) return 1;
  const std::string json{std::istreambuf_iterator<char>(f), {}};

  for (int i = 0; i < iterations; ++i) {
    TwitterSchema s;
    if (!decode_into<Reader>(s, std::string_view{json})) return 1;
  }
  return 0;
}
