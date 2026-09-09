# JSON comparison benchmarks

This suite asks where HOSQ's processing model helps or costs time. It is not a
parser-only ranking, a conformance suite, or a Cartesian product of every knob.

## Typed comparison (primary)

Use `hosq_static_comparison` to compare typed HOSQ and Glaze pipelines with yyjson.
The sixteen cases cover small/medium/large input, id-only/five-field/all-field
selection, canonical and deterministically shuffled medium object keys, and
five-field selection with either unknown payload members (`many_skip`) or a
known-only wire (`many_known`). Wide objects, mixed numbers, and raw UTF-8 versus
escaped strings complete the matrix. Five-field selection reads id and
payload's name, count, ratio, and ok.

Glaze uses typed metadata for both selected and complete output, with
`error_on_unknown_keys=false` and `partial_read=false` for five-field selection.
HOSQ uses unordered constant-key matching plus skip for selection. All-field
HOSQ schemas also use unordered constant-key matching, but have no skip
fallback, so unknown members are rejected like Glaze's strict path. The
`medium/many_skip_shuffled`, `medium/many_known_shuffled`, and
`medium/all_shuffled` cases exercise this ordering contract explicitly; no
ordered path is included in these three-way results. Comparing `many_skip` with
`many_known` isolates the cost of skipping unknown `none` and `values` members.

Each implementation creates a fresh output vector, computes the same digest,
and destroys the output within timing. No implementation pre-reserves its
output vector. yyjson also builds and frees its DOM;
multiple selected fields use one traversal, not repeated linear lookups. HOSQ
and Glaze borrow string views and use common unescape/hash processing; yyjson
provides decoded strings. Input preparation is outside timing, while Glaze's
read-time padding is inside. HOSQ uses bounded input and bundled fast_float.
All implementations must match a Python-generated independent digest and consume
the complete input. These valid-input workloads do not equalize UTF-8 or skipped
value validation guarantees.

Reproduce with pinned yyjson 0.12.0 and Glaze 8.3.0 sources, Google Benchmark
(the recorded run used 1.9.5), and a matching C/C++ toolchain:

```sh
cmake -S bench/comparison -B build/comparison -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
  -DYYJSON_SOURCE=/path/to/yyjson/src/yyjson.c \
  -DGLAZE_SOURCE_DIR=/path/to/glaze-v8.3.0
cmake --build build/comparison --target hosq_static_comparison static_comparison_fixtures
python3 bench/comparison/run_static.py build/comparison/hosq_static_comparison \
  build/comparison/fixtures-static bench/comparison/results/run1 --seed 2026090901
python3 bench/comparison/run_static.py build/comparison/hosq_static_comparison \
  build/comparison/fixtures-static bench/comparison/results/run2 --seed 2026090902
python3 bench/comparison/summarize_static.py bench/comparison/results/run1 \
  bench/comparison/results/run2 bench/comparison/results/summary
python3 bench/comparison/plot_static.py bench/comparison/results/summary/chart-data.json \
  bench/comparison/results/summary/comparison
```

Plotting requires Matplotlib. The runner uses separate sequential processes and
five repetitions of at least 0.1 seconds, shuffling execution order by seed.
Keep power/load conditions stable and do not compile or profile during timing.
Chart ranges show variation between runs, not confidence intervals. An optional
`--before-executable PATH` runs matched controls built from another header snapshot.
Keep generated results untracked. The [checkpoint](../../docs/reader-flow-checkpoint.md)
retains the latest focused measurement; superseded experiment reports are omitted.
Any pre-existing result directory from an older case matrix must be regenerated;
the plotting script does not treat historical 12-case results as valid input for
the current sixteen-case matrix.

The first measurement produced with this matrix is recorded in the
[2026-09-09 typed comparison report](../../docs/benchmarks/typed-json-comparison-2026-09-09.md).

## Generic comparison (diagnostic)

The remaining sections describe `hosq_comparison`, whose full-value cases use
generic traversal. They do not establish typed-deserialization performance.

Twelve workloads and three implementations give 36 diagnostic measurements.
Typed-schema comparisons belong to the separate sixteen-case static suite
above; they are not mixed into these generic results. Every workload has a
distinct diagnostic purpose.

| Workload | Operation | Purpose |
|---|---|---|
| small (109 B) | id only / all values | Fixed per-document cost |
| medium (230 KB) | id only / all values | Working baseline and selection cost |
| large (30 MB) | id only / all values | Size scaling, DOM allocation and traversal |
| wide_first / wide_last (299 KB) | id only | Same 128 unselected prefix-sharing keys, selected key first versus last |
| deep (1 MB, depth about 50) | all values | Container transitions and generic traversal depth |
| numbers (652 KB) | all values | Signed/unsigned integers, uint64 maximum, decimals and exponents |
| strings_utf8 / strings_escaped | all values | Identical decoded strings, raw UTF-8 versus Unicode escapes, including surrogate pairs |

The generic full-value cases use a streaming HOSQ recursive consumer versus
Glaze's owning `generic_u64` DOM and yyjson's DOM. They are diagnostic
streaming-versus-DOM comparisons, not typed-deserialization rankings. Use the
static suite for equivalent typed schemas and known scalar types.

## Result and comparison contract

- Input is loaded/generated outside the timed region. All implementations
  receive the same complete JSON bytes. Glaze receives the recommended mutable
  std::string and temporarily pads/restores it inside the timed call. Logical
  input contents are verified unchanged. HOSQ and yyjson read the same buffer. Each case repeatedly
  processes its own buffer: these are resident-input tests, not disk or cold
  streaming measurements. Large does not guarantee a cache miss on every read.
- HOSQ's generic path uses bounded `FastReader<>`, no padding option. Sparse
  selection uses constant-key schema matching and the standard skip fallback;
  generic full-value traversal keeps no output vector and computes the fixed-
  size digest while consuming the input. The static suite intentionally
  materializes fresh HOSQ vectors so its typed allocation path is comparable
  to the other typed adapters.
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
  The typed comparison records `std::string_view` fields, matching HOSQ's
  borrowed string views; yyjson's views refer to its timed DOM. Input capacity
  can be retained across repetitions, as in resident-buffer use. **The dynamic
  table is not Glaze's typed-deserialization performance.** The medium static
  controls are not part of this generic measurement set.
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
- Valid inputs only: HOSQ's permissive skipped-value handling for the selected
  five-field mode is not equivalent to Glaze's skipped-value validation or
  yyjson's DOM validation and materialization. A faster skip result is an
  end-to-end benefit, not evidence of equal validation work. Invalid-input/error
  performance is deliberately outside this suite.
- Both C and C++ use the same selected compiler and Release optimization.
  yyjson is built from source, not an opaque prebuilt library. The bundled
  fast_float is enabled by default; an explicit opt-out must be reported. No
  allocation-counter overrides are linked.

## Build and run

Use matching yyjson.c and yyjson.h from a pinned release in one directory.
Google Benchmark must provide a CMake package config. HOSQ includes its pinned
fast_float headers, so no separate FastFloat package is required.

```sh
cmake -S bench/comparison -B build/comparison -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
  -DYYJSON_SOURCE=/path/to/yyjson/src/yyjson.c \
  -DGLAZE_SOURCE_DIR=/path/to/glaze-v8.3.0
cmake --build build/comparison --target hosq_comparison comparison_fixtures -j2
python3 bench/comparison/run.py build/comparison/hosq_comparison \
  build/comparison/fixtures build/comparison/results
```

Pass `-DCXX_HOSQ_USE_FAST_FLOAT=OFF` only when measuring the standard-library
floating-point fallback, and report that choice with the results.

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
validation work. Generic Glaze materializes strings and validates UTF-8, while
the typed static adapters borrow string views; HOSQ's raw-string/skip behavior
has different guarantees. Do not infer parser-only speed or a universal
library ranking from these end-to-end results.
