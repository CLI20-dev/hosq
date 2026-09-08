# Residual no-Try reader cost

This is a follow-up to `fast-float-json.md`. Both experimental branches keep
bundled fast_float and its bounded JSON floating-point prefix conversion.
The changes measured here do not replace that backend or change pipeline type
analysis, storage selection, custom steps, reader state, or padding requirements.

## Accepted changes

- Constant member probing first compares a complete `,"key":` or `"key":`
  prefix. Whitespace and unusual key spellings retain the existing path;
  an unsuccessful probe leaves the reader state untouched.
- The ordered entry decoder invokes its index-sequence fold directly instead
  of routing it through `std::apply`. Left-to-right evaluation and short-circuit
  behavior are preserved. No attribute is imposed on user callbacks.

The key change alone shortened the medium case by approximately 7–8%; the
entry fold alone shortened it by approximately 3%. Small integer special cases,
postponed integer overflow checks, a byte-table number terminator check, and an
array continuation hint were also measured. Their gains were absent or did not
persist across the input set, so they were not retained. A fixed literal probe
helped whitespace-heavy input but did not consistently improve the other cases.

A diagnostic that used Clang's statement `always_inline` on arbitrary pipeline
actions improved medium input by 4–6%, but was rejected: it can override a
user's `noinline` and produce a warning under `-Werror`. Moving the call into a
namespace helper also changes friend/access context.

A separate function-level `flatten` experiment preserved the call sites but
regressed medium input by about 2–3%; it was not retained either.

## Fresh hardware and sampled profiles

Time Profiler and CPU Counters were captured separately from timed benchmarks,
using the same Clang 21.1.8 O3 binary for HOSQ and Glaze. The first two seconds,
including benchmark calibration, were excluded. CPU-counter fractions below
use cycle-weighted main-thread P-core intervals. They are CPU pipeline bandwidth
fractions, not percentages of elapsed parse time or branch miss rates.

| CPU bandwidth category | HOSQ canonical | Glaze typed |
|---|---:|---:|
| Instruction delivery bottleneck | 5.61% | 17.42% |
| Discarded work: misprediction or pipeline restart | 0.59% | 0.91% |
| Instruction processing bottleneck | 12.51% | 7.15% |
| Useful work | 81.30% | 74.53% |

For this repeated fixed-schema workload, branch recovery is not the dominant
measured loss. The HOSQ sampled profile instead places about 13% in unsigned
integer parsing, 7.75% in whitespace classification, and 5.79% in numeric token
completeness checks. A sampled instruction being hot does not establish that
removing its source-level check will improve throughput: the independent
integer and terminator experiments above did not yield a robust gain.

The initial folded-symbol bucket was resolved to string, bool and null
callbacks. Bool/null callbacks total 6.93%, but only roughly 4.9% is attributable
to literal resolution setup/loop. Call prologue/epilogue itself is under 1%.
Inlining opportunities primarily affect propagation of cursor/context values.
These results do not establish behavior on unpredictable object layouts or
one-shot cold input.

## Paired end-to-end comparison, 2026-09-08

AC power, charged at 100%; five repetitions per process, two rounds with
reversed case and implementation order, at least 0.12 seconds per repetition.
No compilation or profiler ran during timing. Each implementation passed the
independent Python JSON readback digest oracle. Below is the mean of the two
round medians, in microseconds. “Before” already includes bundled fast_float
JSON mode and the member-number fusion experiment.

| Input | Before | Joined prefix + direct fold | Reduction | yyjson | Glaze typed |
|---|---:|---:|---:|---:|---:|
| small/all, 109 B | 0.112 | 0.097 | 13.6% | 0.190 | 0.112 |
| medium/all, 230 KB | 214.657 | 189.406 | 11.8% | 195.885 | 136.482 |
| large/all, 30 MB | 27221.333 | 24495.143 | 10.0% | 25262.321 | 17849.778 |
| varied/all, 288 KB | 271.283 | 241.881 | 10.8% | 224.910 | 206.970 |
| pretty/all, 567 KB | 393.216 | 389.711 | 0.9% | 314.001 | 280.207 |

The pretty case uses the same varied values, indentation and legal whitespace
on both sides of colons. Reproduce it with the diagnostic generator's
`--pretty` option in a separate empty output directory. It deliberately
exercises the fallback member transition. Its change is within measurement
variation, so this is not a demonstrated improvement on whitespace-heavy input.

HOSQ streams an ordered schema into the digest; Glaze materializes its declared
structs and fixed arrays and then computes that same digest. yyjson constructs
its DOM before traversal. These are end-to-end comparisons with disclosed
storage/error contracts, not identical allocation or invalid-input handling.
The HOSQ consumer's incremental counters explain only about 1–2%: replacing
count increments with counts derived from parsed rows did not close the gap.

The small case now outperforms Glaze in this set, and medium/large are slightly
faster than yyjson. Glaze parity on medium, large, varied or pretty input has
not been achieved. The remaining medium gap is about 39% in elapsed time.

Frozen sources, commands, hashes, traces and raw results are saved under
`bench/comparison/results/residual-profile-2026-09-08/`, which remains ignored
and is excluded from commits.

## Verification of the retained code

The member-fusion tree passes 574 tests across C++20 and C++26 (568 existing
tests plus six joined-prefix test instances). The static-reader tree passes
550 tests. The added tests compare all observable reader state against ordinary
transitions and exercise guard-page endings with no padding, four zero bytes,
and eight zero bytes. The retained member-fusion headers are byte-identical to
the frozen `joined-fold` sources used for the paired benchmark.
