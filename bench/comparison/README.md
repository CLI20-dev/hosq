# JSON comparison benchmarks

Each benchmark case is a separate source file and executable. The source file
contains the HOSQ schema, Glaze schema, yyjson implementation, output
verification, and Google Benchmark registration for that case.

| Source | Executable | Operation |
|---|---|---|
| `id_only.cc` | `hosq_bench_id_only` | Decode only the top-level `id` field |
| `partial_fields.cc` | `hosq_bench_partial_fields` | Decode five fields while ignoring two payload members |
| `all_fields.cc` | `hosq_bench_all_fields` | Decode every declared field |
| `all_fields_randomize.cc` | `hosq_bench_all_fields_randomize` | Decode every field with independently shuffled object keys |
| `id_only_pretty.cc` | `hosq_bench_id_only_pretty` | ID-only projection from four-space-indented JSON |
| `partial_fields_pretty.cc` | `hosq_bench_partial_fields_pretty` | Partial projection from four-space-indented JSON |
| `all_fields_pretty.cc` | `hosq_bench_all_fields_pretty` | Complete projection from four-space-indented JSON |
| `all_fields_randomize_pretty.cc` | `hosq_bench_all_fields_randomize_pretty` | Complete projection with shuffled keys and four-space indentation |

All cases generate the same deterministic, pseudorandom 2,048-record JSON
array from the same fixed seed before timing. HOSQ and Glaze decode typed
records. yyjson builds its DOM and reads the same requested values directly.
Every executable verifies that all three implementations produce the same
checksum before Google Benchmark starts.

## Build

Use yyjson 0.12.0 and Glaze 8.3.0:

```sh
cmake -S bench/comparison -B build/comparison -DCMAKE_BUILD_TYPE=Release \
  -DYYJSON_SOURCE=/path/to/yyjson/src/yyjson.c \
  -DGLAZE_SOURCE_DIR=/path/to/glaze-8.3.0
cmake --build build/comparison -j2
```

## Run

These are ordinary Google Benchmark executables:

```sh
build/comparison/hosq_bench_id_only \
  --benchmark_repetitions=5 --benchmark_min_time=0.1s
build/comparison/hosq_bench_partial_fields \
  --benchmark_repetitions=5 --benchmark_min_time=0.1s
build/comparison/hosq_bench_all_fields \
  --benchmark_repetitions=5 --benchmark_min_time=0.1s
build/comparison/hosq_bench_all_fields_randomize \
  --benchmark_repetitions=5 --benchmark_min_time=0.1s
```

Use `--benchmark_filter=HOSQ`, `Glaze`, or `yyjson` when process isolation is
required. Input generation and cross-library output verification happen before
timing. Timed work includes parsing, requested-value checksum calculation, and
destruction. HOSQ and Glaze also construct typed output; yyjson's DOM
construction and destruction are also timed.
Glaze receives a mutable string, so its normal temporary input padding and
restoration are included in the timed call.

The randomized-key cases use a second fixed-seed `std::mt19937_64` engine only
for member order and permute keys with `std::shuffle`. Their decoded values are
identical to the canonical-order cases.
Pretty inputs use a four-space indent at every nesting level and otherwise keep
the same values and ordering contract as their compact counterparts.

The id-only and partial-fields cases intentionally reflect each library's
normal unknown-member handling: HOSQ performs structural skip, Glaze validates
skipped input, and yyjson materializes the complete DOM. It is an end-to-end
application comparison, not a parser-only or conformance claim.
