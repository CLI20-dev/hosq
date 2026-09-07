# Minimal end-to-end JSON comparison

This suite asks where HOSQ's processing model helps or costs time. It is not a
parser-only ranking, a conformance suite, or a Cartesian product of every knob.
See [the three-library results and chart](../../docs/benchmark-glaze.md).
The [earlier two-library results](../../docs/benchmark-comparison.md) used a different run environment.

Twelve workloads, three implementations, plus paired static-schema controls give
38 measurements. Every workload has a distinct diagnostic purpose.

| Workload | Operation | Purpose |
|---|---|---|
| small (109 B) | id only / all values | Fixed per-document cost |
| medium (230 KB) | id only / all values | Working baseline and selection cost |
| large (30 MB) | id only / all values | Size scaling, DOM allocation and traversal |
| wide_first / wide_last (299 KB) | id only | Same 128 unselected prefix-sharing keys, selected key first versus last |
| deep (1 MB, depth about 50) | all values | Container transitions and generic traversal depth |
| numbers (652 KB) | all values | Signed/unsigned integers, uint64 maximum, decimals and exponents |
| strings_utf8 / strings_escaped | all values | Identical decoded strings, raw UTF-8 versus Unicode escapes, including surrogate pairs |

`medium/all/HOSQtyped` and `medium/all/Glazetyped` produce the same result as `medium/all/HOSQ`, but use
static schemas and known scalar types. Glaze reads a vector of explicitly
registered C++ records (including an array of four known integer values). These are controls, not separate inputs.
They prevent attributing the cost of generic runtime traversal entirely to the
Reader. The remaining full-value cases use a generic recursive consumer;
they do not establish the speed ceiling of a specialized HOSQ pipeline.

## Result and comparison contract

- Input is loaded/generated outside the timed region. All implementations
  receive the same complete JSON bytes. Glaze receives the recommended mutable
  std::string and temporarily pads/restores it inside the timed call. Logical
  input contents are verified unchanged. HOSQ and yyjson read the same buffer. Each case repeatedly
  processes its own buffer: these are resident-input tests, not disk or cold
  streaming measurements. Large does not guarantee a cache miss on every read.
- HOSQ uses bounded `FastReader<>`, no padding option. Sparse selection uses
  constant-key schema matching and the standard skip fallback. It keeps no
  output vector; full consumption produces the same fixed-size digest.
- yyjson uses default `yyjson_read` flags and allocator. Parsing, DOM allocation,
  output computation and `yyjson_doc_free` are all timed. Sparse mode uses one
  object lookup for one requested field; full mode iterates each value once.
  No repeated multi-field linear-lookup disadvantage is introduced.
- Glaze v8.3.0 uses `glz::read` and default validation/minification settings.
  Sparse mode reads `std::vector<Id>` with explicit member metadata and
  `error_on_unknown_keys=false`. `partial_read` stays false: every array element
  is processed and consumed byte count must equal the original input size.
  Full dynamic mode uses `glz::generic_u64`, not the double-only default generic
  type (the fixture includes UINT64_MAX). It is an owning DOM with materialized
  keys. Construction, parsing, consumption and destruction are all timed.
  Output vectors/DOMs are fresh each iteration, with no pre-reserve or reuse.
  Input capacity can be retained across repetitions, as in resident-buffer use.
  **The dynamic table is not Glaze's typed-deserialization performance.** The
  medium static controls are necessary to interpret it.
- Output is ten uint64 accumulators: leaf count, integer sum, double-bit sum,
  string count, decoded UTF-8 byte count, sum of per-string FNV-1a hashes,
  true count, null count, object count and array count. Sparse output counts
  only selected id values. Keys themselves are not hashed. String processing
  is a shared consumer after yyjson decoding, and includes unescaping on HOSQ.
  Hashing is real application work and can dominate long-string cases.
- Python generates deterministic valid JSON, rereads it and independently
  computes the digest. All implementations must equal that oracle before
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
  -DGLAZE_SOURCE_DIR=/path/to/glaze-v8.3.0 \
  -DCXX_HOSQ_USE_FAST_FLOAT=ON
cmake --build build/comparison --target hosq_comparison comparison_fixtures -j2
python3 bench/comparison/run.py build/comparison/hosq_comparison \
  build/comparison/fixtures build/comparison/results
```

The runner starts a fresh process for each case/implementation pair, shuffling
case order and implementation order within each case (`--seed`). Each child
loads and verifies only its target fixture/implementation, then performs five
repetitions (0.1 s minimum). This avoids cross-library allocator state left by a
large Glaze DOM. Verification alone must not parse the other libraries in that
child. The default executable, without selection environment variables, still
verifies all cases and libraries.

The runner records CPU/wall time, medians, dispersion, input and executable
hashes, commands, benchmark context and available power status. Ratios divide
HOSQ or Glaze's median CPU time by yyjson's median for the same case in the same
run; below 1 is faster. Repeat with another seed before interpreting small gaps.
Processes run sequentially, so this is not simultaneous power-normalization;
changing system load or core scheduling can still affect the ratios. Do not
compare absolute times across power conditions. Do not build or profile during
measurements. A shared-process diagnostic is retained outside the final result
set, because its large-allocation timings differed substantially.

To reproduce the chart (requires Matplotlib and NumPy):

```sh
python3 bench/comparison/plot.py results1/results.json results2/results.json comparison
```

Keep the set small. Add a new case only for a missing mechanism, not another
similar document. Pretty-printing, padding variants, allocator reuse, malformed
inputs and hardware counters belong to targeted follow-up experiments when
these results identify a reason to investigate them.


## Glaze API references

Pinned v8.3.0 documentation and implementation were used to check the adapter:

- [Generic JSON and integer modes](https://github.com/stephenberry/glaze/blob/v8.3.0/docs/generic-json.md)
- [Partial read stops early](https://github.com/stephenberry/glaze/blob/v8.3.0/docs/partial-read.md)
- [Options and validation defaults](https://github.com/stephenberry/glaze/blob/v8.3.0/include/glaze/core/opts.hpp)
- [Mutable input padding and restoration](https://github.com/stephenberry/glaze/blob/v8.3.0/include/glaze/core/read.hpp)

This suite measures equal application outputs, not identical memory layouts or
validation work. Glaze's materialized strings undergo UTF-8 validation; HOSQ's
raw-string/skip behavior has different guarantees. Do not infer parser-only
speed or a universal library ranking from these end-to-end results.
