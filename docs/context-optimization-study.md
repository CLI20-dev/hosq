# Context optimization study (2026-09-08)

## Finding

Keep Context. It can disappear completely after inlining; changing its reference
representation does not address the remaining cost. Reader-state ownership,
aliasing, and the scope visible to the optimizer matter separately.

The experiments below preceded adoption of the decoder/To bridge. The live
implementation now contains its eight HOSQ_ALWAYS_INLINE additions. See
`reader-view-specialization.md` for the final validation and direct comparison. Clang 21.1.8, C++23, -O3, fast_float enabled, Apple arm64.
All raw evidence is ignored under
`bench/comparison/results/reader-view-specialization-2026-09-08/context-study/`.

## Assembly experiments

- A locally constructed DecodeContext plus scalar-number pipeline is identical
  to calling Reader::read_number with Reader/state references directly.
- A Context-based digit loop is identical to the same loop updating state
  directly. With externally supplied state, the cursor store remains inside the
  loop. Manually retaining the cursor locally moves that store to the end.
- A locally owned, unescaped state lets the compiler remove both Context and the
  state aggregate: cursor/end/value remain registers. Control-flow layout can
  differ from separately written pointer code, so these loop functions are not
  claimed byte-identical.
- The member-number success-path diagnostic exposes an intermediate cursor store
  and input-byte reload in InputView versus Fusion. With locally owned state,
  both implementations generate identical assembly, even when the final cursor
  is returned to the caller. Local labels/comments are normalized. This probe
  deliberately projects successful reads and does not establish equivalence of
  the complete failure-state/fallback contracts or the entire decoder.
- Replacing Context's reference_wrappers with raw pointers produces identical
  disassembly for both complete benchmark translation units, after namespace
  normalization. Timing differences in that control are not evidence of an
  abstraction cost.
- Merely adding restrict to the external state reference did not eliminate the
  loop's cursor store in the probe. No restrict promise is added to production.

The aliasing concern is concrete: char input can alias state storage in a general
C++ function, and opaque calls receiving state may observe or modify it. An
external reference alone does not require every intermediate store; the compiler
must prove the stores unobservable. A locally constructed Context is not the
same as locally owned, unescaped Reader state.

The diagnostic executable checks 1,000 generated inputs for loop values/cursors,
local-member values/final cursors, and Context/direct numeric agreement. This is
not exhaustive JSON/error validation.

## Whole-workload experiments

The first inline trial marks Decoder::decode_nested/decode_impl inline. The
outlined decoder bodies move inside still-outlined ToImpl operators; it does not
remove the repeated record/payload boundary and shows no consistent benefit.

The second trial additionally marks the two existing ToImpl operators inline.
It connects nested typed decoding to its caller without introducing a new
executor, Context type, tag payload, ownership protocol, or public API. The
outer dynamic collect remains a separate boundary. Fixed-size collects and
ordinary/error paths can still outline; complete state scalar replacement is
not established for the full benchmark.

Historical diagnostic only: this first runner reused a fixture across two
distinct namespaced C++ Fixture types. Despite identical layouts, that cast was
not a sound type contract. These historical timings are superseded by the
`bridge-final` comparison, which prepares each implementation's own fixture.
The frozen benchmark sources and output work are otherwise the same. Each round has 40 shuffled adjacent
current/decoder-inline/bridge-inline groups per case. The second round reverses
case order. CPU time, AC power; no historical absolute times are compared.

| Input | Bridge trial time reduction vs current, round 1 | Round 2 |
|---|---:|---:|
| small | 4.89% | 4.54% |
| medium | 1.43% | 2.95% |
| large | 3.22% | 4.99% |
| varied | 4.04% | 4.27% |
| pretty | 4.61% | 4.44% |
| numbers | 1.79% | 1.23% |

Each percentage is one minus the median within-group time ratio. Samples are
noisy; the two rounds support the direction, not a universal exact gain. This
compares to current InputView, not directly to Fusion. Do not subtract these
percentages from the previous Fusion comparison and call that a new measurement.

Inlining can also remove returned-object temporaries and change register
allocation and branch layout. The improvement is not attributed exclusively to
cursor-store elimination. The trial is output-validated on the six fixtures;
the final adoption validation and code-size checks are recorded separately.
No claim of testing every compiler or arbitrary user pipeline is made.

## Recommended direction

Retain Context and its reference semantics. Prefer exposing a complete built-in
nested-decode chain to the optimizer over changing Context ownership or adding
more runtime state. The adopted decoder/To bridge is the concrete, small change
for this; keep user callbacks and general fallback semantics intact. Do not
force-inline every collect/reader/error routine: greater code visibility does
not guarantee less machine code or better instruction-cache behavior.

Where a Reader scans several bytes internally, use local cursor/end variables
and commit at the established observable boundary. Preserve required state for
custom callbacks, errors and Try. Do not copy the entire Reader state at each
pipeline step or secretly weaken aliasing guarantees to obtain a favorable probe.
