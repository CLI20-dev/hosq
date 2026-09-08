# InputView and reader specialization

This implementation replaces the experimental Fusion dispatcher with ordinary
pipeline execution and a small input customization point. Core changes for this
InputView direction are explicitly approved. The performance comparison remains
Fusion commit `11ff3a93ad0c8b574d6ceda5fda49f99d4ac3d58`.

## The two extension points

`ext::InputView<Input>` describes the input received by one pipeline step:

- `type<Step, Context>` is the input type used by type/storage analysis and
  const/mutable callability checks.
- `get<Step>(input, context)` supplies that input at execution time.
- The default preserves ordinary inputs. A Reader-specific specialization can
  retain an empty native tag for an accepting step, or materialize its ordinary
  View from Context for another step.
- A terminal scan also materializes its ordinary View, preserving the public
  return type and the Reader state previously observed at that boundary.

The pipeline executor does not merge or replace adjacent operations. The type
selection and actual input conversion use the same customization point. User
callbacks retain their normal calling conventions and const/mutable overload
selection; they are not forced inline by this mechanism.

`ext::EntryInput<Reader>` is an optional entry-start customization. Decoder calls
it with the compile-time key, value element, Reader and mutable state. Returning
false leaves ordinary entry decoding responsible for the input. Returning true
means the value pipeline has been executed; it is not the value pipeline's own
match result. This distinction prevents a failed user tail from being retried.

FastReader's specialization only accepts its built-in scalar scan as the first
operation and checks the complete pipeline's callability. It matches the member
prefix once, positions the existing cursor at the value, and invokes the same
pipeline with an empty `MemberInput<Reader, Key>` tag. Scan advances only the
tag's compile-time phase. The value decoder parses from that cursor and commits
the final token. A fast-path miss materializes the pending scalar and follows
the ordinary value decoder, preserving diagnostics.

## Reader-specific implementation

`ScalarInput`, `ArrayInput`, and `MemberInput` have no data members, pointers,
cursors or owned values. Runtime information stays in Context/Reader state.
The shared native adapter selects either the native input or its ordinary View;
there is no separate adapter body for each tag family.

Number, number_into, boolean, string_view and fixed numeric collect use ordinary
extension specializations. Numeric collect still specializes the exact built-in
numeric inner operation as one collect implementation; it does not install a
second pipeline executor. Custom derived steps do not inherit native opt-in by
accident: the self marker must identify the actual step type.

Pending member tags are created only by the entry adapter and scalar scan.
Reader advancement may use the mutable state owned by the entry adapter even
when the existing value pipeline selects const Context overloads. This is an
internal lifetime/call-chain contract, not permission to mutate genuinely const
Reader state. The implementation keeps this boundary local to the member tag.

The ordinary `InputView` interface contains no JSON token, delimiter or Reader
layout assumptions. `tests/test_input_view.cc` exercises a four-byte big-endian
word with an independent Context: the native consumer uses only an empty tag;
a generic consumer and terminal scan receive a byte span; storage analysis and
execution agree. This is a format-neutral extension test, not a production
binary-format parser or a claim of implementing every format.

## Validation and comparison

InputView baseline validation before bridge adoption on 2026-09-08:

- C++20 and C++26: 588/588 CTest cases passed, with warnings treated as errors.
- Focused fast_float-disabled build: 29/29 tests passed.
- The simple scalar-number InputView pipeline and direct Reader call produce
  identical assembly after local-label normalization (Clang 21, `-O3`). This
  establishes elimination for that probe, not every possible user pipeline.
- AC power, 100% battery; each case has 40 adjacent shuffled comparison groups.
- Fresh-context independent quality and measurement audits found no actionable
  defects; the measurement audit recomputed every median and verified the
  production-header and runner hashes.

| Input | InputView / Fusion median | Sample p10–p90 |
|---|---:|---:|
| small | 1.022× | 0.951–1.096× |
| medium | 1.042× | 0.970–1.102× |
| large | 1.038× | 0.984–1.078× |
| varied | 1.037× | 0.971–1.116× |
| pretty | 1.030× | 0.962–1.083× |
| numbers | 1.009× | 0.947–1.083× |

All six paired medians meet the 1.10× gate; the observed median overhead is
0.9–4.2%. Individual groups remain noisy, so this is an empirical acceptance
result for these cases rather than a guarantee for all inputs. Glaze still
leads in this run; adopting InputView does not establish Glaze parity.

Raw benchmark output is kept only under the ignored
`bench/comparison/results/reader-view-specialization-2026-09-08/inputview-final/`
directory and must not be committed.

The comparison uses the same six inputs, output work, compiler settings, and
separately namespaced Fusion/candidate translation units in a single process.
HOSQ consumes unpadded input. Glaze 8.3.0's mutable-input preparation is included
in its timed path. Setup checks decoded values; parse, materialization, postpass
and output destruction are timed. Builds and tests do not overlap timing.
Fusion objects are reused from the recorded `interleaved` build with identical
wrappers and flags; the final build recompiles candidate objects. The final
`commands.json` lists both sets of commands, while baseline compilation
provenance is retained in the earlier build logs and matching object hashes.

The adoption requirement is no more than 10% extra time relative to Fusion in
any case. Adjacent paired groups reduce load drift; sample quantiles describe
variation and are not confidence intervals. Old absolute timings from different
power/load conditions are not used as the acceptance baseline.


## Adopted Context bridge

The final implementation additionally marks Decoder::decode_nested, its five
existing decode_impl overloads, and both decode::ToImpl operators with the
existing HOSQ_ALWAYS_INLINE macro (eight additions). Context representation and
ownership are unchanged; no new executor or user-callback inline policy is added.
This exposes the nested built-in decode chain to the optimizer.

Adoption validation:

- C++20/C++26 full CTest: 588/588 passed.
- Focused fast_float-disabled build: 29/29 passed.
- Focused optimization-disabled build (-O0 -g0): 29/29 passed.
- Production-header hashes match the freshly built benchmark snapshot.
- Fresh independent code-quality and measurement audits passed. The latter
  verified typed-fixture ownership and recomputed all 12 sets of paired ratios.
- The two benchmark translation units' __text sections decreased from
  121,348 to 119,884 bytes and from 100,540 to 99,956 bytes versus prior InputView.
  These are complete benchmark-object sections, not library-only footprint claims.

The final comparison directly runs Fusion, prior InputView, and adopted InputView
in the same binary with distinct namespaces and independently prepared fixtures
of each implementation's correct type. This fixes cross-namespace fixture reuse
in the earlier context-study trial; those historical trial timings are superseded.
Each round has 40 shuffled adjacent groups; the second reverses case order.

| Input | Adopted / Fusion, round 1 | Round 2 | Adopted / prior InputView, round 1 | Round 2 |
|---|---:|---:|---:|---:|
| small | 1.019× | 0.995× | 0.991× | 0.964× |
| medium | 0.990× | 0.986× | 0.967× | 0.949× |
| large | 0.987× | 0.986× | 0.961× | 0.952× |
| varied | 0.983× | 0.974× | 0.959× | 0.962× |
| pretty | 0.978× | 1.005× | 0.966× | 0.991× |
| numbers | 1.017× | 0.975× | 0.982× | 0.970× |

Every per-case median in both rounds satisfies the <=1.10× Fusion gate.
The results support near parity with Fusion on these inputs. Group variation,
especially the first small-input round, is substantial; no universal speedup
or exact theoretical limit is claimed. Final raw CSVs, binary/source hashes,
commands and logs are under the ignored `bridge-final/` sibling of the earlier
results directory. Baseline objects retain their recorded earlier provenance;
only adopted objects are freshly compiled for this comparison.
