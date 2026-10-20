#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include "hosq/bson/reader.hh"
#include "hosq/bson/writer.hh"
#include "hosq/hosq.hh"
#include "hosq/json/compact_writer.hh"
#include "hosq/json/fast_reader.hh"
#include "hosq/json/simple_reader.hh"
#include "hosq/msgpack/reader.hh"
#include "hosq/msgpack/writer.hh"
#include "hosq/types.hh"

namespace {
using namespace hosq;
namespace v = hosq::types::value;
using EC = ErrorCode;

struct Odd {
  auto operator()(int value) const -> bool { return value % 2 != 0; }
};
struct Even {
  auto operator()(int value) const -> bool { return value % 2 == 0; }
};
struct Accept {
  auto operator()(int) const -> bool { return true; }
};
template <class Predicate>
using Candidate = Elem<scan::scalar | decode::number<int> |
                           match::predicate<Predicate{}> | store::assign,
                       encode::value>;

template <class W = json::CompactWriter<>, class T>
auto write_try(const T& value) -> std::string {
  std::string output;
  W writer(output);
  EXPECT_TRUE(encode_into(value, writer)) << writer.error().detail;
  return output;
}

template <class R>
void check_bool_dispatch() {
  struct Split {
    types::Try<Candidate<Odd>> odd;
    types::Try<Candidate<Even>> even;
  } value;
  const auto error = decode_into<R>(value, "[1,2,3]");
  ASSERT_TRUE(error.is_ok()) << error.detail;
  EXPECT_EQ(value.odd.value(), 3);
  EXPECT_EQ(value.even.value(), 2);
  // Try dispatch stores the last match; encoding visits every declared field.
  EXPECT_EQ(write_try(value), "[3,2]");
}

TEST(TrySemantics, BoolCandidatesDispatchAndAssignWithoutFilteringEncoding) {
  check_bool_dispatch<json::SimpleReader>();
  check_bool_dispatch<json::FastReader<>>();
}

TEST(TrySemantics, FirstSuccessfulCandidateWinsInDeclarationOrder) {
  struct Overlap {
    types::Try<Candidate<Accept>> first;
    types::Try<Candidate<Accept>> second;
  } value;
  ASSERT_TRUE(decode_into<json::FastReader<>>(value, "[1,2,3]").is_ok());
  EXPECT_EQ(value.first.value(), 3);
  EXPECT_EQ(value.second.value(), 0);
  EXPECT_EQ(write_try(value), "[3,0]");
}

template <class R>
void check_typed_int_claims() {
  struct Values {
    types::Try<v::Int64> number;
    types::Try<v::String> text;
  } value;
  value.text.value() = "unchanged";
  ASSERT_TRUE(decode_into<R>(value, "[1,2]").is_ok());
  EXPECT_EQ(value.number.value(), 2);
  EXPECT_EQ(write_try(value), R"([2,"unchanged"])");
  R reader;
  typename R::state_type state;
  reader.reset(state, R"(["text"])");
  ASSERT_TRUE(reader.next_token(state));
  const auto error = Decoder<R>::decode_nested(value, reader, state);
  EXPECT_EQ(error.code, EC::type_mismatch);
  EXPECT_EQ(reader.error(state).code, EC::type_mismatch);
  EXPECT_EQ(value.text.value(), "unchanged");
}

TEST(TrySemantics, TypedVoidIntegerClaimsElementAndStopsOnFailure) {
  check_typed_int_claims<json::SimpleReader>();
  check_typed_int_claims<json::FastReader<>>();
}

template <class R>
void check_typed_text_claims() {
  struct Values {
    types::Try<v::String> text;
    types::Try<v::Int64> number;
  } value;
  value.number.value() = 17;
  ASSERT_TRUE(decode_into<R>(value, R"(["a","b"])").is_ok());
  EXPECT_EQ(value.text.value(), "b");
  EXPECT_EQ(decode_into<R>(value, "[42]").code, EC::type_mismatch);
  EXPECT_EQ(value.number.value(), 17);
}

TEST(TrySemantics, TypedVoidTextClaimsElementAndStopsOnFailure) {
  check_typed_text_claims<json::SimpleReader>();
  check_typed_text_claims<json::FastReader<>>();
}

TEST(TrySemantics, TypedContainerClaimsNestedArrayAndRetainsItsError) {
  struct Values {
    types::Try<v::Array<v::Int64>> numbers;
    types::Try<v::Array<v::String>> fallback;
  } value;
  value.fallback.value() = {"unchanged"};
  ASSERT_TRUE(decode_into<json::SimpleReader>(value, "[[1,2],[3]]").is_ok());
  EXPECT_EQ(value.numbers.value(), (std::vector<std::int64_t>{3}));
  EXPECT_EQ(decode_into<json::SimpleReader>(value, R"([["text"]])").code,
            EC::type_mismatch);
  EXPECT_EQ(value.fallback.value(), (std::vector<std::string>{"unchanged"}));
}

template <bool Hard, bool Consume = true>
struct RejectAfterStore {
  template <class C>
  auto operator()(C& ctx) const -> bool {
    static_assert(!std::is_const_v<C>);
    ctx.value() = 99;
    if constexpr (Consume) (void)ctx.reader().next_token(ctx.reader_state());
    if constexpr (Hard)
      ctx.reader().report_error(
          ctx.reader_state(),
          {EC::validation_failed, 73, "injected Try candidate error"});
    return false;
  }
};
template <bool Hard, bool Consume = true>
using Rejecting = Elem<scan::scalar | decode::number<int> | store::assign |
                           match::predicate<RejectAfterStore<Hard, Consume>{}>,
                       encode::value>;

TEST(TrySemantics, FailedMutableBoolCandidatePreservesStorageButRestoresCursor) {
  struct Values {
    types::Try<Rejecting<false>> failed;
    types::Try<Candidate<Accept>> fallback;
  } value;
  const auto input = std::string("\x92\x01\x02", 3);
  ASSERT_TRUE(decode_into<msgpack::Reader<>>(value, input).is_ok());
  EXPECT_EQ(value.failed.value(), 99);
  EXPECT_EQ(value.fallback.value(), 2);
  EXPECT_EQ(write_try(value), "[99,2]");
}

TEST(TrySemantics, FailedMutableBoolCandidatePreservesStickyReaderError) {
  struct Values {
    types::Try<Rejecting<true>> failed;
    types::Try<Candidate<Accept>> fallback;
  } value;
  msgpack::Reader<> reader;
  msgpack::Reader<>::state_type state;
  const auto input = std::string("\x92\x01\x02", 3);
  reader.reset(state, input);
  ASSERT_TRUE(reader.next_token(state));
  const auto error =
      Decoder<msgpack::Reader<>>::decode_nested(value, reader, state);
  EXPECT_EQ(error.code, EC::validation_failed);
  EXPECT_EQ(reader.error(state).code, EC::validation_failed);
  EXPECT_EQ(value.failed.value(), 99);
  EXPECT_EQ(value.fallback.value(), 0);
}

struct NamedValues {
  types::Try<types::Int64<"id">> id;
  types::Try<types::String<"name">> name;
};

template <class W, class R>
void check_named_try() {
  NamedValues original;
  original.id.value().value() = 42;
  original.name.value().value() = "value";
  NamedValues decoded;
  const auto input = write_try<W>(original);
  const auto error = decode_into<R>(decoded, input);
  ASSERT_TRUE(error.is_ok()) << error.detail;
  EXPECT_EQ(decoded.id.value().value(), 42);
  EXPECT_EQ(decoded.name.value().value(), "value");

  struct WrongType {
    types::String<"id"> id;
  } wrong;
  wrong.id.value().value() = "wrong";
  const auto invalid = write_try<W>(wrong);
  EXPECT_EQ(decode_into<R>(decoded, invalid).code, EC::type_mismatch);
}

TEST(TrySemantics, NamedTypedTryRetainsVoidBehaviorAcrossFormats) {
  check_named_try<json::CompactWriter<>, json::SimpleReader>();
  check_named_try<json::CompactWriter<>, json::FastReader<>>();
  check_named_try<bson::Writer<>, bson::Reader<>>();
  check_named_try<msgpack::Writer<>, msgpack::Reader<>>();
}

using DynamicKey =
    Elem<scan::scalar | decode::string_view | store::assign, encode::value>;
using LegacyNumber =
    Elem<scan::scalar | decode::number<int> | store::assign, encode::value>;
template <bool Hard>
struct NamedRejecting {
  types::Try<Entry<Key<"n">, Rejecting<Hard, false>>> failed;
  types::Try<Entry<DynamicKey, LegacyNumber>> fallback;
};

template <class R>
void check_named_checkpoint(std::string_view input) {
  R reader;
  typename R::state_type state;
  NamedRejecting<false> soft;
  reader.reset(state, input);
  ASSERT_TRUE(reader.next_token(state));
  const auto result = Decoder<R>::decode_nested(soft, reader, state);
  EXPECT_EQ(soft.failed.value().value(), 99);
  ASSERT_TRUE(result.is_ok()) << result.detail;
  EXPECT_EQ(soft.fallback.key().value(), "n");
  EXPECT_EQ(soft.fallback.value().value(), 7);

  NamedRejecting<true> hard;
  reader.reset(state, input);
  ASSERT_TRUE(reader.next_token(state));
  const auto hard_result = Decoder<R>::decode_nested(hard, reader, state);
  EXPECT_EQ(hard_result.code, EC::validation_failed);
  EXPECT_EQ(hard_result.offset, 73);
  EXPECT_EQ(hard_result.detail, "injected Try candidate error");
  EXPECT_EQ(hard.failed.value().value(), 99);
  EXPECT_EQ(reader.error(state).code, EC::validation_failed);
  EXPECT_TRUE(hard.fallback.key().value().empty());
  EXPECT_EQ(hard.fallback.value().value(), 0);
}

TEST(TrySemantics, NamedBoolRollbackKeepsStorageAndOriginalHardError) {
  check_named_checkpoint<json::SimpleReader>(R"({"n":7})");
  check_named_checkpoint<json::FastReader<>>(R"({"n":7})");
  check_named_checkpoint<msgpack::Reader<>>(std::string("\x81\xa1n\x07", 4));
  const char bson[] = {12, 0, 0, 0, 16, 'n', 0, 7, 0, 0, 0, 0};
  check_named_checkpoint<bson::Reader<>>(std::string(bson, sizeof(bson)));
}

TEST(TrySemantics, UnmatchedBoolCandidateReturnsOriginalStickyError) {
  struct Values {
    types::Try<Rejecting<true, false>> failed;
  } value;
  json::FastReader<> reader;
  json::FastReader<>::state_type state;
  reader.reset(state, "[7]");
  ASSERT_TRUE(reader.next_token(state));
  const auto error =
      Decoder<json::FastReader<>>::decode_nested(value, reader, state);
  EXPECT_EQ(error.code, EC::validation_failed);
  EXPECT_EQ(error.offset, 73);
  EXPECT_EQ(error.detail, "injected Try candidate error");
  EXPECT_EQ(reader.error(state).code, EC::validation_failed);
  EXPECT_EQ(value.failed.value(), 99);
}

template <bool Matched>
struct HardTerminal {
  template <class C>
    requires(!std::is_const_v<C>)
  auto operator()(C& ctx) const -> bool {
    static_assert(!std::is_const_v<C>);
    ctx.reader().report_error(
        ctx.reader_state(),
        {EC::validation_failed, 73, "original hard terminal error"});
    return Matched;
  }
};

template <bool Matched>
struct ConstHardTerminal {
  template <class C>
  auto operator()(const C& ctx) const -> bool {
    ctx.reader().report_error(
        ctx.reader_state(),
        {EC::validation_failed, 73, "original hard terminal error"});
    return Matched;
  }
};

int later_calls = 0;
struct LaterTerminal {
  template <class C>
    requires(!std::is_const_v<C>)
  auto operator()(C& ctx) const -> bool {
    static_assert(!std::is_const_v<C>);
    ++later_calls;
    ctx.value() = 123;
    ctx.reader().report_error(ctx.reader_state(),
                              {EC::invalid_state, 91, "later callback error"});
    return true;
  }
};
template <class Terminal>
using ObservedNumber = Elem<scan::scalar | decode::number<int> | store::assign |
                                match::predicate<Terminal{}>,
                            encode::value>;
using LaterNumber = ObservedNumber<LaterTerminal>;

void expect_original_terminal_error(const Error& error) {
  EXPECT_EQ(error.code, EC::validation_failed);
  EXPECT_EQ(error.offset, 73);
  EXPECT_EQ(error.detail, "original hard terminal error");
}

template <class R, class Terminal>
void check_array_hard_terminal() {
  struct Values {
    types::Try<ObservedNumber<Terminal>> first;
    types::Try<LaterNumber> later;
  } value;
  value.later.value() = 17;
  later_calls = 0;
  expect_original_terminal_error(decode_into<R>(value, "[7,8]"));
  EXPECT_EQ(value.first.value(), 7);
  EXPECT_EQ(value.later.value(), 17);
  EXPECT_EQ(later_calls, 0);
}

TEST(TrySemantics, ArrayHardFalseAndTrueStopAllLaterCandidates) {
  check_array_hard_terminal<json::SimpleReader, HardTerminal<false>>();
  check_array_hard_terminal<json::SimpleReader, HardTerminal<true>>();
  check_array_hard_terminal<json::FastReader<>, HardTerminal<false>>();
  check_array_hard_terminal<json::FastReader<>, HardTerminal<true>>();
  check_array_hard_terminal<json::SimpleReader, ConstHardTerminal<false>>();
  check_array_hard_terminal<json::SimpleReader, ConstHardTerminal<true>>();
  check_array_hard_terminal<json::FastReader<>, ConstHardTerminal<false>>();
  check_array_hard_terminal<json::FastReader<>, ConstHardTerminal<true>>();
}

template <class R, bool Matched>
void check_const_array_hard_terminal() {
  using ConstCandidate =
      Elem<match::predicate<ConstHardTerminal<Matched>{}>, noop_encoder>;
  static_assert(ConstCandidate::template decoder_const_invokable<DecodeContext<
                    typename ConstCandidate::storage_type, R, TryPhase::first>>);
  struct Values {
    types::Try<ConstCandidate> first;
    types::Try<LaterNumber> later;
  } value;
  value.later.value() = 17;
  later_calls = 0;
  expect_original_terminal_error(decode_into<R>(value, "[7]"));
  EXPECT_EQ(value.later.value(), 17);
  EXPECT_EQ(later_calls, 0);
}

TEST(TrySemantics, ConstArrayHardFalseAndTrueStopAllLaterCandidates) {
  check_const_array_hard_terminal<json::SimpleReader, false>();
  check_const_array_hard_terminal<json::SimpleReader, true>();
  check_const_array_hard_terminal<json::FastReader<>, false>();
  check_const_array_hard_terminal<json::FastReader<>, true>();
}

template <class R, bool Matched>
void check_named_hard_terminal() {
  struct Values {
    types::Try<Entry<Key<"n">, ObservedNumber<HardTerminal<Matched>>>> named;
    types::Try<Entry<DynamicKey, LaterNumber>> fallback;
  } value;
  value.fallback.value().value() = 17;
  later_calls = 0;
  expect_original_terminal_error(decode_into<R>(value, R"({"n":7,"m":8})"));
  EXPECT_EQ(value.named.value().value(), 7);
  EXPECT_TRUE(value.fallback.key().value().empty());
  EXPECT_EQ(value.fallback.value().value(), 17);
  EXPECT_EQ(later_calls, 0);
}

TEST(TrySemantics, NamedHardFalseAndTrueStopCatchall) {
  check_named_hard_terminal<json::SimpleReader, false>();
  check_named_hard_terminal<json::SimpleReader, true>();
  check_named_hard_terminal<json::FastReader<>, false>();
  check_named_hard_terminal<json::FastReader<>, true>();
}

template <class R, bool Matched, bool KeyError>
void check_dynamic_hard_terminal() {
  using HardKey = Elem<scan::scalar | decode::string_view | store::assign |
                           match::predicate<HardTerminal<Matched>{}>,
                       encode::value>;
  using FirstKey = std::conditional_t<KeyError, HardKey, DynamicKey>;
  using FirstValue = std::conditional_t<KeyError, LaterNumber,
                                        ObservedNumber<HardTerminal<Matched>>>;
  struct Values {
    types::Try<Entry<FirstKey, FirstValue>> first;
    types::Try<Entry<DynamicKey, LaterNumber>> later;
  } value;
  value.first.value().value() = 19;
  value.later.value().value() = 17;
  later_calls = 0;
  expect_original_terminal_error(decode_into<R>(value, R"({"n":7})"));
  EXPECT_EQ(value.first.key().value(), "n");
  EXPECT_EQ(value.first.value().value(), KeyError ? 19 : 7);
  EXPECT_TRUE(value.later.key().value().empty());
  EXPECT_EQ(value.later.value().value(), 17);
  EXPECT_EQ(later_calls, 0);
}

TEST(TrySemantics, DynamicKeyAndValueHardErrorsStopLaterCandidates) {
  check_dynamic_hard_terminal<json::SimpleReader, false, false>();
  check_dynamic_hard_terminal<json::SimpleReader, true, false>();
  check_dynamic_hard_terminal<json::SimpleReader, false, true>();
  check_dynamic_hard_terminal<json::SimpleReader, true, true>();
  check_dynamic_hard_terminal<json::FastReader<>, false, false>();
  check_dynamic_hard_terminal<json::FastReader<>, true, false>();
  check_dynamic_hard_terminal<json::FastReader<>, false, true>();
  check_dynamic_hard_terminal<json::FastReader<>, true, true>();
}

template <bool Matched>
void check_final_hard_terminals() {
  struct Array {
    types::Try<ObservedNumber<HardTerminal<Matched>>> last;
  } array;
  expect_original_terminal_error(decode_into<json::FastReader<>>(array, "[7]"));
  struct Named {
    types::Try<Entry<Key<"n">, ObservedNumber<HardTerminal<Matched>>>> last;
  } named;
  expect_original_terminal_error(
      decode_into<json::FastReader<>>(named, R"({"n":7})"));
  struct Dynamic {
    types::Try<Entry<DynamicKey, ObservedNumber<HardTerminal<Matched>>>> last;
  } dynamic;
  expect_original_terminal_error(
      decode_into<json::FastReader<>>(dynamic, R"({"n":7})"));
}

TEST(TrySemantics, FinalHardFalseAndTrueCandidatesReturnOriginalError) {
  check_final_hard_terminals<false>();
  check_final_hard_terminals<true>();
}

TEST(TrySemantics, SoftNoMatchStillReturnsUnknownMember) {
  struct Array {
    types::Try<Candidate<Even>> even;
  } array;
  EXPECT_EQ(decode_into<json::FastReader<>>(array, "[7]").code,
            EC::unknown_member);
  struct Named {
    types::Try<Entry<Key<"n">, Candidate<Even>>> even;
  } named;
  EXPECT_EQ(decode_into<json::FastReader<>>(named, R"({"n":7})").code,
            EC::unknown_member);
  struct Dynamic {
    types::Try<Entry<DynamicKey, Candidate<Even>>> even;
  } dynamic;
  EXPECT_EQ(decode_into<json::FastReader<>>(dynamic, R"({"n":7})").code,
            EC::unknown_member);
}

struct SoftRejectTerminal {
  template <class C>
    requires(!std::is_const_v<C>)
  auto operator()(C&) const -> bool {
    return false;
  }
};

template <class R, bool RejectKey>
void check_dynamic_soft_fallback() {
  using SoftKey = Elem<scan::scalar | decode::string_view | store::assign |
                           match::predicate<SoftRejectTerminal{}>,
                       encode::value>;
  using FirstKey = std::conditional_t<RejectKey, SoftKey, DynamicKey>;
  using FirstValue =
      std::conditional_t<RejectKey, LegacyNumber, Rejecting<false, false>>;
  struct Values {
    types::Try<Entry<FirstKey, FirstValue>> rejected;
    types::Try<Entry<DynamicKey, LegacyNumber>> fallback;
  } value;
  value.rejected.value().value() = 19;
  const auto error = decode_into<R>(value, R"({"n":7,"m":8})");
  ASSERT_TRUE(error.is_ok()) << error.detail;
  EXPECT_EQ(value.rejected.key().value(), "m");
  EXPECT_EQ(value.rejected.value().value(), RejectKey ? 19 : 99);
  EXPECT_EQ(value.fallback.key().value(), "m");
  EXPECT_EQ(value.fallback.value().value(), 8);
}

TEST(TrySemantics, DynamicSoftKeyAndValueRejectionRestoreKeyForFallback) {
  check_dynamic_soft_fallback<json::SimpleReader, false>();
  check_dynamic_soft_fallback<json::SimpleReader, true>();
  check_dynamic_soft_fallback<json::FastReader<>, false>();
  check_dynamic_soft_fallback<json::FastReader<>, true>();
}

template <class R>
void check_string_soft_fallback() {
  using RejectedText =
      Elem<scan::scalar | decode::string_source | construct<std::string> |
               decode::json_unescape | decode::toml_unescape | validate::utf8 |
               store::assign | match::predicate<SoftRejectTerminal{}>,
           encode::value>;
  using AcceptedText =
      Elem<scan::scalar | decode::string_source | construct<std::string> |
               decode::json_unescape | decode::toml_unescape | validate::utf8 |
               store::assign,
           encode::value>;
  struct Values {
    types::Try<Entry<Key<"n">, RejectedText>> rejected;
    types::Try<Entry<DynamicKey, AcceptedText>> fallback;
  } value;
  // Key and value have the same Scalar/string kind. Their offsets, rather than
  // just token categories or cached lengths, distinguish the rollback target.
  ASSERT_TRUE(decode_document_into<R>(value, R"({"n":"","n":"a\nb"})").is_ok());
  EXPECT_EQ(value.fallback.key().value(), "n");
  EXPECT_EQ(value.fallback.value().value(), "a\nb");
}

TEST(TrySemantics, StringSoftRejectionRestoresKeyAcrossEmptyAndEscapedValues) {
  check_string_soft_fallback<json::SimpleReader>();
  check_string_soft_fallback<json::FastReader<>>();
}

template <class R, class T>
void check_prefailed_try(T& value, std::string_view input) {
  R reader;
  typename R::state_type state;
  reader.reset(state, input);
  ASSERT_TRUE(reader.next_token(state));
  reader.report_error(
      state, {EC::validation_failed, 73, "original hard terminal error"});
  later_calls = 0;
  expect_original_terminal_error(
      Decoder<R>::decode_nested(value, reader, state));
  EXPECT_EQ(later_calls, 0);
}

TEST(TrySemantics, PrefailedReadersKeepOriginalErrorBeforeTryShapeValidation) {
  struct Array {
    types::Try<LaterNumber> candidate;
  } array;
  struct Named {
    types::Try<Entry<Key<"n">, LaterNumber>> candidate;
  } named;
  struct Dynamic {
    types::Try<Entry<DynamicKey, LaterNumber>> candidate;
  } dynamic;
  check_prefailed_try<json::SimpleReader>(array, "[7]");
  check_prefailed_try<json::FastReader<>>(array, "[7]");
  check_prefailed_try<json::SimpleReader>(named, R"({"n":7})");
  check_prefailed_try<json::FastReader<>>(named, R"({"n":7})");
  check_prefailed_try<json::SimpleReader>(dynamic, R"({"n":7})");
  check_prefailed_try<json::FastReader<>>(dynamic, R"({"n":7})");
  EXPECT_EQ(array.candidate.value(), 0);
  EXPECT_EQ(named.candidate.value().value(), 0);
  EXPECT_TRUE(dynamic.candidate.key().value().empty());
  EXPECT_EQ(dynamic.candidate.value().value(), 0);
}
}  // namespace
