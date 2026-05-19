#include <ser20/decoder.hh>
#include <ser20/pipeline/scan.hh>
#include <ser20/pipeline/store.hh>
#include <ser20/pipeline/unknown.hh>

#include "ser20/pipeline/decode.hh"

// struct Json {
//   ser20::Entry<ser20::Elem<ser20::decode::noop, ser20::unknown::ignore>,
//                ser20::Elem<ser20::decode::noop | ser20::store::ignore,
//                            ser20::unknown::ignore> >
//       a;
// };

auto main() -> int { decltype(ser20::store::ignore)::analyze_decoder(); }
