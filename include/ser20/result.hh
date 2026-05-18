#pragma once

#include <cassert>
#include <type_traits>
#include <utility>

namespace ser20 {

template <class E>
class Unexpected {
 public:
  explicit Unexpected(E error) : error_(std::move(error)) {}

  [[nodiscard]]
  auto value() & -> E& {
    return error_;
  }

  [[nodiscard]]
  auto value() const& -> const E& {
    return error_;
  }

  [[nodiscard]]
  auto value() && -> E&& {
    return std::move(error_);
  }

 private:
  E error_;
};

template <class E>
auto unexpected(E error) -> Unexpected<E> {
  return Unexpected<E>{std::move(error)};
}

template <class T, class E>
class Result {
 public:
  Result(const T& value) : has_value_(true) { new (&storage_.value) T(value); }

  Result(T&& value) : has_value_(true) {
    new (&storage_.value) T(std::move(value));
  }

  Result(Unexpected<E> unexpected) : has_value_(false) {
    new (&storage_.error) E(std::move(unexpected).value());
  }

  Result(const Result& other) : has_value_(other.has_value_) {
    if (has_value_) {
      new (&storage_.value) T(other.storage_.value);
    } else {
      new (&storage_.error) E(other.storage_.error);
    }
  }

  Result(Result&& other) noexcept(std::is_nothrow_move_constructible_v<T> &&
                                  std::is_nothrow_move_constructible_v<E>)
      : has_value_(other.has_value_) {
    if (has_value_) {
      new (&storage_.value) T(std::move(other.storage_.value));
    } else {
      new (&storage_.error) E(std::move(other.storage_.error));
    }
  }

  auto operator=(const Result& other) -> Result& {
    if (this == &other) {
      return *this;
    }

    this->~Result();
    new (this) Result(other);
    return *this;
  }

  auto operator=(Result&& other) noexcept(
      std::is_nothrow_move_constructible_v<T> &&
      std::is_nothrow_move_constructible_v<E>) -> Result& {
    if (this == &other) {
      return *this;
    }

    this->~Result();
    new (this) Result(std::move(other));
    return *this;
  }

  ~Result() {
    if (has_value_) {
      storage_.value.~T();
    } else {
      storage_.error.~E();
    }
  }

  [[nodiscard]]
  explicit operator bool() const noexcept {
    return has_value_;
  }

  [[nodiscard]]
  auto has_value() const noexcept -> bool {
    return has_value_;
  }

  [[nodiscard]]
  auto value() & -> T& {
    assert(has_value_);
    return storage_.value;
  }

  [[nodiscard]]
  auto value() const& -> const T& {
    assert(has_value_);
    return storage_.value;
  }

  [[nodiscard]]
  auto value() && -> T&& {
    assert(has_value_);
    return std::move(storage_.value);
  }

  [[nodiscard]]
  auto error() & -> E& {
    assert(!has_value_);
    return storage_.error;
  }

  [[nodiscard]]
  auto error() const& -> const E& {
    assert(!has_value_);
    return storage_.error;
  }

  [[nodiscard]]
  auto error() && -> E&& {
    assert(!has_value_);
    return std::move(storage_.error);
  }

  [[nodiscard]]
  auto operator->() -> T* {
    assert(has_value_);
    return &storage_.value;
  }

  [[nodiscard]]
  auto operator->() const -> const T* {
    assert(has_value_);
    return &storage_.value;
  }

  [[nodiscard]]
  auto operator*() & -> T& {
    assert(has_value_);
    return storage_.value;
  }

  [[nodiscard]]
  auto operator*() const& -> const T& {
    assert(has_value_);
    return storage_.value;
  }

  [[nodiscard]]
  auto operator*() && -> T&& {
    assert(has_value_);
    return std::move(storage_.value);
  }

 private:
  union Storage {
    Storage() noexcept {}
    ~Storage() noexcept {}

    Storage(const Storage&) = delete;
    Storage(Storage&&) = delete;
    auto operator=(const Storage&) -> Storage& = delete;
    auto operator=(Storage&&) -> Storage& = delete;

    T value;
    E error;
  };

  bool has_value_;
  Storage storage_;
};

template <class E>
class Result<void, E> {
 public:
  Result() noexcept = default;

  Result(Unexpected<E> unexpected)
      : has_value_(false), error_(std::move(unexpected).value()) {}

  Result(const Result&) = default;
  Result(Result&&) = default;
  auto operator=(const Result&) -> Result& = default;
  auto operator=(Result&&) -> Result& = default;
  ~Result() = default;

  [[nodiscard]]
  explicit operator bool() const noexcept {
    return has_value_;
  }

  [[nodiscard]]
  auto has_value() const noexcept -> bool {
    return has_value_;
  }

  auto value() const noexcept -> void { assert(has_value_); }

  [[nodiscard]]
  auto error() & -> E& {
    assert(!has_value_);
    return error_;
  }

  [[nodiscard]]
  auto error() const& -> const E& {
    assert(!has_value_);
    return error_;
  }

  [[nodiscard]]
  auto error() && -> E&& {
    assert(!has_value_);
    return std::move(error_);
  }

 private:
  bool has_value_{true};
  E error_{};
};

}  // namespace ser20
