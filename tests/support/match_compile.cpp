// Each HOSQ_MATCH_CASE is compiled separately by check_match_boundaries.py.
// Keep positive controls in this same translation unit: a broken include or
// matcher definition must not make every negative case appear to pass.
#include <concepts>
#include <string>
#include <tuple>
#include <type_traits>
#include <vector>

#include "hosq/hosq.hh"
#include "hosq/json/fast_reader.hh"
#include "hosq/json/simple_reader.hh"

using namespace hosq;
namespace v = hosq::types::value;
using Reader = json::SimpleReader;
struct FalsePayload {
  auto operator()(int) const -> bool { return false; }
};
struct RawBoolSource {
  auto operator()() const -> bool { return false; }
};
template <class R>
inline constexpr bool is_fast_reader = false;
template <std::size_t D, json::InputPadding P, json::WhitespaceMode M>
inline constexpr bool is_fast_reader<json::FastReader<D, P, M>> = true;
struct ReaderSpecificMatcher {
  template <class Context>
  auto operator()(int input, const Context& ctx) const {
    if constexpr (is_fast_reader<typename Context::reader_type>)
      return decltype(match::literal<1>)::invoke(input, ctx);
    else
      return input;
  }
};
using ReaderSpecificValue = Elem<scan::scalar | decode::integer<int> |
                                     step<ReaderSpecificMatcher> | store::assign,
                                 encode::value>;
inline constexpr auto integer_match =
    scan::scalar | decode::integer<int> | match::literal<1>;
inline constexpr auto text_match =
    scan::scalar | decode::text |
    match::predicate<[](const std::string& s) { return s == "n"; }>;
using Candidate = Elem<integer_match | store::assign, encode::value>;
using KeyCandidate = Elem<text_match | store::assign, encode::value>;
using EachCandidate =
    Elem<scan::array | decode::each<integer_match | store::assign>,
         noop_encoder>;

int main() {
#if HOSQ_MATCH_CASE == 1
  Candidate value;
  (void)decode_into<Reader>(value, "1");
#elif HOSQ_MATCH_CASE == 2
  Candidate value;
  (void)decode_document_into<Reader>(value, "1");
#elif HOSQ_MATCH_CASE == 3
  Candidate value;
  Decoder<Reader> decoder;
  (void)decoder.decode(value, "1");
#elif HOSQ_MATCH_CASE == 4
  Candidate value;
  Decoder<Reader> decoder;
  (void)decoder.decode_document(value, "1");
#elif HOSQ_MATCH_CASE == 5
  Candidate value;
  Reader reader;
  Reader::state_type state;
  reader.reset(state, "1");
  (void)reader.next_token(state);
  (void)Decoder<Reader>::decode_nested(value, reader, state);
#elif HOSQ_MATCH_CASE == 6
  struct Fixed {
    Candidate value;
  } value;
  (void)decode_into<Reader>(value, "[1]");
#elif HOSQ_MATCH_CASE == 7
  struct Fixed {
    Entry<Key<"n">, Candidate> value;
  } value;
  (void)decode_into<Reader>(value, R"({"n":1})");
#elif HOSQ_MATCH_CASE == 8
  struct Fixed {
    Entry<KeyCandidate, v::Int64> value;
  } value;
  (void)decode_into<Reader>(value, R"({"n":1})");
#elif HOSQ_MATCH_CASE == 9
  v::Array<Candidate> value;
  (void)decode_into<Reader>(value, "[1]");
#elif HOSQ_MATCH_CASE == 10
  v::Mapping<KeyCandidate, v::Int64> value;
  (void)decode_into<Reader>(value, R"({"n":1})");
#elif HOSQ_MATCH_CASE == 11
  v::Mapping<v::Text, Candidate> value;
  (void)decode_into<Reader>(value, R"({"n":1})");
#elif HOSQ_MATCH_CASE == 12
  struct Schema {
    Entry<Key<"n">, Candidate> value;
  };
  v::Record<Schema> value;
  (void)decode_into<Reader>(value, R"({"n":1})");
#elif HOSQ_MATCH_CASE == 13
  v::Optional<Candidate> value;
  (void)decode_into<Reader>(value, "1");
#elif HOSQ_MATCH_CASE == 14
  using E = Elem<scan::array | decode::collect<integer_match> | store::assign,
                 noop_encoder>;
  E value;
  (void)decode_into<Reader>(value, "[1]");
#elif HOSQ_MATCH_CASE == 15
  using E = Elem<scan::array | decode::collect<integer_match, 1> | store::assign,
                 noop_encoder>;
  E value;
  (void)decode_into<Reader>(value, "[1]");
#elif HOSQ_MATCH_CASE == 16
  using E =
      Elem<scan::object |
               decode::collect<text_match, scan::scalar | decode::integer<int>> |
               store::assign,
           noop_encoder>;
  E value;
  (void)decode_into<Reader>(value, R"({"n":1})");
#elif HOSQ_MATCH_CASE == 17
  using E =
      Elem<scan::object |
               decode::collect<scan::scalar | decode::text, integer_match> |
               store::assign,
           noop_encoder>;
  E value;
  (void)decode_into<Reader>(value, R"({"n":1})");
#elif HOSQ_MATCH_CASE == 18
  using E = Elem<
      scan::object |
          decode::collect<text_match, scan::scalar | decode::integer<int>, 1> |
          store::assign,
      noop_encoder>;
  E value;
  (void)decode_into<Reader>(value, R"({"n":1})");
#elif HOSQ_MATCH_CASE == 19
  using E =
      Elem<scan::object |
               decode::collect<scan::scalar | decode::text, integer_match, 1> |
               store::assign,
           noop_encoder>;
  E value;
  (void)decode_into<Reader>(value, R"({"n":1})");
#elif HOSQ_MATCH_CASE == 20
  MatchResult<int> result{true, 1};
  (void)result;
#elif HOSQ_MATCH_CASE == 21
  MatchResult<void> result{true};
  (void)result;
#elif HOSQ_MATCH_CASE == 22
  struct Context {
  } ctx;
  const auto result = decltype(match::literal<1>)::invoke(1, ctx);
  bool discarded_status = result;
  (void)discarded_status;
#elif HOSQ_MATCH_CASE == 23
  MatchResult<int> result{};
  (void)result;
#elif HOSQ_MATCH_CASE == 24
  using RawBool = Elem<step<RawBoolSource>, noop_encoder>;
  RawBool value;
  (void)value;
#elif HOSQ_MATCH_CASE == 25
  EachCandidate value;
  (void)decode_into<Reader>(value, "[1]");
#elif HOSQ_MATCH_CASE == 26
  using E = Elem<scan::array | decode::each<integer_match | store::assign, 1>,
                 noop_encoder>;
  E value;
  (void)decode_document_into<Reader>(value, "[1]");
#elif HOSQ_MATCH_CASE == 27
  struct Schema {
    Try<Entry<Key<"n">, Candidate>> value;
  };
  v::Record<Schema> value;
  (void)decode_into<Reader>(value, R"({"n":1})");
#elif HOSQ_MATCH_CASE == 28
  using BadPredicate =
      Elem<scan::scalar | decode::integer<int> |
               match::predicate<[](int n) { return n; }> | store::assign,
           noop_encoder>;
  BadPredicate value;
  (void)value;
#elif HOSQ_MATCH_CASE == 29
  using BoolDataAfterMatch = decltype(integer_match | step<FalsePayload>);
  using E =
      Elem<scan::array | decode::collect<BoolDataAfterMatch{}> | store::assign,
           noop_encoder>;
  E value;
  (void)decode_into<Reader>(value, "[1]");
#elif HOSQ_MATCH_CASE == 30
  struct Schema {
    EachCandidate value;
  } value;
  (void)decode_document_into<Reader>(value, "[[1]]");
#elif HOSQ_MATCH_CASE == 31
  using E =
      Elem<scan::object |
               decode::each<text_match | store::assign,
                            scan::scalar | decode::integer<int> | store::assign>,
           noop_encoder>;
  E value;
  (void)decode_into<Reader>(value, R"({"n":1})");
#elif HOSQ_MATCH_CASE == 32
  using E = Elem<scan::object |
                     decode::each<scan::scalar | decode::text | store::assign,
                                  integer_match | store::assign>,
                 noop_encoder>;
  E value;
  (void)decode_into<Reader>(value, R"({"n":1})");
#elif HOSQ_MATCH_CASE == 33
  ReaderSpecificValue value;
  (void)decode_into<json::FastReader<>>(value, "1");
#elif HOSQ_MATCH_CASE == 34
  struct Fixed {
    Entry<Key<"n">, ReaderSpecificValue> value;
  } value;
  (void)decode_into<json::FastReader<>>(value, R"({"n":1})");
#elif HOSQ_MATCH_CASE == 35
  v::Array<ReaderSpecificValue> value;
  (void)decode_into<json::FastReader<>>(value, "[1]");
#elif HOSQ_MATCH_CASE == 36
  using E = Elem<scan::array |
                     decode::collect<scan::scalar | decode::integer<int> |
                                     step<ReaderSpecificMatcher>> |
                     store::assign,
                 noop_encoder>;
  E value;
  (void)decode_into<json::FastReader<>>(value, "[1]");
#elif HOSQ_MATCH_CASE == 37
  MatchResult<int> result{std::nullopt};
  (void)result;
#elif HOSQ_MATCH_CASE == 38
  MatchResult<void> result{};
  (void)result;
#elif HOSQ_MATCH_CASE == 39
  using OptionalCandidate = Elem<integer_match | store::once, encode::value>;
  struct Schema {
    types::Field<"n", OptionalCandidate, types::presence::Optional> value;
  } value;
  (void)decode_into<Reader>(value, R"({"n":1})");
#elif HOSQ_MATCH_CASE == 40
  using OptionalCandidate = Elem<integer_match | store::once, encode::value>;
  struct Schema {
    types::Field<"n", OptionalCandidate, types::presence::Optional> value;
    Try<Entry<Key<"m">, v::Int64>> other;
  } value;
  (void)decode_into<Reader>(value, R"({"n":1,"m":2})");
#elif HOSQ_MATCH_CASE == 41
  using RawBool = Elem<scan::scalar | decode::integer<int> | store::assign |
                           step<RawBoolSource>,
                       noop_encoder>;
  RawBool value;
  (void)value;
#elif HOSQ_MATCH_CASE == 42
  using OptionalReaderSpecific =
      Elem<scan::scalar | decode::integer<int> | step<ReaderSpecificMatcher> |
               store::once,
           encode::value>;
  struct Schema {
    types::Field<"n", OptionalReaderSpecific, types::presence::Optional> value;
  } value;
  (void)decode_into<json::FastReader<>>(value, R"({"n":1})");
#elif HOSQ_MATCH_CASE == 100
  struct Choices {
    Try<Candidate> one;
    Try<v::Int64> fallback;
  } value;
  const auto error = decode_document_into<Reader>(value, "[1,2]");
  return !error.is_ok() || value.one.value() != 1 || value.fallback.value() != 2;
#elif HOSQ_MATCH_CASE == 101
  using Validated = Elem<scan::scalar | decode::integer<int> | validate::min<1> |
                             store::assign,
                         encode::value>;
  Validated value;
  if (!decode_document_into<Reader>(value, "2").is_ok() || value.value() != 2)
    return 1;
  return decode_document_into<Reader>(value, "0").is_ok();
#elif HOSQ_MATCH_CASE == 102
  using Bools =
      Elem<scan::array | decode::collect<scan::scalar | decode::typed_boolean> |
               store::assign,
           encode::value>;
  Bools value;
  const auto error = decode_document_into<Reader>(value, "[false,true,false]");
  return !error.is_ok() ||
         value.value() != std::vector<bool>{false, true, false};
#elif HOSQ_MATCH_CASE == 103
  struct Choices {
    Try<EachCandidate> one;
  } value;
  const auto error = decode_document_into<Reader>(value, "[[1,1]]");
  return !error.is_ok() || value.one.value() != std::vector<int>{1, 1};
#elif HOSQ_MATCH_CASE == 104
  ReaderSpecificValue ordinary;
  if (!decode_document_into<Reader>(ordinary, "1").is_ok() ||
      ordinary.value() != 1)
    return 1;
  struct Fixed {
    Entry<Key<"n">, v::Int64> value;
  } fast;
  const auto error =
      decode_document_into<json::FastReader<>>(fast, R"({"n":2})");
  return !error.is_ok() || fast.value.value().value() != 2;
#elif HOSQ_MATCH_CASE == 105
  struct Context {
  } ctx;
  const auto result =
      decltype(match::literal<1> | step<FalsePayload>)::invoke(1, ctx);
  static_assert(
      std::same_as<std::remove_cvref_t<decltype(result)>, MatchResult<bool>>);
  return !result.matched() || result.value();
#elif HOSQ_MATCH_CASE == 106
  using BoolValue = Elem<step<RawBoolSource> | store::assign, encode::value>;
  BoolValue value;
  const auto error = decode_document_into<Reader>(value, "false");
  return !error.is_ok() || value.value();
#elif HOSQ_MATCH_CASE == 107
  struct Schema {
    types::Optional<"n", v::Int64> value;
  } value;
  if (!decode_document_into<Reader>(value, "{}").is_ok()) return 1;
  const auto error =
      decode_document_into<json::FastReader<>>(value, R"({"n":2})");
  return !error.is_ok() || value.value.value().value() != 2;
#else
#error Unknown HOSQ_MATCH_CASE
#endif
}
