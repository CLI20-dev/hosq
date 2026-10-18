#pragma once

#include <concepts>
#include <type_traits>

#include "hosq/element.hh"
#include "hosq/pipeline/validate.hh"
#include "hosq/pipeline_util.hh"
#include "hosq/types_field.hh"

namespace hosq::types::detail {

// Only value-preserving rules are accepted. In particular, validate::custom
// can transform values or depend on a reader and is not a constraint alias.
template <class Rule, class T>
inline constexpr bool constraint_rule = false;

template <auto Min, auto Max, class T>
inline constexpr bool constraint_rule<validate::detail::RangeImpl<Min, Max>, T> =
    requires(const T& value) {
      { value < Min } -> std::convertible_to<bool>;
      { value > Max } -> std::convertible_to<bool>;
    };
template <auto Min, class T>
inline constexpr bool constraint_rule<validate::detail::MinImpl<Min>, T> =
    requires(const T& value) {
      { value < Min } -> std::convertible_to<bool>;
    };
template <auto Max, class T>
inline constexpr bool constraint_rule<validate::detail::MaxImpl<Max>, T> =
    requires(const T& value) {
      { value > Max } -> std::convertible_to<bool>;
    };
template <class T>
inline constexpr bool constraint_rule<validate::detail::NonEmptyImpl, T> =
    requires(const T& value) {
      { value.empty() } -> std::convertible_to<bool>;
    };
template <std::size_t N, class T>
inline constexpr bool constraint_rule<validate::detail::SizeImpl<N>, T> =
    requires(const T& value) {
      { value.size() != N } -> std::convertible_to<bool>;
    };
template <std::size_t Min, std::size_t Max, class T>
inline constexpr bool
    constraint_rule<validate::detail::SizeRangeImpl<Min, Max>, T> =
        requires(const T& value) {
          { value.size() < Min } -> std::convertible_to<bool>;
          { value.size() > Max } -> std::convertible_to<bool>;
        };
template <auto F, class T>
inline constexpr bool constraint_rule<validate::detail::PredicateImpl<F>, T> =
    requires(const T& value) {
      { F(value) } -> std::convertible_to<bool>;
    };

template <class Rules, class T>
struct ConstraintPipeline : std::false_type {};
template <auto... Rules, class T>
struct ConstraintPipeline<Pipeline<Rules...>, T>
    : std::bool_constant<(
          constraint_rule<std::remove_cvref_t<decltype(Rules)>, T> && ...)> {
  template <class Context>
  static auto check(const T& value, Context& ctx) -> void {
    auto next = [&]<auto Rule>() {
      if constexpr (requires { ctx.reader(); }) {
        if (!ctx.reader().error(ctx.reader_state()).is_ok()) return;
      } else {
        if (!ctx.writer().error().is_ok()) return;
      }
      (void)Rule(value, ctx);
    };
    (next.template operator()<Rules>(), ...);
  }
};

template <class E, auto Rules>
concept ConstraintFor =
    hosq::detail::ElemT<E> &&
    (!std::same_as<typename E::storage_type, hosq::detail::NoStorage>) &&
    ConstraintPipeline<std::remove_cvref_t<decltype(Rules)>,
                       typename E::storage_type>::value;

template <class E, auto Rules>
struct ConstrainedDecode {
  template <class>
  using storage_type = typename E::storage_type;

  // Query this Context's terminal without instantiating invoke's body against
  // the storage-analysis context's placeholder storage.
  template <class Context>
    requires(!std::is_const_v<Context> ||
             E::template decoder_const_invokable<std::remove_const_t<Context>>)
  auto operator()(Context& ctx) const ->
      typename E::decoder_type::template final_output_type<void, Context> {
    using Terminal =
        typename E::decoder_type::template final_output_type<void, Context>;
    if constexpr (std::same_as<Terminal, bool>) {
      if (!E::decoder_type::invoke(ctx)) return false;
    } else {
      E::decoder_type::invoke(ctx);
    }
    if (ctx.reader().error(ctx.reader_state()).is_ok())
      ConstraintPipeline<std::remove_cvref_t<decltype(Rules)>,
                         typename E::storage_type>::check(ctx.value(), ctx);
    return hosq::detail::terminal_return<Terminal>(true);
  }
};

template <class E, auto Rules>
struct ConstrainedEncode {
  template <class T, class Context>
    requires(!std::is_const_v<Context> ||
             E::template encoder_const_invokable<std::remove_const_t<Context>>)
  auto operator()(const T& value, Context& ctx) const -> void {
    if (!ctx.writer().error().is_ok()) return;
    if constexpr (std::same_as<typename E::encoder_type,
                               std::remove_cvref_t<decltype(noop_encoder)>>) {
      (void)hosq::detail::encode_error(
          ctx.writer(), ErrorCode::unsupported_type,
          "noop_encoder is reader-only; provide an encoder pipeline");
      return;
    }
    ConstraintPipeline<std::remove_cvref_t<decltype(Rules)>,
                       typename E::storage_type>::check(value, ctx);
    if (ctx.writer().error().is_ok()) E::encoder_type::invoke(value, ctx);
  }
};

}  // namespace hosq::types::detail

namespace hosq::types::value {

/// Reuses E's storage and codecs; checks Rules on const storage after successful
/// decoding and before encoding. Mutable/default storage is not an invariant:
/// encoding rejects invalid values. Rules compose range/min/max, non_empty,
/// size/size_range and predicate; predicates must accept const storage.
template <hosq::detail::ElemT E, auto Rules>
  requires detail::ConstraintFor<E, Rules>
using Constrained = Elem<step<detail::ConstrainedDecode<E, Rules>>,
                         step<detail::ConstrainedEncode<E, Rules>>>;

}  // namespace hosq::types::value

namespace hosq::types {

template <StringLiteral K, hosq::detail::ElemT E, auto Rules>
  requires detail::ConstraintFor<E, Rules>
using Constrained = Field<K, value::Constrained<E, Rules>>;

}  // namespace hosq::types
