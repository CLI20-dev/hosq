# Fixed numeric-array fusion experiment

`experiment/array-number-fusion` adds an optional Reader operation for the
existing fixed-size public pipeline:

```cpp
scan::array |
    decode::collect<scan::scalar | decode::number<T>, N> |
    store::assign
```

The pipeline still determines its storage and result types. Its static
analysis and constraints are unchanged. Execution recognizes only the exact
built-in scalar/number pair inside fixed `collect`; both Reader extensions
must explicitly retain their own `reader_fusion` self tag. Other Readers,
custom extensions and inner user steps keep the ordinary execution path.

`FastReader::try_read_number_array` parses from a local cursor into a temporary
`std::array`. Float and double use the bundled fast_float JSON prefix converter
against the complete remaining logical input. Integers retain the measured
inline parser. On success the Reader commits the ordinary closing `ArrayEnd`,
cursor, context depth and error state once. A failed probe changes neither
Reader state nor output; ordinary `collect` then produces its existing error,
cached token and partial result. The outer pipeline receives the same final
array and Reader state.

The operation needs no allocation, persistent state, new padding requirement,
runtime backend selector or explicit SIMD instructions. Variable-length
collection is unchanged. Arbitrary user actions between array elements are
also unchanged: their observable intermediate Reader state prevents this
particular optimization.

## Comparison method

The baseline already includes bundled fast_float in JSON prefix mode, joined
constant-key prefixes and the direct decoder tuple fold. These measurements
isolate the additional fixed-array operation; they are not a comparison with
`main` or the standard-only numeric backend.

Clang 21.1.8, C++23, O3, Apple ARM64, AC power at 100%. Five repetitions per
process at a minimum of 0.12 seconds each, two rounds reversing both engine and
case order. Reported times are the mean of the two round medians. No builds,
tests or profiling ran concurrently with timed benchmarks. All values and
digests were checked before registration; Python `json.loads` independently
verified every fixture against its manifest. Timing includes parse, consumer,
and destruction of newly allocated output containers.

The native comparison stores identical field types: `uint64_t`, `double`,
`bool`, `nullptr_t`, raw `string_view`, and fixed four-element integer arrays.
Both full-record types are 88 bytes. Both engines create a fresh root vector
and perform the same postpass, including escape decoding while hashing raw
string views. Public API checks require complete input consumption on every
iteration. Glaze uses its public mutable-string reader with padding resize and
restoration included. HOSQ uses bounded, unpadded `FastReader<>`.

Native numeric-array input separately targets `uint64_t id` plus six `double`
values per record in both libraries. Every floating value is checked bitwise,
including the integer-spelled value that rounds to 2^64 as a double.

## Native results

Times in microseconds; lower is faster. HOSQ columns use the same public
pipeline and consumer before and after the optimization.

| Input | HOSQ before | HOSQ after | Glaze | HOSQ time reduction |
|---|---:|---:|---:|---:|
| small, 1 record | 0.1324 | 0.1171 | 0.0926 | 11.5% |
| medium, 2,048 records | 192.593 | 158.536 | 125.494 | 17.7% |
| large, 262,144 records | 24,832.000 | 20,815.625 | 16,751.772 | 16.2% |
| varied numbers and strings, 2,048 records | 244.059 | 204.568 | 163.889 | 16.2% |
| varied and indented, 2,048 records | 367.531 | 346.030 | 232.394 | 5.9% |
| numeric arrays, 8,192 records | 1,031.670 | 856.678 | 638.536 | 17.0% |

This is a substantial improvement, but does not establish Glaze parity.
Matching native storage still leaves approximately 24–26% extra HOSQ time on
medium, large and varied inputs, and 49% on the indented input.

An earlier streaming-digest diagnostic, `Hnative4`, also improved about
13–16% on compact inputs and reached the old Glaze owning-string control on
varied input. That result must not be substituted for the table above: its
consumer and string ownership differ. The original opaque per-element
`Hcanonical` remains as a control and receives no array fusion.

## Validation and remaining work

584 tests pass across C++20 and C++26, including ordinary/fused final-state
comparison, fixed lengths 0/1/4/6, quoted numbers, negative zero, malformed and
short input, overflow, partial outputs, custom-extension opt-out and protected
page boundaries for unpadded/4-byte/8-byte modes. Eight focused tests also pass
with `HOSQ_USE_FAST_FLOAT=0`.

The fresh native profile still attributes about 16% of HOSQ samples to the
integer parser. Helper calls are not a sufficient explanation: the earlier
numeric path was already inlined. Several short-integer candidates, including
a new reuse of the first nondigit mask for 3–7 digits, produced no material
repeatable gain and were rejected. No extra short-integer specialization is
included in this branch.

The native diagnostic uses a non-const `benchmark::DoNotOptimize` operand.
Preliminary measurements made with the deprecated const overload are retained
only as exploratory artifacts; the table uses the corrected diagnostic.

HOSQ's ordered static schema, quoted-scalar acceptance and exact fixed length
have different malformed-input contracts from Glaze. The table compares the
same complete valid inputs and output types, not identical error policies.

Frozen compared sources, build commands, native diagnostic sources, independent
oracles, per-run results and profiles are under
`bench/comparison/results/array-number-fusion-2026-09-08/`. This directory is
ignored and must not be committed. See also [fast_float integration](fast-float-json.md).
