# Minimal end-to-end JSON comparison

This suite asks where HOSQ's processing model helps or costs time. It is not a
parser-only ranking, a conformance suite, or a Cartesian product of every knob.
See [the measured results](../../docs/benchmark-comparison.md).

Twelve workloads, two implementations, plus one static-schema control give
25 measurements. Every workload has a distinct diagnostic purpose.

| Workload | Operation | Purpose |
|---|---|---|
| small (109 B) | id only / all values | Fixed per-document cost |
| medium (230 KB) | id only / all values | Working baseline and selection cost |
| large (30 MB) | id only / all values | Size scaling, DOM allocation and traversal |
| wide_first / wide_last (299 KB) | id only | Same 128 unselected prefix-sharing keys, selected key first versus last |
| deep (1 MB, depth about 50) | all values | Container transitions and generic traversal depth |
| numbers (652 KB) | all values | Signed/unsigned integers, uint64 maximum, decimals and exponents |
| strings_utf8 / strings_escaped | all values | Identical decoded strings, raw UTF-8 versus Unicode escapes, including surrogate pairs |

`medium/all/HOSQtyped` produces the same result as `medium/all/HOSQ`, but uses a
static schema and known scalar types. It is a control, not a separate input.
It prevents attributing the cost of generic runtime traversal entirely to the
Reader. The remaining full-value cases use a generic recursive consumer;
they do not establish the speed ceiling of a specialized HOSQ pipeline.

## Result and comparison contract

- Input is immutable and loaded/generated outside the timed region. Both
  implementations receive the same complete JSON bytes. Each case repeatedly
  processes its own buffer: these are resident-input tests, not disk or cold
  streaming measurements. Large does not guarantee a cache miss on every read.
- HOSQ uses bounded `FastReader<>`, no padding option. Sparse selection uses
  constant-key schema matching and the standard skip fallback. It keeps no
  output vector; full consumption produces the same fixed-size digest.
- yyjson uses default `yyjson_read` flags and allocator. Parsing, DOM allocation,
  output computation and `yyjson_doc_free` are all timed. Sparse mode uses one
  object lookup for one requested field; full mode iterates each value once.
  No repeated multi-field linear-lookup disadvantage is introduced.
- Output is ten uint64 accumulators: leaf count, integer sum, double-bit sum,
  string count, decoded UTF-8 byte count, sum of per-string FNV-1a hashes,
  true count, null count, object count and array count. Sparse output counts
  only selected id values. Keys themselves are not hashed. String processing
  is a shared consumer after yyjson decoding, and includes unescaping on HOSQ.
  Hashing is real application work and can dominate long-string cases.
- Python generates deterministic valid JSON, rereads it and independently
  computes the digest. Both implementations must equal that oracle before
  benchmarks run; matching each other alone is insufficient. Raw UTF-8 and
  escaped-string fixtures have equal decoded digests. Input hashes are recorded.
- Valid inputs only: HOSQ's permissive skipped-value handling is not equivalent
  to yyjson's full validation. A faster skip result is an end-to-end benefit,
  not evidence of equal validation work. Invalid-input/error performance is
  deliberately outside this suite.
- Both C and C++ use the same selected compiler and Release optimization.
  yyjson is built from source, not an opaque prebuilt library. fast_float is
  optional and must be reported. No allocation-counter overrides are linked.

## Build and run

Use matching yyjson.c and yyjson.h from a pinned release in one directory.
Google Benchmark and optionally FastFloat must provide CMake package configs.

```sh
cmake -S bench/comparison -B build/comparison -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
  -DYYJSON_SOURCE=/path/to/yyjson/src/yyjson.c \
  -DCXX_HOSQ_USE_FAST_FLOAT=ON
cmake --build build/comparison --target hosq_comparison comparison_fixtures -j2
python3 bench/comparison/run.py build/comparison/hosq_comparison \
  build/comparison/fixtures build/comparison/results
```

The harness alternates implementations and cases using Google Benchmark's
random interleaving (five repetitions, 0.1 s minimum by default). It records CPU
and wall time, medians, dispersion, input sizes, executable hash and available
power status. The reported ratio is median HOSQ CPU time / median yyjson CPU
time; below 1 means HOSQ is faster. Do not combine these times with earlier
runs under different power conditions. Short battery runs are exploratory;
inspect dispersion and repeat comparisons before acting on small differences.
All verification and input loading complete before timing; do not build or
profile concurrently with a measurement.

Keep the set small. Add a new case only for a missing mechanism, not another
similar document. Pretty-printing, padding variants, allocator reuse, malformed
inputs and hardware counters belong to targeted follow-up experiments when
these results identify a reason to investigate them.
