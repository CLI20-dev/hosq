#pragma once

#include <string>
#include <unordered_set>
#include <vector>

#include "hosq/context.hh"
#include "hosq/json/scalar_codec.hh"
#include "hosq/pipeline/scan.hh"

namespace hosq::json::detail {
struct ValueRange {
  std::size_t offset;
  std::size_t length;
  TokenType type;
  std::uint8_t context;
};

// Consumes the current value, leaving its final token current. The caller owns
// document-end validation and the lifetime of the source bytes.
template <bool TrackPath, ReaderConcept R, class Visitor>
auto validate_value(R& reader, typename R::state_type& state, Visitor&& visitor,
                    std::size_t max_depth = 128) -> bool {
  if (::hosq::detail::reader_error(reader, state)) return false;
  struct Frame {
    Token begin;
    std::unordered_set<std::string> keys;
    std::size_t index = 0;
    bool key = true;
  };
  std::vector<Frame> stack;
  ErrorPath path;
  auto fail = [&](ErrorCode code, std::size_t offset, std::string_view why) {
    ::hosq::detail::report_reader_error(reader, state, {code, offset, why});
    return false;
  };
  auto resolve = [&]() {
    DecodeContext<void, R, TryPhase::first> ctx(reader, state);
    if constexpr (requires { ext::ScanScalar<R>::resolved(ctx); })
      (void)ext::ScanScalar<R>::resolved(ctx);
    else
      (void)ext::ScanScalar<R>{}(ctx);
    return !::hosq::detail::reader_error(reader, state);
  };
  auto completed = [&]() {
    if (!stack.empty()) {
      auto& parent = stack.back();
      if (parent.begin.type == TokenType::ObjectBegin) parent.key = true;
      ++parent.index;
      if constexpr (TrackPath) path.pop_back();
    }
  };
  for (;;) {
    auto token = reader.current_token(state);
    if (!stack.empty() && stack.back().begin.type == TokenType::ObjectBegin &&
        stack.back().key && token.type != TokenType::ObjectEnd) {
      if (token.type != TokenType::Scalar ||
          token.context != static_cast<std::uint8_t>(JsonTokenContext::string))
        return fail(ErrorCode::unexpected_token, token.offset,
                    "expected JSON object key string");
      if (!resolve()) return false;
      token = reader.current_token(state);
      const auto raw = reader.input(state).as_string_view().substr(token.offset,
                                                                   token.length);
      std::string key;
      if (!string_codec::unescape_json_string(raw, key))
        return fail(ErrorCode::invalid_escape, token.offset,
                    "invalid JSON key string");
      auto& frame = stack.back();
      if (!frame.keys.insert(key).second)
        return fail(ErrorCode::duplicate_member, token.offset,
                    "duplicate JSON object key");
      frame.key = false;
      if constexpr (TrackPath) path.push_back(PathSegment::field(key));
      if (!reader.next_token(state)) return false;
      continue;
    }
    if (token.type == TokenType::ObjectEnd ||
        token.type == TokenType::ArrayEnd) {
      if (stack.empty() ||
          (token.type == TokenType::ObjectEnd) !=
              (stack.back().begin.type == TokenType::ObjectBegin) ||
          (token.type == TokenType::ObjectEnd && !stack.back().key))
        return fail(ErrorCode::unexpected_token, token.offset,
                    "unexpected JSON container end");
      const auto begin = stack.back().begin;
      stack.pop_back();
      visitor(path, ValueRange{begin.offset,
                               token.offset + token.length - begin.offset,
                               begin.type, begin.context});
      if (stack.empty()) return true;
      completed();
    } else {
      if constexpr (TrackPath) {
        if (!stack.empty() && stack.back().begin.type == TokenType::ArrayBegin)
          path.push_back(PathSegment::element(stack.back().index));
      }
      if (token.type == TokenType::ObjectBegin ||
          token.type == TokenType::ArrayBegin) {
        if (stack.size() >= max_depth)
          return fail(ErrorCode::depth_limit_exceeded, token.offset,
                      "JSON subtree container depth limit");
        stack.push_back(Frame{token, {}});
      } else if (token.type == TokenType::Scalar) {
        if (!resolve()) return false;
        token = reader.current_token(state);
        const auto raw = reader.input(state).as_string_view().substr(
            token.offset, token.length);
        auto range =
            ValueRange{token.offset, token.length, token.type, token.context};
        if (token.context ==
            static_cast<std::uint8_t>(JsonTokenContext::string)) {
          std::string decoded;
          if (!string_codec::unescape_json_string(raw, decoded))
            return fail(ErrorCode::invalid_escape, token.offset,
                        "invalid JSON string");
          --range.offset;
          range.length += 2;
        } else if (token.context !=
                       static_cast<std::uint8_t>(JsonTokenContext::literal) ||
                   (raw != "true" && raw != "false" && raw != "null" &&
                    !valid_number(raw))) {
          const bool numeric =
              !raw.empty() &&
              (raw.front() == '-' || (raw.front() >= '0' && raw.front() <= '9'));
          return fail(
              numeric ? ErrorCode::invalid_number : ErrorCode::invalid_token,
              token.offset, "invalid JSON literal");
        }
        visitor(path, range);
        if (stack.empty()) return true;
        completed();
      } else {
        return fail(ErrorCode::unexpected_token, token.offset,
                    "expected JSON value");
      }
    }
    if (!reader.next_token(state)) return false;
  }
}
}  // namespace hosq::json::detail
