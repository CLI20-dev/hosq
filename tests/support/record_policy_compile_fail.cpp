// Compile with -DHOSQ_RECORD_POLICY_FAILURE=1..7 and -fsyntax-only.
// Each case must fail with the corresponding Record / Field static assertion.
#include <tuple>

#include "hosq/types.hh"

namespace t = hosq::types;
namespace v = t::value;
#if HOSQ_RECORD_POLICY_FAILURE == 1
using Schema = std::tuple<t::Field<"x", v::Text, t::InputAliases<"x">>>;
#elif HOSQ_RECORD_POLICY_FAILURE == 2
using Schema = std::tuple<t::Field<"x", v::Text, t::InputAliases<"old", "old">>>;
#elif HOSQ_RECORD_POLICY_FAILURE == 3
using Schema = std::tuple<t::Field<"x", v::Text, t::InputAliases<"y">>,
                          t::Field<"y", v::Text>>;
#elif HOSQ_RECORD_POLICY_FAILURE == 4
using Schema = std::tuple<t::Field<"x", v::Text, t::InputAliases<"old">>,
                          t::Field<"y", v::Text, t::InputAliases<"old">>>;
#elif HOSQ_RECORD_POLICY_FAILURE == 5
using Schema = std::tuple<t::Field<"x", v::Text, t::Default<[] { return "a"; }>,
                                   t::Default<[] { return "b"; }>>>;
#elif HOSQ_RECORD_POLICY_FAILURE == 6
using Schema = std::tuple<
    t::Field<"x", v::Text, t::InputAliases<"a">, t::InputAliases<"b">>>;
#elif HOSQ_RECORD_POLICY_FAILURE == 7
using Schema = std::tuple<t::Field<"x", v::Text, int>>;
#else
#error Select HOSQ_RECORD_POLICY_FAILURE from 1 through 7.
#endif
v::Record<Schema> invalid;
