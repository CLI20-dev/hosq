#pragma once

// #include <array>
// #include <cstddef>
// #include <cstdint>
// #include <string_view>
// #include <type_traits>
//
// #include "ser20/error.hh"
// #include "ser20/reader_base.hh"
// #include "ser20/result.hh"
//
// namespace ser20 {
// namespace json {
//
// enum class JsonObjectContext : std::uint8_t {
//   array = 0,
//   object = 1,
// };
//
// enum class JsonExpect : std::uint8_t {
//   key,
//   key_or_end,
//   value,
//   value_or_end,
//   comma_delimiter,
//   colon_delimiter,
// };
//
// template <std::size_t MaxDepth>
// struct JsonFastReaderState {
//   std::array<std::uint8_t, MaxDepth> ctx{};
//   std::size_t ctx_size{0};
//
//   const char* begin{nullptr};
//   const char* pos{nullptr};
//   const char* end{nullptr};
//
//   JsonExpect expect{JsonExpect::value};
//   bool allow_top_level_value_after_scan{false};
//
//   Token current{
//       .offset = 0,
//       .length = 0,
//       .type = TokenType::End,
//       .context = static_cast<std::uint8_t>(JsonTokenContext::none),
//   };
// };
//
// template <std::size_t MaxDepth = 1024>
// class FastReader {
//  public:
//   using input_type = std::string_view;
//   using state_type = JsonFastReaderState<MaxDepth>;
//
//   explicit FastReader() noexcept = default;
//
//   FastReader(const FastReader&) = default;
//   auto operator=(const FastReader&) -> FastReader& = default;
//
//   FastReader(FastReader&&) = delete;
//   auto operator=(FastReader&&) -> FastReader& = delete;
//
//   ~FastReader() = default;
//
//   auto reset(state_type& st, std::string_view input) const noexcept -> void {
//     st.begin = input.data();
//     st.pos = input.data();
//     st.end = input.data() + input.size();
//
//     st.ctx_size = 0;
//     st.expect = JsonExpect::value;
//     st.allow_top_level_value_after_scan = false;
//
//     st.current = Token{
//         .offset = 0,
//         .length = 0,
//         .type = TokenType::End,
//         .context = static_cast<std::uint8_t>(JsonTokenContext::none),
//     };
//   }
//
//   [[nodiscard]]
//   auto input(const state_type& st) const noexcept -> input_type {
//     return input_type{
//         st.begin,
//         static_cast<std::size_t>(st.end - st.begin),
//     };
//   }
//
//   [[nodiscard]]
//   auto current_token(state_type& st) const noexcept -> Token& {
//     return st.current;
//   }
//
//   [[nodiscard]]
//   auto current_token(const state_type& st) const noexcept -> const Token& {
//     return st.current;
//   }
//
//   auto advance(state_type& st, std::size_t n) const -> Result<void, Error> {
//     if (static_cast<std::size_t>(st.end - st.pos) < n) [[unlikely]] {
//       return unexpected(Error{
//           .code = ErrorCode::unexpected_eof,
//           .offset = offset(st),
//       });
//     }
//
//     st.current.length = n;
//     st.pos += n;
//     return {};
//   }
//
//   auto advance_unchecked(state_type& st, std::size_t n) const noexcept -> void {
//     st.current.length = n;
//     st.pos += n;
//   }
//
//   [[nodiscard]]
//   auto next_token(state_type& st) const -> Result<void, Error> {
//     auto result = read_one(st);
//     if (!result) [[unlikely]] {
//       return unexpected(result.error());
//     }
//
//     st.current = *result;
//     return {};
//   }
//
//   friend auto allow_next_top_level_value_after_scan(const FastReader&,
//                                                     state_type& st) noexcept
//       -> void {
//     st.allow_top_level_value_after_scan = true;
//   }
//
//   // ---------------------------------------------------------------------------
//   // Public fast-path readers.
//   //
//   // scan::* can call these directly to avoid read_one()'s expectation switch.
//   // ---------------------------------------------------------------------------
//
//   [[nodiscard]]
//   auto read_one(state_type& st) const -> Result<Token, Error> {
//     switch (st.expect) {
//       case JsonExpect::key:
//         return read_object_key(st, false);
//       case JsonExpect::key_or_end:
//         return read_object_key(st, true);
//       case JsonExpect::value:
//         return read_value(st);
//       case JsonExpect::value_or_end:
//         return read_array_value_or_end(st);
//       case JsonExpect::comma_delimiter:
//         return read_value_end(st);
//       case JsonExpect::colon_delimiter:
//         return read_object_colon_then_value(st);
//     }
//
//     return unexpected(make_error(ErrorCode::unknown_error, offset(st)));
//   }
//
//   [[nodiscard]]
//   auto read_value(state_type& st) const -> Result<Token, Error> {
//     const char* p = st.pos;
//
//   value_begin:
//     if (p >= st.end) [[unlikely]] {
//       st.pos = p;
//       return unexpected(make_error(ErrorCode::unexpected_eof, end_offset(st)));
//     }
//
//     const char c = *p;
//     const std::size_t off = offset(st, p);
//
//     if (c == '"') {
//       consume_value_start(st);
//       st.expect = JsonExpect::comma_delimiter;
//       st.pos = p;
//       return make_scalar_token(off, JsonTokenContext::string);
//     }
//
//     if (is_num_start(c)) {
//       consume_value_start(st);
//       st.expect = JsonExpect::comma_delimiter;
//       st.pos = p;
//       return make_scalar_token(off, JsonTokenContext::literal);
//     }
//
//     if (c == '{') {
//       return read_object_begin_at(st, p);
//     }
//
//     if (c == '[') {
//       return read_array_begin_at(st, p);
//     }
//
//     if (c == 't' || c == 'f' || c == 'n') {
//       consume_value_start(st);
//       st.expect = JsonExpect::comma_delimiter;
//       st.pos = p;
//       return make_scalar_token(off, JsonTokenContext::literal);
//     }
//
//     if (is_ws(c)) {
//       do {
//         ++p;
//         if (p >= st.end) [[unlikely]] {
//           st.pos = p;
//           return unexpected(
//               make_error(ErrorCode::unexpected_eof, end_offset(st)));
//         }
//       } while (is_ws(*p));
//
//       goto value_begin;  // NOLINT(cppcoreguidelines-avoid-goto)
//     }
//
//     if (can_read_value(st)) {
//       // Preserve old FastReader behavior:
//       // any other non-whitespace byte in value position starts a
//       // caller-validated literal.
//       consume_value_start(st);
//       st.expect = JsonExpect::comma_delimiter;
//       st.pos = p;
//       return make_scalar_token(off, JsonTokenContext::literal);
//     }
//
//     st.pos = p;
//     return unexpected(make_error(ErrorCode::unexpected_token, off));
//   }
//
//   [[nodiscard]]
//   auto read_array_value_or_end(state_type& st) const -> Result<Token, Error> {
//     const char* p = st.pos;
//
//     if (!skip_ws_require_char(st, p)) [[unlikely]] {
//       st.pos = p;
//       return unexpected(make_error(ErrorCode::unexpected_eof, end_offset(st)));
//     }
//
//     if (*p == ']') {
//       return read_array_end_at(st, p);
//     }
//
//     st.pos = p;
//     return read_value(st);
//   }
//
//   [[nodiscard]]
//   auto read_object_key(state_type& st, bool allow_end) const
//       -> Result<Token, Error> {
//     const char* p = st.pos;
//
//     if (!skip_ws_require_char(st, p)) [[unlikely]] {
//       st.pos = p;
//       return unexpected(make_error(ErrorCode::unexpected_eof, end_offset(st)));
//     }
//
//     const char c = *p;
//     const std::size_t off = offset(st, p);
//
//     if (c == '"') [[likely]] {
//       st.expect = JsonExpect::colon_delimiter;
//       st.pos = p;
//       return make_scalar_token(off, JsonTokenContext::string);
//     }
//
//     if (allow_end && c == '}') {
//       return read_object_end_at(st, p);
//     }
//
//     st.pos = p;
//     return unexpected(make_error(ErrorCode::unexpected_token, off));
//   }
//
//   [[nodiscard]]
//   auto read_object_colon_then_value(state_type& st) const
//       -> Result<Token, Error> {
//     const char* p = st.pos;
//
//     if (!skip_ws_require_char(st, p)) [[unlikely]] {
//       st.pos = p;
//       return unexpected(make_error(ErrorCode::unexpected_eof, end_offset(st)));
//     }
//
//     if (*p != ':') [[unlikely]] {
//       st.pos = p;
//       return unexpected(make_error(ErrorCode::unexpected_token, offset(st, p)));
//     }
//
//     ++p;
//     st.expect = JsonExpect::value;
//     st.pos = p;
//     return read_value(st);
//   }
//
//   [[nodiscard]]
//   auto read_value_end(state_type& st) const -> Result<Token, Error> {
//     const char* p = st.pos;
//
//     if (!skip_ws_allow_eof(st, p)) {
//       st.pos = p;
//
//       if (st.ctx_size == 0) {
//         return make_none_token(end_offset(st), TokenType::End);
//       }
//
//       return unexpected(make_error(ErrorCode::unexpected_eof, end_offset(st)));
//     }
//
//     if (st.ctx_size == 0) [[unlikely]] {
//       st.pos = p;
//       return unexpected(make_error(ErrorCode::unexpected_token, offset(st, p)));
//     }
//
//     if (current_ctx(st) == JsonObjectContext::object) {
//       return read_object_value_end_at(st, p);
//     }
//
//     return read_array_value_end_at(st, p);
//   }
//
//   [[nodiscard]]
//   auto read_object_value_end_at(state_type& st, const char* p) const
//       -> Result<Token, Error> {
//     const char c = *p;
//
//     if (c == ',') [[likely]] {
//       ++p;
//       st.expect = JsonExpect::key;
//       st.pos = p;
//       return read_object_key(st, false);
//     }
//
//     if (c == '}') {
//       return read_object_end_at(st, p);
//     }
//
//     st.pos = p;
//     return unexpected(make_error(ErrorCode::unexpected_token, offset(st, p)));
//   }
//
//   [[nodiscard]]
//   auto read_array_value_end_at(state_type& st, const char* p) const
//       -> Result<Token, Error> {
//     const char c = *p;
//
//     if (c == ',') [[likely]] {
//       ++p;
//       st.expect = JsonExpect::value;
//       st.pos = p;
//       return read_value(st);
//     }
//
//     if (c == ']') {
//       return read_array_end_at(st, p);
//     }
//
//     st.pos = p;
//     return unexpected(make_error(ErrorCode::unexpected_token, offset(st, p)));
//   }
//
//   [[nodiscard]]
//   auto read_object_begin_at(state_type& st, const char* p) const
//       -> Result<Token, Error> {
//     const std::size_t off = offset(st, p);
//
//     if (!can_read_value(st)) [[unlikely]] {
//       st.pos = p;
//       return unexpected(make_error(ErrorCode::unexpected_token, off));
//     }
//
//     consume_value_start(st);
//
//     auto pushed = push_context(st, JsonObjectContext::object, off);
//     if (!pushed) [[unlikely]] {
//       st.pos = p;
//       return unexpected(pushed.error());
//     }
//
//     st.expect = JsonExpect::key_or_end;
//     st.pos = p;
//
//     return make_none_token(off, TokenType::ObjectBegin, unresolved_length);
//   }
//
//   [[nodiscard]]
//   auto read_array_begin_at(state_type& st, const char* p) const
//       -> Result<Token, Error> {
//     const std::size_t off = offset(st, p);
//
//     if (!can_read_value(st)) [[unlikely]] {
//       st.pos = p;
//       return unexpected(make_error(ErrorCode::unexpected_token, off));
//     }
//
//     consume_value_start(st);
//
//     auto pushed = push_context(st, JsonObjectContext::array, off);
//     if (!pushed) [[unlikely]] {
//       st.pos = p;
//       return unexpected(pushed.error());
//     }
//
//     st.expect = JsonExpect::value_or_end;
//     st.pos = p;
//
//     return make_none_token(off, TokenType::ArrayBegin, unresolved_length);
//   }
//
//   [[nodiscard]]
//   auto read_object_end_at(state_type& st, const char* p) const
//       -> Result<Token, Error> {
//     const std::size_t off = offset(st, p);
//
//     if (st.ctx_size == 0 || current_ctx(st) != JsonObjectContext::object ||
//         (st.expect != JsonExpect::key_or_end &&
//          st.expect != JsonExpect::comma_delimiter)) [[unlikely]] {
//       st.pos = p;
//       return unexpected(make_error(ErrorCode::unexpected_token, off));
//     }
//
//     pop_context(st);
//     ++p;
//
//     st.pos = p;
//     st.expect = JsonExpect::comma_delimiter;
//
//     return make_none_token(off, TokenType::ObjectEnd);
//   }
//
//   [[nodiscard]]
//   auto read_array_end_at(state_type& st, const char* p) const
//       -> Result<Token, Error> {
//     const std::size_t off = offset(st, p);
//
//     if (st.ctx_size == 0 || current_ctx(st) != JsonObjectContext::array ||
//         (st.expect != JsonExpect::value_or_end &&
//          st.expect != JsonExpect::comma_delimiter)) [[unlikely]] {
//       st.pos = p;
//       return unexpected(make_error(ErrorCode::unexpected_token, off));
//     }
//
//     pop_context(st);
//     ++p;
//
//     st.pos = p;
//     st.expect = JsonExpect::comma_delimiter;
//
//     return make_none_token(off, TokenType::ArrayEnd);
//   }
//
//  private:
//   [[nodiscard]]
//   static constexpr auto is_ws(char c) noexcept -> bool {
//     return c == ' ' || c == '\t' || c == '\r' || c == '\n';
//   }
//
//   [[nodiscard]]
//   static constexpr auto is_num_start(char c) noexcept -> bool {
//     return c == '-' || (c >= '0' && c <= '9');
//   }
//
//   [[nodiscard]]
//   static auto offset(const state_type& st, const char* p) noexcept
//       -> std::size_t {
//     return static_cast<std::size_t>(p - st.begin);
//   }
//
//   [[nodiscard]]
//   static auto offset(const state_type& st) noexcept -> std::size_t {
//     return offset(st, st.pos);
//   }
//
//   [[nodiscard]]
//   static auto end_offset(const state_type& st) noexcept -> std::size_t {
//     return static_cast<std::size_t>(st.end - st.begin);
//   }
//
//   [[nodiscard]]
//   static auto can_read_value(const state_type& st) noexcept -> bool {
//     return st.expect == JsonExpect::value ||
//            st.expect == JsonExpect::value_or_end ||
//            (st.ctx_size == 0 && st.allow_top_level_value_after_scan);
//   }
//
//   static auto consume_value_start(state_type& st) noexcept -> void {
//     st.allow_top_level_value_after_scan = false;
//   }
//
//   [[nodiscard]]
//   static auto make_error(ErrorCode code, std::size_t off) noexcept -> Error {
//     return Error{
//         .code = code,
//         .offset = off,
//     };
//   }
//
//   [[nodiscard]]
//   static auto make_none_token(std::size_t off, TokenType type,
//                               std::size_t length = 0) noexcept -> Token {
//     return Token{
//         .offset = off,
//         .length = length,
//         .type = type,
//         .context = static_cast<std::uint8_t>(JsonTokenContext::none),
//     };
//   }
//
//   [[nodiscard]]
//   static auto make_scalar_token(std::size_t off,
//                                 JsonTokenContext context) noexcept -> Token {
//     return Token{
//         .offset = off,
//         .length = unresolved_length,
//         .type = TokenType::ScalarBegin,
//         .context = static_cast<std::uint8_t>(context),
//     };
//   }
//
//   [[nodiscard]]
//   static auto current_ctx(const state_type& st) noexcept -> JsonObjectContext {
//     return st.ctx[st.ctx_size - 1] != 0 ? JsonObjectContext::object
//                                         : JsonObjectContext::array;
//   }
//
//   static auto push_context(state_type& st, JsonObjectContext type,
//                            std::size_t off) -> Result<void, Error> {
//     if (st.ctx_size >= MaxDepth) [[unlikely]] {
//       return unexpected(Error{
//           .code = ErrorCode::depth_limit_exceeded,
//           .offset = off,
//       });
//     }
//
//     st.ctx[st.ctx_size++] =
//         type == JsonObjectContext::object ? std::uint8_t{1} : std::uint8_t{0};
//
//     return {};
//   }
//
//   static auto pop_context(state_type& st) noexcept -> void { --st.ctx_size; }
//
//   [[nodiscard]]
//   static auto skip_ws_require_char(const state_type& st, const char*& p) noexcept
//       -> bool {
//     while (p < st.end && is_ws(*p)) {
//       ++p;
//     }
//
//     return p < st.end;
//   }
//
//   [[nodiscard]]
//   static auto skip_ws_allow_eof(const state_type& st, const char*& p) noexcept
//       -> bool {
//     while (p < st.end && is_ws(*p)) {
//       ++p;
//     }
//
//     return p < st.end;
//   }
// };
//
// static_assert(std::is_trivially_copyable_v<FastReader<>>);
// static_assert(FastReaderConcept<FastReader<>>);
//
// }  // namespace json
//
// template <std::size_t MaxDepth>
// struct ReaderTraits<json::FastReader<MaxDepth>> {
//   static constexpr Format format = Format::json;
//   static constexpr bool is_binary_format = false;
// };
//
// }  // namespace ser20

