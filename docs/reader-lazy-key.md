# Delimiter-aware lazy key matching

2026-09-07, `experiment/static-reader`.

FastReader can now match constant keys directly from the input without first
finding the closing quote. The reader already consumes the opening quote;
`meta::match_terminated<'"'>` checks the key and its closing quote together.
A matching prefix alone is insufficient: `id_extra` must not match `id`.
The matcher commits the token length and position before invoking the handler,
so value decoding and Try checkpoints see the same resolved key state.

The ordinary exact-view `meta::match` and delimiter-aware matching share one
trie implementation. The delimiter and commit callback are compile-time
specializations; the exact-view path has a no-op callback. No new state,
allocation, padding requirement, or explicit ISA intrinsics are added.

Decoder uses an optional Reader hook. Readers without it retain their previous
scan-then-match path. FastReader opts into direct matching only for unresolved
string tokens and plain ASCII candidate keys without quotes/backslashes;
other cases retain the existing matching semantics.

On a miss, scan can be postponed until the fallback only when the generic
fallback is exactly the standard `scan::scalar | decode::skip` key pipeline
with a `decode::skip` value pipeline. Those types establish that no custom work
observes the unresolved key before its scan. Custom key pipelines retain the
previous resolved-key contract. This avoids resolving the same key through
both the constant-key path and the standard fallback.

## Measurements under the current power conditions

The user reported battery operation. Historical absolute times and yyjson gaps
are not used as a reference. Before and after versions were compiled into the
same binary with separate namespaces to prevent mixed inline definitions.
Benchmarks ran with randomized interleaving, seven repetitions, a short 0.15 s
minimum, and no concurrent builds. Raw power status was captured before/after;
the OS displayed both `AC Power` and `discharging`, so no precise power-policy
inference is made from that status. All comparisons below are within a run.

| Twitter selection | First reduction in CPU time | Repeat reduction |
|---|---:|---:|
| Four shallow fields | 4.44% | 3.64% |
| Four fields including user.id | 7.75% | 6.46% |
| All 25 status fields, including descendants | 2.35% | 2.69% |

The earlier variant that matched quotes but still eagerly resolved every miss
improved less. No claim of a new yyjson ratio follows from these measurements.
The short runs, uncontrolled core assignment and changing system load mean
small differences should not be interpreted with sub-percent precision.

## Validation

- 364,920 differential comparisons with the preceding implementation: values,
  errors, offsets, reader state, custom fallback observations, truncated data,
  duplicate/prefix/empty/special keys, and exact inaccessible guard-page
  boundaries for bounded, P4 and P8 readers.
- C++20 and C++26: 264 tests passed in each configuration.
- Added tests for delimiter matching, commit-before-handler ordering, no scan
  callback on a hit, and controlled miss deferral.
- Final library assembly for the measured workloads is byte-identical to the
  measured deferred variant.

Artifacts are in the sibling directory
`hosq-yyjson-comparison/twitter-audit/lazy-key/`: `before/include` freezes the
preceding headers, `candidate/include` is delimiter matching without deferred
standard misses, and `deferred/include` is the adopted algorithm. `triple.json`
and `repeat.json` contain the within-run results; power snapshots are stored
alongside them. `check.py` is the differential harness. `prepare.py`,
`refactor.py`, `defer.py`, and the paired/triple build scripts record the
experiment construction; do not regenerate the frozen before directory from
the changed library when reproducing a comparison.
