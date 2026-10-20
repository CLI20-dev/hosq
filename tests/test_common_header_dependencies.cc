// Common value and pipeline headers must not import a concrete format parser.
#include <cstddef>

#include "hosq/detail/native_encode.hh"
#include "hosq/pipeline/string.hh"
#include "hosq/pipeline/typed_decode.hh"
#include "hosq/value_types.hh"

namespace hosq::toml {
template <std::size_t>
class Reader;
}
namespace hosq::detail {
struct NativeState;
template <class T>
concept CompleteTypeForHeaderTest = requires { sizeof(T); };
static_assert(!CompleteTypeForHeaderTest<toml::Reader<64>>);
static_assert(!CompleteTypeForHeaderTest<NativeState>);
}  // namespace hosq::detail
