#pragma once

#include <charconv>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include "hosq/detail/number_parse.hh"
#include "hosq/json/string_codec.hh"
#include "hosq/pipeline/string.hh"
#include "hosq/scalar_codec.hh"

namespace hosq::json::detail {
inline auto valid_number(std::string_view raw) -> bool {
  std::size_t pos = 0;
  if (pos < raw.size() && raw[pos] == '-') ++pos;
  if (pos == raw.size()) return false;
  if (raw[pos] == '0')
    ++pos;
  else {
    if (raw[pos] < '1' || raw[pos] > '9') return false;
    do {
      ++pos;
    } while (pos < raw.size() && raw[pos] >= '0' && raw[pos] <= '9');
  }
  auto digits = [&]() {
    const auto start = pos;
    while (pos < raw.size() && raw[pos] >= '0' && raw[pos] <= '9') ++pos;
    return start != pos;
  };
  if (pos < raw.size() && raw[pos] == '.') {
    ++pos;
    if (!digits()) return false;
  }
  if (pos < raw.size() && (raw[pos] == 'e' || raw[pos] == 'E')) {
    ++pos;
    if (pos < raw.size() && (raw[pos] == '+' || raw[pos] == '-')) ++pos;
    if (!digits()) return false;
  }
  return pos == raw.size();
}

struct StringSourceCodec {
  template <class Context>
  auto operator()(const ScalarView&, const Context& ctx,
                  std::string_view& output) const -> bool {
    output = {};
    const auto& reader = ctx.reader();
    const auto& state = ctx.reader_state();
    if (::hosq::detail::reader_error(reader, state)) return false;
    const auto& token = reader.current_token(state);
    if (token.type != TokenType::Scalar ||
        token.context != static_cast<std::uint8_t>(JsonTokenContext::string)) {
      ::hosq::detail::report_reader_error(
          reader, state,
          {ErrorCode::type_mismatch, token.offset, "expected string"});
      return false;
    }
    output =
        reader.input(state).as_string_view().substr(token.offset, token.length);
    return true;
  }
};
struct UnescapeStringCodec {
  template <class Context>
  auto operator()(std::string input, const Context& ctx) const -> std::string {
    std::string output;
    if (!string_codec::unescape_json_string(input, output)) {
      ::hosq::detail::report_reader_error(
          ctx.reader(), ctx.reader_state(),
          {ErrorCode::invalid_escape,
           ctx.reader().current_token(ctx.reader_state()).offset,
           "invalid JSON string body"});
      return {};
    }
    return output;
  }
};

// JSON owns its literal grammar, escapes, token context and string syntax.
struct SemanticScalarCodec {
  template <class View, class Context, class T>
  auto operator()(const View&, const Context& ctx, T& output) const -> bool {
    const auto& reader = ctx.reader();
    const auto& state = ctx.reader_state();
    if (::hosq::detail::reader_error(reader, state)) return false;
    const auto& token = reader.current_token(state);
    auto fail = [&](ErrorCode code, std::string_view why) {
      ::hosq::detail::report_reader_error(reader, state,
                                          {code, token.offset, why});
      return false;
    };
    const bool literal =
        token.type == TokenType::Scalar &&
        token.context == static_cast<std::uint8_t>(JsonTokenContext::literal);
    const bool string =
        token.type == TokenType::Scalar &&
        token.context == static_cast<std::uint8_t>(JsonTokenContext::string);
    const auto raw =
        reader.input(state).as_string_view().substr(token.offset, token.length);
    if constexpr (std::same_as<T, SemanticNumber>) {
      if (!literal)
        return fail(ErrorCode::type_mismatch, "expected numeric literal");
      const bool floating = raw.find_first_of(".eE") != std::string_view::npos;
      if (raw.empty() ||
          !(raw.front() == '-' || (raw.front() >= '0' && raw.front() <= '9')) ||
          floating != (output.requested == SemanticNumber::Category::floating))
        return fail(ErrorCode::type_mismatch,
                    output.requested == SemanticNumber::Category::floating
                        ? "expected floating-point category"
                        : "expected integer category");
      if (!valid_number(raw))
        return fail(ErrorCode::invalid_number, "invalid JSON number grammar");
      std::from_chars_result result;
      if (floating) {
        double value{};
        result = hosq::detail::number_from_chars(raw.data(),
                                                 raw.data() + raw.size(), value);
        output.value = value;
      } else if (raw.front() == '-') {
        std::int64_t value{};
        result = std::from_chars(raw.data(), raw.data() + raw.size(), value);
        output.value = value;
        output.negative_integer = true;
      } else {
        std::uint64_t value{};
        result = std::from_chars(raw.data(), raw.data() + raw.size(), value);
        output.value = value;
      }
      if (result.ec == std::errc::result_out_of_range)
        return fail(ErrorCode::out_of_range, "numeric literal out of range");
      if (result.ec != std::errc{} || result.ptr != raw.data() + raw.size())
        return fail(ErrorCode::invalid_number, "invalid numeric literal");
      return true;
    } else if constexpr (std::same_as<T, std::string>) {
      if (!string) return fail(ErrorCode::type_mismatch, "expected string");
      std::string semantic;
      if (!string_codec::unescape_json_string(raw, semantic))
        return fail(ErrorCode::invalid_escape, "invalid JSON string body");
      output = std::move(semantic);
      return true;
    } else if constexpr (std::same_as<T, bool>) {
      if (literal && (raw == "true" || raw == "false")) {
        output = raw == "true";
        return true;
      }
      return fail(ErrorCode::type_mismatch, "expected boolean literal");
    } else if constexpr (std::same_as<T, std::nullptr_t>) {
      if (literal && raw == "null") return true;
      return fail(ErrorCode::type_mismatch, "expected null");
    } else
      return fail(ErrorCode::unsupported_type,
                  "JSON has no codec for this scalar type");
  }
};
}  // namespace hosq::json::detail
