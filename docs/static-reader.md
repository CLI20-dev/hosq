# Static Reader specialization experiment

The current experiment preserves existing pipeline expressions and storage/type
analysis while specializing execution. It adds no runtime hint objects, reader
state fields, virtual calls, input padding, or mandatory intermediate storage.

## Dispatch and position proofs

`ReaderBase<Derived>` forwards supported compile-time arguments to
`next_token_impl<...>(state)`, otherwise to `next_token_impl(state)`.
`hosq::next_token<...>(reader, state)` also accepts legacy readers implementing
only `next_token(state)`. Constness and exception specifications are retained.

`Expect` is advisory: custom pipeline steps may move the cursor, so these paths
retain a generic fallback. `ReadPosition` instead expresses a proven position.
Decoder uses it immediately after reset and after a key operation that cannot
advance to another token. FastReader then enters the value/colon-value parser
without loading and dispatching on `expect`. Input validation still runs.
Other formats can implement or ignore these static arguments.

## Adjacent scalar/number fusion

`scan::scalar | decode::number<T>` and the `number_into<T>` variant can execute
through `ReaderBase::read_number<T>` when the Reader supports it. Analysis still
sees the original steps, including their storage and const-callability rules.
The normal executor handles all following steps and MatchResult behavior.

Fusion is limited to these adjacent built-in operations. Unsupported Readers
and custom scan/number extensions retain the ordinary path. An extension must
provide a `reader_fusion` alias naming itself to opt in; inheriting a different
extension's alias does not silently opt in. The fused operation must preserve
that extension's behavior. Arbitrary intervening steps are never reordered.

FastReader uses conversion's end pointer as the scalar length, avoiding the
preliminary length scan. Integers consume bounded groups of up to eight digits.
Overflow, invalid suffixes, escapes and cached tokens use the original fallback,
including its error details, partial writes, and cache behavior. Floating-point
conversion still uses `std::from_chars`.

## String scanning

Strings use bounded 16-byte local blocks and a standard C++ classification
loop designed for compiler auto-vectorization. FastReader has no NEON intrinsics
or architecture-specific scanner selection. Escapes and tails remain scalar.
See [Auto-vectorized string scanning](reader-autovec.md). The same scanner serves scalar
resolution and skipping, including truncated strings. This preserves the
existing raw-string semantics; it does not add full JSON string validation.

## Verification

C++20 and C++26 each pass 248 tests. The portable scanner configuration also
passes 248 tests. Added coverage compares fused/unfused numeric behavior,
overflow, partial writes, caches, customization, following mismatches, arbitrary
byte patterns and block boundaries. Guard-page tests check that scanning and
integer parsing do not read beyond the input. ASan/UBSan execution timed out
before main produced output and remains unverified in this environment.

```sh
clang++ -std=c++20 -O3 -S -I include bench/reader/dispatch_codegen.cc -o dispatch.s
```

With Clang 21.1.8 on ARM64, `fused_pipeline` and `fused_direct` have identical
462-instruction bodies after normalizing local label numbers. `proven_member`
has no byte load of the `expect` state field. This verifies the examined
adapter path, not universal code generation for every user pipeline.

## Performance

The final benchmark links the previous implementation under a different
namespace alongside the new one, avoiding ODR collisions. It compares both
and yyjson 0.12.0 in one process with randomized interleaving, nine repetitions,
minimum 0.25 s per case and 0.05 s warmup. Inputs and aggregate results are
verified. C++20, Clang 21.1.8, `-O3 -DNDEBUG`; yyjson is the installed Release
library. Times below are median CPU microseconds.

| Workload | Previous | Current | yyjson |
|---|---:|---:|---:|
| Twitter aggregation | 446.7 | 330.9 | 175.2 |
| uint64 array aggregation | 2095.9 | 966.4 | 754.4 |
| double array aggregation | 2605.9 | 2007.8 | 628.5 |

These are reductions of 25.9%, 53.9%, and 23.0%. Integer aggregation approaches
but does not match yyjson; Twitter and floating-point work still have substantial
gaps. yyjson builds a DOM and validates/decodes the full input, while FastReader
can skip unused content. Equal outputs on valid inputs do not imply identical
validation contracts. The speedup comes from the specialized work enabled by
static dispatch, fusion and block scanning, not from CRTP syntax alone.

## Historical optional fast_float backend

The measurements below describe the earlier opt-in configuration. The current
backend is bundled and defaults on; see [JSON prefix conversion](fast-float-json.md).

fast_float v8.2.10 (official tagged source), Clang 21.1.8, C++20, -O3 -DNDEBUG,
Apple ARM64. Identical current HOSQ headers compiled with backend off/on in
separate namespaces in one process. Nine randomized repetitions, 0.25 s minimum,
0.05 s warmup, no concurrent builds during measurement. Median CPU microseconds.
yyjson 0.12.0 installed Release library. Aggregate outputs verified before timing.

| Workload | std::from_chars | fast_float | yyjson |
|---|---:|---:|---:|
| Twitter | 313.801 | 312.872 | 167.367 |
| UInt64 | 935.756 | 944.209 | 729.868 |
| Double | 1961.915 | 983.698 | 609.144 |

Double aggregation takes 49.9% less time (1.99x throughput). Relative time to
yyjson falls from 3.22x to 1.61x. Integer and Twitter differences are negligible.
The double input is a ~1 MiB positive decimal array with six fractional digits;
this result is not a claim about every floating-point distribution. HOSQ sums
directly; yyjson builds a DOM then sums. Equal results do not imply identical
validation contracts.

Implementation: HOSQ_USE_FAST_FLOAT=1 selects float/double conversion statically;
integers retain their existing path and other types use std::from_chars.
Errors fall back to the platform std::from_chars to preserve existing output and
error behavior, including this platform's writes on range errors. No runtime
backend switch or extra allocation is introduced. All translation units in an
application should use the same macro configuration.

Current setup: CXX_HOSQ_USE_FAST_FLOAT defaults to ON and uses the bundled
headers; no external FastFloat package or include directory is needed. Set it
to OFF to opt out. Direct header use defaults on as well and accepts
HOSQ_USE_FAST_FLOAT=0. See [the bundled backend](fast-float-json.md) for current
conversion semantics and verification. The measurements above predate this
packaging change.

Validation: 249 tests pass for C++20 and C++26 with fast_float, including float
and double differential checks against std::from_chars on edge cases and 10,000
random decimals for each type (value, signed zero, consumed length, error code).

The default backend also passes all 249 tests with the portable scanner.
Installed-package consumer configuration, compilation and execution pass.

## Profile-guided string scan and skip cache (historical)

The NEON string path described below was subsequently removed; see
[Portable scanner baseline](reader-no-neon.md). These numbers are historical.

Implementation at that stage on experiment/static-reader (uncommitted):

1. On ARM64 NEON, pack string stop-byte matches into a 64-bit mask and jump
   directly to the first match. Preserve escape skipping and bounded tails.
2. resolve_skip now records the consumed scalar length in the existing token.
   Previously decode::skip advanced the cursor while leaving length unresolved;
   the following next_token could scan the same scalar again. Successful skips
   now resolve the token length, as ScanScalar already does. This is an intentional
   change to the observable length cache; no new state or allocation is added.

No pipeline/storage restriction and no new dependency. The portable string scan
is retained. Integer and float conversion algorithms are unchanged in the final
version.


Final loaded-machine randomized benchmark: Twitter 458.13 -> 424.25 us (-7.4% median), yyjson 248.62 us. UInt64 and Double were within ~1%. 101 randomized repetitions of 8 iterations each; CPU-time CV 8–17%, so this is not a stable idle-machine speedup claim. All 250 tests pass for C++20/C++26 with fast_float and C++20 portable/default. Structural SIMD experiments were slower and removed.
