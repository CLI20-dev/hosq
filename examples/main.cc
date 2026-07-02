#include <hosq/decoder.hh>
#include <hosq/pipeline/scan.hh>
#include <hosq/pipeline/store.hh>
#include <hosq/pipeline/unknown.hh>

#include "hosq/pipeline/decode.hh"

// struct Json {
//   hosq::Entry<hosq::Elem<hosq::decode::noop, hosq::unknown::ignore>,
//                hosq::Elem<hosq::decode::noop | hosq::store::ignore,
//                            hosq::unknown::ignore> >
//       a;
// };

template <class T>
class ReaderBase {};

auto main() -> int { return 0; }
