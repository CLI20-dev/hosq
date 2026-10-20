#pragma once

#include <charconv>
#include <string>
#include <vector>

#include "hosq/detail/native_reader.hh"
#include "hosq/detail/temporal.hh"
#include "hosq/meta_match.hh"
#include "hosq/toml/string_codec.hh"

namespace hosq::toml {
// TOML 1.0.0. A structural index reconciles dotted keys and out-of-order table
// declarations before exposing the ordinary HOSQ token traversal. Scalar text
// remains a span of the caller's input; no general-purpose value DOM is built.
template <std::size_t MaxDepth = 64>
class Reader : public ReaderBase<Reader<MaxDepth>>,
               public detail::NativeReaderAccess {
  static constexpr std::size_t absent = std::numeric_limits<std::size_t>::max();
  struct Node {
    Token token{0, 0, TokenType::End, 0};
    detail::NativeScalar scalar;
    std::string key;
    std::vector<std::size_t> children;
    bool defined = false;
    bool explicit_header = false;
    bool sealed = false;
    bool array_of_tables = false;
    std::size_t end = 0;
    std::size_t source = 0;
  };
  struct KeyPart {
    Token token;
    std::string name;
    std::size_t source = 0;
  };
  struct Event {
    std::size_t node;
    TokenType type;
  };

 public:
  using detail::NativeReaderAccess::read_semantic;
  struct state_type : detail::NativeState {
    std::vector<Node> nodes;
    std::vector<Event> events;
    std::size_t cursor = 0;
    std::size_t current = absent;
    struct Checkpoint {
      std::size_t cursor, current;
      Token token;
      detail::NativeScalar scalar;
    };
    auto checkpoint() const -> Checkpoint {
      return {cursor, current, token, scalar};
    }
    void rollback(Checkpoint c) {
      cursor = c.cursor;
      current = c.current;
      token = c.token;
      scalar = c.scalar;
    }
  };
  void reset(state_type& s, input_type in) const {
    s = {};
    s.input = in;
    const auto invalid = detail::invalid_utf8_offset(in);
    if (invalid != in.size()) {
      s.failure = {ErrorCode::invalid_utf8, invalid, "invalid TOML UTF-8"};
      return;
    }
    if (MaxDepth == 0) {
      fail(s, ErrorCode::depth_limit_exceeded, "TOML root depth");
      return;
    }
    make_container(s, TokenType::ObjectBegin, 0);
    s.nodes[0].defined = true;
    std::size_t table = 0;
    while (true) {
      whitespace(s, true);
      if (s.failure || s.pos == in.size()) break;
      if (at(s, '[')) {
        const auto start = s.pos++;
        const bool array = at(s, '[');
        if (array) ++s.pos;
        std::vector<KeyPart> path;
        if (!key_path(s, path)) return;
        if (!at(s, ']')) {
          fail(s, ErrorCode::unexpected_token, "unclosed TOML table header");
          return;
        }
        ++s.pos;
        if (array) {
          if (!at(s, ']')) {
            fail(s, ErrorCode::unexpected_token,
                 "unclosed TOML array table header");
            return;
          }
          ++s.pos;
        }
        if (!line_end(s)) return;
        table = declare_table(s, path, array, start);
        if (table == absent) return;
      } else if (!assignment(s, table, 1) || !line_end(s))
        return;
    }
    if (s.failure) return;
    s.nodes[0].end = in.size();
    if (!index_events(s, 0, 1)) return;
    s.token = {0, 0, TokenType::End, 0};
    s.scalar = {};
  }
  auto next_token_impl(state_type& s) const -> bool {
    if (s.failure) return false;
    if (s.cursor == s.events.size()) {
      s.current = absent;
      s.token = {s.input.size(), 0, TokenType::End, 0};
      s.scalar = {};
      return true;
    }
    const auto e = s.events[s.cursor++];
    s.current = e.node;
    const auto& node = s.nodes[e.node];
    s.scalar = node.scalar;
    s.token = node.token;
    if (e.type == TokenType::ObjectEnd || e.type == TokenType::ArrayEnd)
      s.token = {node.end, 0, e.type, 0};
    return true;
  }
  // Owned semantic strings resolve escapes and multiline continuations. The
  // borrowed decode::string_view API deliberately retains original raw bytes.
  auto read_string(const state_type& s, std::string& output) const -> bool {
    output.clear();
    if (s.failure) return false;
    if (s.current == absent || s.token.type != TokenType::Scalar ||
        s.token.context !=
            static_cast<std::uint8_t>(detail::NativeContext::string)) {
      report_error(
          s, {ErrorCode::type_mismatch, s.token.offset, "expected TOML string"});
      return false;
    }
    const auto& node = s.nodes[s.current];
    if (!node.children.empty()) {
      output = node.key;
      return true;
    }
    state_type cursor;
    cursor.input = s.input;
    cursor.pos = node.source;
    if (!string(cursor, &output)) {
      output.clear();
      report_error(s, cursor.failure);
      return false;
    }
    return true;
  }
  auto read_semantic(const state_type& s, LocalDateValue& output) const -> bool {
    return read_temporal(s, output, detail::TemporalKind::date,
                         detail::parse_local_date);
  }
  auto read_semantic(const state_type& s, LocalTimeValue& output) const -> bool {
    return read_temporal(s, output, detail::TemporalKind::time,
                         detail::parse_local_time);
  }
  auto read_semantic(const state_type& s, LocalDateTimeValue& output) const
      -> bool {
    return read_temporal(s, output, detail::TemporalKind::local_datetime,
                         detail::parse_local_datetime);
  }
  auto read_semantic(const state_type& s, OffsetDateTimeValue& output) const
      -> bool {
    return read_temporal(s, output, detail::TemporalKind::offset_datetime,
                         detail::parse_offset_datetime);
  }
  // An Instant is an explicit conversion of an offset date-time. A local
  // wall-clock value does not identify a point on the Unix time line.
  auto read_semantic(const state_type& s, InstantValue& output) const -> bool {
    output = {};
    OffsetDateTimeValue offset;
    if (!read_semantic(s, offset)) return false;
    if (!detail::offset_to_instant(offset, output)) {
      report_error(s, {ErrorCode::conversion_error, s.token.offset,
                       "TOML date-time cannot identify an Instant"});
      return false;
    }
    return true;
  }
  // Constant schema keys compare decoded names. decode::string_view continues
  // to expose borrowed raw key/value bytes, consistent with the JSON API.
  template <class Handlers, class Scan, class Deferred>
  auto match_const_key(state_type& s, Handlers& handlers, Scan&&, Deferred) const
      -> bool {
    if (s.current == absent || s.token.type != TokenType::Scalar) return false;
    return meta::match(std::string_view{s.nodes[s.current].key}, handlers);
  }

 private:
  template <class Value, class Parse>
  auto read_temporal(const state_type& s, Value& output,
                     detail::TemporalKind kind, Parse parse) const -> bool {
    output = {};
    if (s.failure) return false;
    if (s.current == absent || s.token.type != TokenType::Scalar ||
        s.token.context !=
            static_cast<std::uint8_t>(detail::NativeContext::datetime)) {
      report_error(s, {ErrorCode::type_mismatch, s.token.offset,
                       "expected native TOML date/time"});
      return false;
    }
    if (s.current >= s.nodes.size() || s.token.offset > s.input.size() ||
        s.token.length > s.input.size() - s.token.offset) {
      report_error(s, {ErrorCode::invalid_state, s.token.offset,
                       "invalid TOML date/time token span"});
      return false;
    }
    const auto text = s.input.substr(s.token.offset, s.token.length);
    if (detail::classify_temporal(text) != kind) {
      report_error(s, {ErrorCode::type_mismatch, s.token.offset,
                       "TOML date/time category does not match schema"});
      return false;
    }
    Value parsed;
    if (!parse(text, parsed)) {
      report_error(s, {ErrorCode::conversion_error, s.token.offset,
                       "TOML date/time is not representable without loss"});
      return false;
    }
    output = parsed;
    return true;
  }
  static auto at(const state_type& s, char c) -> bool {
    return s.pos < s.input.size() && s.input[s.pos] == c;
  }
  static auto make_container(state_type& s, TokenType type, std::size_t offset)
      -> std::size_t {
    Node node;
    node.token = {offset, 0, type, 0};
    node.end = s.input.size();
    s.nodes.push_back(std::move(node));
    return s.nodes.size() - 1;
  }
  static auto member(const state_type& s, std::size_t object,
                     std::string_view name) -> std::size_t {
    for (auto key : s.nodes[object].children)
      if (s.nodes[key].key == name) return key;
    return absent;
  }
  static auto attach(state_type& s, std::size_t object, const KeyPart& key,
                     std::size_t value) -> std::size_t {
    Node node;
    node.token = key.token;
    node.key = key.name;
    node.source = key.source;
    node.children.push_back(value);
    const auto id = s.nodes.size();
    s.nodes.push_back(std::move(node));
    s.nodes[object].children.push_back(id);
    return id;
  }
  static auto table_child(state_type& s, std::size_t parent, const KeyPart& key,
                          bool dotted) -> std::size_t {
    if (s.nodes[parent].sealed) {
      fail(s, ErrorCode::duplicate_member, "cannot extend inline TOML table");
      return absent;
    }
    const auto k = member(s, parent, key.name);
    if (k == absent) {
      const auto child =
          make_container(s, TokenType::ObjectBegin, key.token.offset);
      s.nodes[child].defined = dotted;
      attach(s, parent, key, child);
      return child;
    }
    auto child = s.nodes[k].children[0];
    if (!dotted && s.nodes[child].array_of_tables)
      child = s.nodes[child].children.back();
    if (s.nodes[child].token.type != TokenType::ObjectBegin ||
        s.nodes[child].sealed) {
      fail(s, ErrorCode::duplicate_member,
           "TOML table conflicts with existing value");
      return absent;
    }
    if (dotted && s.nodes[child].explicit_header) {
      fail(s, ErrorCode::duplicate_member,
           "dotted key cannot extend explicitly declared TOML table");
      return absent;
    }
    if (dotted) s.nodes[child].defined = true;
    return child;
  }
  static auto declare_table(state_type& s, const std::vector<KeyPart>& path,
                            bool array, std::size_t start) -> std::size_t {
    std::size_t parent = 0;
    for (std::size_t i = 0; i + 1 < path.size(); ++i) {
      parent = table_child(s, parent, path[i], false);
      if (parent == absent) return absent;
    }
    if (s.nodes[parent].sealed) {
      fail(s, ErrorCode::duplicate_member, "cannot extend inline TOML table");
      return absent;
    }
    const auto& last = path.back();
    const auto key = member(s, parent, last.name);
    std::size_t value;
    if (key == absent) {
      value = make_container(
          s, array ? TokenType::ArrayBegin : TokenType::ObjectBegin, start);
      attach(s, parent, last, value);
      s.nodes[value].array_of_tables = array;
    } else {
      value = s.nodes[key].children[0];
      if (array) {
        if (!s.nodes[value].array_of_tables) {
          fail(s, ErrorCode::duplicate_member,
               "array table conflicts with TOML value");
          return absent;
        }
      } else if (s.nodes[value].token.type != TokenType::ObjectBegin ||
                 s.nodes[value].defined || s.nodes[value].sealed) {
        fail(s, ErrorCode::duplicate_member, "duplicate TOML table");
        return absent;
      }
    }
    if (array) {
      const auto child = make_container(s, TokenType::ObjectBegin, start);
      s.nodes[child].defined = true;
      s.nodes[child].explicit_header = true;
      s.nodes[value].children.push_back(child);
      return child;
    }
    s.nodes[value].defined = true;
    s.nodes[value].explicit_header = true;
    return value;
  }
  static auto assignment(state_type& s, std::size_t object, std::size_t depth)
      -> bool {
    std::vector<KeyPart> path;
    if (!key_path(s, path)) return false;
    if (!at(s, '='))
      return fail(s, ErrorCode::unexpected_token, "expected TOML equals");
    ++s.pos;
    whitespace(s, false);
    const auto value = parse_value(s, depth + 1);
    if (value == absent) return false;
    for (std::size_t i = 0; i + 1 < path.size(); ++i) {
      object = table_child(s, object, path[i], true);
      if (object == absent) return false;
    }
    if (s.nodes[object].sealed || member(s, object, path.back().name) != absent)
      return fail(s, ErrorCode::duplicate_member, "duplicate TOML key");
    attach(s, object, path.back(), value);
    return true;
  }
  static auto index_events(state_type& s, std::size_t node, std::size_t depth)
      -> bool {
    const auto type = s.nodes[node].token.type;
    const bool object = type == TokenType::ObjectBegin;
    const bool container = object || type == TokenType::ArrayBegin;
    if (container && depth > MaxDepth)
      return fail(s, ErrorCode::depth_limit_exceeded, "TOML container depth");
    s.events.push_back({node, type});
    if (!container) return true;
    for (auto child : s.nodes[node].children) {
      if (object) {
        s.events.push_back({child, TokenType::Scalar});
        if (!index_events(s, s.nodes[child].children[0], depth + 1))
          return false;
      } else if (!index_events(s, child, depth + 1))
        return false;
    }
    s.events.push_back(
        {node, object ? TokenType::ObjectEnd : TokenType::ArrayEnd});
    return true;
  }
  static void whitespace(state_type& s, bool lines) {
    while (s.pos < s.input.size()) {
      const char c = s.input[s.pos];
      if (c == ' ' || c == '\t' || (lines && c == '\n'))
        ++s.pos;
      else if (lines && c == '\r') {
        ++s.pos;
        if (!at(s, '\n')) {
          fail(s, ErrorCode::invalid_token, "bare TOML carriage return");
          return;
        }
        ++s.pos;
      } else if (lines && c == '#') {
        if (!comment(s)) return;
      } else
        break;
    }
  }
  static auto comment(state_type& s) -> bool {
    while (s.pos < s.input.size() && !at(s, '\n') && !at(s, '\r')) {
      const auto b = static_cast<unsigned char>(s.input[s.pos++]);
      if ((b < 0x20 && b != '\t') || b == 0x7f)
        return fail(s, ErrorCode::invalid_token, "control byte in TOML comment");
    }
    return true;
  }
  static auto line_end(state_type& s) -> bool {
    whitespace(s, false);
    if (at(s, '#') && !comment(s)) return false;
    if (s.pos == s.input.size()) return true;
    if (at(s, '\r')) {
      ++s.pos;
      if (!at(s, '\n'))
        return fail(s, ErrorCode::invalid_token, "bare TOML carriage return");
    }
    if (!at(s, '\n'))
      return fail(s, ErrorCode::unexpected_token, "expected TOML line end");
    ++s.pos;
    return true;
  }
  static auto string(state_type& s, std::string* decoded = nullptr,
                     bool is_key = false) -> bool {
    string_codec::Cursor cursor{s.input, s.pos};
    const bool valid = string_codec::parse(cursor, decoded, is_key);
    s.pos = cursor.pos;
    if (!valid) return fail(s, cursor.failure.code, cursor.failure.detail);
    s.token = cursor.token;
    s.token.context = static_cast<std::uint8_t>(detail::NativeContext::string);
    return true;
  }
  static auto key_path(state_type& s, std::vector<KeyPart>& parts) -> bool {
    do {
      whitespace(s, false);
      if (s.failure) return false;
      KeyPart part;
      part.source = s.pos;
      if (at(s, '"') || at(s, '\'')) {
        if (!string(s, &part.name, true)) return false;
        part.token = s.token;
      } else {
        const auto start = s.pos;
        while (s.pos < s.input.size()) {
          const char c = s.input[s.pos];
          if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                (c >= '0' && c <= '9') || c == '_' || c == '-'))
            break;
          ++s.pos;
        }
        if (start == s.pos)
          return fail(s, ErrorCode::invalid_token, "invalid TOML key");
        part.token = {start, s.pos - start, TokenType::Scalar,
                      static_cast<std::uint8_t>(detail::NativeContext::string)};
        part.name = s.input.substr(start, s.pos - start);
      }
      parts.push_back(std::move(part));
      whitespace(s, false);
      if (!at(s, '.')) return true;
      ++s.pos;
    } while (true);
  }
  static auto number_form(std::string_view text) -> int {
    std::size_t pos = 0;
    auto digit = [](char c, int base) {
      int value = -1;
      if (c >= '0' && c <= '9')
        value = c - '0';
      else if (c >= 'a' && c <= 'f')
        value = c - 'a' + 10;
      else if (c >= 'A' && c <= 'F')
        value = c - 'A' + 10;
      return value >= 0 && value < base;
    };
    auto digits = [&](int base) {
      if (pos >= text.size() || !digit(text[pos], base)) return false;
      ++pos;
      while (pos < text.size()) {
        if (digit(text[pos], base))
          ++pos;
        else if (text[pos] == '_') {
          ++pos;
          if (pos >= text.size() || !digit(text[pos], base)) return false;
          ++pos;
        } else
          break;
      }
      return true;
    };
    if (text.size() >= 2 && text[0] == '0') {
      const int base = text[1] == 'x'   ? 16
                       : text[1] == 'o' ? 8
                       : text[1] == 'b' ? 2
                                        : 0;
      if (base) {
        pos = 2;
        return digits(base) && pos == text.size() ? base : 0;
      }
    }
    if (pos < text.size() && (text[pos] == '+' || text[pos] == '-')) ++pos;
    if (pos >= text.size()) return 0;
    if (text[pos] == '0')
      ++pos;
    else if (!digits(10))
      return 0;
    if (pos == text.size()) return 10;
    bool floating = false;
    if (text[pos] == '.') {
      ++pos;
      floating = true;
      if (!digits(10)) return 0;
    }
    if (pos < text.size() && (text[pos] == 'e' || text[pos] == 'E')) {
      ++pos;
      floating = true;
      if (pos < text.size() && (text[pos] == '+' || text[pos] == '-')) ++pos;
      if (!digits(10)) return 0;
    }
    return floating && pos == text.size() ? -10 : 0;
  }
  static auto datetime(std::string_view text) -> bool {
    auto digits = [&](std::size_t pos, std::size_t n) -> int {
      if (pos > text.size() || n > text.size() - pos) return -1;
      int value = 0;
      for (std::size_t i = 0; i < n; ++i) {
        const char c = text[pos + i];
        if (c < '0' || c > '9') return -1;
        value = value * 10 + c - '0';
      }
      return value;
    };
    std::size_t pos = 0;
    const bool date = text.size() >= 10 && text[4] == '-' && text[7] == '-';
    if (date) {
      const int year = digits(0, 4), month = digits(5, 2), day = digits(8, 2);
      if (year < 0 || month < 1 || month > 12 || day < 1) return false;
      constexpr int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
      const bool leap = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
      if (day > days[month - 1] + (month == 2 && leap)) return false;
      if (text.size() == 10) return true;
      if (text[10] != 'T' && text[10] != 't' && text[10] != ' ') return false;
      pos = 11;
    }
    if (text.size() - pos < 8 || text[pos + 2] != ':' || text[pos + 5] != ':')
      return false;
    const int hour = digits(pos, 2), minute = digits(pos + 3, 2),
              second = digits(pos + 6, 2);
    if (hour < 0 || hour > 23 || minute < 0 || minute > 59 || second < 0 ||
        second > 60)
      return false;
    pos += 8;
    if (pos < text.size() && text[pos] == '.') {
      ++pos;
      const auto start = pos;
      while (pos < text.size() && text[pos] >= '0' && text[pos] <= '9') ++pos;
      if (pos == start) return false;
    }
    if (pos == text.size()) return true;
    if (!date) return false;
    if (text[pos] == 'Z' || text[pos] == 'z') return pos + 1 == text.size();
    if ((text[pos] != '+' && text[pos] != '-') || text.size() - pos != 6 ||
        text[pos + 3] != ':')
      return false;
    const int offset_hour = digits(pos + 1, 2),
              offset_minute = digits(pos + 4, 2);
    return offset_hour >= 0 && offset_hour <= 23 && offset_minute >= 0 &&
           offset_minute <= 59;
  }
  static auto parse_value(state_type& s, std::size_t depth) -> std::size_t {
    using C = detail::NativeContext;
    whitespace(s, false);
    s.scalar = {};
    if (s.pos == s.input.size()) {
      fail(s, ErrorCode::unexpected_eof, "missing TOML value");
      return absent;
    }
    const auto start = s.pos;
    if (at(s, '[') || at(s, '{')) {
      if (depth > MaxDepth) {
        fail(s, ErrorCode::depth_limit_exceeded, "TOML container depth");
        return absent;
      }
      const bool array = at(s, '[');
      const char close = array ? ']' : '}';
      ++s.pos;
      const auto node = make_container(
          s, array ? TokenType::ArrayBegin : TokenType::ObjectBegin, start);
      whitespace(s, array);
      if (s.failure) return absent;
      while (!at(s, close)) {
        if (array) {
          const auto child = parse_value(s, depth + 1);
          if (child == absent) return absent;
          s.nodes[node].children.push_back(child);
        } else if (!assignment(s, node, depth))
          return absent;
        whitespace(s, array);
        if (s.failure) return absent;
        if (at(s, close)) break;
        if (!at(s, ',')) {
          fail(s, ErrorCode::unexpected_token, "expected TOML comma");
          return absent;
        }
        ++s.pos;
        whitespace(s, array);
        if (s.failure) return absent;
        if (!array && at(s, close)) {
          fail(s, ErrorCode::unexpected_token, "inline table trailing comma");
          return absent;
        }
      }
      s.nodes[node].end = s.pos++;
      if (!array)
        for (std::size_t i = node; i < s.nodes.size(); ++i)
          if (s.nodes[i].token.type == TokenType::ObjectBegin)
            s.nodes[i].sealed = true;
      return node;
    }
    if (at(s, '"') || at(s, '\'')) {
      if (!string(s)) return absent;
    } else {
      while (s.pos < s.input.size() &&
             std::string_view(" \t\r\n,#]}").find(s.input[s.pos]) ==
                 std::string_view::npos)
        ++s.pos;
      auto raw = s.input.substr(start, s.pos - start);
      // RFC 3339 permits a single space between a full date and its time.
      if (raw.size() == 10 && raw[4] == '-' && raw[7] == '-' && at(s, ' ') &&
          s.input.size() - s.pos >= 9 && s.input[s.pos + 3] == ':') {
        ++s.pos;
        while (s.pos < s.input.size() &&
               std::string_view(" \t\r\n,#]}").find(s.input[s.pos]) ==
                   std::string_view::npos)
          ++s.pos;
        raw = s.input.substr(start, s.pos - start);
      }
      if (raw == "true" || raw == "false") {
        s.scalar = raw == "true";
        scalar_token(s, start, C::boolean);
      } else if ((raw.size() >= 10 && raw[4] == '-' && raw[7] == '-') ||
                 raw.find(':') != std::string_view::npos) {
        if (!datetime(raw)) {
          fail(s, ErrorCode::invalid_token, "invalid TOML date/time");
          return absent;
        }
        scalar_token(s, start, C::datetime);
      } else {
        const int form = number_form(raw);
        std::string clean;
        for (char c : raw)
          if (c != '_') clean += c;
        if (form > 0) {
          std::string_view digits = clean;
          if (form != 10)
            digits.remove_prefix(2);
          else if (!digits.empty() && digits[0] == '+')
            digits.remove_prefix(1);
          std::int64_t number = 0;
          const auto result = std::from_chars(
              digits.data(), digits.data() + digits.size(), number, form);
          if (result.ec == std::errc::result_out_of_range) {
            fail(s, ErrorCode::out_of_range, "TOML integer exceeds int64");
            return absent;
          }
          if (result.ec != std::errc{} ||
              result.ptr != digits.data() + digits.size()) {
            fail(s, ErrorCode::invalid_number, "invalid TOML integer");
            return absent;
          }
          s.scalar = number;
        } else if (raw == "inf" || raw == "+inf" || raw == "-inf" ||
                   raw == "nan" || raw == "+nan" || raw == "-nan") {
          s.scalar = raw.ends_with("nan")
                         ? std::numeric_limits<double>::quiet_NaN()
                         : (raw.front() == '-'
                                ? -std::numeric_limits<double>::infinity()
                                : std::numeric_limits<double>::infinity());
        } else if (form == -10) {
          std::string_view digits = clean;
          if (digits[0] == '+') digits.remove_prefix(1);
          double number = 0;
          const auto result = std::from_chars(
              digits.data(), digits.data() + digits.size(), number);
          if (result.ec == std::errc::result_out_of_range) {
            fail(s, ErrorCode::out_of_range, "TOML float out of range");
            return absent;
          }
          if (result.ec != std::errc{} ||
              result.ptr != digits.data() + digits.size()) {
            fail(s, ErrorCode::invalid_number, "invalid TOML float");
            return absent;
          }
          s.scalar = number;
        } else {
          fail(s, ErrorCode::invalid_number, "invalid TOML value");
          return absent;
        }
        scalar_token(s, start, C::number);
      }
    }
    Node node;
    node.token = s.token;
    node.scalar = s.scalar;
    node.source = start;
    s.nodes.push_back(std::move(node));
    return s.nodes.size() - 1;
  }
};
}  // namespace hosq::toml
namespace hosq {
template <std::size_t N>
struct ReaderTraits<toml::Reader<N>> {
  static constexpr Format format = Format::toml;
  static constexpr bool is_binary_format = false;
};
namespace ext {
template <std::size_t N>
struct DecodeNumber<toml::Reader<N>> : detail::NativeDecodeNumber {};
template <std::size_t N>
struct DecodeNumberInto<toml::Reader<N>> : detail::NativeDecodeNumberInto {};
template <std::size_t N>
struct DecodeBoolean<toml::Reader<N>> : detail::NativeDecodeBoolean {};
template <std::size_t N>
struct DecodeStringView<toml::Reader<N>> : detail::NativeDecodeStringView {};
}  // namespace ext
static_assert(ReaderConcept<toml::Reader<>>);
}  // namespace hosq
