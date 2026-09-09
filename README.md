# HOSQ

HOSQ is a C++20 header-only library for decoding known parts of structured
input without first constructing a general-purpose DOM.

The public implementation is under `include/hosq`. A schema is a C++ type whose
entries compose scanning, decoding, matching, validation, and storage steps.
`Try<Entry<...>>` members provide key-order-independent object dispatch.

```cpp
#include <hosq/hosq.hh>
#include <hosq/json/fast_reader.hh>

using namespace hosq;

template <class T>
using Number = Elem<scan::scalar | decode::number<T> | store::assign,
                    noop_encoder>;

struct Record {
  Try<Entry<Key<"id">, Number<int>>> id;
};

Record record;
auto result = decode_into<json::FastReader<>>(record, R"({"id":42})");
```

## Tests

```sh
cmake -S . -B build -DCXX_HOSQ_ENABLE_TEST=ON
cmake --build build --target hosq_tests_cxx20
ctest --test-dir build
```

## Benchmarks

The maintained HOSQ/Glaze/yyjson comparisons are in
[`bench/comparison`](bench/comparison/README.md). Each case has its own source
file and Google Benchmark executable; the HOSQ schema is visible in that case
file.
