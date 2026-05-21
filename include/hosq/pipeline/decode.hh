#pragma once

#include <charconv>
#include <concepts>
#include <cstddef>
#include <string_view>
#include <system_error>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "hosq/context.hh"
#include "hosq/detail/always_false.hh"
#include "hosq/detail/as_tuple.hh"
#include "hosq/detail/pipeline_analysis.hh"
#include "hosq/entry.hh"
#include "hosq/error.hh"
#include "hosq/pipeline.hh"
#include "hosq/result.hh"
#include "hosq/try.hh"
#include "hosq/view.hh"

namespace hosq::decode {

template <class Key, class Value>
struct object_items {
  std::vector<Key> keys;
  std::vector<Value> values;
};

namespace detail {

template <class T>
concept Number = (std::integral<std::remove_cvref_t<T>> &&
                  !std::same_as<std::remove_cvref_t<T>, bool>) ||
                 std::floating_point<std::remove_cvref_t<T>>;

template <Number T>
struct NumberImpl {
  auto operator()(std::string_view input) const
      -> Result<std::remove_cvref_t<T>, Error> {
    using Value = std::remove_cvref_t<T>;
    Value value{};
    const char* first = input.data();
    const char* last = input.data() + input.size();
    const auto result = std::from_chars(first, last, value);
    if (result.ec == std::errc::result_out_of_range) [[unlikely]] {
      return unexpected(Error{.code = ErrorCode::out_of_range});
    }
    if (result.ec != std::errc{} || result.ptr != last) [[unlikely]] {
      return unexpected(Error{.code = ErrorCode::invalid_number});
    }
    return value;
  }
};

struct IdentityImpl {
  template <class T>
  auto operator()(T value) const -> T {
    return value;
  }
};

template <auto F>
struct CustomImpl {
  template <class T>
  auto operator()(T value) const -> std::invoke_result_t<decltype(F), T> {
    return F(std::move(value));
  }
};

template <auto Pipeline, hosq::detail::PipelineVerificationMode Mode>
using pipeline_info_t =
    hosq::detail::pipeline_analysis_t<Mode, hosq::detail::AnyDecodeContext, void,
                                      std::remove_cvref_t<decltype(Pipeline)>>;

template <class Analysis>
using pipeline_storage_t = hosq::detail::pipeline_storage_t<Analysis>;

template <class Analysis>
using pipeline_terminal_t = hosq::detail::pipeline_terminal_t<Analysis>;

template <class Analysis>
inline constexpr bool pipeline_has_storage_v =
    !std::same_as<pipeline_storage_t<Analysis>, hosq::detail::NoStorage>;

template <class Terminal>
inline constexpr bool each_terminal_v =
    std::same_as<Terminal, void> || std::same_as<Terminal, bool>;

template <class Result>
using lifted_result_t = hosq::detail::lift_result_t<Result>;

struct VoidContextScratch {};

template <class State>
auto make_reader_checkpoint(State& state) {
  if constexpr (CheckpointableStateConcept<State>) {
    return state.checkpoint();
  } else {
    static_assert(std::is_copy_constructible_v<State> &&
                  std::is_copy_assignable_v<State>);
    return state;
  }
}

template <class State, class Checkpoint>
auto rollback_reader_checkpoint(State& state, Checkpoint&& checkpoint) -> void {
  if constexpr (CheckpointableStateConcept<State>) {
    state.rollback(std::forward<Checkpoint>(checkpoint));
  } else {
    static_assert(std::is_copy_constructible_v<State> &&
                  std::is_copy_assignable_v<State>);
    state = std::forward<Checkpoint>(checkpoint);
  }
}

template <auto Pipeline, class Context>
using pipeline_invoke_result_t =
    decltype(std::remove_cvref_t<decltype(Pipeline)>::invoke(
        std::declval<Context&>()));

template <auto Pipeline, class Context>
auto invoke_for_each(Context& ctx, bool& aggregate) -> Result<void, Error> {
  using pipeline_type = std::remove_cvref_t<decltype(Pipeline)>;
  using raw_result = pipeline_invoke_result_t<Pipeline, Context>;

  if constexpr (std::same_as<raw_result, void>) {
    pipeline_type::invoke(ctx);
    return {};
  } else if constexpr (hosq::detail::HosqResultType<raw_result>) {
    auto ret = pipeline_type::invoke(ctx);
    if (!ret) [[unlikely]] {
      return unexpected(std::move(ret).error());
    }

    using value_type = typename std::remove_cvref_t<raw_result>::value_type;
    if constexpr (std::same_as<value_type, bool>) {
      aggregate = aggregate && static_cast<bool>(*ret);
    }
    return {};
  } else if constexpr (std::same_as<raw_result, bool>) {
    aggregate = aggregate && pipeline_type::invoke(ctx);
    return {};
  } else {
    static_assert(hosq::detail::always_false<raw_result>,
                  "each pipeline must return void, bool, Result<void, Error>, "
                  "or Result<bool, Error>");
  }
}

template <auto Pipeline, class Context>
auto invoke_for_collect(Context& ctx)
    -> lifted_result_t<pipeline_invoke_result_t<Pipeline, Context>> {
  using pipeline_type = std::remove_cvref_t<decltype(Pipeline)>;
  using raw_result = pipeline_invoke_result_t<Pipeline, Context>;
  using lifted_result = lifted_result_t<raw_result>;

  if constexpr (hosq::detail::HosqResultType<raw_result>) {
    return pipeline_type::invoke(ctx);
  } else {
    static_assert(!std::same_as<raw_result, void>,
                  "collect pipeline must return a value");
    return lifted_result{pipeline_type::invoke(ctx)};
  }
}

template <class Ret>
auto hard_decode_result_to_void(Ret&& ret, const Error& current_error)
    -> Result<void, Error> {
  using ret_type = std::remove_cvref_t<Ret>;

  if constexpr (hosq::detail::HosqResultType<ret_type>) {
    if (!ret) [[unlikely]] {
      return unexpected(ret.error());
    }
    return {};
  } else if constexpr (std::same_as<ret_type, bool>) {
    if (!ret) [[unlikely]] {
      return unexpected(Error{
          .code = ErrorCode::validation_failed,
          .offset = current_error.offset,
      });
    }
    return {};
  } else {
    return {};
  }
}

template <ScanPhase Phase, hosq::detail::TryT T, ReaderConcept R>
  requires hosq::detail::EntryT<typename std::remove_cvref_t<T>::base_type>
auto try_decode_entry_key_candidate(
    T& entry, R& reader, typename R::state_type& reader_state)
    -> Result<bool, Error> {
  using entry_type = std::remove_cvref_t<T>;
  using entry_context_type = DecodeContext<T, R, Phase>;
  using void_context_type = DecodeContext<void, R, Phase>;
  using ret_type = decltype(std::declval<T&>().decode_key(
      std::declval<void_context_type&>()));

  auto entry_ctx = entry_context_type{entry, reader, reader_state};
  auto ctx = void_context_type{entry_ctx};

  if constexpr (entry_type::template key_decoder_const_invokable<R, Phase>) {
    const auto& cctx = ctx;
    if constexpr (hosq::detail::HosqResultType<std::remove_cvref_t<ret_type>>) {
      auto ret = entry.decode_key(cctx);
      if (!ret) [[unlikely]] {
        if (ret.error().code == ErrorCode::candidate_rejected) {
          return false;
        }
        return unexpected(ret.error());
      }
      return *ret;
    } else {
      return entry.decode_key(cctx);
    }
  } else {
    auto checkpoint = make_reader_checkpoint(reader_state);
    if constexpr (hosq::detail::HosqResultType<std::remove_cvref_t<ret_type>>) {
      auto ret = entry.decode_key(ctx);
      if (!ret) [[unlikely]] {
        if (ret.error().code == ErrorCode::candidate_rejected) {
          rollback_reader_checkpoint(reader_state, std::move(checkpoint));
          return false;
        }
        rollback_reader_checkpoint(reader_state, std::move(checkpoint));
        return unexpected(ret.error());
      }
      if (!*ret) {
        rollback_reader_checkpoint(reader_state, std::move(checkpoint));
        return false;
      }
      return true;
    } else {
      auto ret = entry.decode_key(ctx);
      if (!ret) {
        rollback_reader_checkpoint(reader_state, std::move(checkpoint));
        return false;
      }
      return true;
    }
  }
}

template <hosq::detail::TryT T, ReaderConcept R>
  requires hosq::detail::EntryT<typename std::remove_cvref_t<T>::base_type>
auto decode_entry_value_hard(
    T& entry, R& reader, typename R::state_type& reader_state)
    -> Result<void, Error> {
  constexpr auto phase = ScanPhase::first;

  using entry_context_type = DecodeContext<T, R, phase>;
  using void_context_type = DecodeContext<void, R, phase>;
  using ret_type = decltype(std::declval<T&>().decode_value(
      std::declval<void_context_type&>()));

  if (!reader.next_token(reader_state)) [[unlikely]] {
    return unexpected(reader.error(reader_state));
  }

  auto entry_ctx = entry_context_type{entry, reader, reader_state};
  auto ctx = void_context_type{entry_ctx};

  if constexpr (std::same_as<ret_type, void>) {
    entry.decode_value(ctx);
    return {};
  } else {
    auto ret = entry.decode_value(ctx);
    return hard_decode_result_to_void(ret, reader.error(reader_state));
  }
}

template <ScanPhase Phase, hosq::detail::TryT T, ReaderConcept R>
  requires hosq::detail::EntryT<typename std::remove_cvref_t<T>::base_type>
auto try_decode_entry_candidate(
    T& entry, R& reader, typename R::state_type& reader_state)
    -> Result<bool, Error> {
  auto key_ret =
      try_decode_entry_key_candidate<Phase>(entry, reader, reader_state);
  if (!key_ret) [[unlikely]] {
    return unexpected(key_ret.error());
  }
  if (!*key_ret) {
    return false;
  }

  auto value_ret = decode_entry_value_hard(entry, reader, reader_state);
  if (!value_ret) [[unlikely]] {
    return unexpected(value_ret.error());
  }
  return true;
}

template <class Tuple, ReaderConcept R, std::size_t... I>
auto try_decode_entry_candidates_impl(
    Tuple& tuple, R& reader, typename R::state_type& reader_state,
    std::index_sequence<I...>) -> Result<bool, Error> {
  Result<bool, Error> result{false};

  ([&]() -> bool {
    auto& entry = std::get<I>(tuple);
    auto candidate = try_decode_entry_candidate<I == 0 ? ScanPhase::first
                                                       : ScanPhase::normal>(
        entry, reader, reader_state);
    if (!candidate) [[unlikely]] {
      result = unexpected(candidate.error());
      return true;
    }
    if (*candidate) {
      result = true;
      return true;
    }
    return false;
  }() || ...);

  return result;
}

template <class KeyStorage, class ValueStorage>
struct ObjectEachStorage {
  static constexpr bool key_has_storage =
      !std::same_as<KeyStorage, hosq::detail::NoStorage>;
  static constexpr bool value_has_storage =
      !std::same_as<ValueStorage, hosq::detail::NoStorage>;

  static_assert(key_has_storage || value_has_storage,
                "decode::each<KeyP, ValueP> requires key or value pipeline to "
                "define storage_type<Input>");
  static_assert(!(key_has_storage && value_has_storage),
                "decode::each<KeyP, ValueP> currently forbids both key and "
                "value pipelines from defining storage_type<Input>");

  using type = std::conditional_t<key_has_storage, KeyStorage, ValueStorage>;
};

template <auto... Pipelines>
struct EachImpl {
  static_assert(
      hosq::detail::always_false<
          std::tuple<std::integral_constant<std::size_t, sizeof...(Pipelines)>>>,
      "decode::each requires one array pipeline or two object "
      "pipelines");
};

template <auto Pipeline>
struct EachImpl<Pipeline> {
  static_assert(hosq::detail::PipelineObject<decltype(Pipeline)>,
                "decode::each<P> requires P to be a hosq::Pipeline");

  using info = pipeline_info_t<
      Pipeline, hosq::detail::PipelineVerificationMode::exactly_one_storage>;
  using terminal_type = pipeline_terminal_t<info>;

  static_assert(each_terminal_v<terminal_type>,
                "decode::each<P> terminal must be void, bool, Result<void, "
                "Error>, or Result<bool, Error>");

  template <class Input>
  using storage_type = pipeline_storage_t<info>;

  auto operator()(ArrayView<hosq::detail::PipelineProbeReader, ScanPhase::first>,
                  hosq::detail::AnyDecodeContext) const
      -> std::conditional_t<std::same_as<terminal_type, bool>,
                            Result<bool, Error>, Result<void, Error>>;

  template <ReaderConcept R, ScanPhase Phase, class Storage>
  auto operator()(ArrayView<R, Phase> view,
                  DecodeContext<Storage, R, Phase>& ctx) const
      -> std::conditional_t<std::same_as<terminal_type, bool>,
                            Result<bool, Error>, Result<void, Error>> {
    auto& reader = view.reader();
    auto& state = view.reader_state();
    bool aggregate = true;

    while (true) {
      if (!reader.next_token(state)) [[unlikely]] {
        return unexpected(reader.error(state));
      }

      const auto& tok = reader.current_token(state);
      if (tok.type == TokenType::ArrayEnd) {
        if constexpr (std::same_as<terminal_type, bool>) {
          return aggregate;
        } else {
          return {};
        }
      }

      auto elem_ctx =
          DecodeContext<Storage, R, ScanPhase::first>{ctx.elem(), reader, state};
      auto ret = invoke_for_each<Pipeline>(elem_ctx, aggregate);
      if (!ret) [[unlikely]] {
        return unexpected(std::move(ret).error());
      }
    }
  }
};

template <auto KeyPipeline, auto ValuePipeline>
struct EachImpl<KeyPipeline, ValuePipeline> {
  static_assert(hosq::detail::PipelineObject<decltype(KeyPipeline)> &&
                    hosq::detail::PipelineObject<decltype(ValuePipeline)>,
                "decode::each<KeyP, ValueP> requires hosq::Pipeline operands");

  using key_info =
      pipeline_info_t<KeyPipeline, hosq::detail::PipelineVerificationMode::none>;
  using value_info =
      pipeline_info_t<ValuePipeline,
                      hosq::detail::PipelineVerificationMode::none>;
  using key_storage_type = pipeline_storage_t<key_info>;
  using value_storage_type = pipeline_storage_t<value_info>;
  using terminal_type =
      std::conditional_t<std::same_as<pipeline_terminal_t<key_info>, bool> ||
                             std::same_as<pipeline_terminal_t<value_info>, bool>,
                         bool, void>;

  static_assert(each_terminal_v<pipeline_terminal_t<key_info>>,
                "decode::each<KeyP, ValueP> key terminal must be void, bool, "
                "Result<void, Error>, or Result<bool, Error>");
  static_assert(each_terminal_v<pipeline_terminal_t<value_info>>,
                "decode::each<KeyP, ValueP> value terminal must be void, bool, "
                "Result<void, Error>, or Result<bool, Error>");

  template <class Input>
  using storage_type =
      typename ObjectEachStorage<key_storage_type, value_storage_type>::type;

  auto operator()(
      ObjectView<hosq::detail::PipelineProbeReader, ScanPhase::first>,
      hosq::detail::AnyDecodeContext) const
      -> std::conditional_t<std::same_as<terminal_type, bool>,
                            Result<bool, Error>, Result<void, Error>>;

  template <ReaderConcept R, ScanPhase Phase, class Storage>
  auto operator()(ObjectView<R, Phase> view,
                  DecodeContext<Storage, R, Phase>& ctx) const
      -> std::conditional_t<std::same_as<terminal_type, bool>,
                            Result<bool, Error>, Result<void, Error>> {
    auto& reader = view.reader();
    auto& state = view.reader_state();
    bool aggregate = true;

    while (true) {
      if (!reader.next_token(state)) [[unlikely]] {
        return unexpected(reader.error(state));
      }

      const auto& tok = reader.current_token(state);
      if (tok.type == TokenType::ObjectEnd) {
        if constexpr (std::same_as<terminal_type, bool>) {
          return aggregate;
        } else {
          return {};
        }
      }

      if constexpr (pipeline_has_storage_v<key_info>) {
        auto key_ctx = DecodeContext<Storage, R, ScanPhase::first>{
            ctx.elem(), reader, state};
        auto key_ret = invoke_for_each<KeyPipeline>(key_ctx, aggregate);
        if (!key_ret) [[unlikely]] {
          return unexpected(std::move(key_ret).error());
        }
      } else {
        auto scratch = VoidContextScratch{};
        auto key_storage_ctx =
            DecodeContext<VoidContextScratch, R, ScanPhase::first>{
                scratch, reader, state};
        auto key_ctx = DecodeContext<void, R, ScanPhase::first>{key_storage_ctx};
        auto key_ret = invoke_for_each<KeyPipeline>(key_ctx, aggregate);
        if (!key_ret) [[unlikely]] {
          return unexpected(std::move(key_ret).error());
        }
      }

      if (!reader.next_token(state)) [[unlikely]] {
        return unexpected(reader.error(state));
      }

      if constexpr (pipeline_has_storage_v<value_info>) {
        auto value_ctx = DecodeContext<Storage, R, ScanPhase::first>{
            ctx.elem(), reader, state};
        auto value_ret = invoke_for_each<ValuePipeline>(value_ctx, aggregate);
        if (!value_ret) [[unlikely]] {
          return unexpected(std::move(value_ret).error());
        }
      } else {
        auto scratch = VoidContextScratch{};
        auto value_storage_ctx =
            DecodeContext<VoidContextScratch, R, ScanPhase::first>{
                scratch, reader, state};
        auto value_ctx =
            DecodeContext<void, R, ScanPhase::first>{value_storage_ctx};
        auto value_ret = invoke_for_each<ValuePipeline>(value_ctx, aggregate);
        if (!value_ret) [[unlikely]] {
          return unexpected(std::move(value_ret).error());
        }
      }
    }
  }
};

template <auto... Pipelines>
struct CollectImpl {
  static_assert(
      hosq::detail::always_false<
          std::tuple<std::integral_constant<std::size_t, sizeof...(Pipelines)>>>,
      "decode::collect requires one array pipeline or two object "
      "pipelines");
};

template <auto Pipeline>
struct CollectImpl<Pipeline> {
  static_assert(hosq::detail::PipelineObject<decltype(Pipeline)>,
                "decode::collect<P> requires P to be a hosq::Pipeline");

  using info =
      pipeline_info_t<Pipeline,
                      hosq::detail::PipelineVerificationMode::no_storage>;
  using value_type = pipeline_terminal_t<info>;

  static_assert(!std::same_as<value_type, void>,
                "decode::collect<P> requires a value-returning pipeline");

  template <ReaderConcept R, ScanPhase Phase>
  auto operator()(ArrayView<R, Phase> view) const
      -> Result<std::vector<value_type>, Error> {
    auto& reader = view.reader();
    auto& state = view.reader_state();
    auto values = std::vector<value_type>{};

    while (true) {
      if (!reader.next_token(state)) [[unlikely]] {
        return unexpected(reader.error(state));
      }

      const auto& tok = reader.current_token(state);
      if (tok.type == TokenType::ArrayEnd) {
        return values;
      }

      auto scratch = VoidContextScratch{};
      auto storage_ctx = DecodeContext<VoidContextScratch, R, ScanPhase::first>{
          scratch, reader, state};
      auto elem_ctx = DecodeContext<void, R, ScanPhase::first>{storage_ctx};
      auto ret = invoke_for_collect<Pipeline>(elem_ctx);
      if (!ret) [[unlikely]] {
        return unexpected(std::move(ret).error());
      }
      values.push_back(std::move(*ret));
    }
  }
};

template <auto KeyPipeline, auto ValuePipeline>
struct CollectImpl<KeyPipeline, ValuePipeline> {
  static_assert(hosq::detail::PipelineObject<decltype(KeyPipeline)> &&
                    hosq::detail::PipelineObject<decltype(ValuePipeline)>,
                "decode::collect<KeyP, ValueP> requires hosq::Pipeline "
                "operands");

  using key_info =
      pipeline_info_t<KeyPipeline,
                      hosq::detail::PipelineVerificationMode::no_storage>;
  using value_info =
      pipeline_info_t<ValuePipeline,
                      hosq::detail::PipelineVerificationMode::no_storage>;
  using key_type = pipeline_terminal_t<key_info>;
  using value_type = pipeline_terminal_t<value_info>;

  static_assert(!std::same_as<key_type, void>,
                "decode::collect<KeyP, ValueP> key pipeline must return a "
                "value");
  static_assert(!std::same_as<value_type, void>,
                "decode::collect<KeyP, ValueP> value pipeline must return a "
                "value");

  template <ReaderConcept R, ScanPhase Phase>
  auto operator()(ObjectView<R, Phase> view) const
      -> Result<object_items<key_type, value_type>, Error> {
    auto& reader = view.reader();
    auto& state = view.reader_state();
    auto items = object_items<key_type, value_type>{};

    while (true) {
      if (!reader.next_token(state)) [[unlikely]] {
        return unexpected(reader.error(state));
      }

      const auto& tok = reader.current_token(state);
      if (tok.type == TokenType::ObjectEnd) {
        return items;
      }

      auto key_scratch = VoidContextScratch{};
      auto key_storage_ctx =
          DecodeContext<VoidContextScratch, R, ScanPhase::first>{key_scratch,
                                                                 reader, state};
      auto key_ctx = DecodeContext<void, R, ScanPhase::first>{key_storage_ctx};
      auto key_ret = invoke_for_collect<KeyPipeline>(key_ctx);
      if (!key_ret) [[unlikely]] {
        return unexpected(std::move(key_ret).error());
      }

      if (!reader.next_token(state)) [[unlikely]] {
        return unexpected(reader.error(state));
      }

      auto value_scratch = VoidContextScratch{};
      auto value_storage_ctx =
          DecodeContext<VoidContextScratch, R, ScanPhase::first>{value_scratch,
                                                                 reader, state};
      auto value_ctx =
          DecodeContext<void, R, ScanPhase::first>{value_storage_ctx};
      auto value_ret = invoke_for_collect<ValuePipeline>(value_ctx);
      if (!value_ret) [[unlikely]] {
        return unexpected(std::move(value_ret).error());
      }

      items.keys.push_back(std::move(*key_ret));
      items.values.push_back(std::move(*value_ret));
    }
  }
};

template <class T>
struct ToImpl {
  static_assert(hosq::detail::AsTryEntryTupleT<T>,
                "decode::to<T> currently requires T to be a tuple of "
                "hosq::Try<hosq::Entry<...>> members");

  template <ReaderConcept R, ScanPhase Phase>
  auto operator()(ObjectView<R, Phase> view) const -> Result<T, Error> {
    auto value = T{};
    auto tuple = hosq::detail::as_tuple(value);
    constexpr auto tuple_size =
        std::tuple_size_v<std::remove_cvref_t<decltype(tuple)>>;

    auto& reader = view.reader();
    auto& state = view.reader_state();

    while (true) {
      if (!reader.next_token(state)) [[unlikely]] {
        return unexpected(reader.error(state));
      }

      const auto& tok = reader.current_token(state);
      if (tok.type == TokenType::ObjectEnd) {
        return value;
      }

      auto ret = try_decode_entry_candidates_impl(
          tuple, reader, state, std::make_index_sequence<tuple_size>{});
      if (!ret) [[unlikely]] {
        return unexpected(ret.error());
      }
      if (!*ret) [[unlikely]] {
        return unexpected(Error{
            .code = ErrorCode::unknown_member,
            .offset = tok.offset,
        });
      }
    }
  }
};

}  // namespace detail

/// Parses a numeric string_view into arithmetic type T using std::from_chars.
template <detail::Number T>
inline constexpr auto number = step<detail::NumberImpl<T>>;

namespace detail {

struct BooleanImpl {
  auto operator()(std::string_view sv) const -> Result<bool, Error> {
    if (sv == "true") return true;
    if (sv == "false") return false;
    return unexpected(Error{.code = ErrorCode::type_mismatch});
  }
};

struct StringViewImpl {
  auto operator()(std::string_view sv) const -> std::string_view { return sv; }
};

}  // namespace detail

/// Converts "true"/"false" to bool; returns type_mismatch for anything else.
inline constexpr auto boolean = step<detail::BooleanImpl>;

/// Passes string_view through unchanged; escape resolution is left to the
/// caller.
inline constexpr auto string_view = step<detail::StringViewImpl>;

/// Passes any value through unchanged.
inline constexpr auto identity = step<detail::IdentityImpl>;

/// Applies user-defined conversion F to the input; F may return a plain value or
/// Result<U, Error>.
template <auto F>
inline constexpr auto custom = step<detail::CustomImpl<F>>;

template <auto... Pipelines>
inline constexpr auto each = step<detail::EachImpl<Pipelines...>>;

template <auto... Pipelines>
inline constexpr auto collect = step<detail::CollectImpl<Pipelines...>>;

template <class T>
inline constexpr auto to = step<detail::ToImpl<T>>;

}  // namespace hosq::decode
