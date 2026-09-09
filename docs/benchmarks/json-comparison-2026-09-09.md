# JSON comparison benchmark — 2026-09-09

![JSON comparison benchmark](json-comparison-2026-09-09.png)

Median CPU time for a deterministic, pseudorandom 2,048-record JSON array;
lower is better.
Each parser was measured in a separate process with Google Benchmark 1.9.5,
seven repetitions, and a minimum run time of 0.1 seconds per repetition.

| Case | Format | Key order | HOSQ | Glaze | yyjson |
|---|---|---|---:|---:|---:|
| ID only | Compact | Canonical | 351.6 µs (CV 2.9%) | 266.1 µs (CV 4.0%) | 227.5 µs (CV 4.4%) |
| Partial fields | Compact | Canonical | 406.3 µs (CV 2.8%) | 235.9 µs (CV 1.2%) | 239.7 µs (CV 4.4%) |
| All fields | Compact | Canonical | 469.0 µs (CV 3.4%) | 243.6 µs (CV 4.3%) | 260.0 µs (CV 4.0%) |
| All fields | Compact | Shuffled per record | 559.9 µs (CV 0.6%) | 263.0 µs (CV 7.6%) | 274.3 µs (CV 9.5%) |
| ID only | Pretty | Canonical | 447.1 µs (CV 3.0%) | 310.1 µs (CV 4.7%) | 301.4 µs (CV 2.4%) |
| Partial fields | Pretty | Canonical | 499.8 µs (CV 0.8%) | 304.7 µs (CV 2.1%) | 305.7 µs (CV 4.0%) |
| All fields | Pretty | Canonical | 593.1 µs (CV 1.5%) | 316.5 µs (CV 3.4%) | 323.4 µs (CV 2.8%) |
| All fields | Pretty | Shuffled per record | 680.0 µs (CV 2.4%) | 372.7 µs (CV 2.9%) | 366.9 µs (CV 7.4%) |

## Scope and fairness

- Each case is a separate source file and executable.
- Every executable uses Google Benchmark directly and registers HOSQ, Glaze,
  and yyjson implementations for the same input.
- HOSQ and Glaze construct typed output. yyjson constructs and frees its DOM.
- Glaze uses its normal mutable-buffer padding path; padding and restoration are
  included in the timed call.
- All three implementations calculate the same checksum from the requested
  values inside the timed region. The checksums are compared before timing.
- The partial-fields case reads `id`, `name`, `count`, `ratio`, and `ok`, while
  ignoring `none` and `values` according to each library's normal mechanism.
- The ID-only case reads `id` and ignores the complete payload.
- The all-fields case reads and validates every member.
- Scalar and array values are generated with a fixed-seed SplitMix64 sequence;
  generation remains outside the timed region.
- Shuffled-key inputs use an independent fixed-seed SplitMix64 sequence for
  member order, preserving the canonical cases' decoded values.
- Pretty inputs use four spaces per indentation level and preserve the decoded
  values and key-order contract of their compact counterparts.
- Input construction and cross-library verification are outside the timed region.

This is an end-to-end application comparison, not an isolated tokenizer or
schema-dispatch benchmark. macOS reported that CPU affinity could not be set,
so the values should be treated as a local comparison rather than a portable
hardware result.
