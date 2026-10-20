#pragma once

#include <concepts>
#include <cstddef>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "hosq/context.hh"
#include "hosq/json/simple_reader.hh"
#include "hosq/scalar_codec.hh"

namespace hosq::detail {

// Diagnostic traversal uses only the Reader protocol and scalar codecs. It is
// deliberately independent of schema decoding and never touches its state.
template <ReaderConcept R>
auto locate_error_path(typename R::input_type input, std::size_t offset)
    -> ErrorPath {
  using Replay = std::conditional_t<reader_traits_t<R>::format == Format::json,
                                    json::SimpleReader, R>;
  if constexpr (!std::default_initializable<Replay> ||
                !std::default_initializable<typename Replay::state_type> ||
                !std::constructible_from<typename Replay::input_type,
                                         typename R::input_type>) {
    return {};
  } else {
    Replay reader;
    typename Replay::state_type state{};
    reader.reset(state, typename Replay::input_type(input));
    if (reader.error(state) || offset > reader.input(state).size()) return {};

    struct Frame {
      bool object;
      std::size_t ordinal = 0;
      bool key = true;
      PathSegment member;
      std::size_t path_size;
    };
    std::vector<Frame> frames;
    ErrorPath path;
    bool root_complete = false;

    auto position = [&] {
      auto result = path;
      if (!frames.empty()) {
        const auto& frame = frames.back();
        result.push_back(frame.object ? frame.member
                                      : PathSegment::element(frame.ordinal));
      }
      return result;
    };
    auto complete = [&] {
      if (frames.empty()) {
        root_complete = true;
      } else {
        auto& frame = frames.back();
        if (frame.object && frame.key) {
          frame.key = false;
        } else {
          ++frame.ordinal;
          frame.key = true;
          if (frame.object) frame.member = PathSegment::key_at(frame.ordinal);
        }
      }
    };

    while (!root_complete) {
      const bool ok = reader.next_token(state);
      const auto token = reader.current_token(state);
      const bool closing = token.type == TokenType::ObjectEnd ||
                           token.type == TokenType::ArrayEnd;
      auto here = closing ? path : position();

      if (!frames.empty() && frames.back().object && frames.back().key &&
          token.type == TokenType::Scalar && ok) {
        // A rejected string probe must not poison traversal (binary mappings
        // can use arbitrary scalar or compound keys).
        if constexpr (std::copy_constructible<typename Replay::state_type>) {
          auto probe = state;
          DecodeContext<void, Replay> ctx{reader, probe};
          std::string name;
          struct ScalarProbe {};
          if (ext::DecodeSemanticScalar<Replay>{}(ScalarProbe{}, ctx, name) &&
              !reader.error(probe)) {
            frames.back().member = PathSegment::key(name, frames.back().ordinal);
            here = position();
          }
        }
      }

      const bool scalar_span =
          token.type == TokenType::Scalar && token.length != unresolved_length &&
          offset >= token.offset && offset - token.offset < token.length;
      if (token.type != TokenType::End && token.type != TokenType::Error &&
          (offset == token.offset || scalar_span))
        return here;

      if (!ok || reader.error(state)) {
        if (reader.error(state).offset != offset) return {};
        // Some readers recognize an opening token before rejecting the
        // following missing element/member.
        if (token.type == TokenType::ArrayBegin) {
          here.push_back(PathSegment::element(0));
        } else if (token.type == TokenType::ObjectBegin) {
          here.push_back(PathSegment::key_at(0));
        }
        return here;
      }
      if (token.type == TokenType::End || token.type == TokenType::Error)
        return {};
      if (closing) {
        if (frames.empty()) return {};
        frames.pop_back();
        path.resize(frames.empty() ? 0 : frames.back().path_size);
        complete();
      } else if (token.type == TokenType::ObjectBegin ||
                 token.type == TokenType::ArrayBegin) {
        path = std::move(here);
        frames.push_back(Frame{token.type == TokenType::ObjectBegin, 0, true,
                               PathSegment::key_at(0), path.size()});
      } else {
        complete();
      }
    }
    return {};
  }
}

}  // namespace hosq::detail
