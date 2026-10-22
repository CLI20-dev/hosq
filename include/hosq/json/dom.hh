#pragma once

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "hosq/decoder.hh"
#include "hosq/json/detail/validated_value.hh"
#include "hosq/json/fast_reader.hh"
#include "hosq/pipeline.hh"
#include "hosq/writer_base.hh"

namespace hosq::json {

template <bool Indexed, std::size_t MaxDepth>
class BasicDom;

namespace detail {
struct DomEntry {
  std::string pointer;
  ValueRange range;
};

struct DomData {
  std::string input;
  ValueRange root{};
  std::vector<DomEntry> index;
};

inline auto append_pointer_token(std::string& pointer, std::string_view token)
    -> void {
  pointer += '/';
  for (char c : token) {
    if (c == '~')
      pointer += "~0";
    else if (c == '/')
      pointer += "~1";
    else
      pointer += c;
  }
}

inline auto dom_pointer(const ErrorPath& path) -> std::string {
  std::string pointer;
  for (const auto& part : path) {
    if (part.has_name)
      append_pointer_token(pointer, part.name);
    else
      append_pointer_token(pointer, std::to_string(part.index));
  }
  return pointer;
}

// RFC 6901 string form, not a URI fragment. Values are selected by decoded
// object names and canonical zero-based array indices; no Unicode normalization.
inline auto parse_dom_pointer(std::string_view pointer,
                              std::vector<std::string>& tokens) -> bool {
  if (::hosq::detail::invalid_utf8_offset(pointer) != pointer.size())
    return false;
  if (pointer.empty()) return true;
  if (pointer.front() != '/') return false;
  for (std::size_t begin = 1;;) {
    auto end = pointer.find('/', begin);
    if (end == std::string_view::npos) end = pointer.size();
    std::string token;
    for (std::size_t pos = begin; pos < end; ++pos) {
      const char c = pointer[pos];
      if (c != '~') {
        token += c;
      } else {
        if (++pos == end || (pointer[pos] != '0' && pointer[pos] != '1'))
          return false;
        token += pointer[pos] == '0' ? '~' : '/';
      }
    }
    tokens.push_back(std::move(token));
    if (end == pointer.size()) return true;
    begin = end + 1;
  }
}

template <class R>
auto resolve_dom_scalar(R& reader, typename R::state_type& state) -> void {
  DecodeContext<void, R> ctx{reader, state};
  if constexpr (requires { ext::ScanScalar<R>::resolved(ctx); })
    (void)ext::ScanScalar<R>::resolved(ctx);
  else
    (void)ext::ScanScalar<R>{}(ctx);
}

// Only used on privately owned, already validated input. It deliberately
// performs no conversion and builds no paths, key sets, or retained index.
template <class R>
auto skip_dom_value(R& reader, typename R::state_type& state) -> bool {
  auto token = reader.current_token(state);
  if (token.type == TokenType::Scalar) {
    resolve_dom_scalar(reader, state);
    return ::hosq::detail::reader_error(reader, state).is_ok();
  }
  if (token.type != TokenType::ObjectBegin &&
      token.type != TokenType::ArrayBegin)
    return false;
  std::size_t depth = 1;
  while (depth != 0) {
    if (!reader.next_token(state)) return false;
    token = reader.current_token(state);
    if (token.type == TokenType::Scalar) {
      resolve_dom_scalar(reader, state);
      if (::hosq::detail::reader_error(reader, state)) return false;
    } else if (token.type == TokenType::ObjectBegin ||
               token.type == TokenType::ArrayBegin) {
      ++depth;
    } else if (token.type == TokenType::ObjectEnd ||
               token.type == TokenType::ArrayEnd) {
      --depth;
    } else {
      return false;
    }
  }
  return true;
}

template <std::size_t MaxDepth>
auto search_dom(const DomData& data, const std::vector<std::string>& tokens,
                ValueRange& range) -> bool {
  FastReader<MaxDepth> reader;
  typename FastReader<MaxDepth>::state_type state;
  reader.reset(state, data.input);
  if (!reader.next_token(state)) return false;
  for (const auto& part : tokens) {
    const auto type = reader.current_token(state).type;
    if (type == TokenType::ObjectBegin) {
      bool found = false;
      while (reader.next_token(state)) {
        if (reader.current_token(state).type == TokenType::ObjectEnd) break;
        resolve_dom_scalar(reader, state);
        if (::hosq::detail::reader_error(reader, state)) return false;
        const auto key = reader.current_token(state);
        std::string decoded;
        if (!string_codec::unescape_json_string(
                std::string_view(data.input).substr(key.offset, key.length),
                decoded))
          return false;
        if (!reader.next_token(state)) return false;
        if (decoded == part) {
          found = true;
          break;
        }
        if (!skip_dom_value(reader, state)) return false;
      }
      if (!found) return false;
    } else if (type == TokenType::ArrayBegin) {
      if (part.empty() || (part.size() > 1 && part.front() == '0')) return false;
      std::size_t target{};
      const auto parsed =
          std::from_chars(part.data(), part.data() + part.size(), target);
      if (parsed.ec != std::errc{} || parsed.ptr != part.data() + part.size())
        return false;
      std::size_t ordinal = 0;
      for (;;) {
        if (!reader.next_token(state) ||
            reader.current_token(state).type == TokenType::ArrayEnd)
          return false;
        if (ordinal == target) break;
        if (!skip_dom_value(reader, state)) return false;
        ++ordinal;
      }
    } else {
      return false;
    }
  }
  auto first = reader.current_token(state);
  if (!skip_dom_value(reader, state)) return false;
  const auto last = reader.current_token(state);
  if (first.type == TokenType::Scalar) {
    first = last;
    const bool quoted =
        first.context == static_cast<std::uint8_t>(JsonTokenContext::string);
    range = {first.offset - (quoted ? 1 : 0), first.length + (quoted ? 2 : 0),
             first.type, first.context};
  } else {
    range = {first.offset, last.offset + last.length - first.offset, first.type,
             first.context};
  }
  return true;
}

struct DomAccess {
  template <bool Indexed, std::size_t MaxDepth, ReaderConcept R>
  static auto capture(R& reader, typename R::state_type& state)
      -> BasicDom<Indexed, MaxDepth>;
};
}  // namespace detail

/// An untyped selection. It retains the owned source across DOM destruction or
/// reassignment. Borrowed pipeline results require a DOM/selection owner to
/// live.
template <std::size_t MaxDepth = 128>
class DomSelection {
 public:
  [[nodiscard]] auto exists() const noexcept -> bool {
    return data_ && error_.is_ok();
  }

  [[nodiscard]] auto source() const noexcept -> std::string_view {
    if (!exists()) return {};
    return std::string_view(data_->input).substr(range_.offset, range_.length);
  }

  /// Apply the supplied Elem/schema's decoder pipeline. Lookup performs no
  /// numeric conversion or string unescape. Diagnostics refer to the owned DOM.
  template <class T>
    requires ::hosq::detail::RequiredDecoderAcceptableT<T>
  [[nodiscard]] auto decode_into(T& output, DecodeOptions options = {}) const
      -> Error {
    if (!exists()) return error_;
    auto error = decode_document_into<FastReader<MaxDepth>>(
        output, source(), {.error_path = false});
    if (error) {
      error.offset += range_.offset;
      if (options.error_path)
        error.path = ::hosq::detail::locate_error_path<FastReader<MaxDepth>>(
            data_->input, error.offset);
    }
    return error;
  }

 private:
  template <bool, std::size_t>
  friend class BasicDom;
  DomSelection(std::shared_ptr<const detail::DomData> data,
               detail::ValueRange range, Error error)
      : data_(std::move(data)), range_(range), error_(std::move(error)) {}

  std::shared_ptr<const detail::DomData> data_;
  detail::ValueRange range_{};
  Error error_;
};

/// Owned, validated arbitrary JSON. Indexed stores a sorted flat path index;
/// otherwise only the immutable source is retained and each lookup searches it.
/// Copies share immutable ownership. No DOM-style type conversions or mutation.
template <bool Indexed, std::size_t MaxDepth = 128>
class BasicDom {
 public:
  BasicDom() = default;

  /// Validate one whole document before publishing its owned source/index.
  /// An unsuccessful assignment leaves this DOM unchanged.
  [[nodiscard]] auto assign(std::string_view input, DecodeOptions options = {})
      -> Error {
    // Copy first: input may be a selection borrowed from this DOM itself.
    auto owned = std::make_shared<detail::DomData>();
    owned->input.assign(input);
    FastReader<MaxDepth> reader;
    typename FastReader<MaxDepth>::state_type state;
    reader.reset(state, owned->input);
    auto finish_error = [&] {
      auto error = reader.error(state);
      if (error && options.error_path)
        error.path = ::hosq::detail::locate_error_path<FastReader<MaxDepth>>(
            owned->input, error.offset);
      return error;
    };
    if (!reader.next_token(state)) return finish_error();
    if (!detail::validate_value<Indexed>(
            reader, state,
            [&](const ErrorPath& path, detail::ValueRange range) {
              owned->root = range;  // Root is the final, postorder visit.
              if constexpr (Indexed)
                owned->index.push_back({detail::dom_pointer(path), range});
            },
            MaxDepth))
      return finish_error();
    if (!hosq::next_token<Expect::document_end>(reader, state))
      return finish_error();
    if (reader.current_token(state).type != TokenType::End)
      return {ErrorCode::trailing_data, reader.current_token(state).offset,
              "expected end of DOM document"};
    if constexpr (Indexed)
      std::sort(
          owned->index.begin(), owned->index.end(),
          [](const auto& a, const auto& b) { return a.pointer < b.pointer; });
    data_ = std::move(owned);
    return Error::ok();
  }

  [[nodiscard]] auto source() const noexcept -> std::string_view {
    if (!data_) return {};
    return std::string_view(data_->input)
        .substr(data_->root.offset, data_->root.length);
  }

  [[nodiscard]] auto operator[](std::string_view pointer) const
      -> DomSelection<MaxDepth> {
    if (!data_)
      return {data_, {}, {ErrorCode::invalid_state, 0, "DOM has no document"}};
    std::vector<std::string> tokens;
    if (!detail::parse_dom_pointer(pointer, tokens))
      return {data_, {}, {ErrorCode::invalid_token, 0, "invalid JSON Pointer"}};
    detail::ValueRange range{};
    bool found = false;
    if constexpr (Indexed) {
      // Pointer escapes have one canonical encoding. Re-encoding also handles
      // decoded '~01' correctly and preserves embedded NUL bytes.
      std::string canonical;
      for (const auto& part : tokens)
        detail::append_pointer_token(canonical, part);
      const auto it =
          std::lower_bound(data_->index.begin(), data_->index.end(), canonical,
                           [](const auto& entry, const auto& key) {
                             return entry.pointer < key;
                           });
      found = it != data_->index.end() && it->pointer == canonical;
      if (found) range = it->range;
    } else {
      found = detail::search_dom<MaxDepth>(*data_, tokens, range);
    }
    return {data_, range,
            found ? Error::ok()
                  : Error{ErrorCode::missing_required, data_->root.offset,
                          "JSON Pointer does not select a value"}};
  }

 private:
  friend struct detail::DomAccess;
  std::shared_ptr<const detail::DomData> data_;
};

template <std::size_t MaxDepth = 128>
using GenericDom = BasicDom<true, MaxDepth>;
template <std::size_t MaxDepth = 128>
using DeferredDom = BasicDom<false, MaxDepth>;

namespace detail {
template <bool Indexed, std::size_t MaxDepth, ReaderConcept R>
auto DomAccess::capture(R& reader, typename R::state_type& state)
    -> BasicDom<Indexed, MaxDepth> {
  BasicDom<Indexed, MaxDepth> output;
  if (::hosq::detail::reader_error(reader, state)) return output;
  if constexpr (reader_traits_t<R>::format != Format::json) {
    ::hosq::detail::report_reader_error(
        reader, state,
        {ErrorCode::unsupported_type, reader.current_token(state).offset,
         "JSON DOM capture requires a JSON Reader"});
  } else {
    auto owned = std::make_shared<DomData>();
    if (!validate_value<Indexed>(
            reader, state,
            [&](const ErrorPath& path, ValueRange range) {
              owned->root = range;
              if constexpr (Indexed)
                owned->index.push_back({dom_pointer(path), range});
            },
            MaxDepth))
      return output;
    const auto begin = owned->root.offset;
    owned->input =
        reader.input(state).as_string_view().substr(begin, owned->root.length);
    owned->root.offset = 0;
    if constexpr (Indexed) {
      for (auto& entry : owned->index) entry.range.offset -= begin;
      std::sort(
          owned->index.begin(), owned->index.end(),
          [](const auto& a, const auto& b) { return a.pointer < b.pointer; });
    }
    output.data_ = std::move(owned);
  }
  return output;
}

template <bool Indexed, std::size_t MaxDepth>
struct DecodeDom {
  template <class Context>
    requires(!std::is_const_v<Context>)
  auto operator()(Context& ctx) const -> BasicDom<Indexed, MaxDepth> {
    return DomAccess::capture<Indexed, MaxDepth>(ctx.reader(),
                                                 ctx.reader_state());
  }
};

struct EncodeDom {
  template <bool Indexed, std::size_t MaxDepth, class Context>
  auto operator()(const BasicDom<Indexed, MaxDepth>& dom, Context& ctx) const
      -> void {
    auto& writer = ctx.writer();
    using W = std::remove_cvref_t<decltype(writer)>;
    if (::hosq::detail::writer_error(writer)) return;
    if constexpr (writer_traits_t<W>::format != Format::json) {
      ::hosq::detail::report_writer_error(
          writer, {ErrorCode::unsupported_type, writer.output().size(),
                   "JSON DOM replay requires a JSON Writer"});
    } else {
      if (dom.source().empty()) {
        ::hosq::detail::report_writer_error(
            writer, {ErrorCode::invalid_state, writer.output().size(),
                     "DOM has no document to encode"});
        return;
      }
      FastReader<MaxDepth> reader;
      typename FastReader<MaxDepth>::state_type state;
      reader.reset(state, dom.source());
      while (reader.next_token(state)) {
        auto token = reader.current_token(state);
        if (token.type == TokenType::End) return;
        if (token.type == TokenType::Scalar) {
          resolve_dom_scalar(reader, state);
          token = reader.current_token(state);
          if (::hosq::detail::reader_error(reader, state)) break;
          const auto bytes = dom.source().substr(token.offset, token.length);
          const auto buffer =
              writer.write({token.length, token.type, token.context});
          if (::hosq::detail::writer_error(writer)) return;
          if (buffer.size() < bytes.size()) {
            ::hosq::detail::report_writer_error(
                writer, {ErrorCode::write_failed, writer.output().size(),
                         "JSON Writer returned a short scalar buffer"});
            return;
          }
          std::copy(bytes.begin(), bytes.end(), buffer.begin());
          if (!writer.commit(bytes.size())) {
            if (::hosq::detail::writer_error(writer).is_ok())
              ::hosq::detail::report_writer_error(
                  writer, {ErrorCode::write_failed, writer.output().size(),
                           "JSON Writer refused scalar commit"});
            return;
          }
        } else {
          (void)writer.write({0, token.type, 0});
          if (::hosq::detail::writer_error(writer)) return;
        }
      }
      ::hosq::detail::report_writer_error(
          writer, {ErrorCode::invalid_state, writer.output().size(),
                   "validated DOM source could not be replayed"});
    }
  }
};
}  // namespace detail
}  // namespace hosq::json

namespace hosq::decode {
/// Capture the current arbitrary JSON subtree, validating all of its contents.
template <std::size_t MaxDepth = 128>
inline constexpr auto json_dom = step<json::detail::DecodeDom<true, MaxDepth>>;
template <std::size_t MaxDepth = 128>
inline constexpr auto json_deferred =
    step<json::detail::DecodeDom<false, MaxDepth>>;
}  // namespace hosq::decode

namespace hosq::encode {
/// Replay the owned JSON value, preserving scalar lexemes and JSON kinds.
inline constexpr auto json_dom = step<json::detail::EncodeDom>;
}  // namespace hosq::encode
