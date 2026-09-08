# Ordered constant entries without Try

`Entry<hosq::Key<"id">, Value>` now works in ordinary object schemas without
`Try`. Entries consume fields in declaration order. This does not change the
arbitrary-order matching behavior of Try schemas or custom key pipelines.

The ordinary Decoder previously treated a constant key as an Elem and tried to
call its `decode` method; such schemas did not compile. Constant entries now
have a separate key path with an ordinary scan/match fallback for other Readers.
After matching a built-in constant key, Decoder can pass the proven member-value
position rather than an advisory hint.

FastReader provides an optional `try_read_const_member<Key>` probe. It checks
whitespace, the comma when required, opening quote, full key including closing
quote, colon, and the next value's opening byte. On success it produces the same
value token/state as ordinary key scanning and member-value advancement, without
constructing the intermediate key token. Scalars, objects and arrays are
supported; container depth is checked before committing any state.

A failed probe leaves the entire state intact. The ordinary path handles
mismatches, truncations, depth errors, non-ASCII/escaped schema keys, unresolved
previous values, and states changed by custom operations. No input padding,
explicit SIMD, Reader state fields, allocations, or value-pipeline restrictions
are added. Value pipelines still run normally. The hook does not predecode or
cache numbers and does not skip validation of their contents.

## Measurement

Same 230031-byte medium/all input and independently verified digest. Clang
21.1.8, C++23, O3, fast_float 8.2.10; yyjson 0.12.0 and Glaze 8.3.0. AC power,
seven repetitions per condition, 0.15 s minimum, fresh processes, two rounds with
reversed order. No simultaneous build/profile. CPU median microseconds:

| Implementation | Round 1 | Round 2 |
|---|---:|---:|
| Prior ordinary Entry with explicit scalar-scan/key-equality pipeline | 417.73 | 410.03 |
| New native constant Entry, no Try | 245.47 | 237.10 |
| Existing Try constant-key schema | 359.01 | 357.72 |
| yyjson parse, visit, free | 198.33 | 197.63 |
| Glaze typed records, visit, free | 134.97 | 136.19 |

The ordinary ordered workload takes 41.2–42.2% less time. Native constant-key
schemas had no compilable prior path, so the baseline uses explicit key equality
with the same expected names and field order. This is an end-to-end comparison
on valid input, not a claim of identical storage or validation contracts. HOSQ
aggregates directly; Glaze materializes records with fixed inner arrays; yyjson
materializes a DOM. HOSQ still takes 1.20–1.24x yyjson and 1.74–1.82x Glaze.
Glaze-level performance has **not** been achieved.

Forcing `decode_nested` and the ordinary object decoder to inline gave
244.73/239.18 us against 245.47/237.10 us, no repeatable gain. Those directives
were removed. An earlier scalar-only probe took 269–284 us in its own paired
run; extending the probe to containers avoids redoing successfully checked
keys on nested fields. Do not combine measurements from different runs.

## Reproduction and verification

The existing optional `hosq_static_gap` target has a new `Hplain` mode, using
real constant Entry schemas. `Hordered` remains the older handwritten sequence
diagnostic and is not the production entry decoder. `yyjson` is also available:

```sh
cmake --build BUILD --target hosq_static_gap
BUILD/hosq_static_gap FIXTURES Hplain --benchmark_repetitions=7 --benchmark_min_time=0.15s
BUILD/hosq_static_gap FIXTURES Glazeoriginal --benchmark_repetitions=7 --benchmark_min_time=0.15s
BUILD/hosq_static_gap FIXTURES yyjson --benchmark_repetitions=7 --benchmark_min_time=0.15s
```

Tests cover both SimpleReader and FastReader, correct and quoted numeric values,
wrong/reordered keys, missing fields, missing delimiters, all prefix truncations,
probe success equivalence, mutation-free probe misses, container stack state,
and bounded input immediately before a protected page.

Raw data and scripts are local, ignored artifacts under
`bench/comparison/results/const-final-2026-09-08/`.

For the later joined-prefix and direct-fold implementation, current regression
tests and the fresh paired benchmark, see [residual reader cost](reader-residual-profile.md).
