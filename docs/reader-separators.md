# FastReader: consume known separators directly

2026-09-07. Implemented on `experiment/static-reader`, following the current
16-byte auto-vectorized string scanner. No explicit ISA intrinsics added.

The container-skip path now consumes `: ` immediately after a key and `,\n`
immediately after a value. Both paths retain the destination's whitespace
handling, so additional spaces, tabs and newlines remain accepted. Other input
continues through the existing fallback. Bounded readers check that two bytes
remain; padded readers use their existing readable-zero-padding contract.
No new input requirement, allocation, storage type or public API is introduced.

This avoids generic whitespace classification at common separator positions.
A generic four-space loop in normal `skip_ws`, forced scalar-scan inlining and
container-stack outlining were not adopted: they did not improve this workload.

## Measurement

Original Twitter JSON, 631,515 bytes; P4 reader, direct sums without a vector.
Clang 21.1.8, O3, fast_float enabled. yyjson 0.12.0 compiled with the same
compiler, ordinary parse + DOM lookup + free. Padding preparation is outside
HOSQ timing; validation work for unused values differs between libraries.
Each run used seven repetitions with randomized interleaving, without a profiler
or concurrent build. Values below are median CPU microseconds per document.

| Run | Before | Both separators | yyjson | HOSQ time reduction |
|---|---:|---:|---:|---:|
| Initial, 0.5 s minimum | 231.71 | 221.07 | 173.09 | 4.59% |
| Repeat, 0.7 s minimum | 229.02 | 219.61 | 169.79 | 4.11% |

HOSQ still takes about 1.29 times yyjson's time in the repeat. These results do
not establish equality with yyjson or performance on other inputs/platforms.
Final library assembly is byte-identical to the measured candidate's assembly.

## Verification

- C++20 and C++26: 262 tests passed in each configuration.
- 246,734 differential comparisons against the preceding reader, including
  malformed/truncated data, reader state, next-token results, depth limits,
  bounded/P4/P8 modes and exact inaccessible guard-page boundaries.
- Added a regression test for extra whitespace after separators and EOF within
  the optimized transitions.

Reproduction artifacts are in the sibling directory
`hosq-yyjson-comparison/twitter-audit/current-profile/`: `third.py`,
`third-build.json`, `third.json`, `third-repeat.json`, `check.py`, and `final.s`.
`current/include` freezes the preceding implementation; `comma/include` is the
measured candidate. Do not regenerate the frozen inputs from the changed tree
when reproducing the before/after comparison.
