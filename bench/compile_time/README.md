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

Two independent reductions address this:

- Build each decoder handler in a helper specialized only on its entry, keeping
  other fields out of its lambda type. The existing forced-inlining policy is
  retained.
- Adapt the key-only planner from
  [gen740/meta_match at fbc01a0](https://github.com/gen740/meta_match/commit/fbc01a0c7639cd34e4664225c3c0c21aaf3d71c8).
  Dispatch keeps the original handler tuple and compile-time indices rather than
  constructing candidate subtuples. Planning is reused across callable types.
  hosq's terminated-key API still commits the reader position before invoking
  the handler; custom entries retain the compatibility route.

Exploratory measurements on arm64 macOS with GCC 16.2.0, C++20, `-O0 -g`:

| Actual `document.cc` | Wall time | Object size |
|---|---:|---:|
| Baseline | 138.00 s | about 1.9 GiB |
| Per-entry decoder handler only | 44.76 s | 329 MiB |
| Handler change and updated key dispatcher | 37.25 s | 228 MiB |

These are single exploratory samples on a shared development host, including
assembler work. They are not Linux VM measurements or a claim that every schema
gets this speedup. The initially observed `-O3` VM stall has not been reproduced
on that VM. `-g` must be checked in the effective compiler invocation, including
build-environment flags, before attributing a Release build to this exact cause.

## Final frozen-source comparison

The final source hashes, header hashes, exact object byte counts and exit codes
are retained in [results.json](results.json). Runs were sequential on the same
shared host, using the same frozen `document.cc` and `schema.hh`, GCC 16.2.0,
C++20 and `-ftime-report`. Each row is one sample, not a statistical estimate.

| Flags | Baseline wall | Final wall | Baseline object | Final object |
|---|---:|---:|---:|---:|
| `-O0 -g` | 118.65 s | 37.06 s | 1956.04 MiB | 228.03 MiB |
| `-O3 (no -g)` | 54.82 s | 52.50 s | 1.19 MiB | 1.18 MiB |
| `-O3 -g` | not measured | 79.99 s | not measured | 222.87 MiB |

In the final `-O0 -g` pair, GCC's `symout` time fell from 75.40 to
8.45 seconds; template instantiation was 3.86 versus 3.57 seconds. The large
benefit is specifically debug-info generation. The no-debug optimized pair shows
only a small change (54.82 to 52.50 seconds), which should not be overinterpreted.

## Runtime tradeoff and verification

The existing `bench/performance` workloads were verified to produce identical
checksums, then measured with Clang 21.1.8 and `-O3` in the project's Nix
performance shell. Each of eight workloads used separate ABBA processes, five
repetitions per process, a 0.15-second minimum and no warmup.

This is a compile-cost improvement, not a universal runtime speedup. Most cases
were within 0.5%, but the randomized compact-object case was **5.60% slower**;
partial pretty objects were 2.81% slower and randomized pretty objects 1.94%
slower. A shorter exploratory run also showed a roughly 4.7% regression for
randomized compact objects. Treat these tradeoffs as part of the change; shared
host timing and some high sample variation limit precision.

Validation of the combined implementation:

- Clang 21.1.8 C++20 and C++26: 653 tests passed per standard, one existing
  platform-dependent wider-floating-point test skipped per standard.
- C++20 matcher-boundary compile-rejection/positive-control suite passed.
- Upstream `meta_match` contract suite through a namespace/include adapter:
  1,254,723 active checks at each of `-O0` and `-O3` with GCC 16.2.0 and Clang
  21.1.8, plus duplicate-key negative fixtures with positive controls.
- Apple clang 21.0.0: the same upstream checks at `-O0`, `-O3`, and ASan+UBSan,
  plus 18 focused hosq dispatch/decoder tests under ASan+UBSan, all passed.
  The Nix Clang ASan runtime deadlocked before `main` even for an empty probe,
  so its sanitizer run is not counted as a pass.
- The standalone workload compiled with GCC 16.2.0 at `-O0 -g` (22.71 seconds,
  177,135,448 object bytes in one sample); its syntax-only control also passed.
- Changed C++ files passed the project's clang-format check.
