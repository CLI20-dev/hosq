# Assembly-guided FastReader iteration

> Historical measurements below include the former NEON FastReader path.
> FastReader now uses a portable C++ scanner without ISA intrinsics; see
> [Portable scanner baseline](reader-no-neon.md).

This iteration starts from the direct-transition Reader documented in
`reader-transitions.md`. The adopted change is a shared, constexpr 256-byte
classification table for whitespace and unquoted-scalar terminators. It replaces
classification predicates in `skip_ws`, container whitespace handling,
`resolve_skip`, and `ScanScalar`. The flags preserve every byte value's previous
meaning, including control bytes in the permissive scalar-skip contract.
There is no dynamic table construction, allocation, per-reader storage, new
input assumption, or change to pipeline composition. The SIMD string scanner
is unchanged.

## How the assembly guided the experiments

Both HOSQ and official yyjson 0.12.0 C source were compiled with Clang 21.1.8 and
O3. The release assembly was inspected alongside a fresh 15-second Time Profiler
capture of the starting Reader. Debug sampling identifies hot source regions;
it is not an instruction-cycle or branch-miss measurement. The profile retains
unresolved optimized source locations rather than assigning them by guesswork.

### 1. Stop-position dependency in the SIMD string scanner

The starting HOSQ path contains:

```asm
rbit x5, x5
clz  x5, x5
add  x4, x4, x5, lsr #2
ldrb w5, [x4]
cmp  w5, #34
b.eq quote_found
```

After finding the first quote/backslash, it reloads that byte to distinguish
which one it found. A variant retained the quote mask and removed this reload.
Assembly confirmed that the load/compare was replaced by another `shrn`, `fmov`,
`neg`, `and`, and `tst`. This trades a dependent load for more mask operations.
It measured essentially unchanged: 254.46 -> 254.33 us in its paired run.
The suspected dependency exists, but removing it was not a measured win.

### 2. yyjson's scalar character-classification loop

An inlined yyjson string path uses a repeated pattern such as:

```asm
ldrb w24, [x9]
ldrb w13, [x20, w24, uxtw]
tbz  w13, #0, stop
```

The surrounding code unrolls this classification. A HOSQ variant used a
256-byte quote/backslash table and sixteen unrolled byte checks, with bounded
reads and the existing permissive string-skip behavior. It was slower:
254.46 -> 279.17 us (+9.7%). This was not a wholesale port of yyjson's string
reader; yyjson also handles decoding and validation. The experiment rejects
this particular replacement, not every possible scalar scanner.

Flattening the SIMD block/tail loops removed a redundant-looking loop boundary
check but gave only roughly 0.6-1.2% reductions across runs. It was not adopted.

### 3. Container stack spilling into scalar skips

The generic HOSQ SkipImpl assembly reserved 160 bytes and performed stack-guard
work before choosing scalar versus container. The array used by the inlined
container scanner forced this frame even on scalar skips.

An experimental noinline `scan_container(State&, char)` isolated that stack.
The scalar caller then reserved 16 bytes; container branches restored that
frame and tail-jumped to the scanner. This is a normal parsing path and was
not marked cold. The structural code-generation improvement was real, but
whole-document improvements were only around 0-1.3% in the comparison runs.
Combined with classification tables it did not consistently improve both input
styles. The outlining variant is preserved as an experiment and not adopted.

The yyjson read entry itself has a larger stack frame (1248 bytes plus saved
registers in this build), but that entry runs per parse rather than per skipped
value. Raw frame size is not enough to compare cost; invocation frequency and
which paths pay for it matter.

### 4. Whitespace-loop restructuring

The starting container whitespace loop can return to its four-byte comparison
after consuming a single leftover whitespace byte. A variant followed a common
pretty-JSON layout in stages: optional newline, four-space groups, then the
remaining whitespace. It preserved arbitrary whitespace input, but slowed the
whole benchmark from 248.08 to 280.85 us (+13.2%). It was rejected. Fewer source
loop restarts alone do not establish faster machine code.

### 5. Byte classification: the adopted change

The starting whitespace/scalar-terminator predicates compile to comparisons,
variable shifts, masks, and conditional branches. Unlike a whole string scanner,
these tiny tests are repeated without SIMD amortization. Replacing just their
classification with a shared flag table produces byte-load/table-load/bit-test
loops similar to yyjson's classification approach. This was independently
measured for whitespace only and for whitespace plus scalar terminators:

| Variant | Paired CPU median |
|---|---:|
| Starting Reader | 255.87 us |
| Whitespace table only | 247.13 us |
| Whitespace + scalar-terminator table | 245.54 us |

This is evidence for the selected classification change, not evidence that
lookup tables are universally faster: the string-table experiment above lost.

## Final comparison

Same executable, nine randomized repetitions, minimum 0.3 seconds per case,
0.05 seconds warmup, four verified aggregate outputs. No builds or profiling
ran concurrently with timing. Input preparation and file I/O are outside timing.
yyjson uses default parse, DOM lookup, and free, compiled with the same compiler.
Parser validation contracts remain different as documented in the earlier audit.
All HOSQ timing entries below are zero-padded-4 streaming paths.

| Input | Starting Reader | Adopted table | Table + outlining | yyjson |
|---|---:|---:|---:|---:|
| twitter.json | 253.18 us | 238.85 us | 237.49 us | 166.11 us |
| Same content, outside-string whitespace removed | 225.94 us | 224.08 us | 226.01 us | 145.65 us |

The adopted change reduces pretty-input time by 5.7%. Compact input is effectively
unchanged (the 0.8% reduction is small relative to run variation). Final-group
CPU coefficients of variation are 0.7-1.8%. An earlier independent table
comparison showed a 4.0% reduction. Absolute times across different runs should
not be treated as paired comparisons or added to earlier gains.

The remaining pretty-input ratio is about 1.44x yyjson. This iteration does not
establish yyjson-level performance or identify all remaining costs. In
particular, hardware branch misses and instruction stalls were not measured;
source-level timing and instruction sequences must not be presented as those
hardware metrics.

## Validation and artifacts

The differential harness compares the old and table-based Reader over 241,204
cases, including scalar inputs, malformed JSON-like bytes, arbitrary bytes,
truncations, depth limits, and exact guard-page boundaries for bounded/P4/P8
inputs. It compares position, error code/offset, stack size, expectation state,
and the following token.

All 260 tests pass in each of C++20/NEON/fast_float, C++26/NEON/fast_float,
and C++20/non-NEON/default-number-backend configurations. Full-suite logs are
recorded alongside the benchmarks. Artifacts are in the
sibling `hosq-yyjson-comparison/twitter-audit/assembly/` directory:

- `yyjson.s`, `quote.s`, `outline.s`, `class_table.s`, and other variants: assembly.
- `current.xml`, `current-sources.json`, `current-profile.txt`, `profile.dSYM`:
  fresh starting-Reader profile and source resolution.
- `results.json`, `outline-results.json`, `ws-results.json`, `classes-results.json`:
  isolated experiment measurements.
- `final-pretty.json`, `final-compact.json`: final comparisons.
- `build.py`, `outline.py`, `whitespace.py`, `classes.py`, `combine.py`, `check.py`:
  build/measurement candidates and differential harness.
- `before.hh`, `reader.patch`, `hashes.json`: change boundaries and provenance.

The code remains on experiment/static-reader, uncommitted and unpushed.
