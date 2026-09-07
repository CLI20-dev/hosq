# FastReader skip transitions

> Historical measurements below include the former NEON FastReader path.
> FastReader now uses a portable C++ scanner without ISA intrinsics; see
> [Portable scanner baseline](reader-no-neon.md).

`DecodeSkipValue<FastReader<...>>` now uses key, value, and value-end labels to
skip ordinary JSON containers. After a key it goes directly to its colon and
value; after a value it handles a comma or closing delimiter; a comma selects
the next key/value path using the existing container stack. Nested containers
resume the parent's value-end path. No new Reader state, allocation, input
prepass, storage restriction, or padding requirement is introduced.

The skip path matches `true`/`null` in four bytes and the tail of `false` in four
bytes. Numbers are traversed without conversion. Indentation is consumed in
four-space groups, followed by the ordinary JSON whitespace characters. The
bounded Reader checks available bytes before word reads. Padded Readers use the
existing zero-padding contract; word reads use `memcmp`, requiring no alignment.
Strings continue to use the existing bounded quote/escape scanner.

This is not a new JSON validator. The previous structural scanner remains as a
fallback, preserving permissive skipping of unused content, including malformed
literal spellings. Fast transitions consume only known non-structural bytes.
Nesting, matching close delimiters, string termination, and memory bounds retain
their previous behavior. Single quotes do not become JSON string delimiters.

## Cold paths and branch hints

EOF inside a container/string, depth overflow, and mismatched closing delimiters
are error paths. Skip error construction is outlined into a `cold, noinline`
helper on GCC/Clang, and a noinline helper on MSVC. Error-only checks have
`unlikely` hints. Value types, commas versus closes, and empty containers are
not assigned fixed likelihoods because their frequency depends on input.
The structural fallback is not marked cold: normal empty containers also use
its closing-delimiter path.

Existing Reader checks were reviewed rather than indiscriminately deleted.
The normal document-end branch in `read_comma_or_close` remains unhinted; EOF in
`read_value`, `read_key`, or an unfinished container represents an error.
The pre-existing `likely` on the entire `read_one` switch body does not select
one expectation state as more likely than another; it was not relied upon for
this optimization. This change does not claim all Reader error paths have been
outlined or all validation has been eliminated.

## Measurement

Apple ARM64, Clang 21.1.8, C++20, O3, fast_float, zero-padded-4 input, direct
streaming of four requested numeric fields per status to sums. Nine randomized
repetitions per case, minimum 0.3 seconds, 0.05 seconds warmup. CPU medians:

| Input | Previous Reader | New Reader | yyjson 0.12.0 |
|---|---:|---:|---:|
| twitter.json | 279.03 us | 257.59 us | 168.30 us |
| Same data with outside-string whitespace removed | 229.15 us | 228.34 us | 147.12 us |

The final comparison uses the same executable, compiler, input bytes, and four
verified output sums. yyjson uses its default parse/DOM/search/free path; input
preparation is outside timing. Parser validation contracts remain different.
The pretty-input reduction is 7.7%; an earlier run of the candidate gave 5.0%.
Compact input is effectively unchanged. This does not reach yyjson performance.

Ablations: direct transitions with scalar whitespace were approximately neutral;
error outlining alone and removing the new skip-path unlikely hints were also
within roughly 1% in their comparison groups. Optimized assembly confirms actual
calls to the outlined skip-error helper. No hardware branch-miss measurements
were obtained, so the speedup is not attributed solely to branch prediction.

## Validation

- 260 tests pass for C++20 and C++26 with fast_float/NEON.
- 260 tests pass for C++20 with the default number backend and NEON disabled.
- 240,987 differential cases against the previous skip implementation compare
  consumed input, error code/offset, stack size, expectation state, and the next
  token. Cases include truncated literals, malformed input, random JSON-like
  bytes, arbitrary bytes, and a depth limit of two.
- Exact guard-page boundaries exercise bounded, four-byte, and eight-byte inputs.

The source change is on the existing experiment/static-reader branch and is not
committed or pushed. Reproducible benchmarks and differential harnesses are in
`hosq-yyjson-comparison/twitter-audit/transitions/` alongside the worktree.
