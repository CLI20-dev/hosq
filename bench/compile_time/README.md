# Schema compile-time regression

`decode_schema.cc` is a standalone C++20 workload with five decoded roots,
optional fields, nested objects, arrays/mappings and captured `json_dom` values.
It needs only hosq and the standard library. The wide 19-field definition and
9-field note deliberately retain the shape that exposed GCC debug-info growth.
It is a compile workload, not a runtime throughput benchmark.

Run the **same source** against baseline and changed headers, one compiler job
at a time. Keep compiler, standard library, flags and machine identical:

```sh
python3 bench/compile_time/measure.py --compiler g++ \
  --include /path/to/baseline/include --output /tmp/hosq-compile-base \
  -- -O0 -g -ftime-report
python3 bench/compile_time/measure.py --compiler g++ \
  --include /path/to/changed/include --output /tmp/hosq-compile-head \
  -- -O0 -g -ftime-report
```

The runner saves compiler output, elapsed time, object bytes, the exact command,
compiler/platform identity, and hashes of the source and changed headers. Use
separate output directories for repetitions. Also compare `-O0 -fsyntax-only`,
`-O0` without `-g`, and `-O3` with/without `-g`; keep Clang's `-ftime-report`
separate from GCC's when interpreting phase names. Timing is machine-dependent;
object/debug-section size is useful supporting evidence, not a portable limit.

## Cause and changes

The original decoder created every constant-key handler lambda inside a
function specialized on the **entire field tuple**. That enclosing specialization
became part of each lambda type. The handler tuple consequently repeated the
whole schema in every element; recursive candidate subtuples and standard tuple
machinery amplified those names further. GCC spent most of the affected debug
build emitting those types and names, rather than instantiating templates.

On the reported Tsugumi `document.cc` with hosq at
`38c3c8e21f570a9698006c361fc58bf0365896e0`, GCC 16.2.0's `-O0 -g -ftime-report`
recorded 96.47 seconds in `symout`, versus 3.98 seconds in template instantiation.
The object was about 1.9 GiB; a handler-related debug string was 735,091 bytes.
A schema-only translation unit also reproduced the growth once `-g` was added.
`-O0` does not isolate the frontend or exclude debug generation.

Two reductions address this:

- Build each decoder handler in a helper specialized only on its entry, keeping
  other fields out of its lambda type. The existing forced-inlining policy is
  retained.
- Keep the original handler tuple and traverse compile-time candidate indices.
  Metadata depends only on keys, so different callable types reuse it. This
  follows the key-only indexing approach from
  [gen740/meta_match at fbc01a0](https://github.com/gen740/meta_match/commit/fbc01a0c7639cd34e4664225c3c0c21aaf3d71c8).

The matcher is internal to hosq and accepts only decoder-built `Handler` tuples.
Complete-string matching and JSON terminated-key matching share one sequential
trie. A single remaining candidate is checked against the complete key, and the
terminated form commits the reader before invoking the handler. There are no
custom-entry adapters, legacy depth/ref-tuple entry points, or alternate
length-based planning strategies.

## Frozen-source comparison

Source/header hashes, exact object sizes and exit codes are retained in
[results.json](results.json). Baseline and final runs use the same frozen
`document.cc` and `schema.hh`, GCC 16.2.0, C++20 and `-ftime-report` on the same
shared host. Baseline values come from the initial investigation; final values
were remeasured after the internal matcher cleanup, with one compiler invocation
at a time. Each row is one sample, not a statistical estimate.

| Flags | Baseline wall | Final wall | Baseline object | Final object |
|---|---:|---:|---:|---:|
| `-O0 -g` | 118.65 s | 38.31 s | 1956.04 MiB | 226.06 MiB |
| `-O3` (no `-g`) | 54.82 s | 52.59 s | 1.19 MiB | 1.20 MiB |

For `-O0 -g`, GCC's `symout` time fell from 75.40 to 8.70 seconds;
template instantiation was 3.86 versus 3.64 seconds. The large benefit is
specifically debug-info generation. The no-debug optimized difference is small
and should not be overinterpreted. The original Linux VM stall has not been
retested; these measurements do not establish that its cause is resolved.

## Runtime and validation

The existing eight `bench/performance` workloads produced identical checksums
with the baseline, the pre-cleanup PR head (`e66f90a`) and the final headers.
Clang 21.1.8 `-O3` measurements ran in the order baseline / pre-cleanup / final /
final / pre-cleanup / baseline per workload, with five repetitions per process,
a 0.15-second minimum, and no warmup. The table compares median CPU time;
positive means slower. Shared-host timing limits the precision of small changes.

| Workload | Final vs baseline | Final vs pre-cleanup |
|---|---:|---:|
| `id_only` | -0.14% | +0.13% |
| `partial_fields` | +0.24% | +0.46% |
| `all_fields` | -0.64% | -0.90% |
| `all_fields_randomize` | +5.35% | -0.38% |
| `id_only_pretty` | -0.76% | -0.60% |
| `partial_fields_pretty` | +0.47% | -1.59% |
| `all_fields_pretty` | +0.80% | +1.14% |
| `all_fields_randomize_pretty` | +2.83% | +0.07% |

Validation of the final implementation:

- Clang 21.1.8 C++20 and C++26: 651 runtime tests passed per standard, with
  one existing platform-dependent wider-floating-point test skipped per standard.
- Matcher-boundary compile-rejection tests and positive controls passed in both
  C++20 and C++26 (1,306 CTest entries total, including the two skips).
- GCC 16.2.0 rejected duplicate ASCII, empty, embedded-NUL and high-byte keys;
  the corresponding distinct-key positive controls compiled successfully.
- Apple clang 21.0.0 ASan+UBSan: all 16 focused matcher/reader/decoder tests passed.
- `nix fmt -- --ci` passed.
