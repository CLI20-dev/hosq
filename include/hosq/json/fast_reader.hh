#pragma once

#include <array>
#include <bit>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>

#include "hosq/context.hh"
#include "hosq/detail/compiler.hh"
#include "hosq/error.hh"
#include "hosq/json/scalar_codec.hh"
#include "hosq/meta_match.hh"
#include "hosq/pipeline/decode.hh"
#include "hosq/pipeline/scan.hh"
#include "hosq/reader_base.hh"

namespace hosq::json::detail {
// EntryInput only constructs this tag after proving the scalar member prefix.
// The key and scan phase are compile-time facts; there is no runtime payload.
template <class Reader, StringLiteral Key, bool Scanned = false>
struct MemberInput {
 private:
  constexpr MemberInput() = default;
  friend struct hosq::ext::EntryInput<Reader>;
  friend struct hosq::ext::ScanScalar<Reader>;

 public:
  template <class Context>
  HOSQ_ALWAYS_INLINE auto state(Context& ctx) const ->
      typename Reader::state_type& {
    // Only EntryInput and ScanScalar can create this pending-member tag. Its
    // internal caller contract pairs it with the mutable state owned by
    // EntryInput, even when const tail dispatch presents a const context view.
    return const_cast<typename Reader::state_type&>(ctx.reader_state());
  }
  template <class Context>
  auto materialize(Context& ctx) const -> hosq::ScalarView;
};
template <class Reader>
struct ScalarInput {
  template <class Context>
  static auto materialize(Context& ctx) -> hosq::ScalarView;
};
template <class Reader>
struct ArrayInput {
  template <class Context>
  static auto materialize(Context& ctx) -> hosq::ArrayView;
};
}  // namespace hosq::json::detail

namespace hosq::json::detail {
// Bit 0: JSON whitespace. Bit 1: an unquoted scalar terminator.
// Preserve the existing structural-skip contract, including control bytes.
inline constexpr auto fast_byte_classes = [] {
  std::array<unsigned char, 256> flags{};
  flags[9] = flags[10] = flags[13] = flags[32] = 1;
  for (int i = 0; i <= 32; ++i) flags[i] |= 2;
  for (int i : {44, 125, 93, 58}) flags[i] |= 2;
  return flags;
}();
}  // namespace hosq::json::detail

namespace hosq {
namespace json {

// Padded modes require the named number of readable zero bytes after the
// logical input. No address alignment, mutation or ownership is required.
enum class InputPadding : std::uint8_t {
  none = 0,
  zero_padded_4 = 4,
  zero_padded_8 = 8
};

// This is a compile-time whitespace hint. Every mode accepts all JSON
// whitespace; it only selects the local fast path at a token boundary.
enum class WhitespaceMode : std::uint8_t { Pretty, Compact, Auto };

enum class JsonFastExpect : std::uint8_t {
  value,
  value_or_end,
  key_or_end,
  colon_delimiter,
  comma_delimiter,
};

template <std::size_t MaxDepth>
struct JsonFastReaderCheckpoint {
  const char* pos;
  std::size_t ctx_size;
  JsonFastExpect expect;
};

// State for FastReader. Uses checkpoint()/rollback() that intentionally exclude
// `current` so that lazily-resolved string/literal lengths survive rollback and
// are reused by subsequent Try candidates without re-scanning.
template <std::size_t MaxDepth>
struct JsonFastReaderState {
  const char* begin{nullptr};
  mutable const char* pos{nullptr};
  const char* end{nullptr};

  std::string_view input_view{};

  Token current{
      .offset = 0,
      .length = 0,
      .type = TokenType::End,
      .context = static_cast<std::uint8_t>(JsonTokenContext::none),
  };

  std::array<std::uint8_t, MaxDepth> ctx_stack{};
  std::size_t ctx_size{0};

  JsonFastExpect expect{JsonFastExpect::value};
  mutable hosq::detail::ErrorInfo last_error{hosq::detail::ErrorInfo::ok()};
  mutable bool error_flag{false};
  mutable Token error_token{
      .offset = 0,
      .length = 0,
      .type = TokenType::Error,
      .context = 0,
  };

  [[nodiscard]] auto checkpoint() const noexcept
      -> JsonFastReaderCheckpoint<MaxDepth> {
    return {pos, ctx_size, expect};
  }

  auto rollback(const JsonFastReaderCheckpoint<MaxDepth>& cp) noexcept -> void {
    pos = cp.pos;
    ctx_size = cp.ctx_size;
    expect = cp.expect;
    // `current` is intentionally NOT restored — cached lengths are reused
  }
};

template <std::size_t MaxDepth = 128, InputPadding Padding = InputPadding::none,
          WhitespaceMode Mode = WhitespaceMode::Auto>
class FastReader : public ReaderBase<FastReader<MaxDepth, Padding, Mode>> {
 public:
  static constexpr auto input_padding = Padding;
  static constexpr auto whitespace_mode = Mode;
  static constexpr std::size_t padding_bytes = static_cast<std::size_t>(Padding);
  using input_type = std::string_view;
  using state_type = JsonFastReaderState<MaxDepth>;

  FastReader() = default;

  // The concrete object lives throughout the callback. Its pointer must not
  // escape. State is shared, so cursor/error/checkpoint semantics are unchanged.
  template <class Function>
    requires(Mode == WhitespaceMode::Auto)
  HOSQ_ALWAYS_INLINE decltype(auto) with_reader(state_type& state,
                                                Function&& function) const {
    const char* p = state.begin;
    const char* end = state.end;
    while (p != end && is_ws(*p)) ++p;
    const bool pretty = p != end && end - p >= 3 && (*p == '{' || *p == '[') &&
                        is_ws(p[1]) && is_ws(p[2]);
    if (pretty) {
      FastReader<MaxDepth, Padding, WhitespaceMode::Pretty> reader;
      return std::forward<Function>(function)(&reader);
    }
    FastReader<MaxDepth, Padding, WhitespaceMode::Compact> reader;
    return std::forward<Function>(function)(&reader);
  }

  HOSQ_ALWAYS_INLINE auto reset(state_type& st, input_type input) const noexcept
      -> void {
    st.begin = input.data();
    st.pos = input.data();
    st.end = input.data() + input.size();
    st.input_view = input;
    st.ctx_size = 0;
    st.expect = JsonFastExpect::value;
    st.last_error = hosq::detail::ErrorInfo::ok();
    st.error_flag = false;
    st.current = {
        .offset = 0,
        .length = 0,
        .type = TokenType::End,
        .context = static_cast<std::uint8_t>(JsonTokenContext::none),
    };
  }

  HOSQ_ALWAYS_INLINE auto next_token_impl(state_type& st) const noexcept
      -> bool {
    if (st.error_flag) [[unlikely]]
      return false;
    Token tok{};
    if (!read_one(st, tok)) [[unlikely]] {
      return false;
    }
    st.current = tok;
    clear_success_error(st);
    return true;
  }

  // A hint only selects a fast path. A custom pipeline may have advanced the
  // reader itself, so unexpected states still use the original state machine.
  template <Expect E, JsonTokenContext Kind = JsonTokenContext::none>
  HOSQ_ALWAYS_INLINE auto next_token_impl(state_type& st) const noexcept
      -> bool {
    if (st.error_flag) [[unlikely]]
      return false;
    Token tok{};
    bool ok;
    if constexpr (E == Expect::value) {
      ok = st.expect == JsonFastExpect::value ? read_value<Kind>(st, tok)
                                              : read_one(st, tok);
    } else if constexpr (E == Expect::member_value) {
      ok = st.expect == JsonFastExpect::colon_delimiter
               ? read_colon_then_value<Kind>(st, tok)
               : read_one(st, tok);
    } else if constexpr (E == Expect::next_member_or_end) {
      if (st.expect == JsonFastExpect::key_or_end)
        ok = read_key(st, true, tok);
      else if (st.expect == JsonFastExpect::comma_delimiter)
        ok = read_comma_or_close<Kind>(st, tok);
      else
        ok = read_one(st, tok);
    } else if constexpr (E == Expect::next_element_or_end) {
      if (st.expect == JsonFastExpect::value_or_end)
        ok = read_value_or_array_end<Kind>(st, tok);
      else if (st.expect == JsonFastExpect::comma_delimiter)
        ok = read_comma_or_close<Kind>(st, tok);
      else
        ok = read_one(st, tok);
    } else {
      ok = st.expect == JsonFastExpect::comma_delimiter
               ? read_comma_or_close<Kind>(st, tok)
               : read_one(st, tok);
    }
    if (!ok) [[unlikely]]
      return false;
    st.current = tok;
    clear_success_error(st);
    return true;
  }

  // These callers have a static position proof. Validate the input directly;
  // there is no runtime expect load, switch, or advisory-state guard here.
  template <ReadPosition Position>
  HOSQ_ALWAYS_INLINE auto next_token_impl(state_type& st) const noexcept
      -> bool {
    if (st.error_flag) [[unlikely]]
      return false;
    Token tok{};
    bool ok;
    if constexpr (Position == ReadPosition::value)
      ok = read_value(st, tok);
    else
      ok = read_colon_then_value(st, tok);
    if (!ok) [[unlikely]]
      return false;
    st.current = tok;
    clear_success_error(st);
    return true;
  }

 private:
  // Clear a previous failure when a subsequent reader operation succeeds.
  static HOSQ_ALWAYS_INLINE void clear_success_error(state_type& st) noexcept {
    const auto canonical = hosq::detail::ErrorInfo::ok();
    if (st.last_error.code == canonical.code) {
      return;
    }
    st.last_error = canonical;
  }

  template <StringLiteral Key>
  HOSQ_ALWAYS_INLINE const char* const_member_start(const state_type& st) const {
    constexpr bool plain = [] {
      for (std::size_t i = 0; i < Key.size(); ++i) {
        const unsigned char c = Key.value[i];
        if (c < 0x20 || c >= 0x80 || c == '"' || c == '\\') return false;
      }
      return true;
    }();
    if constexpr (!plain) return nullptr;
    if (st.error_flag || !in_object(st)) return nullptr;
    const char* p = st.pos;
    const bool comma = st.expect == JsonFastExpect::comma_delimiter;
    if (comma) {
      if (st.current.length == unresolved_length) return nullptr;
    } else if (st.expect != JsonFastExpect::key_or_end)
      return nullptr;
    static constexpr auto punctuated = [] {
      std::array<char, Key.size() + 4> result{};
      result[0] = ',';
      result[1] = '"';
      for (std::size_t i = 0; i < Key.size(); ++i) result[i + 2] = Key.value[i];
      result[Key.size() + 2] = '"';
      result.back() = ':';
      return result;
    }();
    const auto prefix_size = punctuated.size() - (comma ? 0 : 1);
    if (std::size_t(st.end - p) >= prefix_size &&
        (comma ? std::memcmp(p, punctuated.data(), punctuated.size()) == 0
               : std::memcmp(p, punctuated.data() + 1, punctuated.size() - 1) ==
                     0)) {
      p += prefix_size;
    } else {
      if (comma) {
        if (!skip_whitespace(p, st.end) || *p != ',') return nullptr;
        ++p;
      }
      if (!skip_whitespace(p, st.end)) return nullptr;
      constexpr auto quoted_size = Key.size() + 2;
      if (std::size_t(st.end - p) < quoted_size ||
          std::memcmp(p, punctuated.data() + 1, quoted_size) != 0)
        return nullptr;
      p += quoted_size;
      if (!skip_whitespace(p, st.end) || *p != ':') return nullptr;
      ++p;
    }
    if (!skip_whitespace(p, st.end)) return nullptr;
    return p;
  }

 public:
  // A successful probe performs the normal key/value transition without
  // materializing an intermediate key token. A miss leaves all state intact.
  template <StringLiteral Key>
  HOSQ_ALWAYS_INLINE bool try_read_const_member(state_type& st) const {
    const char* p = const_member_start<Key>(st);
    if (!p) return false;
    const char c = *p;
    if (c == '{' || c == '[') {
      if (st.ctx_size == MaxDepth) return false;
      st.ctx_stack[st.ctx_size++] = c == '{' ? 1 : 0;
      st.pos = p + 1;
      st.current = make_tok(
          off(st, p), 1,
          c == '{' ? TokenType::ObjectBegin : TokenType::ArrayBegin, kNoneCtx);
      st.expect =
          c == '{' ? JsonFastExpect::key_or_end : JsonFastExpect::value_or_end;
    } else {
      if (c != '"' && !is_num_start(c) && c != 't' && c != 'f' && c != 'n')
        return false;
      const bool string = c == '"';
      st.pos = p + (string ? 1 : 0);
      st.current = lazy_scalar(off(st, st.pos), string ? kStrCtx : kLitCtx);
      st.expect = JsonFastExpect::comma_delimiter;
    }
    clear_success_error(st);
    return true;
  }

  // A hit establishes the pending-member phase encoded by MemberInput.
  // No key or scalar token is materialized until its consumer needs one.
  template <StringLiteral Key>
  HOSQ_ALWAYS_INLINE bool begin_const_scalar_member(state_type& st) const {
    const char* p = const_member_start<Key>(st);
    if (!p ||
        (*p != '"' && !is_num_start(*p) && *p != 't' && *p != 'f' && *p != 'n'))
      return false;
    st.pos = p;
    return true;
  }

  HOSQ_ALWAYS_INLINE bool begin_matched_scalar_member(state_type& st) const {
    if (st.error_flag || st.expect != JsonFastExpect::colon_delimiter)
      return false;
    const char* p = st.pos;
    if (!skip_whitespace(p, st.end) || *p != ':') return false;
    ++p;
    if (!skip_whitespace(p, st.end)) return false;
    if (*p != '"' && !is_num_start(*p) && *p != 't' && *p != 'f' && *p != 'n')
      return false;
    st.pos = p;
    return true;
  }

  HOSQ_ALWAYS_INLINE void materialize_scalar_member(state_type& st) const {
    const bool string = *st.pos == '"';
    st.pos += string ? 1 : 0;
    st.current = lazy_scalar(off(st, st.pos), string ? kStrCtx : kLitCtx);
    st.expect = JsonFastExpect::comma_delimiter;
    clear_success_error(st);
  }

  // These value operations require the pending-member phase established by
  // begin_const_scalar_member. A miss leaves it intact for materialization.
  HOSQ_ALWAYS_INLINE bool try_read_member_string_view(
      state_type& st, std::string_view& value) const {
    const char* p = st.pos;

    const char c = *p;

    const bool string = c == '"';
    const char* const first = p + (string ? 1 : 0);
    if (string) {
      p = string_end(first, st.end);
      if (p == st.end) return false;
    } else {
      p = first;
      while (Padding != InputPadding::none || p < st.end) {
        const auto byte = static_cast<std::uint8_t>(*p);
        if (hosq::json::detail::fast_byte_classes[byte] & 2) break;
        ++p;
      }
    }

    const auto length = static_cast<std::size_t>(p - first);
    const auto parsed = std::string_view(first, length);
    st.current = make_tok(off(st, first), length, TokenType::Scalar,
                          string ? kStrCtx : kLitCtx);
    st.pos = p + (string ? 1 : 0);
    st.expect = JsonFastExpect::comma_delimiter;
    clear_success_error(st);
    value = parsed;
    return true;
  }

  HOSQ_ALWAYS_INLINE bool try_read_member_boolean(state_type& st,
                                                  bool& value) const {
    const char* p = st.pos;

    const bool string = *p == '"';
    const char* const first = p + (string ? 1 : 0);
    const auto available = static_cast<std::size_t>(st.end - first);
    std::size_t length;
    bool parsed;
    if (available >= 4 && std::memcmp(first, "true", 4) == 0) {
      length = 4;
      parsed = true;
    } else if (available >= 5 && std::memcmp(first, "false", 5) == 0) {
      length = 5;
      parsed = false;
    } else {
      return false;
    }

    p = first + length;
    if (string) {
      if (p == st.end || *p != '"') return false;
    } else if (p != st.end &&
               !(hosq::json::detail::fast_byte_classes[static_cast<std::uint8_t>(
                     *p)] &
                 2)) {
      return false;
    }

    st.current = make_tok(off(st, first), length, TokenType::Scalar,
                          string ? kStrCtx : kLitCtx);
    st.pos = p + (string ? 1 : 0);
    st.expect = JsonFastExpect::comma_delimiter;
    clear_success_error(st);
    value = parsed;
    return true;
  }

  template <class T>
  HOSQ_ALWAYS_INLINE bool try_read_member_number(state_type& st,
                                                 T& value) const {
    const char* p = st.pos;
    const bool string = *p == '"';
    const char* first = p + (string ? 1 : 0);
    T parsed{};
    const auto result = parse_number(first, st.end, parsed);
    if (result.ec != std::errc{}) return false;
    p = result.ptr;
    const bool complete =
        string ? p < st.end && *p == '"'
               : p == st.end || static_cast<unsigned char>(*p) <= ' ' ||
                     *p == ',' || *p == '}' || *p == ']' || *p == ':';
    if (!complete) return false;
    st.current = make_tok(off(st, first), static_cast<std::size_t>(p - first),
                          TokenType::Scalar, string ? kStrCtx : kLitCtx);
    st.pos = p + (string ? 1 : 0);
    st.expect = JsonFastExpect::comma_delimiter;
    clear_success_error(st);
    value = parsed;
    return true;
  }

  // Optional transactional operation for an exact built-in numeric collect.
  // The ordinary Collect path remains responsible for every diagnostic: a
  // miss leaves both the Reader state and output untouched.
  template <class T, std::size_t N>
    requires decode::detail::Number<T>
  HOSQ_ALWAYS_INLINE bool try_read_number_array(state_type& st,
                                                std::array<T, N>& value) const {
    if (st.error_flag || st.expect != JsonFastExpect::value_or_end ||
        st.ctx_size == 0 || st.ctx_stack[st.ctx_size - 1] != 0 ||
        st.current.type != TokenType::ArrayBegin) {
      return false;
    }

    const char* p = st.pos;
    std::array<T, N> parsed{};

    for (std::size_t index = 0; index < N; ++index) {
      if (!skip_whitespace(p, st.end)) return false;

      const bool string = *p == '"';
      if constexpr (!std::integral<T>) {
        if (!string && !is_num_start(*p)) return false;
      }
      const char* first = p + (string ? 1 : 0);
      const auto result = parse_number(first, st.end, parsed[index]);
      if (result.ec != std::errc{}) return false;

      p = result.ptr;
      if (string) {
        if (p == st.end || *p != '"') return false;
        ++p;
      }

      if (!skip_whitespace(p, st.end)) return false;
      if (index + 1 < N) {
        if (*p != ',') return false;
        ++p;
      }
    }

    if constexpr (N == 0) {
      if (!skip_whitespace(p, st.end)) return false;
    }
    if (*p != ']') return false;

    value = parsed;
    st.current = make_tok(off(st, p), 1, TokenType::ArrayEnd, kNoneCtx);
    st.pos = p + 1;
    pop_ctx(st);
    st.expect = JsonFastExpect::comma_delimiter;
    clear_success_error(st);
    return true;
  }

  template <class... Handlers, class ScanKey, bool DeferredKey>
  HOSQ_ALWAYS_INLINE bool match_const_key(
      state_type& st, std::tuple<Handlers...>& handlers, ScanKey scan_key,
      std::bool_constant<DeferredKey>) const {
    constexpr bool builtin_skip = [] {
      if constexpr (requires {
                      typename ext::ScanScalar<FastReader>::member_skip_reader;
                      typename ext::DecodeSkipValue<
                          FastReader>::member_skip_reader;
                    }) {
        return std::same_as<
                   typename ext::ScanScalar<FastReader>::member_skip_reader,
                   FastReader> &&
               std::same_as<
                   typename ext::DecodeSkipValue<FastReader>::member_skip_reader,
                   FastReader>;
      }
      return false;
    }();
    constexpr bool plain_keys = [] {
      return (([] {
                for (unsigned char c : Handlers::view()) {
                  if (c < 0x20 || c >= 0x80 || c == '"' || c == '\\')
                    return false;
                }
                return true;
              }()) &&
              ...);
    }();
    if constexpr (plain_keys) {
      auto& tok = st.current;
      if (!st.error_flag && tok.type == TokenType::Scalar &&
          tok.context == kStrCtx && tok.length == unresolved_length) {
        for (;;) {
          const char* first = st.begin + tok.offset;
          auto commit = [&](std::size_t length) {
            tok.length = length;
            st.pos = first + length + 1;
          };
          if (meta::match_terminated<'"'>(
                  std::string_view(first, st.end - first), handlers, commit))
            return true;
          if constexpr (DeferredKey) {
            // Only the standard skip pair permits bypassing its callbacks.
            // Continue at the next key; a final member or uncertain prefix
            // stays on the ordinary path, including its exact error handling.
            if constexpr (builtin_skip) {
              if (try_skip_scalar_member(st)) continue;
            }
          } else {
            (void)scan_key();
          }
          return false;
        }
      }
    }
    return meta::match(scan_key(), handlers);
  }

  // Keep integer digits inline; use the shared backend for exceptional input.
  // Float/double use JSON from_chars over the full remaining bounded input.
  template <class T>
  static HOSQ_ALWAYS_INLINE auto parse_number(const char* first, const char* end,
                                              T& out) -> std::from_chars_result {
    if constexpr (std::integral<T> && sizeof(T) <= sizeof(uint64_t)) {
      const char* p = first;
      bool negative = false;
      if constexpr (std::is_signed_v<T>) {
        if (p != end && *p == '-') {
          negative = true;
          ++p;
        }
      }
      const char* digits = p;
      const uint64_t limit =
          uint64_t(std::numeric_limits<T>::max()) + uint64_t(negative);
      uint64_t number = 0;
      if constexpr (sizeof(T) >= 4 &&
                    std::endian::native == std::endian::little) {
        while (padding_bytes >= 8 || end - p >= 8) {
          uint64_t bytes;
          std::memcpy(&bytes, p, 8);
          if (((bytes + 0x4646464646464646ULL) |
               (bytes - 0x3030303030303030ULL)) &
              0x8080808080808080ULL)
            break;
          uint64_t group = bytes & 0x0f0f0f0f0f0f0f0fULL;
          group = (group * 10 + (group >> 8)) & 0x00ff00ff00ff00ffULL;
          group = (group * 100 + (group >> 16)) & 0x0000ffff0000ffffULL;
          group = (group * 10000 + (group >> 32)) & 0xffffffffULL;
          if (number > (limit - group) / 100000000ULL)
            return hosq::detail::number_from_chars<T, true>(first, end, out);
          number = number * 100000000ULL + group;
          p += 8;
        }
      }
      while (Padding != InputPadding::none || p != end) {
        const auto digit = unsigned(static_cast<unsigned char>(*p)) - '0';
        if (digit > 9) break;
        if (number > limit / 10 || (number == limit / 10 && digit > limit % 10))
          return hosq::detail::number_from_chars<T, true>(first, end, out);
        number = number * 10 + digit;
        ++p;
      }
      if (p == digits)
        return hosq::detail::number_from_chars<T, true>(first, end, out);
      if constexpr (std::is_signed_v<T>) {
        if (negative && number != 0)
          out = T(-static_cast<T>(number - 1) - 1);
        else
          out = static_cast<T>(number);
      } else {
        out = static_cast<T>(number);
      }
      return {p, std::errc{}};
    } else {
      return hosq::detail::number_from_chars<T, true>(first, end, out);
    }
  }

  // The adjacent scalar scan + numeric decode can use from_chars' end
  // pointer as the token boundary. No preliminary scalar-length pass is needed.
  // Resolved tokens, invalid prefixes and escapes keep the original path so
  // error details, partial writes and the cache used by Try remain unchanged.
  template <class T, bool Into>
  HOSQ_ALWAYS_INLINE auto read_number_impl(const state_type& st, T& value) const
      -> bool {
    const auto& tok = current_token(st);
    if (tok.type == TokenType::Scalar && tok.length == unresolved_length &&
        (tok.context == kStrCtx || tok.context == kLitCtx)) {
      const char* first = st.begin + tok.offset;
      T parsed{};
      const auto result = parse_number(first, st.end, parsed);
      if (result.ec == std::errc{}) {
        const char* p = result.ptr;
        const bool string = tok.context == kStrCtx;
        const bool complete =
            string ? p < st.end && *p == '"'
                   : p == st.end || static_cast<unsigned char>(*p) <= ' ' ||
                         *p == ',' || *p == '}' || *p == ']' || *p == ':';
        if (complete) {
          tok.length = static_cast<std::size_t>(p - first);
          st.pos = p + (string ? 1 : 0);
          value = parsed;
          return true;
        }
      }
    }
    return read_number_fallback<T, Into>(st, value);
  }

 private:
  template <class T, bool Into>
  auto read_number_fallback(const state_type& st, T& value) const -> bool {
    auto& reader = const_cast<FastReader&>(*this);
    auto& state = const_cast<state_type&>(st);
    const DecodeContext<T, FastReader> ctx{value, reader, state};
    auto scalar = ext::ScanScalar<FastReader>::resolved(ctx);
    if constexpr (Into) {
      ext::DecodeNumberIntoDefault<FastReader>{}.template operator()<T>(scalar,
                                                                        ctx);
    } else {
      value = ext::DecodeNumberDefault<FastReader>{}.template operator()<T>(
          scalar, ctx);
    }
    return hosq::detail::reader_error(*this, st).is_ok();
  }

 public:
  // Sets the error flag and records err (first call wins; subsequent calls are
  // no-ops). After this, current_token() always returns a TokenType::Error
  // token.
  void report_error(const state_type& st, Error err) const noexcept {
    if (!st.error_flag) {
      st.last_error = err;
      st.error_flag = true;
    }
  }

  [[nodiscard]] HOSQ_ALWAYS_INLINE auto current_token(
      const state_type& st) const noexcept -> const Token& {
    if (st.error_flag) [[unlikely]]
      return st.error_token;
    return st.current;
  }

  // Mutable overload used by ext::ScanScalar specializations to cache resolved
  // lengths directly into the token.
  [[nodiscard]] HOSQ_ALWAYS_INLINE auto current_token(
      state_type& st) const noexcept -> Token& {
    if (st.error_flag) [[unlikely]]
      return st.error_token;
    return st.current;
  }

  [[nodiscard]] HOSQ_ALWAYS_INLINE auto error(
      const state_type& st) const noexcept -> Error {
    return st.last_error;
  }

  [[nodiscard]] HOSQ_ALWAYS_INLINE auto input(
      const state_type& st) const noexcept -> BytesView {
    return BytesView::from_string_view(st.input_view);
  }

  // Scan full bounded blocks; escape handling and the short tail stay scalar.
  // No padding, prepass, allocation or decoded-string buffer is required.
  static HOSQ_ALWAYS_INLINE auto string_end(const char* p,
                                            const char* end) noexcept -> const
      char* {
    constexpr std::size_t width = 16;
    while (end - p >= static_cast<std::ptrdiff_t>(width)) {
      // A local copy makes independence from the input explicit to the
      // optimizer. The optimized loop keeps these bytes in registers.
      std::array<unsigned char, width> matches;
      std::memcpy(matches.data(), p, width);
#if defined(__clang__)
      // Early scalar unrolling prevents loop vectorization in Clang.
#pragma clang loop unroll(disable)
#endif
      for (auto& byte : matches) {
        byte = static_cast<unsigned char>(-int((byte == '"') | (byte == '\\')));
      }
      const auto words = std::bit_cast<std::array<uint64_t, 2>>(matches);
      const uint64_t bits = words[0] ? words[0] : words[1];
      if (bits == 0) {
        p += width;
        continue;
      }
      std::size_t offset = words[0] ? 0 : 8;
      if constexpr (std::endian::native == std::endian::little) {
        offset += std::countr_zero(bits) / 8;
      } else if constexpr (std::endian::native == std::endian::big) {
        offset += std::countl_zero(bits) / 8;
      } else {
        offset = 0;
        while (!matches[offset]) ++offset;
      }
      p += offset;
      if (*p == '"') return p;
      if (end - p < 2) return end;
      p += 2;
    }
    while (p < end) {
      if (*p == '"') return p;
      if (*p == '\\') {
        if (end - p < 2) return end;
        p += 2;
      } else {
        ++p;
      }
    }
    return end;
  }

  // Advances st.pos past the current scalar token and caches its length.
  // Called by the state machine when a Scalar was never scanned by an ext::
  // step.
  HOSQ_ALWAYS_INLINE auto resolve_skip(state_type& st) const noexcept -> bool {
    if (st.current.length != unresolved_length) return true;
    if (st.current.context ==
        static_cast<std::uint8_t>(JsonTokenContext::string)) {
      const char* p = string_end(st.begin + st.current.offset, st.end);
      if (p != st.end) {
        st.current.length =
            static_cast<std::size_t>(p - (st.begin + st.current.offset));
        st.pos = p + 1;
        return true;
      }
      st.last_error = hosq::detail::ErrorInfo{
          .code = ErrorCode::unexpected_eof,
          .offset = static_cast<std::size_t>(st.end - st.begin),
          .detail = "unterminated string while skipping scalar"};
      return false;
    } else {
      // literal
      const char* p = st.begin + st.current.offset;
      while (Padding != InputPadding::none || p < st.end) {
        const auto c = static_cast<std::uint8_t>(*p);
        if (hosq::json::detail::fast_byte_classes[c] & 2) break;
        ++p;
      }
      st.current.length =
          static_cast<std::size_t>(p - (st.begin + st.current.offset));
      st.pos = p;
      return true;
    }
  }

 private:
  static constexpr auto kStrCtx =
      static_cast<std::uint8_t>(JsonTokenContext::string);
  static constexpr auto kLitCtx =
      static_cast<std::uint8_t>(JsonTokenContext::literal);
  static constexpr auto kNoneCtx =
      static_cast<std::uint8_t>(JsonTokenContext::none);

  HOSQ_ALWAYS_INLINE bool try_skip_scalar_member(state_type& st) const noexcept {
    const char* p = string_end(st.begin + st.current.offset, st.end);
    if (p == st.end) return false;
    ++p;
    if (!skip_whitespace(p, st.end) || *p != ':') return false;
    ++p;
    if (!skip_whitespace(p, st.end)) return false;
    if (*p == '"') {
      p = string_end(p + 1, st.end);
      if (p == st.end) return false;
      ++p;
    } else {
      if (!is_num_start(*p) && *p != 't' && *p != 'f' && *p != 'n') return false;
      do {
        ++p;
      } while (p != st.end &&
               !(detail::fast_byte_classes[static_cast<unsigned char>(*p)] & 2));
    }
    if (!skip_whitespace(p, st.end) || *p != ',') return false;
    ++p;
    if (!skip_whitespace(p, st.end) || *p != '"') return false;

    // No state is changed until both this scalar member and the next key's
    // opening quote are known. Containers and errors use the existing fallback.
    st.pos = p + 1;
    st.current = lazy_scalar(off(st, p + 1), kStrCtx);
    st.expect = JsonFastExpect::colon_delimiter;
    return true;
  }

  static HOSQ_ALWAYS_INLINE auto is_ws(char c) noexcept -> bool {
    return (detail::fast_byte_classes[static_cast<unsigned char>(c)] & 1) != 0;
  }

  static HOSQ_ALWAYS_INLINE auto skip_ws_generic(const char*& p,
                                                 const char* end) noexcept
      -> bool {
    if constexpr (Padding != InputPadding::none) {
      while (is_ws(*p)) ++p;
    } else {
      while (p < end && is_ws(*p)) ++p;
    }
    return p < end;
  }

  static HOSQ_ALWAYS_INLINE auto skip_ws_pretty(const char*& p,
                                                const char* end) noexcept
      -> bool {
    if constexpr (Padding != InputPadding::none) {
      while (is_ws(*p)) {
        if (*p == ' ' && std::memcmp(p, "    ", 4) == 0)
          p += 4;
        else
          ++p;
      }
    } else {
      while (p < end && is_ws(*p)) {
        if (*p == ' ' && end - p >= 4 && std::memcmp(p, "    ", 4) == 0)
          p += 4;
        else
          ++p;
      }
    }
    return p < end;
  }

 public:
  static HOSQ_ALWAYS_INLINE auto skip_whitespace(const char*& p,
                                                 const char* end) noexcept
      -> bool {
    if (p >= end) return false;
    if constexpr (Mode == WhitespaceMode::Compact) {
      return is_ws(*p) ? skip_ws_generic(p, end) : true;
    } else if constexpr (Mode == WhitespaceMode::Pretty) {
      return skip_ws_pretty(p, end);
    } else {
      // Auto makes a local choice from the byte we had to inspect anyway.
      return is_ws(*p) ? skip_ws_pretty(p, end) : true;
    }
  }

 private:
  static HOSQ_ALWAYS_INLINE auto off(const state_type& st,
                                     const char* p) noexcept -> std::size_t {
    return static_cast<std::size_t>(p - st.begin);
  }

  static HOSQ_ALWAYS_INLINE auto is_num_start(char c) noexcept -> bool {
    return c == '-' || (c >= '0' && c <= '9');
  }

  static HOSQ_ALWAYS_INLINE auto make_tok(std::size_t o, std::size_t len,
                                          TokenType type,
                                          std::uint8_t ctx) noexcept -> Token {
    return {.offset = o, .length = len, .type = type, .context = ctx};
  }

  static HOSQ_ALWAYS_INLINE auto lazy_scalar(std::size_t content_off,
                                             std::uint8_t ctx) noexcept
      -> Token {
    return make_tok(content_off, unresolved_length, TokenType::Scalar, ctx);
  }

  HOSQ_ALWAYS_INLINE auto push_ctx(state_type& st, std::uint8_t kind,
                                   std::size_t err_off) const noexcept -> bool {
    if (st.ctx_size >= MaxDepth) [[unlikely]] {
      st.last_error =
          hosq::detail::ErrorInfo{.code = ErrorCode::depth_limit_exceeded,
                                  .offset = err_off,
                                  .detail = "fast reader container depth limit"};
      return false;
    }
    st.ctx_stack[st.ctx_size++] = kind;
    return true;
  }

  HOSQ_ALWAYS_INLINE auto pop_ctx(state_type& st) const noexcept -> void {
    if (st.ctx_size > 0) --st.ctx_size;
  }

  [[nodiscard]]
  HOSQ_ALWAYS_INLINE auto in_object(const state_type& st) const noexcept
      -> bool {
    return st.ctx_size > 0 && st.ctx_stack[st.ctx_size - 1] == 1;
  }

  auto read_one(state_type& st, Token& out) const noexcept -> bool {
    switch (st.expect)
      [[likely]] {
        case JsonFastExpect::value:
          return read_value(st, out);
        case JsonFastExpect::value_or_end:
          return read_value_or_array_end(st, out);
        case JsonFastExpect::key_or_end:
          return read_key(st, true, out);
        case JsonFastExpect::colon_delimiter:
          return read_colon_then_value(st, out);
        case JsonFastExpect::comma_delimiter:
          return read_comma_or_close(st, out);
      }
    st.last_error = hosq::detail::ErrorInfo{
        .code = ErrorCode::invalid_state,
        .detail = "unknown fast reader expectation state"};
    return false;
  }

  template <JsonTokenContext Kind = JsonTokenContext::none>
  HOSQ_ALWAYS_INLINE auto read_value(state_type& st, Token& out) const noexcept
      -> bool {
    const char* p = st.pos;

    if (!skip_whitespace(p, st.end)) [[unlikely]] {
      st.pos = p;
      st.last_error = hosq::detail::ErrorInfo{.code = ErrorCode::unexpected_eof,
                                              .offset = off(st, p),
                                              .detail = "expected JSON value"};
      return false;
    }

    const char c = *p;

    auto string_value = [&]() {
      const auto o = off(st, p);
      st.expect = JsonFastExpect::comma_delimiter;
      st.pos = p + 1;

      out.offset = o + 1;
      out.length = unresolved_length;
      out.type = TokenType::Scalar;
      out.context = kStrCtx;
      return true;
    };
    auto literal_value = [&]() {
      const auto o = off(st, p);
      st.expect = JsonFastExpect::comma_delimiter;
      st.pos = p;

      out.offset = o;
      out.length = unresolved_length;
      out.type = TokenType::Scalar;
      out.context = kLitCtx;
      return true;
    };
    if constexpr (Kind == JsonTokenContext::literal) {
      if (is_num_start(c) || c == 't' || c == 'f' || c == 'n')
        return literal_value();
      if (c == '"') return string_value();
    } else {
      if (c == '"') return string_value();
      if (is_num_start(c) || c == 't' || c == 'f' || c == 'n')
        return literal_value();
    }

    if (c == '{') {
      const auto o = off(st, p);
      if (!push_ctx(st, 1, o)) [[unlikely]] {
        return false;
      }
      st.expect = JsonFastExpect::key_or_end;
      st.pos = p + 1;

      out.offset = o;
      out.length = 1;
      out.type = TokenType::ObjectBegin;
      out.context = kNoneCtx;
      return true;
    }

    if (c == '[') {
      const auto o = off(st, p);
      if (!push_ctx(st, 0, o)) [[unlikely]] {
        return false;
      }
      st.expect = JsonFastExpect::value_or_end;
      st.pos = p + 1;

      out.offset = o;
      out.length = 1;
      out.type = TokenType::ArrayBegin;
      out.context = kNoneCtx;
      return true;
    }

    const auto o = off(st, p);
    st.pos = p;
    st.last_error = hosq::detail::ErrorInfo{.code = ErrorCode::unexpected_token,
                                            .offset = o,
                                            .detail = "expected JSON value"};
    return false;
  }

  template <JsonTokenContext Kind = JsonTokenContext::none>
  HOSQ_ALWAYS_INLINE auto read_value_or_array_end(state_type& st,
                                                  Token& out) const noexcept
      -> bool {
    const char* p = st.pos;
    if (!skip_whitespace(p, st.end)) [[unlikely]] {
      st.pos = p;
      st.last_error =
          hosq::detail::ErrorInfo{.code = ErrorCode::unexpected_eof,
                                  .offset = off(st, p),
                                  .detail = "expected array value or end"};
      return false;
    }
    if (*p == ']') {
      pop_ctx(st);
      st.expect = JsonFastExpect::comma_delimiter;
      st.pos = p + 1;
      out = make_tok(off(st, p), 1, TokenType::ArrayEnd, kNoneCtx);
      return true;
    }
    st.pos = p;
    return read_value<Kind>(st, out);
  }

  HOSQ_ALWAYS_INLINE auto read_key(state_type& st, bool allow_end,
                                   Token& out) const noexcept -> bool {
    const char* p = st.pos;
    if (!skip_whitespace(p, st.end)) [[unlikely]] {
      st.pos = p;
      st.last_error =
          hosq::detail::ErrorInfo{.code = ErrorCode::unexpected_eof,
                                  .offset = off(st, p),
                                  .detail = "expected object key or end"};
      return false;
    }

    const char c = *p;
    const std::size_t o = off(st, p);

    if (c == '"') {
      st.expect = JsonFastExpect::colon_delimiter;
      st.pos = p + 1;
      out = lazy_scalar(o + 1, kStrCtx);
      return true;
    }

    if (allow_end && c == '}') {
      pop_ctx(st);
      st.expect = JsonFastExpect::comma_delimiter;
      st.pos = p + 1;
      out = make_tok(o, 1, TokenType::ObjectEnd, kNoneCtx);
      return true;
    }

    st.pos = p;
    st.last_error = hosq::detail::ErrorInfo{
        .code = ErrorCode::unexpected_token,
        .offset = o,
        .detail = "expected object key string or object end"};
    return false;
  }

  // After a key string token: skip it if unresolved, find ':', then read value.
  template <JsonTokenContext Kind = JsonTokenContext::none>
  HOSQ_ALWAYS_INLINE auto read_colon_then_value(state_type& st,
                                                Token& out) const noexcept
      -> bool {
    if (st.current.length == unresolved_length) {
      if (!resolve_skip(st)) [[unlikely]]
        return false;
    }
    const char* p = st.pos;
    if (!skip_whitespace(p, st.end)) [[unlikely]] {
      st.pos = p;
      st.last_error =
          hosq::detail::ErrorInfo{.code = ErrorCode::unexpected_eof,
                                  .offset = off(st, p),
                                  .detail = "expected ':' after object key"};
      return false;
    }
    if (*p != ':') [[unlikely]] {
      st.pos = p;
      st.last_error =
          hosq::detail::ErrorInfo{.code = ErrorCode::unexpected_token,
                                  .offset = off(st, p),
                                  .detail = "expected ':' after object key"};
      return false;
    }
    ++p;
    st.expect = JsonFastExpect::value;
    st.pos = p;
    return read_value<Kind>(st, out);
  }

  // After a value token: skip it if unresolved, then find ',' or '}' or ']'.
  template <JsonTokenContext Kind = JsonTokenContext::none>
  HOSQ_ALWAYS_INLINE auto read_comma_or_close(state_type& st,
                                              Token& out) const noexcept
      -> bool {
    if (st.current.length == unresolved_length) {
      if (!resolve_skip(st)) [[unlikely]]
        return false;
    }
    const char* p = st.pos;
    if (!skip_whitespace(p, st.end)) {
      st.pos = p;
      if (st.ctx_size == 0) {
        out = make_tok(off(st, p), 0, TokenType::End, kNoneCtx);
        return true;
      }
      st.last_error =
          hosq::detail::ErrorInfo{.code = ErrorCode::unexpected_eof,
                                  .offset = off(st, p),
                                  .detail = "expected comma or container end"};
      return false;
    }

    const char c = *p;
    const std::size_t o = off(st, p);

    if (st.ctx_size == 0) [[unlikely]] {
      st.pos = p;
      st.last_error = hosq::detail::ErrorInfo{
          .code = ErrorCode::trailing_data,
          .offset = o,
          .detail = "non-whitespace data after root value"};
      return false;
    }

    if (c == ',') {
      st.pos = p + 1;
      if (in_object(st)) {
        return read_key(st, false, out);
      } else {
        return read_value<Kind>(st, out);
      }
    }

    if (c == '}') {
      if (!in_object(st)) [[unlikely]] {
        st.pos = p;
        st.last_error = hosq::detail::ErrorInfo{
            .code = ErrorCode::unexpected_token,
            .offset = o,
            .detail = "expected array end, found object end"};
        return false;
      }
      pop_ctx(st);
      st.expect = JsonFastExpect::comma_delimiter;
      st.pos = p + 1;
      out = make_tok(o, 1, TokenType::ObjectEnd, kNoneCtx);
      return true;
    }

    if (c == ']') {
      if (in_object(st)) [[unlikely]] {
        st.pos = p;
        st.last_error = hosq::detail::ErrorInfo{
            .code = ErrorCode::unexpected_token,
            .offset = o,
            .detail = "expected object end, found array end"};
        return false;
      }
      pop_ctx(st);
      st.expect = JsonFastExpect::comma_delimiter;
      st.pos = p + 1;
      out = make_tok(o, 1, TokenType::ArrayEnd, kNoneCtx);
      return true;
    }

    st.pos = p;
    st.last_error =
        hosq::detail::ErrorInfo{.code = ErrorCode::unexpected_token,
                                .offset = o,
                                .detail = "expected comma or container end"};
    return false;
  }
};

}  // namespace json

template <std::size_t MaxDepth, json::InputPadding Padding,
          json::WhitespaceMode Mode>
struct ReaderTraits<json::FastReader<MaxDepth, Padding, Mode>> {
  static constexpr Format format = Format::json;
  static constexpr bool is_binary_format = false;
};

namespace detail {
template <std::size_t MaxDepth, json::InputPadding Padding,
          json::WhitespaceMode Mode>
struct ReaderErrorAccess<json::FastReader<MaxDepth, Padding, Mode>> {
  using Reader = json::FastReader<MaxDepth, Padding, Mode>;
  static auto get(const Reader&, const typename Reader::state_type& state)
      -> ErrorState {
    return {state.last_error.code};
  }
  static void report(const Reader&, const typename Reader::state_type& state,
                     ErrorInfo error) {
    if (!state.error_flag) {
      state.last_error = error;
      state.error_flag = true;
    }
  }
};
}  // namespace detail

static_assert(ReaderConcept<json::FastReader<>>);

}  // namespace hosq

// ── ext specializations ──────────────────────────────────────────────────────

namespace hosq::ext {

template <std::size_t Depth, json::InputPadding Padding>
struct ReaderDispatch<
    json::FastReader<Depth, Padding, json::WhitespaceMode::Auto>> {
  template <class Function>
  HOSQ_ALWAYS_INLINE decltype(auto) operator()(
      json::FastReader<Depth, Padding, json::WhitespaceMode::Auto>& reader,
      json::JsonFastReaderState<Depth>& state, Function&& function) const {
    return reader.with_reader(state, std::forward<Function>(function));
  }
};

template <ReaderConcept R>
struct DecodeSkipValue;

template <std::size_t MaxDepth, hosq::json::InputPadding Padding,
          hosq::json::WhitespaceMode Mode>
struct ScanScalar<hosq::json::FastReader<MaxDepth, Padding, Mode>> {
  using Reader = hosq::json::FastReader<MaxDepth, Padding, Mode>;
  using State = typename Reader::state_type;
  using member_skip_reader = Reader;  // Opt in only the builtin adapter.

  template <StringLiteral Key, class Context>
  HOSQ_ALWAYS_INLINE auto operator()(json::detail::MemberInput<Reader, Key>,
                                     Context&) const
      -> json::detail::MemberInput<Reader, Key, true> {
    return {};
  }

  template <class T, TryPhase Phase>
  HOSQ_ALWAYS_INLINE auto operator()(const DecodeContext<T, Reader, Phase>&)
      const -> json::detail::ScalarInput<Reader> {
    return {};
  }

  template <class T, TryPhase Phase>
  HOSQ_ALWAYS_INLINE static auto resolved(
      const DecodeContext<T, Reader, Phase>& ctx) -> ScalarView {
    const auto& reader = ctx.reader();
    auto& st = const_cast<State&>(ctx.reader_state());
    auto& tok = reader.current_token(st);

    if (tok.type != TokenType::Scalar) [[unlikely]] {
      hosq::detail::report_reader_error(
          reader, st,
          hosq::detail::ErrorInfo{
              .code = ErrorCode::type_mismatch,
              .offset = tok.offset,
              .detail = "fast scan scalar expected scalar token"});
      return {};
    }

    if (tok.length == unresolved_length) {
      resolve_scalar(reader, st, tok);
    } else {
      advance_resolved(st, tok);
    }
    return {};
  }

 private:
  static HOSQ_ALWAYS_INLINE auto advance_resolved(State& st, const Token& tok)
      -> void {
    if (tok.context == static_cast<std::uint8_t>(JsonTokenContext::string)) {
      st.pos = st.begin + tok.offset + tok.length + 1;
      return;
    }
    st.pos = st.begin + tok.offset + tok.length;
  }

  static HOSQ_ALWAYS_INLINE auto resolve_scalar(const Reader& reader, State& st,
                                                Token& tok) -> void {
    if (tok.context == static_cast<std::uint8_t>(JsonTokenContext::string)) {
      const char* const content = st.begin + tok.offset;
      const char* p = Reader::string_end(content, st.end);
      if (p != st.end) {
        tok.length = static_cast<std::size_t>(p - content);
        st.pos = p + 1;
        return;
      }
    } else if (tok.context ==
               static_cast<std::uint8_t>(JsonTokenContext::literal)) {
      const char* const lit_start = st.begin + tok.offset;
      const char* p = lit_start;
      while (Padding != hosq::json::InputPadding::none || p < st.end) {
        const auto c = static_cast<std::uint8_t>(*p);
        if (hosq::json::detail::fast_byte_classes[c] & 2) break;
        ++p;
      }
      tok.length = static_cast<std::size_t>(p - lit_start);
      st.pos = p;
      return;
    } else {
      hosq::detail::report_reader_error(
          reader, st,
          hosq::detail::ErrorInfo{
              .code = ErrorCode::type_mismatch,
              .offset = tok.offset,
              .detail = "fast scalar resolver found unknown context"});
      return;
    }

    hosq::detail::report_reader_error(
        reader, st,
        hosq::detail::ErrorInfo{
            .code = ErrorCode::unexpected_eof,
            .offset = static_cast<std::size_t>(st.end - st.begin),
            .detail = "unterminated string while resolving scalar",
        });
  }
};

template <std::size_t MaxDepth, hosq::json::InputPadding Padding,
          hosq::json::WhitespaceMode Mode>
struct DecodeSkipValue<hosq::json::FastReader<MaxDepth, Padding, Mode>> {
  using Reader = hosq::json::FastReader<MaxDepth, Padding, Mode>;
  using State = typename Reader::state_type;
  using member_skip_reader = Reader;  // Opt in only the builtin adapter.

  template <class T, TryPhase Phase>
  HOSQ_ALWAYS_INLINE auto operator()(const ScalarView&,
                                     DecodeContext<T, Reader, Phase>&) const
      -> void {}

  template <class T, TryPhase Phase>
  HOSQ_ALWAYS_INLINE auto operator()(const ObjectView&,
                                     DecodeContext<T, Reader, Phase>& ctx) const
      -> void {
    skip_container(ctx, TokenType::ObjectBegin, '}');
  }

  template <class T, TryPhase Phase>
  HOSQ_ALWAYS_INLINE auto operator()(const ArrayView&,
                                     DecodeContext<T, Reader, Phase>& ctx) const
      -> void {
    skip_container(ctx, TokenType::ArrayBegin, ']');
  }

  template <class T, TryPhase Phase>
  HOSQ_ALWAYS_INLINE auto operator()(DecodeContext<T, Reader, Phase>& ctx) const
      -> void {
    auto& reader = ctx.reader();
    auto& st = ctx.reader_state();
    if constexpr (Phase == TryPhase::normal) {
      if (!reader.next_token(st)) [[unlikely]] {
        return;
      }
    }

    const auto& tok = reader.current_token(st);
    if (tok.type == TokenType::Scalar) {
      skip_scalar(reader, st, tok);
      return;
    }
    if (tok.type == TokenType::ObjectBegin) {
      skip_container(ctx, TokenType::ObjectBegin, '}');
      return;
    }
    if (tok.type == TokenType::ArrayBegin) {
      skip_container(ctx, TokenType::ArrayBegin, ']');
      return;
    }
    hosq::detail::report_reader_error(
        reader, st,
        hosq::detail::ErrorInfo{
            .code = ErrorCode::type_mismatch,
            .offset = tok.offset,
            .detail = "fast decode::skip expected scalar, array, or object"});
  }

 private:
  static HOSQ_ALWAYS_INLINE auto skip_scalar(const Reader& reader, State& st,
                                             const Token& tok) -> void {
    if (tok.length == unresolved_length) {
      (void)reader.resolve_skip(st);
      return;
    }
    if (tok.context == static_cast<std::uint8_t>(JsonTokenContext::string)) {
      st.pos = st.begin + tok.offset + tok.length + 1;
      return;
    }
    st.pos = st.begin + tok.offset + tok.length;
  }

#if defined(__GNUC__) || defined(__clang__)
  __attribute__((cold, noinline))
#elif defined(_MSC_VER)
  __declspec(noinline)
#endif
  static void skip_error(State& st, ErrorCode code, std::size_t offset,
                         const char* detail) {
    st.last_error = hosq::detail::ErrorInfo{
        .code = code, .offset = offset, .detail = detail};
  }

  template <class T, TryPhase Phase>
  static HOSQ_ALWAYS_INLINE auto skip_container(
      DecodeContext<T, Reader, Phase>& ctx, TokenType expected_type,
      char expected_close) -> void {
    auto& reader = ctx.reader();
    auto& st = ctx.reader_state();
    const auto& tok = reader.current_token(st);
    if (tok.type != expected_type) [[unlikely]] {
      hosq::detail::report_reader_error(
          reader, st,
          hosq::detail::ErrorInfo{
              .code = ErrorCode::type_mismatch,
              .offset = tok.offset,
              .detail = "fast skip_container got wrong opening token"});
      return;
    }

    // Keep the active close in a local; save only parents when nesting.
    std::array<char, MaxDepth> close_stack;
    std::size_t depth = 1;
    const char* p = st.pos;
    // The reader owns the whitespace hint and keeps all mode fallbacks valid.
    const auto spaces = [&]() { (void)Reader::skip_whitespace(p, st.end); };
    if (expected_close == '}') goto key_begin;
    goto value_begin;

  key_begin:
    spaces();
    if (p >= st.end) [[unlikely]]
      goto eof;
    if (*p != '"') goto scan_value;
    p = Reader::string_end(p + 1, st.end);
    if (p == st.end) [[unlikely]]
      goto eof;
    ++p;
    // Consume the common key separator before entering generic whitespace
    // handling. value_begin still accepts any additional whitespace.
    if (Padding != hosq::json::InputPadding::none || st.end - p >= 2) {
      if (std::memcmp(p, ": ", 2) == 0) {
        p += 2;
        goto value_begin;
      }
    }
    spaces();
    if (p >= st.end) [[unlikely]]
      goto eof;
    if (*p != ':') goto scan_value;
    ++p;

  value_begin:
    spaces();
    if (p >= st.end) [[unlikely]]
      goto eof;
    if (*p == '"') {
      p = Reader::string_end(p + 1, st.end);
      if (p == st.end) [[unlikely]]
        goto eof;
      ++p;
      goto value_end;
    }
    if (*p == '{') {
      ++p;
      goto push_object;
    }
    if (*p == '[') {
      ++p;
      goto push_array;
    }
    if (Padding != hosq::json::InputPadding::none || st.end - p >= 4) {
      if (std::memcmp(p, "true", 4) == 0 || std::memcmp(p, "null", 4) == 0) {
        p += 4;
        goto value_end;
      }
      if (*p == 'f' &&
          (Padding != hosq::json::InputPadding::none || st.end - p >= 5) &&
          std::memcmp(p + 1, "alse", 4) == 0) {
        p += 5;
        goto value_end;
      }
    }
    // A numeric run can contain no nesting delimiters or string escapes.
    if ((*p >= '0' && *p <= '9') || *p == '-') {
      do {
        ++p;
      } while (p < st.end && ((*p >= '0' && *p <= '9') || *p == '.' ||
                              *p == 'e' || *p == 'E' || *p == '+' || *p == '-'));
      goto value_end;
    }
    goto scan_value;

  value_end:
    // A comma followed by a newline already completes the value separator.
    // The destination still handles indentation and any other whitespace.
    if (Padding != hosq::json::InputPadding::none || st.end - p >= 2) {
      if (std::memcmp(p, ",\n", 2) == 0) {
        p += 2;
        goto comma_consumed;
      }
    }
    spaces();
    if (p >= st.end) [[unlikely]]
      goto eof;
    if (*p == ',') {
      ++p;
    comma_consumed:
      if (expected_close == '}') goto key_begin;
      goto value_begin;
    }
    if (*p == '}' || *p == ']') {
      ++p;
      goto pop_container;
    }
    goto scan_value;

  scan_value:
    if (p >= st.end) goto eof;

    switch (*p++) {
      case '"':
        goto scan_string_value;
      case '{':
        goto push_object;
      case '[':
        goto push_array;
      case '}':
      case ']':
        goto pop_container;
      default:
        goto scan_value;
    }

  scan_string_value:
    p = Reader::string_end(p, st.end);
    if (p == st.end) goto eof;
    ++p;
    goto scan_value;

  push_object:
    if (depth >= MaxDepth) [[unlikely]] {
      skip_error(st, ErrorCode::depth_limit_exceeded,
                 static_cast<std::size_t>((p - 1) - st.begin),
                 "fast skip object depth limit");
      return;
    }
    close_stack[depth - 1] = expected_close;
    ++depth;
    expected_close = '}';
    goto key_begin;

  push_array:
    if (depth >= MaxDepth) [[unlikely]] {
      skip_error(st, ErrorCode::depth_limit_exceeded,
                 static_cast<std::size_t>((p - 1) - st.begin),
                 "fast skip array depth limit");
      return;
    }
    close_stack[depth - 1] = expected_close;
    ++depth;
    expected_close = ']';
    goto value_begin;

  pop_container:
    if (*(p - 1) != expected_close) [[unlikely]] {
      skip_error(st, ErrorCode::unexpected_token,
                 static_cast<std::size_t>((p - 1) - st.begin),
                 "fast skip found mismatched container close");
      return;
    }
    --depth;
    if (depth != 0) {
      expected_close = close_stack[depth - 1];
      goto value_end;
    }

    st.pos = p;
    st.expect = hosq::json::JsonFastExpect::comma_delimiter;
    if (st.ctx_size > 0) {
      --st.ctx_size;
    }
    return;

  eof:
    skip_error(st, ErrorCode::unexpected_eof,
               static_cast<std::size_t>(st.end - st.begin),
               "fast skip reached EOF inside container");
  }
};

}  // namespace hosq::ext

namespace hosq::json::detail {
template <class Step>
struct NumberValue {};
template <decode::detail::Number T>
struct NumberValue<decode::detail::NumberImpl<T>> {
  using type = T;
};
template <class Step, class Context, class Default, class = void>
struct AcceptedInput {
  using type = Default;
};
template <class Step, class Context, class Default>
struct AcceptedInput<Step, Context, Default,
                     std::void_t<typename Step::view_step,
                                 typename Step::template input_type<Context>>> {
  using type =
      std::conditional_t<std::same_as<typename Step::view_step, Step>,
                         typename Step::template input_type<Context>, Default>;
};
template <class Native, class Ordinary, class Accepted = Native>
struct NativeInputView {
  template <class Step, class Context>
  using type = std::conditional_t<
      std::same_as<typename AcceptedInput<Step, Context, Ordinary>::type,
                   Accepted>,
      Native, Ordinary>;
  template <class Step, class Context>
  HOSQ_ALWAYS_INLINE static auto get(Native& input, Context& ctx)
      -> type<Step, Context> {
    if constexpr (std::same_as<type<Step, Context>, Native>)
      return std::move(input);
    else
      return input.materialize(ctx);
  }
};
}  // namespace hosq::json::detail

namespace hosq::ext {
template <class R>
struct InputView<json::detail::ScalarInput<R>>
    : json::detail::NativeInputView<json::detail::ScalarInput<R>, ScalarView> {};
template <class R>
struct InputView<json::detail::ArrayInput<R>>
    : json::detail::NativeInputView<json::detail::ArrayInput<R>, ArrayView> {};

template <class R, StringLiteral Key>
struct InputView<json::detail::MemberInput<R, Key, true>>
    : json::detail::NativeInputView<json::detail::MemberInput<R, Key, true>,
                                    ScalarView, json::detail::ScalarInput<R>> {};

template <std::size_t Depth, json::InputPadding Padding,
          json::WhitespaceMode Mode>
struct DecodeNumber<json::FastReader<Depth, Padding, Mode>>
    : DecodeNumberDefault<json::FastReader<Depth, Padding, Mode>> {
  using Reader = json::FastReader<Depth, Padding, Mode>;
  using input_type = json::detail::ScalarInput<Reader>;
  using DecodeNumberDefault<Reader>::operator();
  template <class T, StringLiteral Key, class Context>
  HOSQ_ALWAYS_INLINE auto operator()(
      json::detail::MemberInput<Reader, Key, true> input, Context& ctx) const
      -> std::remove_cvref_t<T> {
    using ScalarInputTag = json::detail::ScalarInput<Reader>;
    std::remove_cvref_t<T> value{};
    if (ctx.reader().try_read_member_number(input.state(ctx), value)) {
      return value;
    }
    ctx.reader().materialize_scalar_member(input.state(ctx));
    return (*this).template operator()<T>(ScalarInputTag{}, ctx);
  }
  template <class T, class Context>
  HOSQ_ALWAYS_INLINE auto operator()(const input_type&, const Context& ctx) const
      -> std::remove_cvref_t<T> {
    std::remove_cvref_t<T> value{};
    ctx.reader().template read_number<T>(ctx.reader_state(), value);
    return value;
  }
};
template <std::size_t Depth, json::InputPadding Padding,
          json::WhitespaceMode Mode>
struct DecodeNumberInto<json::FastReader<Depth, Padding, Mode>>
    : DecodeNumberIntoDefault<json::FastReader<Depth, Padding, Mode>> {
  using Reader = json::FastReader<Depth, Padding, Mode>;
  using input_type = json::detail::ScalarInput<Reader>;
  using DecodeNumberIntoDefault<Reader>::operator();
  template <class T, StringLiteral Key, class Context>
  HOSQ_ALWAYS_INLINE auto operator()(
      json::detail::MemberInput<Reader, Key, true> input, Context& ctx) const
      -> void {
    using ScalarInputTag = json::detail::ScalarInput<Reader>;
    std::remove_cvref_t<T> value{};
    if (ctx.reader().try_read_member_number(input.state(ctx), value)) {
      ctx.value() = value;
      return;
    }
    ctx.reader().materialize_scalar_member(input.state(ctx));
    (*this).template operator()<T>(ScalarInputTag{}, ctx);
  }
  template <class T, class Context>
  HOSQ_ALWAYS_INLINE auto operator()(const input_type&, const Context& ctx) const
      -> void {
    ctx.reader().template read_number<T, true>(ctx.reader_state(), ctx.value());
  }
};
template <std::size_t Depth, json::InputPadding Padding,
          json::WhitespaceMode Mode>
struct DecodeStringView<json::FastReader<Depth, Padding, Mode>>
    : DecodeStringViewDefault<json::FastReader<Depth, Padding, Mode>> {
  using Reader = json::FastReader<Depth, Padding, Mode>;
  using input_type = json::detail::ScalarInput<Reader>;
  using DecodeStringViewDefault<Reader>::operator();
  template <StringLiteral Key, class Context>
  HOSQ_ALWAYS_INLINE auto operator()(
      json::detail::MemberInput<Reader, Key, true> input, Context& ctx) const
      -> std::string_view {
    using ScalarInputTag = json::detail::ScalarInput<Reader>;
    std::string_view value{};
    if (ctx.reader().try_read_member_string_view(input.state(ctx), value)) {
      return value;
    }
    ctx.reader().materialize_scalar_member(input.state(ctx));
    return (*this)(ScalarInputTag{}, ctx);
  }
  template <class Storage, TryPhase Phase>
  HOSQ_ALWAYS_INLINE auto operator()(
      const input_type&, const DecodeContext<Storage, Reader, Phase>& ctx) const
      -> std::string_view {
    auto view = input_type::materialize(ctx);
    return DecodeStringViewDefault<Reader>{}(view, ctx);
  }
};
template <std::size_t Depth, json::InputPadding Padding,
          json::WhitespaceMode Mode>
struct DecodeBoolean<json::FastReader<Depth, Padding, Mode>>
    : DecodeBooleanDefault<json::FastReader<Depth, Padding, Mode>> {
  using Reader = json::FastReader<Depth, Padding, Mode>;
  using input_type = json::detail::ScalarInput<Reader>;
  using DecodeBooleanDefault<Reader>::operator();
  template <StringLiteral Key, class Context>
  HOSQ_ALWAYS_INLINE auto operator()(
      json::detail::MemberInput<Reader, Key, true> input, Context& ctx) const
      -> bool {
    using ScalarInputTag = json::detail::ScalarInput<Reader>;
    bool value{};
    if (ctx.reader().try_read_member_boolean(input.state(ctx), value)) {
      return value;
    }
    ctx.reader().materialize_scalar_member(input.state(ctx));
    return (*this)(ScalarInputTag{}, ctx);
  }
  template <class Storage, TryPhase Phase>
  HOSQ_ALWAYS_INLINE auto operator()(
      const input_type&, DecodeContext<Storage, Reader, Phase>& ctx) const
      -> bool {
    auto& st = ctx.reader_state();
    const auto& tok = ctx.reader().current_token(st);
    if (tok.type == TokenType::Scalar && tok.length == unresolved_length) {
      const char* first = st.begin + tok.offset;
      const auto remaining = static_cast<std::size_t>(st.end - first);
      const bool is_true = remaining >= 4 && std::memcmp(first, "true", 4) == 0;
      const bool is_false =
          !is_true && remaining >= 5 && std::memcmp(first, "false", 5) == 0;
      if (is_true || is_false) {
        const std::size_t length = is_true ? 4 : 5;
        const char* end = first + length;
        const bool quoted =
            tok.context == static_cast<std::uint8_t>(JsonTokenContext::string);
        const bool complete =
            quoted ? end < st.end && *end == '"'
                   : end == st.end || (json::detail::fast_byte_classes
                                           [static_cast<unsigned char>(*end)] &
                                       2);
        if (complete) {
          tok.length = length;
          st.pos = end + (quoted ? 1 : 0);
          return is_true;
        }
      }
    }
    auto view = input_type::materialize(ctx);
    return DecodeBooleanDefault<Reader>{}(view, ctx);
  }
};

template <std::size_t Depth, json::InputPadding Padding,
          json::WhitespaceMode Mode>
struct ScanArray<json::FastReader<Depth, Padding, Mode>> {
  using Reader = json::FastReader<Depth, Padding, Mode>;
  template <class Storage, TryPhase Phase>
  HOSQ_ALWAYS_INLINE static auto resolved(
      const DecodeContext<Storage, Reader, Phase>& ctx) -> ArrayView {
    const auto& tok = ctx.reader().current_token(ctx.reader_state());
    if (tok.type != TokenType::ArrayBegin) [[unlikely]]
      hosq::detail::report_reader_error(
          ctx.reader(), ctx.reader_state(),
          hosq::detail::ErrorInfo{.code = ErrorCode::type_mismatch,
                                  .offset = tok.offset,
                                  .detail = "scan::array expected array"});
    return {};
  }
  template <class Storage, TryPhase Phase>
  HOSQ_ALWAYS_INLINE auto operator()(DecodeContext<Storage, Reader, Phase>& ctx)
      const -> json::detail::ArrayInput<Reader> {
    (void)resolved(ctx);
    return {};
  }
};

template <std::size_t Depth, json::InputPadding Padding,
          json::WhitespaceMode Mode, auto Scan, auto Number, std::size_t N>
  requires(std::same_as<std::remove_cvref_t<decltype(Scan)>,
                        scan::detail::ScanScalar> &&
           requires {
             typename json::detail::NumberValue<
                 std::remove_cvref_t<decltype(Number)>>::type;
           })
struct DecodeCollect<json::FastReader<Depth, Padding, Mode>,
                     Pipeline<Scan, Number>, N>
    : DecodeCollectDefault<json::FastReader<Depth, Padding, Mode>,
                           Pipeline<Scan, Number>, N> {
  using Reader = json::FastReader<Depth, Padding, Mode>;
  using input_type = json::detail::ArrayInput<Reader>;
  using Base = DecodeCollectDefault<Reader, Pipeline<Scan, Number>, N>;
  using Base::operator();
  template <class Storage, TryPhase Phase>
  HOSQ_ALWAYS_INLINE auto operator()(
      const input_type&, DecodeContext<Storage, Reader, Phase>& ctx) const
      -> std::array<typename Base::terminal_type, N> {
    std::array<typename Base::terminal_type, N> values{};
    if (ctx.reader().try_read_number_array(ctx.reader_state(), values))
      return values;
    auto view = input_type::materialize(ctx);
    return Base{}(view, ctx);
  }
};
template <std::size_t Depth, json::InputPadding Padding,
          json::WhitespaceMode Mode>
struct EntryInput<json::FastReader<Depth, Padding, Mode>> {
  using Reader = json::FastReader<Depth, Padding, Mode>;
  template <auto First, auto... Rest>
  static consteval bool accepts(Pipeline<First, Rest...>*) {
    return std::same_as<std::remove_cvref_t<decltype(First)>,
                        scan::detail::ScanScalar>;
  }
  template <StringLiteral Key, bool Matched = false, class Value>
  HOSQ_ALWAYS_INLINE bool decode(Value& value, Reader& reader,
                                 typename Reader::state_type& state) const {
    using Pipe = typename Value::decoder_type;
    using Ctx = DecodeContext<typename Value::storage_type, Reader,
                              Matched ? TryPhase::first : TryPhase::normal>;
    using Tag = json::detail::MemberInput<Reader, Key>;
    if constexpr (accepts(static_cast<Pipe*>(nullptr))) {
      constexpr bool use_const = Value::template decoder_const_invokable<Ctx>;
      constexpr bool accepts_tag =
          use_const ? Pipe::template callable_with_const_context<Ctx, Tag>()
                    : Pipe::template callable_with_mutable_context<Ctx, Tag>();
      if constexpr (accepts_tag) {
        using ActiveContext = std::conditional_t<use_const, const Ctx, Ctx>;
        using Result =
            typename Pipe::template final_output_type<Tag, ActiveContext>;
        // A matching effect may not disappear through this void fast path.
        if constexpr (!std::same_as<Result, void>) return false;
        if constexpr (Matched) {
          if (!reader.begin_matched_scalar_member(state)) return false;
        } else {
          if (!reader.template begin_const_scalar_member<Key>(state))
            return false;
        }
        Ctx ctx{value.storage(), reader, state};
        if constexpr (use_const)
          (void)Pipe::invoke(Tag{}, static_cast<const Ctx&>(ctx));
        else
          (void)Pipe::invoke(Tag{}, ctx);
        return true;
      }
    }
    return false;
  }
  template <StringLiteral Key, class Value>
  HOSQ_ALWAYS_INLINE bool decode_matched(
      Value& value, Reader& reader, typename Reader::state_type& state) const {
    return decode<Key, true>(value, reader, state);
  }
};
}  // namespace hosq::ext

namespace hosq::json::detail {
template <class Reader, StringLiteral Key, bool Scanned>
template <class Context>
HOSQ_ALWAYS_INLINE auto MemberInput<Reader, Key, Scanned>::materialize(
    Context& ctx) const -> hosq::ScalarView {
  ctx.reader().materialize_scalar_member(this->state(ctx));
  return ext::ScanScalar<Reader>::resolved(ctx);
}
template <class Reader>
template <class Context>
HOSQ_ALWAYS_INLINE auto ScalarInput<Reader>::materialize(Context& ctx)
    -> hosq::ScalarView {
  return ext::ScanScalar<Reader>::resolved(ctx);
}
template <class Reader>
template <class Context>
HOSQ_ALWAYS_INLINE auto ArrayInput<Reader>::materialize(Context& ctx)
    -> hosq::ArrayView {
  return ext::ScanArray<Reader>::resolved(ctx);
}
}  // namespace hosq::json::detail

namespace hosq::ext {
template <std::size_t Depth, json::InputPadding Padding,
          json::WhitespaceMode Mode>
struct DecodeSemanticScalar<json::FastReader<Depth, Padding, Mode>>
    : json::detail::SemanticScalarCodec {};
}  // namespace hosq::ext

namespace hosq::ext {
template <std::size_t Depth, json::InputPadding Padding,
          json::WhitespaceMode Mode>
struct DecodeStringSource<json::FastReader<Depth, Padding, Mode>>
    : json::detail::StringSourceCodec {};
template <std::size_t Depth, json::InputPadding Padding,
          json::WhitespaceMode Mode>
struct UnescapeString<json::FastReader<Depth, Padding, Mode>, Format::json>
    : json::detail::UnescapeStringCodec {};
}  // namespace hosq::ext
