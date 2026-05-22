// #pragma once
//
// #include <cassert>
// #include <type_traits>
// #include <utility>
//
// #include "hosq/error.hh"
//
// namespace hosq {
//
// class ResultTag {};
//
// template <class E>
// class Unexpected {
//  public:
//   explicit Unexpected(E error) : error_(std::move(error)) {}
//
//   [[nodiscard]]
//   auto value() & -> E& {
//     return error_;
//   }
//
//   [[nodiscard]]
//   auto value() const& -> const E& {
//     return error_;
//   }
//
//   [[nodiscard]]
//   auto value() && -> E&& {
//     return std::move(error_);
//   }
//
//  private:
//   E error_;
// };
//
// template <class E>
// auto unexpected(E error) -> Unexpected<E> {
//   return Unexpected<E>{std::move(error)};
// }
//
// template <class T, class E>
// class Result {
//  public:
//   using value_type = T;
//   using error_type = E;
//   using result_tag = ResultTag;
//
//   Result(const value_type& value) : has_value_(true) {
//     new (&storage_.value) value_type(value);
//   }
//
//   Result(value_type&& value) : has_value_(true) {
//     new (&storage_.value) value_type(std::move(value));
//   }
//
//   Result(Unexpected<error_type> unexpected) : has_value_(false) {
//     new (&storage_.error) error_type(std::move(unexpected).value());
//   }
//
//   Result(const Result& other) : has_value_(other.has_value_) {
//     if (has_value_) {
//       new (&storage_.value) value_type(other.storage_.value);
//     } else {
//       new (&storage_.error) error_type(other.storage_.error);
//     }
//   }
//
//   Result(Result&& other) noexcept(
//       std::is_nothrow_move_constructible_v<value_type> &&
//       std::is_nothrow_move_constructible_v<error_type>)
//       : has_value_(other.has_value_) {
//     if (has_value_) {
//       new (&storage_.value) value_type(std::move(other.storage_.value));
//     } else {
//       new (&storage_.error) error_type(std::move(other.storage_.error));
//     }
//   }
//
//   auto operator=(const Result& other) -> Result& {
//     if (this == &other) {
//       return *this;
//     }
//
//     this->~Result();
//     new (this) Result(other);
//     return *this;
//   }
//
//   auto operator=(Result&& other) noexcept(
//       std::is_nothrow_move_constructible_v<value_type> &&
//       std::is_nothrow_move_constructible_v<error_type>) -> Result& {
//     if (this == &other) {
//       return *this;
//     }
//
//     this->~Result();
//     new (this) Result(std::move(other));
//     return *this;
//   }
//
//   ~Result() {
//     if (has_value_) {
//       storage_.value.~value_type();
//     } else {
//       storage_.error.~error_type();
//     }
//   }
//
//   [[nodiscard]]
//   explicit operator bool() const noexcept {
//     return has_value_;
//   }
//
//   [[nodiscard]]
//   auto has_value() const noexcept -> bool {
//     return has_value_;
//   }
//
//   [[nodiscard]]
//   auto value() & -> value_type& {
//     assert(has_value_);
//     return storage_.value;
//   }
//
//   [[nodiscard]]
//   auto value() const& -> const value_type& {
//     assert(has_value_);
//     return storage_.value;
//   }
//
//   [[nodiscard]]
//   auto value() && -> value_type&& {
//     assert(has_value_);
//     return std::move(storage_.value);
//   }
//
//   [[nodiscard]]
//   auto error() & -> error_type& {
//     assert(!has_value_);
//     return storage_.error;
//   }
//
//   [[nodiscard]]
//   auto error() const& -> const error_type& {
//     assert(!has_value_);
//     return storage_.error;
//   }
//
//   [[nodiscard]]
//   auto error() && -> error_type&& {
//     assert(!has_value_);
//     return std::move(storage_.error);
//   }
//
//   [[nodiscard]]
//   auto operator->() -> value_type* {
//     assert(has_value_);
//     return &storage_.value;
//   }
//
//   [[nodiscard]]
//   auto operator->() const -> const value_type* {
//     assert(has_value_);
//     return &storage_.value;
//   }
//
//   [[nodiscard]]
//   auto operator*() & -> value_type& {
//     assert(has_value_);
//     return storage_.value;
//   }
//
//   [[nodiscard]]
//   auto operator*() const& -> const value_type& {
//     assert(has_value_);
//     return storage_.value;
//   }
//
//   [[nodiscard]]
//   auto operator*() && -> value_type&& {
//     assert(has_value_);
//     return std::move(storage_.value);
//   }
//
//  private:
//   union Storage {
//     Storage() noexcept {}
//     ~Storage() noexcept {}
//
//     Storage(const Storage&) = delete;
//     Storage(Storage&&) = delete;
//     auto operator=(const Storage&) -> Storage& = delete;
//     auto operator=(Storage&&) -> Storage& = delete;
//
//     value_type value;
//     error_type error;
//   };
//
//   bool has_value_;
//   Storage storage_;
// };
//
// template <class E>
// class Result<void, E> {
//  public:
//   using value_type = void;
//   using error_type = E;
//   using result_tag = ResultTag;
//
//   Result() noexcept = default;
//
//   Result(Unexpected<error_type> unexpected)
//       : has_value_(false), error_(std::move(unexpected).value()) {}
//
//   Result(const Result&) = default;
//   Result(Result&&) = default;
//   auto operator=(const Result&) -> Result& = default;
//   auto operator=(Result&&) -> Result& = default;
//   ~Result() = default;
//
//   [[nodiscard]]
//   explicit operator bool() const noexcept {
//     return has_value_;
//   }
//
//   [[nodiscard]]
//   auto has_value() const noexcept -> bool {
//     return has_value_;
//   }
//
//   auto value() const noexcept -> void { assert(has_value_); }
//
//   [[nodiscard]]
//   auto error() & -> error_type& {
//     assert(!has_value_);
//     return error_;
//   }
//
//   [[nodiscard]]
//   auto error() const& -> const error_type& {
//     assert(!has_value_);
//     return error_;
//   }
//
//   [[nodiscard]]
//   auto error() && -> error_type&& {
//     assert(!has_value_);
//     return std::move(error_);
//   }
//
//  private:
//   bool has_value_{true};
//   error_type error_;
// };
//
// namespace detail {
//
// template <class T>
// concept ResultType = requires {
//   typename std::remove_cvref_t<T>::result_tag;
//   typename std::remove_cvref_t<T>::value_type;
//   typename std::remove_cvref_t<T>::error_type;
// } && std::same_as<typename std::remove_cvref_t<T>::result_tag, ResultTag>;
//
// template <class T>
// concept HosqResultType =
//     ResultType<T> &&
//     std::same_as<typename std::remove_cvref_t<T>::error_type, Error>;
//
// }  // namespace detail
//
// }  // namespace hosq
