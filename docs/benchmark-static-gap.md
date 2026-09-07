# Static HOSQ / Glaze gap investigation — 2026-09-07

The 230 KB static-schema gap is real in this benchmark: it is not explained by
Glaze's bulk count arithmetic, the fixed length of its inner array, or the
fixture's field order. A diagnostic that bypasses HOSQ's general member dispatch
reduces HOSQ time by 27–28%, but still leaves it about 1.9× Glaze. The measured
cause is a substantial cost in the key-to-value control path; the remaining
cost is distributed across token/state transitions, numeric parsing and other
work. We have not assigned every part of the remaining gap to a causal factor.

## DOM clarification

DOM means a materialized document tree of objects, arrays and scalar values.
**HOSQ does not construct a DOM here.** Its generic visitor walks the Reader and
accumulates results directly. yyjson always builds its DOM in this comparison.
Glaze uses a `generic_u64` DOM in the dynamic full-value cases, and ordinary
C++ records in the static case. The chart now labels these paths explicitly.

## Controls before attributing the gap to the library

Each control runs in its own process, checks the same independent expected
Stats, and performs five repetitions with a 0.1-second minimum. These are
single-input diagnostics, not additional workloads in the default suite.
Absolute times belong to their own run and should not be compared across runs.

| Control | CPU median µs | Interpretation |
|---|---:|---|
| HOSQ baseline | 341.70 | Original static consumer |
| HOSQ fixed inner length 4 | 347.36 | Matching Glaze's length information does not remove the gap |
| HOSQ standard scan/number pipeline | 351.76 | Replacing direct read_number calls with built-in pipeline does not improve it |
| Glaze original fixed-array model | 130.07 | Original static consumer |
| Glaze same model, incremental counts | 131.01 | Bulk count arithmetic is not the reason |
| HOSQ without successful-token error resets | 332.51 | Only 2.7% faster; valid-input diagnostic, not a correctness-validated change |
| HOSQ short-integer prototype | 348.81 | Did not improve; no adoption |
| Both HOSQ prototypes together | 336.63 | Not enough to explain the gap |

A separate field-order control reversed every object's keys, preserving the
values and byte count. HOSQ was 344.98 / 339.78 µs and Glaze 127.95 / 127.22 µs
(original / reversed). This rules out a large fixture-order advantage in this
case; it does not prove order never matters.

Reading Glaze into per-record `vector<uint64_t>` instead of `array<uint64_t,4>`
cost about 269 µs in the first control run. That adds thousands of allocations;
it must not be used to claim the original static-array implementation was
unfair or artificially slow Glaze to match HOSQ. The HOSQ fixed-4 control above
is the useful test of whether additional length information closes the gap.

## Causal diagnostic: direct member sequence

The ordered diagnostic still calls HOSQ's Reader transitions and scalar
operations, validates each expected key including its closing quote, and
checks structural closes. It replaces the schema/member loop, multi-key matcher
and callback dispatch with a fixed sequence of member operations.

| Implementation | Run 1 µs | Repeat µs |
|---|---:|---:|
| HOSQ normal static schema | 334.66 | 335.45 |
| HOSQ fixed-order direct sequence | 244.14 | 241.40 |
| Glaze normal static schema | 126.57 | 128.00 |

HOSQ improves **27.0% / 28.0%**, while remaining **1.93× / 1.89×** Glaze.
This intervention removes several parts of control flow together; it does not
isolate the cost of meta-match alone or measure branch misprediction.

**The diagnostic assumes exact field order and is not a proposed production
replacement.** It would lose the library's flexibility if adopted as-is. A
production optimization must retain arbitrary order and custom pipeline behavior.

## Profile and assembly evidence

Time Profiler recorded each static implementation separately for eight seconds
using the same O3 C++23 build with debug information and frame pointers. Samples
before two seconds and the benchmark CPUInfo initialization were excluded.
The time limit terminates the profiling process; its timings are not benchmark
results. The release assembly was generated separately without profiler flags.

Exclusive sample attribution for HOSQ (grouped by the sampled leaf function):

| Region | HOSQ sample weight |
|---|---:|
| Token / structure / whitespace functions | 30.0% |
| Key match / handler dispatch functions | 24.0% |
| Number parsing / typed numeric read functions | 16.6% |
| Shared string digest consumer | 4.3% |
| Remaining functions | 25.0% |

These are statistical source attributions, not hardware branch-miss counters.
Inlining and instruction scheduling affect exact line attribution. The hottest
individual HOSQ locations include the integer digit loop (~8.2%), typed number
entry, key matcher and comma/container transitions. No single instruction was
shown to explain the full 2.6× difference.

The concrete path visible in the current source and generated assembly is:

1. The constant-key handler knows its value pipeline, but calls
   `next_token<ReadPosition::member_value>` before it executes that pipeline
   (`decoder.hh`, around line 357).
2. FastReader's position specialization calls general `read_colon_then_value`
   and writes `st.current` and a successful error state
   (`fast_reader.hh`, around lines 193–205). The expected scalar type has not
   been fused into this entry point.
3. `read_number_impl<T>` then checks scalar kind/context/cached length, parses,
   checks the delimiter, and updates token length and position
   (around lines 303–319).
4. The next member/element again passes through state, unresolved-value and
   container transitions (around lines 723–758).

The HOSQ release assembly contains the corresponding loads, context/type
branches, and state stores. By contrast Glaze's typed `decode_index<Payload,1>`
checks `count` plus its closing quote, consumes the colon/whitespace and enters
its integer decoder directly. It does not build HOSQ's intermediate Token.
Glaze still has function calls, whitespace checks, validation and allocation;
its speed should not be attributed to removing all such work.

The two numeric interfaces also have different accepted-input contracts:
HOSQ's read_number fast path supports both string and literal token contexts,
whereas the measured Glaze uint64 member uses its ordinary unquoted-number
reader. Static output type alone therefore does not authorize removing all
HOSQ context checks. That requires an appropriate input constraint or proof.

## Implication for the next implementation

The next target is the connection from a matched key to its known value
operation, and from a completed value to the next member. Static knowledge is
not yet eliminating enough of the intermediate Reader/Token work. A compatible
optimization should fuse operations only where the pipeline contract proves
that intermediate state is not observed, retaining the generic path for custom
steps. The evidence does not justify enforcing fixed key order, removing
validation broadly, or claiming CRTP alone guarantees equal generated code.

No production HOSQ header was changed in this investigation. The valid-input
error-reset and integer experiments were confined to copied headers and were
not adopted. Full correctness work is required before applying any such change.

## Reproduction and artifacts

Build the optional diagnostic target using the same CMake configuration:

```sh
cmake --build build/comparison --target hosq_static_gap -j2
build/comparison/hosq_static_gap build/comparison/fixtures Hordered \
  --benchmark_repetitions=5 --benchmark_min_time=0.1s
```

Other modes: `Horiginal`, `Hfixed4`, `Glazeoriginal`, `Glazecounts`,
`Glazevariable`. Run them separately, with no concurrent build/profile work.
The fixed-order mode only accepts the generated medium fixture's field order.

[Stored summaries, samples and profile attribution](../bench/comparison/results/2026-09-07-glaze/static-gap/).
The larger local investigation directory is
`hosq-yyjson-comparison/glaze-static-diagnosis/`, containing frozen sources,
`release.s`, symbolized profile XML, debug build commands and copied-header
experiments. It is not required to build the normal minimal benchmark suite.
