# Bundled fast_float and JSON prefix conversion

HOSQ includes the unmodified fast_float v8.2.10 headers and upstream licenses
under `include/hosq/third_party/fast_float`. The source URL and archive SHA-256
are recorded beside the headers. No external FastFloat package is required,
including when using an installed HOSQ CMake package.

The backend defaults to enabled both for direct header use and CMake. Set
`HOSQ_USE_FAST_FLOAT=0` for direct use or `CXX_HOSQ_USE_FAST_FLOAT=OFF` in CMake
to use the standard conversion backend instead.

## Execution path

`FastReader::parse_number` passes `[first, state.end)` to the numeric backend.
Float and double select `fast_float::chars_format::json` at compile time. On a
successful prefix conversion, `read_number_impl` uses the returned pointer to
resolve the cached token and advance the cursor. The adjacent built-in
`scan::scalar | decode::number<T>` pipeline therefore needs no preliminary
scalar-length scan. The ordered-member fusion experiment uses the same
conversion and final token state.

The existing integer fast path stays inline. Generic integer conversion and
its exceptional/overflow fallback use fast_float as well. Replacing the hot
integer path wholesale with the public fast_float integral overload was
measured and rejected: outlined generic-base parsing penalizes short integers.

The generic `decode::number` pipeline keeps general `from_chars` semantics so
other readers and numeric strings retain their existing behavior. Failed float
conversions use the standard backend to preserve error codes and output-write
semantics, including overflow/underflow and previously accepted numeric forms.
Consequently JSON mode is an optimized conversion path, not a new strict-JSON
validation policy. fast_float's integral overload does not enforce the JSON
floating-point grammar.

`from_chars` intentionally accepts a prefix: even JSON mode can return the `1`
prefix of `1e+`. The reader still checks the returned position for its scalar
terminator or closing quote before committing a successful token. Incomplete
exponents and non-numeric suffixes continue through ordinary error handling.

There is no new allocation, padding requirement, reader-state field, or runtime
backend selector. HOSQ passes `char` input; fast_float's explicit SIMD helpers
are for `char16_t` and are not selected on this path. The dependency headers
remain unmodified, including those unused upstream helpers.

## Verification

The member-fusion branch passes 568 tests across C++20 and C++26, including
numeric extrema, randomized float/integer comparison with `std::from_chars`,
prefix parsing, protected-page input boundaries, cached tokens and custom
pipeline extensions. The standard-backend opt-out passes the 31 focused
numeric/reader/pipeline tests. A separate installed-package consumer reads a
mixed numeric array using only the installed headers and HOSQ package config.

Performance numbers below compare identical consumers and inputs on the same
machine. The previous benchmark already enabled fast_float's general floating
backend; these numbers do not compare fast_float with a wholly standard-only
build. Raw outputs remain ignored under `bench/comparison/results/`.

## Paired benchmark (2026-09-08)

Clang 21.1.8, C++23, O3, Apple ARM64, AC power at 100%. Seven repetitions
per process, two rounds with reversed implementation and case order. No
builds or profiling ran during measurement. All results passed the independent
JSON readback digest oracle. These are the two-round median times in microseconds
for the same no-Try canonical consumer on `experiment/member-value-fusion`.

| Input | Previous | Bundled JSON backend | yyjson | Glaze typed |
|---|---:|---:|---:|---:|
| small/all | 0.108 | 0.107 | 0.174 | 0.106 |
| medium/all | 211.236 | 206.835 | 192.466 | 130.560 |
| large/all | 27150.875 | 26508.500 | 24705.667 | 17259.292 |
| medium_varied/all | 260.936 | 258.458 | 212.619 | 198.797 |

The bundled/JSON connection reduces total time by about 1–2.4% in this paired set.
The JSON-mode-only preliminary experiment was within about 1% of the existing
general floating backend. This is a small improvement, not Glaze parity on
medium or large inputs. The small input is already approximately equal.

An inline five-digit integer candidate was also tested: it gained about 2% on
medium uniform input but regressed about 1–2% on large/varied input versus the
accepted grouped-integer path. It was not adopted. A separate combined-key
experiment is retained only under ignored results and is not included in the
accepted-backend column above.

Glaze materializes its declared structs and fixed four-element arrays; HOSQ
feeds the same digest directly from ordered members and array iteration.
Both process the full valid input and produce the same ten-word digest.
Their error contracts and storage models differ; these are end-to-end figures,
not identical-validation or identical-allocation claims.

Raw measurements and frozen compared sources:
`bench/comparison/results/fast-float-json-2026-09-08/` (ignored, not committed).
