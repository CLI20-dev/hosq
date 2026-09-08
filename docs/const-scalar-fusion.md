# Constant-member scalar fusion

The array-fusion experiment also recognizes a constant member whose value
pipeline begins with the exact built-in `scan::scalar | decode::string_view`
pair. `FastReader::try_read_const_string_view` combines key matching, the
key/value transition, and scalar resolution. It commits the same resolved
token, cursor and error state before invoking the remaining user pipeline.
Both raw strings and literal views retain their existing meaning.

The pipeline still determines storage, constraints and context callability.
The Reader probe uses a mutable context to perform the member transition;
the remaining steps receive the same const or mutable context selected by the
original pipeline analysis. In particular, adding the optimization does not
change `StringViewImpl`'s const signature or choose a different user overload.

An unsuccessful probe changes neither state nor output. The existing member
and scalar path then produces its original diagnostics and partial output.
The exact scalar extension must carry its own `reader_fusion` self tag, and
the Reader must provide the optional operation. Custom scanners without that
tag and other pipelines retain their original execution. There is no new
persistent state, allocation, padding requirement or explicit SIMD operation.

## Paired native comparison

The baseline here already contains bundled fast_float JSON prefix conversion,
joined constant-key matching, the direct tuple fold and fixed numeric-array
fusion. This table isolates the additional string-view operation.

Both engines materialize the same field types in 88-byte records and fresh
root vectors, including raw string views and fixed four-element arrays. They
perform the same postpass and complete-input checks; destruction is timed.
HOSQ uses unpadded `FastReader<>` without `Try`. Glaze uses its public mutable
string reader with padding preparation/restoration timed. All fields and
floating-point bits are checked before timing.

Clang 21.1.8, C++23, O3, Apple ARM64, AC power at 100%; two rounds with engine
and case order reversed, five repetitions of at least 0.12 seconds each.
Times below are means of the round medians in microseconds. No builds, tests
or profiling ran during timing.

| Input | HOSQ before | HOSQ after | Glaze | HOSQ time reduction |
|---|---:|---:|---:|---:|
| small, 1 record | 0.11919 | 0.11228 | 0.09415 | 5.8% |
| medium, 2,048 records | 161.200 | 151.659 | 127.303 | 5.9% |
| large, 262,144 records | 21,161.313 | 19,603.264 | 16,856.200 | 7.4% |
| varied numbers and strings | 212.895 | 195.945 | 169.442 | 8.0% |
| varied and indented | 358.009 | 351.440 | 242.768 | 1.8% |

The remaining native gap is about 16–19% on medium, large and varied input,
and 45% on indented input. This does not establish Glaze parity. These are
valid-input comparisons; the libraries' malformed-input contracts differ.

Separate medium-input controls found no repeatable useful gain from moving
constant-key fallback into a non-inline helper or merely selecting the
existing 8-byte-padded Reader. The padding control charged its resize,
restoration and storage checks to each parse. A public `decode::each` control
that decodes directly into root vector elements changed total time by only
about 1%; it therefore does not justify changing `collect`'s construction and
exception behavior. None of these controls changes the accepted library.

## Verification

602 tests pass across C++20 and C++26. The nine new tests exercise hook
selection, const/mutable overload preservation, custom-scanner opt-out, full
Reader-state equality, unchanged output on misses, fallback errors and partial
outputs, and all tested input prefixes at protected page boundaries for
unpadded/4-byte/8-byte input. All nine also pass with fast_float disabled.

The initially explored string-view prototype was not selected by the const
dispatch gate. Its timing is not evidence of an optimization. The measurements
above use the corrected gate, with counters proving hook selection and scanner
elimination before timing.

Sources, commands, results and diagnostic controls are retained under
`bench/comparison/results/array-number-fusion-2026-09-08/`, especially
`view-active`, `view-native-broad`, `outline-initial`, `inplace-initial` and
`padding-initial`. These are ignored local artifacts and must not be committed.
