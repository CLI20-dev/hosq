# FastReader checkpoint

Checkpoint: 2026-09-08 implementation, committed as `18d94e8`.
The ideas below are not implemented or measured.

## Current flow

For an unordered selected member such as `"count":123`:

1. The object loop requests `next_member_or_end`. FastReader handles whitespace,
   separators, and the opening quote, leaving an unresolved key at the cursor.
2. meta-match narrows candidates and matches the key including its closing quote.
   The reader resolves the key token and advances past the quote.
3. The statically typed handler tries `EntryInput::decode_matched`. For an
   eligible scalar pipeline, FastReader checks the colon and scalar start, then
   passes an empty `MemberInput` tag with the cursor at the value.
4. `scan::scalar` changes the tag phase without scanning. The specialized value
   consumer parses directly from the cursor, commits the value token, cursor,
   separator expectation, and error state, then passes the value to storage.
5. Control returns through the handler and matcher to the object loop. The next
   transition checks the state and delimiter and prepares the next key.

These are logical stages, not necessarily surviving calls or memory writes.
The handler already carries a concrete lambda type; its inline annotation helps
expose the chain to the optimizer rather than removing function-pointer dispatch.

Unknown scalar members can remain inside FastReader when the fallback is the
exact supported built-in skip pair. Unknown containers and custom fallbacks use
the ordinary decoder path. Bool-returning Try value pipelines retain rollback.

## Next investigation

Consider delaying resolved key/value token materialization until an ordinary
consumer needs it, and reusing delimiter information already obtained during
value parsing. Container fallback may also be able to reuse matching information;
a previous key-end-cache-only trial did not improve performance.

Preserve custom-step observations, errors, final state, fallback, and rollback.
Do not introduce key-order assumptions into unordered selection. Verify changes
in generated assembly and compare identical inputs and outputs; removing source
statements alone does not demonstrate lower runtime cost.

## Retained evidence

The InputView implementation with the Context inline bridge met the Fusion
acceptance gate on six paired workloads: per-case median time ratios were
0.974–1.019 across two rounds, within the required 1.10 limit. This was an earlier
acceptance checkpoint, not a fresh comparison of every later optimization.

The final matched-member experiment used Clang 21 at `-O3`, AC power, Compact
inputs, two reordered rounds, and five repetitions per process. For unordered
Medium/5 fields, mean round-median CPU time fell from 280.217 to 240.359 µs
(14.2%); handler inlining alone took 262.435 µs. Glaze took 138.243 µs in that
session, leaving HOSQ about 1.74 times slower. Medium/id, medium/all, large/id,
large/all, and wide-first/id changed by less than 1% in either direction.
These observations do not establish universal parity or significant tiny gains.

Validation at this checkpoint: 307 tests under each of C++20 and C++26;
56,538 differential cases covering values, errors, cursors, tokens, and final
state; reversed and shuffled key-order digest checks. The tests were rerun before
the PR commit. Raw measurements and logs remain local under the ignored
`bench/comparison/results/` directory. They are not distributed with the source.
