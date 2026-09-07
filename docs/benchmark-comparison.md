# JSON workload comparison — 2026-09-07

The previous Twitter field-selection benchmark did not cover small-document
fixed costs, size scaling, wide objects, deep traversal or escaped strings.
`bench/comparison` adds 12 deterministic cases and one static-schema control
(25 measurements). It complements the real Twitter fixture; it does not turn
synthetic averages into a universal score.

## Current-condition results

Both implementations were built with Clang 21.1.8, Release (-O3, NDEBUG):
HOSQ at `ed54f74`, bounded FastReader, fast_float 8.2.10; yyjson 0.12.0 source,
default allocator and flags. The consumer produces an identical fixed-size
digest without an output vector. yyjson parsing, DOM creation, consumption
and freeing are included. Input loading is excluded for both.

The user reported battery operation. OS status recorded `discharging` despite
also displaying `AC Power`; no policy inference is made from that ambiguity.
Two complete runs used randomized interleaving and five repetitions per
measurement. There was no build/profile work during either accepted run.
Ratios below use CPU-time medians independently within each run, not historical
absolute timings. A development run overlapping compilation was excluded.

**HOSQ time / yyjson time; below 1 is faster.**

| Case | Input bytes | Run 1 | Run 2 |
|---|---:|---:|---:|
| Small, id only | 109 | 0.676 | 0.690 |
| Small, all values | 109 | 0.992 | 1.108 |
| Medium, id only | 230,031 | 1.477 | 1.485 |
| Medium, all values | 230,031 | 1.829 | 1.829 |
| Medium, all values, static schema control | 230,031 | 1.822 | 1.829 |
| Large, id only | 30,006,521 | 1.056 | 1.122 |
| Large, all values | 30,006,521 | 1.825 | 1.825 |
| Wide object, id first | 298,515 | 2.893 | 2.874 |
| Wide object, id last | 298,515 | 2.171 | 2.178 |
| Deep containers, all values | 1,031,083 | 2.162 | 2.148 |
| Numbers, all values | 652,030 | 1.862 | 1.843 |
| Long UTF-8 strings, all values | 527,251 | 0.874 | 0.869 |
| Same strings Unicode-escaped, all values | 1,084,307 | 1.644 | 1.630 |

## What this identifies

1. **Sparse extraction is not automatically faster.** It wins for the 109-byte
   document, but loses for the medium and wide inputs. In the wide pair HOSQ
   stays around 442–446 µs whether id is first or last; yyjson changes from
   about 154 to 204–205 µs. HOSQ still processes the remaining members through
   schema matching and skip. yyjson must still parse the full input, but its
   post-parse lookup stops at the first matching key. The pair identifies a
   useful wide-object workload to profile. It does not measure branch misses.
2. **Size changes the tradeoff.** Sparse HOSQ/yyjson is about 1.48 at 230 KB and
   1.06–1.12 at 30 MB, while full consumption stays about 1.83. Allocation,
   cache behavior and traversal are hypotheses for follow-up, not individually
   measured causes. Reused input buffers do not constitute a cold-stream test.
3. **Generic recursion alone does not explain the medium full-consumption gap.**
   The static-schema control produces the same output in essentially the same
   time. This does not prove individual recursive calls are free: static
   dispatch and key matching have their own costs. It shows that replacing the
   consumer with this static schema does not remove the end-to-end gap.
4. **Escaped strings deserve a separate optimization target.** The raw UTF-8
   and Unicode-escaped pair has identical decoded digests. HOSQ changes from
   faster to slower. HOSQ's raw-string consumer performs unescaping while
   hashing; yyjson unescapes during parsing. These numbers include both stages
   and must not be labeled string-scanner-only timings.
5. **Deep and numeric workloads expose further limits.** They are roughly
   2.15× and 1.85× yyjson in this generic consumer. Numeric type discovery first
   attempts integer conversion, then converts decimal/exponent values to
   double. Consequently this is not a pure fast_float versus yyjson number
   conversion test or a fully specialized numeric-pipeline ceiling.

Small/all varies from approximate parity to 11% slower between runs. Large/id
also varies, with yyjson run-2 CV about 5.2%; deep yyjson CV is about 4.8%.
Treat these as exploratory battery-condition results, not regression thresholds.
No universal average is reported: the case distribution would decide its value.

## Verification and reproduction

Each case passed HOSQ and yyjson comparison against a separately computed Python
oracle before timing. The static schema matched the same medium/all oracle. The standalone CMake
build and oracle verification also passed with fast_float disabled; only the
fast_float-enabled configuration was timed.
The string pair has equal decoded outputs. SHA-256 input and executable hashes,
raw samples, dispersion, CPU/wall time and power state are retained in
[results](../bench/comparison/results/2026-09-07/).

[Build, fixture generation and timing commands](../bench/comparison/README.md)
are standalone; no external Twitter file is required. JSON fixtures are
regenerated, not committed. Large logs/binaries/profiles from earlier experiments
are not dependencies. No HOSQ library source changed in this benchmark work.
