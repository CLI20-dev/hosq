# Optional zero-padded FastReader

> Historical measurements below include the former NEON FastReader path.
> FastReader now uses a portable C++ scanner without ISA intrinsics; see
> [Portable scanner baseline](reader-no-neon.md).

The input contract is selected by the Reader type. Existing `FastReader<>` and
`FastReader<MaxDepth>` remain bounded readers of ordinary `std::string_view`.

```cpp
using Bounded = hosq::json::FastReader<>;
using Padded4 = hosq::json::FastReader<128,
    hosq::json::InputPadding::zero_padded_4>;
using Padded8 = hosq::json::FastReader<128,
    hosq::json::InputPadding::zero_padded_8>;
```

Padded modes require respectively four or eight **readable zero bytes directly
after the logical input**. This applies even to empty input. The view length
excludes padding. Address alignment is not required. Reader does not allocate,
copy, mutate or own the input. The caller keeps the input and padding alive.
The contract is not checked at runtime; selecting a padded mode is its guarantee.

```cpp
#include <array>
#include <hosq/json/fast_reader.hh>

using R = hosq::json::FastReader<128,
    hosq::json::InputPadding::zero_padded_8>;
std::array<char, 15> input{'[', '1', ',', '2', ',', '3', ']'};
// Remaining eight bytes are zero-initialized.
R reader;
R::state_type state;
reader.reset(state, std::string_view(input.data(), 7));
```

Use the same Reader type in `decode_into<R>` or existing custom pipelines. The
ReaderTraits, ScanScalar and DecodeSkipValue specializations include the padding
parameter, retaining fusion and format metadata. State size is unchanged.
`R::input_padding` and `R::padding_bytes` are compile-time properties.

The 4-byte mode removes per-byte end comparisons from whitespace, literal and
integer-tail scans. A zero byte terminates those scans at the logical end. The
8-byte mode additionally removes the remaining-length test from the grouped
8-digit integer scan: a block intersecting zero padding cannot be all digits.
Overflow still falls back to the original bounded conversion. String scanning
retains its block bounds and escape rules.

We also tested 16-byte padding and unconditional NEON string loads. It made
Twitter slower in the same-process comparison, so it was not retained. The
8-byte mode targets the profitable integer block check without that string
change. More padding is not automatically faster.

Validation covers arbitrary bytes, malformed input, signed integer overflow,
pipeline fusion, token/error equivalence, and guard pages immediately following
exactly four/eight padding bytes. Each variant is compared with the bounded
reader. 258 tests pass in C++20/C++26 with NEON/fast_float and C++20 with the
portable scanner/default numeric backend.

Final median comparison (microseconds, bounded / 4-byte / 8-byte / yyjson):
Twitter 291.27 / 273.62 / 277.75 / 168.02;
UInt64 929.37 / 819.23 / 783.45 / 737.72;
Double 977.12 / 978.82 / 982.77 / 608.69.
Input buffers are prepared before timing; nine randomized repetitions with 0.25 s minimum.
