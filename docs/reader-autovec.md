# Portable auto-vectorized string scanning

FastReader now classifies bounded 16-byte blocks using standard C++ rather than
ISA intrinsics. It copies the block into a local byte array, maps each byte to
quote/backslash match flags, and uses std::bit_cast plus std::countr_zero or
std::countl_zero to locate the first match. Escapes and the bounded tail preserve
the previous scanner's behavior. No input alignment, extra padding, allocation,
new Reader state, or pipeline/storage restriction is introduced.

The source contains no NEON header, vector type, or intrinsic. Clang receives
`#pragma clang loop unroll(disable)` on the classification loop. This inhibits
premature scalar unrolling; it does not select a vector width or an ISA. Other
compilers see the same C++ loop without that hint. Their generated code and
performance have not been benchmarked here.

## Why the first attempts lost

1. A straightforward array-of-match-bytes loop was scalarized and expanded into
   repeated comparisons, conditional selects, and integer OR operations.
2. A compact bit-mask reduction and an any-match reduction did produce vector
   instructions, but with widening/shuffling/reduction overhead. Vectorization
   alone did not make them faster.
3. Inhibiting early unrolling allowed a clean vector comparison, but a separate
   output array produced an input/output overlap check, a scalar fallback, and
   stack stores/reloads in the optimized code.
4. Copying the input into a local array and classifying that array in place made
   the memory relationship explicit. The final optimized ARM64 loop has a
   16-byte load, two byte-vector comparisons, OR, and register extraction. The
   overlap check and temporary-array memory round-trip disappeared.

This follows generated assembly and compiler vectorization remarks, not an
assumption that a loop must vectorize because its source is branchless.

The same scanner was also compiled to x86-64 assembly. Clang generated `movdqu`,
`pcmpeqb`, `por`, and scalar extraction instructions. This verifies cross-target
code generation for the scanner, not an x86 execution/performance test or a
full cross-platform library build. The Nix compiler wrapper emitted its usual
cross-target warning; compilation succeeded and the output is x86 assembly.

## Measurements and tradeoff

All timings below are CPU medians in microseconds, with P4 input and streaming
to the same four verified sums. Clang 21.1.8, C++20, O3, fast_float, nine randomized
repetitions, minimum 0.3 seconds per case, 0.05 seconds warmup. Input preparation
is outside timing. yyjson 0.12.0 uses its default parse/DOM lookup/free path and
the same compiler. No builds or profiling ran concurrently with measurement.

Early comparisons, each against the baseline in the same measurement group:

| Candidate | Baseline | Candidate |
|---|---:|---:|
| 8-byte match array, ordinary unrolling | 253.22 | 321.76 |
| 16-byte match array, ordinary unrolling | 253.22 | 316.92 |
| 16-byte bit-mask reduction | 253.22 | 332.95 |
| 16-byte any-match reduction | 253.22 | 268.28 |
| Match array with unrolling inhibited | 255.49 | 267.80 |
| Any-match with unrolling inhibited | 255.49 | 265.10 |
| In-place local block, unrolling inhibited | 253.48 | 229.35 |
| Same local block without the hint | 253.48 | 325.89 |

Final size comparison:

| Input | Previous 8-byte SWAR | New 16-byte C++ loop | 8-byte C++ loop | yyjson |
|---|---:|---:|---:|---:|
| Twitter | 251.96 | 230.27 | 236.07 | 171.79 |
| Same content with outside-string whitespace removed | 204.40 | 210.40 | 214.57 | 147.17 |

The selected 16-byte loop improves the original Twitter workload by 8.6%, but
regresses compact Twitter by 2.9%. An earlier final comparison gave 9.5% better
pretty input and 2.4% worse compact input. The compact regression is repeatable
and is not presented as a universal speedup. The 8-byte alternative is slower
than the 16-byte candidate on both inputs. The experiment branch retains the
16-byte candidate with this tradeoff recorded; it has not reached yyjson speed.

The exact reason the gain does not transfer to compact input is not isolated.
No hardware branch-miss or stall-counter attribution is claimed. Do not add
percentage gains from separate runs or compare absolute timings across runs as
though they were paired.

## Validation

- 241,204 differential cases against the previous SWAR Reader, comparing direct
  string endpoints as well as parser position, error/offset, stack size,
  expectation state, and following token.
- The same differential cases pass for the 8-byte auto-vectorized alternative.
- Exact guard-page boundaries cover bounded, P4, and P8 inputs.
- Explicit escaped-quote/backslash runs cross every position around block
  boundaries, including truncation and embedded raw NUL/high-bit bytes.
- All 261 tests pass in C++20 and C++26 with fast_float, and C++20 with the default
  number backend. The latter build's old `-U__ARM_NEON` flag does not disable
  compiler auto-vectorization and is not described as SIMD-free.
- Source cleanup after measuring the winning prototype produced identical ARM64
  release assembly to that prototype.

Artifacts are in the sibling
`hosq-yyjson-comparison/twitter-audit/autovec/` directory. They include variant
sources/headers, generated assembly, vectorizer remarks, build commands,
`results.json`, `hints-results.json`, `inplace-results.json`, `final-*.json`,
`eight-*.json`, differential tests, and the x86 scanner assembly. Only the
selected portable loop is applied to the library. Changes remain uncommitted
on experiment/static-reader.
