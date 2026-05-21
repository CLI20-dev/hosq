#pragma once

#include <type_traits>

#include "hosq/error.hh"
#include "hosq/pipeline_util.hh"
#include "hosq/reader_base.hh"
#include "hosq/result.hh"

#if defined(__GNUC__) || defined(__clang__)
#define HOSQ_ALWAYS_INLINE_ATTR __attribute__((always_inline))
#define HOSQ_FUNCTION_ALWAYS_INLINE inline __attribute__((always_inline))
#elif defined(_MSC_VER)
#define HOSQ_ALWAYS_INLINE_ATTR
#define HOSQ_FUNCTION_ALWAYS_INLINE __forceinline
#else
#define HOSQ_ALWAYS_INLINE_ATTR
#define HOSQ_FUNCTION_ALWAYS_INLINE inline
#endif

namespace hosq::ext {



template <ReaderConcept R>
struct ScanLiteral {

  auto operator()(DecodeContext<void, R>& ctx) const -> Result<std::string_view, Error> {

  };

  auto operator()(DecodeContext<void, R>& ctx) const -> Result<std::string_view, Error> {

  };


};






};

namespace hosq::scan {







namespace detail {

template <ReaderConcept T>
struct ScanLiteralDispatch {


};



}


// namespace detail {
//
// struct ScanLiteralImpl {
//   template <class Context>
//   auto operator()(const Context& ctx) const -> Result<std::string_view, Error> {
//     const auto& reader = ctx.reader();
//     return scan_literal_for(reader, reader.current_token());
//   }
// };
//
// struct ScanStringImpl {
//   template <class Context>
//   auto operator()(Context& ctx) const -> Result<std::string_view, Error> {
//     const auto& reader = ctx.reader();
//     return scan_string_for(reader, reader.current_token());
//   }
// };
//
// struct ScanScalarImpl {
//   template <class Context>
//   auto operator()(const Context& ctx) const -> Result<std::string_view, Error> {
//     const auto& reader = ctx.reader();
//     return scan_scalar_for(reader, reader.current_token());
//   }
// };
//
// struct ScanObjectImpl {
//   template <class Context>
//     requires(!std::is_const_v<std::remove_reference_t<Context>>)
//   auto operator()(Context& ctx) const -> Result<std::string_view, Error> {
//     auto& reader = ctx.reader();
//     return scan_object_for(reader, reader.current_token());
//   }
// };
//
// struct ScanArrayImpl {
//   template <class Context>
//     requires(!std::is_const_v<std::remove_reference_t<Context>>)
//   auto operator()(Context& ctx) const -> Result<std::string_view, Error> {
//     auto& reader = ctx.reader();
//     return scan_array_for(reader, reader.current_token());
//   }
// };
//
// }  // namespace detail

// inline constexpr auto literal = hosq::step<detail::ScanLiteralImpl>;
// inline constexpr auto string = hosq::step<detail::ScanStringImpl>;
// inline constexpr auto scalar = hosq::step<detail::ScanScalarImpl>;
// inline constexpr auto object = hosq::step<detail::ScanObjectImpl>;
// inline constexpr auto array = hosq::step<detail::ScanArrayImpl>;

}  // namespace hosq::scan

#undef HOSQ_ALWAYS_INLINE_ATTR
#undef HOSQ_FUNCTION_ALWAYS_INLINE
