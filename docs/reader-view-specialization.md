# FastReader and InputView

FastReader optimizations use reader-specific pipeline specializations. The core
executes the ordinary pipeline; it does not recognize and replace step sequences
with a separate fusion dispatcher.

## Extension contracts

`ext::InputView<Input>` defines `type<Step, Context>` for type/storage analysis
and `get<Step>(input, context)` for execution. Its default preserves the input.
A reader specialization can pass an empty native tag to a compatible step and
materialize the ordinary View for other consumers. Terminal scans also return
the ordinary View and preserve the observable reader state.

FastReader's `ScalarInput`, `ArrayInput`, and `MemberInput` tags contain no data,
pointers, or cursors. Runtime state remains in the existing Reader/Context.
Number, boolean, string-view, and fixed numeric collection consumers use normal
extension specializations. Custom steps retain their calling conventions and
const/mutable overload selection; derived steps do not implicitly opt in.

`ext::EntryInput<Reader>` optionally starts entry decoding through a native tag.
A return value of `true` means that it executed the value pipeline, not that the
pipeline itself returned success. A return value of `false` selects ordinary
decoding. The optional `decode_matched` hook reuses an already matched constant
key. FastReader uses this hook for eligible scalar pipelines; bool-returning
Try pipelines retain their existing checkpoint/rollback path.

The native scalar scan advances a compile-time tag phase. Its consumer parses
from the existing cursor and commits the resulting token. Unsupported consumers
materialize the ordinary View, preserving errors and observable state. InputView
contains no JSON-specific token or delimiter assumptions; `test_input_view.cc`
also exercises it with a four-byte big-endian input.

## Input options

```cpp
using namespace hosq::json;
using Automatic = FastReader<>;
using Pretty = FastReader<128, InputPadding::none, WhitespaceMode::Pretty>;
using Compact = FastReader<128, InputPadding::none, WhitespaceMode::Compact>;
using Padded = FastReader<128, InputPadding::zero_padded_8>;
```

The parameters select maximum depth, padding contract, and whitespace strategy.
The default accepts bounded input without padding or address-alignment promises.
Optional `zero_padded_4` and `zero_padded_8` require that many readable zero bytes
after the logical input, including for empty input. The view excludes those
bytes; the caller supplies their lifetime. FastReader does not check this
contract, allocate padding, or take ownership of the input.

All whitespace modes accept every JSON whitespace character. Compact favors
inputs without whitespace; Pretty groups runs of spaces. Auto skips leading
whitespace and chooses Pretty when the two bytes after the opening object or
array delimiter are whitespace; otherwise it chooses Compact. A mistaken format
prediction does not change which whitespace is accepted.

`ext::ReaderDispatch` selects a concrete reader once per Decoder scope. Nested
processing then uses that concrete type and shared state, with no stored mode or
indirect mode call. Custom reader-dependent steps must accept the concrete type.
For a low-level token loop, call `reader.with_reader(state, [&](auto* actual) {
/* run the complete loop using actual */ });`. The pointer is valid only within
the callback. Calling Auto's token methods directly retains local whitespace
handling instead of this scope-wide dispatch.

Scanning uses portable C++; compiler auto-vectorization is allowed. Floating
point conversion uses bundled fast_float in JSON mode by default. Benchmark
builds can select the standard-library fallback with
`-DCXX_HOSQ_USE_FAST_FLOAT=OFF`.

See the [checkpoint](reader-flow-checkpoint.md) for the current processing flow
and retained measurement summary, and the [benchmark README](../bench/comparison/README.md)
for reproducible comparisons.
