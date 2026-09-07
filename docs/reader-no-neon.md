# Portable scanner baseline

FastReader no longer includes arm_neon.h, calls NEON intrinsics, or selects a
string scanner using ARM/NEON architecture macros. The initial replacement used the same bounded eight-byte SWAR scanner on every
target. It has since been superseded by a standard C++ loop that the compiler
can auto-vectorize; see [Auto-vectorized string scanning](reader-autovec.md).

The four/eight-byte input padding policies, classification table, static Reader
interface, and pipeline/storage composition are unchanged. No additional input
padding or alignment is required. The compiler remains free to optimize standard
C++ code, including automatic vectorization; this change removes explicit NEON
dependence rather than disabling the compiler's vectorizer or changing the ABI.

This change concerns FastReader. The separate legacy indexed_reader.hh and its
ScannerNeon implementation are outside this FastReader path.

Earlier documents retain historical measurements, explicitly marked as such.
Use the latest auto-vectorized Reader report for current performance numbers.

## Historical SWAR measurement

Twitter direct streaming to four sums, Clang 21.1.8, O3, fast_float, nine
randomized repetitions, minimum 0.3 seconds and 0.05 seconds warmup. Input
preparation is outside timing. yyjson 0.12.0 is built with the same compiler
and uses default parse, DOM lookup, and free. CPU medians:

| Reader | Time |
|---|---:|
| FastReader bounded | 285.11 us |
| FastReader zero-padded-4 | 258.03 us |
| FastReader zero-padded-8 | 258.64 us |
| yyjson | 174.42 us |

Both C++20 and C++26 pass all 260 tests, including guard-page boundary tests.
The timings above record the SWAR baseline at that stage; no NEON FastReader variant
was included in this comparison.
