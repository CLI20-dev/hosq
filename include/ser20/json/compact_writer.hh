#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

#include "ser20/error.hh"
#include "ser20/reader_base.hh"
#include "ser20/result.hh"
#include "ser20/writer_base.hh"

namespace ser20 {

namespace json {

template <std::size_t MaxDepth = 1024>
class CompactWriter {
 public:
  using output_type = std::string_view;
  using buffer_type = std::span<char>;

  explicit CompactWriter(std::string& output) noexcept : output_(output) {}

  CompactWriter(const CompactWriter&) = delete;
  auto operator=(const CompactWriter&) -> CompactWriter& = delete;

  CompactWriter(CompactWriter&&) = delete;
  auto operator=(CompactWriter&&) -> CompactWriter& = delete;

  ~CompactWriter() = default;

  [[nodiscard]]
  auto write(const WriteRequest& request) -> Result<buffer_type, Error> {
    if (finished_ || pending_.active) {
      return unexpected(Error{
          .code = ErrorCode::unexpected_token,
          .offset = output_.size(),
      });
    }

    switch (request.type) {
      case TokenType::ObjectBegin:
        return begin_container(request, FrameKind::object, '{');

      case TokenType::ObjectEnd:
        return end_container(request, FrameKind::object, '}');

      case TokenType::ArrayBegin:
        return begin_container(request, FrameKind::array, '[');

      case TokenType::ArrayEnd:
        return end_container(request, FrameKind::array, ']');

      case TokenType::ScalarBegin:
        return begin_scalar(request);

      case TokenType::End:
        return end_document(request);
    }

    return unexpected(Error{
        .code = ErrorCode::unexpected_token,
        .offset = output_.size(),
    });
  }

  [[nodiscard]]
  auto commit(std::size_t written) -> Result<void, Error> {
    if (finished_ || !pending_.active) {
      return unexpected(Error{
          .code = ErrorCode::unexpected_token,
          .offset = output_.size(),
      });
    }

    if (written > pending_.capacity) {
      return unexpected(Error{
          .code = ErrorCode::unexpected_token,
          .offset = output_.size(),
      });
    }

    output_.resize(pending_.begin + written);

    if (pending_.quoted) {
      output_.push_back('"');
    }

    if (pending_.role == PendingRole::key) {
      output_.push_back(':');
      frame().expecting_key = false;
    } else {
      after_value();
    }

    pending_ = {};
    return {};
  }

  [[nodiscard]]
  auto output() const noexcept -> std::string_view {
    return output_;
  }

 private:
  enum class FrameKind : std::uint8_t {
    object,
    array,
  };

  struct Frame {
    FrameKind kind;
    std::size_t count = 0;
    bool expecting_key = false;
  };

  enum class PendingRole : std::uint8_t {
    none,
    key,
    value,
  };

  struct Pending {
    bool active = false;
    bool quoted = false;
    PendingRole role = PendingRole::none;
    std::size_t begin = 0;
    std::size_t capacity = 0;
  };

  [[nodiscard]]
  auto frame() noexcept -> Frame& {
    return stack_[depth_ - 1];
  }

  [[nodiscard]]
  auto frame() const noexcept -> const Frame& {
    return stack_[depth_ - 1];
  }

  [[nodiscard]]
  auto before_value() -> Result<void, Error> {
    if (depth_ == 0) {
      if (root_written_) {
        return unexpected(Error{
            .code = ErrorCode::unexpected_token,
            .offset = output_.size(),
        });
      }

      return {};
    }

    auto& current = frame();

    if (current.kind == FrameKind::array) {
      if (current.count != 0) {
        output_.push_back(',');
      }

      return {};
    }

    if (current.expecting_key) {
      return unexpected(Error{
          .code = ErrorCode::unexpected_token,
          .offset = output_.size(),
      });
    }

    return {};
  }

  [[nodiscard]]
  auto before_key() -> Result<void, Error> {
    if (depth_ == 0 || frame().kind != FrameKind::object ||
        !frame().expecting_key) {
      return unexpected(Error{
          .code = ErrorCode::unexpected_token,
          .offset = output_.size(),
      });
    }

    if (frame().count != 0) {
      output_.push_back(',');
    }

    return {};
  }

  auto after_value() noexcept -> void {
    if (depth_ == 0) {
      root_written_ = true;
      return;
    }

    auto& current = frame();

    if (current.kind == FrameKind::array) {
      ++current.count;
      return;
    }

    current.expecting_key = true;
    ++current.count;
  }

  [[nodiscard]]
  auto begin_container(const WriteRequest& request, FrameKind kind, char open)
      -> Result<buffer_type, Error> {
    if (request.context != 0) {
      return unexpected(Error{
          .code = ErrorCode::unexpected_token,
          .offset = output_.size(),
      });
    }

    if (depth_ >= MaxDepth) {
      return unexpected(Error{
          .code = ErrorCode::depth_limit_exceeded,
          .offset = output_.size(),
      });
    }

    auto prepared = before_value();
    if (!prepared) {
      return unexpected(prepared.error());
    }

    output_.push_back(open);

    stack_[depth_++] = Frame{
        .kind = kind,
        .count = 0,
        .expecting_key = kind == FrameKind::object,
    };

    static_cast<void>(request.length);

    return buffer_type{};
  }

  [[nodiscard]]
  auto end_container(const WriteRequest& request, FrameKind kind, char close)
      -> Result<buffer_type, Error> {
    if (request.length != 0 || request.context != 0) {
      return unexpected(Error{
          .code = ErrorCode::unexpected_token,
          .offset = output_.size(),
      });
    }

    if (depth_ == 0 || frame().kind != kind) {
      return unexpected(Error{
          .code = ErrorCode::unexpected_token,
          .offset = output_.size(),
      });
    }

    if (kind == FrameKind::object && !frame().expecting_key) {
      return unexpected(Error{
          .code = ErrorCode::unexpected_token,
          .offset = output_.size(),
      });
    }

    output_.push_back(close);
    --depth_;
    after_value();

    return buffer_type{};
  }

  [[nodiscard]]
  auto begin_scalar(const WriteRequest& request) -> Result<buffer_type, Error> {
    if (request.length == unresolved_length) {
      return unexpected(Error{
          .code = ErrorCode::unexpected_token,
          .offset = output_.size(),
      });
    }

    const auto context = static_cast<JsonTokenContext>(request.context);

    if (context != JsonTokenContext::string &&
        context != JsonTokenContext::literal) {
      return unexpected(Error{
          .code = ErrorCode::unexpected_token,
          .offset = output_.size(),
      });
    }

    const bool is_string = context == JsonTokenContext::string;
    const bool is_key = depth_ != 0 && frame().kind == FrameKind::object &&
                        frame().expecting_key;

    if (is_key) {
      if (!is_string) {
        return unexpected(Error{
            .code = ErrorCode::unexpected_token,
            .offset = output_.size(),
        });
      }

      auto prepared = before_key();
      if (!prepared) {
        return unexpected(prepared.error());
      }
    } else {
      auto prepared = before_value();
      if (!prepared) {
        return unexpected(prepared.error());
      }
    }

    if (is_string) {
      output_.push_back('"');
    }

    const std::size_t begin = output_.size();
    output_.resize(begin + request.length);

    pending_ = Pending{
        .active = true,
        .quoted = is_string,
        .role = is_key ? PendingRole::key : PendingRole::value,
        .begin = begin,
        .capacity = request.length,
    };

    return buffer_type{output_.data() + begin, request.length};
  }

  [[nodiscard]]
  auto end_document(const WriteRequest& request) -> Result<buffer_type, Error> {
    if (request.length != 0 || request.context != 0) {
      return unexpected(Error{
          .code = ErrorCode::unexpected_token,
          .offset = output_.size(),
      });
    }

    if (depth_ != 0 || !root_written_) {
      return unexpected(Error{
          .code = ErrorCode::unexpected_eof,
          .offset = output_.size(),
      });
    }

    finished_ = true;
    return buffer_type{};
  }

 private:
  std::string& output_;

  std::array<Frame, MaxDepth> stack_{};
  std::size_t depth_{0};

  bool root_written_{false};
  bool finished_{false};

  Pending pending_{};
};

static_assert(WriterConcept<CompactWriter<>>);

}  // namespace json

template <std::size_t MaxDepth>
struct WriterTraits<json::CompactWriter<MaxDepth>> {
  static constexpr Format format = Format::json;
  static constexpr bool is_binary_format = false;
};

}  // namespace ser20
