# HOSQ

HOSQ is a C++20 header-only library for decoding structured input into typed
schemas without first constructing a general-purpose DOM. Common
`hosq::types` describe integers, text, arrays, maps, and records once; JSON,
BSON, MessagePack, and TOML readers and writers handle their wire formats.
Custom scanning, decoding, validation, and storage pipelines remain available
when an application needs more control.

The public implementation is under `include/hosq`. Add `include` to your
compiler's header search path; the examples below use C++20.

- [Quick start](#quick-start)
- [Basic schemas](#basic-schemas)
- [Common types](#common-types)
- [Decoding complete documents](#decoding-complete-documents)
- [Encoding with the format writers](#encoding-with-the-format-writers)
- [Other input formats](#other-input-formats)
- [Advanced: custom pipelines](#advanced-custom-pipelines)
- [Tests](#tests)
- [Benchmarks](#benchmarks)

## Quick start

Use `hosq::types` for named fields and `hosq::types::value` for unkeyed
values, including the root `Record<Schema>`. Members may arrive in any order.
Ordinary entries are required; unknown or duplicate members are errors.

```cpp
#include <cassert>
#include <hosq/hosq.hh>
#include <hosq/json/fast_reader.hh>

namespace t = hosq::types;
namespace v = hosq::types::value;

struct User {
  t::Uint64<"id"> id;
  t::Text<"name"> name;
};

int main() {
  v::Record<User> user;
  const auto error = hosq::decode_document_into<hosq::json::FastReader<>>(
      user, R"({"name":"a\nb","id":42})");
  if (error) return 1;  // error.code, error.offset, and error.detail explain it.
  assert(*user.value().id == 42);
  assert(*user.value().name == "a\nb");
}
```

`Text` owns the decoded string and resolves escapes. `Uint64` checks the
integer category and range; it does not accept `42.0` or `"42"` as an integer.
The outer `.value()` accesses the record, then the entry, then its stored value.

## Basic schemas

Named aliases take the key first; their element arguments come from `v`.
Compose them for nested records, arrays, and dynamic maps.
`t::Optional<"nickname", v::Text>` combines a missing-member marker with
optional storage so absence can be observed.

```cpp
#include <cassert>
#include <hosq/hosq.hh>
#include <hosq/json/fast_reader.hh>

namespace t = hosq::types;
namespace v = hosq::types::value;

struct Address {
  t::Text<"city"> city;
};
struct Profile {
  t::Record<"address", Address> address;
  t::Array<"tags", v::Text> tags;
  t::Mapping<"scores", v::Text, v::Int64> scores;
  t::Optional<"nickname", v::Text> nickname;
};

int main() {
  v::Record<Profile> profile;
  const auto error = hosq::decode_document_into<hosq::json::FastReader<>>(
      profile, R"({"tags":["cpp"],"scores":{"tests":3},
                   "address":{"city":"Kyoto"}})");
  if (error) return 1;
  assert(*profile.value().address->city == "Kyoto");
  assert(profile.value().tags->at(0) == "cpp");
  const auto& scores = *profile.value().scores;
  assert(scores.keys().at(0) == "tests" && scores.values().at(0) == 3);
  assert(!*profile.value().nickname);
}
```

The optional nickname is omitted when this `Record` is encoded. A present
nickname must be text: `null` does not mean missing. Use `v::Null` when an
explicit null is part of the schema. Without optional storage, a missing
`t::Try<t::Field<..., E>>` in this `Record` keeps the default value of `E`
and encodes that value later. Each typed `Record` decode builds fresh schema
storage; the legacy in-place dispatch described below preserves prior values.

## Common types

`<hosq/types.hh>` (also included by `<hosq/hosq.hh>`) provides aliases
with storage independent of the reader. In the following table, aliases
are unkeyed `Elem` types in `v = hosq::types::value`. Use the same schema with
JSON, BSON, MessagePack, and TOML readers and writers; the codec determines the wire
representation and rejects values that its format cannot represent.

| Alias | Storage / meaning |
| --- | --- |
| `Int<T = std::int64_t>`, `Uint<T = std::uint64_t>` | Checked integer values; floating-point input is rejected even when integral |
| `Int8/16/32/64`, `Uint8/16/32/64` | Fixed-width signed and unsigned integers |
| `Float<T = double>`, `Float32`, `Float64` | Floating-point input, with checked range and narrowing |
| `Bool`, `Null` | `bool`, `std::nullptr_t`; strict scalar categories |
| `Text` | Owned `std::string`, with escapes resolved |
| `RawText` | Borrowed `RawTextValue`, with the source representation tagged |
| `Bytes` | Owned `BytesValue` payload and BSON binary subtype |
| `Array<E>` | `std::vector<E::storage_type>` |
| `Mapping<K, V>` | `hosq::Map` of parallel key/value vectors |
| `Record<S>`, `Object<S>` | A fixed object schema `S` of constant-key entries |
| `Optional<E>` | `std::optional<E::storage_type>`, for observable absence |
| `Instant` | `InstantValue`: Unix seconds and a nonnegative nanosecond remainder |
| `OffsetDateTime` | `OffsetDateTimeValue`: local date/time and offset in minutes |
| `LocalDateTime`, `LocalDate`, `LocalTime` | Corresponding `*Value` calendar fields, with no implicit time zone |
| `Decimal128` | `Decimal128Value`: 16 owned BSON wire bytes, without arithmetic or conversion to double |
| `ObjectId` | `ObjectIdValue`: 12 owned BSON wire bytes |
| `BsonTimestamp` | `BsonTimestampValue`: logical timestamp seconds and increment, distinct from `Instant` |
| `Extension` | `ExtensionValue`: signed MessagePack type tag and owned byte payload |

Use `v::Record<User>` or `v::Array<v::Uint64>` for root values. Named fields
are in `t = hosq::types`; every scalar and extended alias above has a
key-first counterpart, such as `t::Uint64<"id">`, `t::Text<"name">`, and
`t::Instant<"created_at">`. Generic numeric fields are `t::Int<"n", T>`,
`t::Uint<"n", T>`, and `t::Float<"n", T>`, with the same default storage types
as their unkeyed counterparts.

| Named alias | Meaning |
| --- | --- |
| `t::Array<"ids", v::Uint64>` | A named array |
| `t::Mapping<"scores", v::Text, v::Int64>` | A named dynamic map |
| `t::Record<"user", User>`, `t::Object<"user", User>` | Synonyms for a named fixed object |
| `t::Optional<"nickname", v::Text>` | `t::Try<t::Field<"nickname", v::Optional<v::Text>>>` |
| `t::Field<"n", E>` | Exactly `hosq::Entry<hosq::Key<"n">, E>` |

`t::Try` is a using declaration for the existing `hosq::Try` dispatch marker;
it adds no new matching or rollback policy. Low-level `Elem`, `Entry`, and
`Key` remain available unchanged. See [Advanced: custom pipelines](#advanced-custom-pipelines)
for the all-`Try` aggregate dispatch contract.

Numeric encoders pass their arithmetic storage directly to `encode::value`.
The existing `decode::number<T>` and `encode::number<T>` retain their previous
behavior: in particular, native
`number<int>` may accept an integral floating-point value and must not be used
as a strict integer category check.

`Float` accepts floating-point wire values only (JSON literals containing a
decimal point or exponent). The decoder interprets floating input as binary64;
conversion to a narrower type must preserve that binary value and signed zero.
For example, `Float32` accepts `1.5` but rejects binary64 `0.1` rather than
silently rounding it. NaN payload bits are not preserved. Decimal text is
rounded to binary64 during parsing, so this is not an arbitrary precision
decimal codec.

`Text` resolves JSON Unicode escapes and surrogate pairs and TOML string
escapes before storage; encoding escapes the semantic text once. `RawText`
borrows the caller's input, which must remain alive and unchanged for every
use of the value, including encoding. Its format tag distinguishes JSON
escaped string bodies, full TOML string literals, and semantic UTF-8 text;
the encoder resolves that representation before writing the destination
format. Copying the wrapper does not extend the input lifetime. Use `Text`
for ownership.

`Mapping` rejects duplicate decoded keys. `Record`/`Object` reject duplicate
and unknown members and require ordinary `Entry` fields. A `Try<Entry<...>>`
may be absent and keeps its fresh schema default; use optional storage when
absence must be observable. Explicit `Null`, a missing member, and an empty
optional are separate states. Within
`Record`/`Object`, an empty optional in a `Try<Entry<...>>` is omitted during
encoding. An empty optional elsewhere is an encoding error;
it is never implicitly encoded as null.

Mapping key comparisons resolve raw text, compare nested object members
independent of input order, and otherwise use value equality. Floating keys
follow C++ equality (`NaN` is not equal to itself). Duplicate detection is
quadratic in the member count; this is intended for typed schema data rather
than large indexed maps.

BSON and TOML integers must fit signed 64 bits, even when the C++ storage is
`Uint64`; MessagePack and JSON support the full `Uint64` range. JSON rejects
non-finite floating-point values. BSON roots must be documents and TOML roots
must be tables; JSON/BSON/TOML object keys must be strings (BSON keys also
exclude NUL). TOML has no null or binary value, and JSON has no binary value.
`Bytes` retains BSON subtypes, including the old subtype 2 wire length;
MessagePack can represent only generic binary (subtype 0).

### Extended values and format support

Extended aliases use the owned value structs from
[`extended_value_types.hh`](include/hosq/extended_value_types.hh). Their
representation is explicit; choosing a common alias does not imply that every
wire format can express it.

| Alias | JSON | BSON | MessagePack | TOML |
| --- | --- | --- | --- | --- |
| `Instant` | Unsupported | UTC datetime (exact int64 milliseconds) | Timestamp extension (nanoseconds) | Offset date-time; encoded as UTC |
| `OffsetDateTime` | Unsupported | Unsupported | Unsupported | Native offset date-time |
| `LocalDateTime`, `LocalDate`, `LocalTime` | Unsupported | Unsupported | Unsupported | Corresponding native date/time |
| `Decimal128`, `ObjectId`, `BsonTimestamp` | Unsupported | Corresponding BSON scalar | Unsupported | Unsupported |
| `Extension` | Unsupported | Unsupported | Signed tag and opaque payload | Unsupported |

`InstantValue` stores signed Unix seconds and `nanoseconds` in `[0, 10^9)`;
for example, one nanosecond before the epoch is `{-1, 999999999}`. Writers
reject non-normalized values. BSON cannot encode sub-millisecond instants or
values outside its signed 64-bit millisecond range. MessagePack supports its
32-, 64-, and 96-bit timestamp forms and emits the smallest applicable form.

TOML local values never acquire an implicit UTC offset. An `Instant` schema
explicitly converts a TOML offset date-time with a known offset to Unix time.
This conversion rejects local date/time, unknown `-00:00` offsets, and leap
seconds instead of discarding offset metadata or normalizing a leap second.
`OffsetDateTime` preserves the offset, including unknown `-00:00`, and civil
types preserve leap-second fields. Dates use the Gregorian calendar and years
0000 through 9999. Fractional seconds must be exactly representable in
nanoseconds; nonzero sub-nanosecond digits are rejected. Encoding an `Instant`
as TOML also requires its UTC date to fit those calendar bounds.

`Decimal128` preserves its 16 bytes, including special encodings, without
numeric coercion. `BsonTimestamp` is a logical timestamp, not BSON's UTC
datetime. `Extension` retains the signed tag and payload; reserved timestamp
tag `-1` still requires a valid timestamp payload. Use `Instant` for timestamp
semantics. Unsupported combinations report an error: JSON does not invent
strings or Extended JSON objects for any of these values. Other opaque BSON
scalars can still be validated and skipped. Format-specific shorthand aliases
are deliberately deferred.

## Decoding complete documents

Use `decode_document_into<R>` (or `Decoder<R>::decode_document`) to require
the schema to consume one complete document and reject trailing values.
Reader depth and format checks apply during decoding. A decode error may leave
partially updated storage; only use the result after checking
`error.is_ok()` or `!error`.

With the `User` schema and `user` variable from the quick start:

```cpp
const auto trailing = hosq::decode_document_into<hosq::json::FastReader<>>(
    user, R"({"id":42,"name":"Ada"} true)");
assert(trailing.code == hosq::ErrorCode::trailing_data);

const auto wrong_type = hosq::decode_document_into<hosq::json::FastReader<>>(
    user, R"({"id":42.0,"name":"Ada"})");
assert(wrong_type.code == hosq::ErrorCode::type_mismatch);
```

`decode_into` and `Decoder::decode_nested` retain their cursor contract. A
custom pipeline may observe or consume only part of the input; successful
partial decoding does not establish that the entire document is valid. See
[Advanced: custom pipelines](#advanced-custom-pipelines).

## Encoding with the format writers

The common aliases already contain encoder pipelines. Assign their stored
values, choose a writer, and call `encode_into`. This example writes
MessagePack and reads it back with the same schema:

```cpp
#include <cassert>
#include <string>
#include <hosq/hosq.hh>
#include <hosq/msgpack/reader.hh>
#include <hosq/msgpack/writer.hh>

namespace t = hosq::types;
namespace v = hosq::types::value;

struct User {
  t::Uint64<"id"> id;
  t::Text<"name"> name;
};

int main() {
  v::Record<User> user;
  *user.value().id = 42;
  *user.value().name = "Ada";
  std::string output;
  hosq::msgpack::Writer writer{output};
  if (!hosq::encode_into(user, writer)) {
    // Inspect writer.error(); discard partial output on failure.
    return 1;
  }
  v::Record<User> decoded;
  const auto error = hosq::decode_document_into<hosq::msgpack::Reader<>>(
      decoded, output);
  if (error) return 1;
  assert(*decoded.value().name == "Ada");
  assert(*decoded.value().id == 42);
}
```

Replace the writer with `bson::Writer`, `toml::Writer`, or
`json::CompactWriter` for the same schema. The writer headers are
`hosq/{msgpack,bson,toml}/writer.hh` and `hosq/json/compact_writer.hh`. Each
writer takes a `std::string&`, returns `std::string_view` from `output()`, and
implements `WriterConcept`. For BSON, MessagePack, and TOML,
`Writer<MaxDepth, MaxOutputBytes>` bounds depth and output size. Their default
depth is 64; BSON's default output limit is INT32_MAX, while MessagePack/TOML
default to the string's addressable size. Output limits include any bytes
already in the supplied string. JSON's `CompactWriter<MaxDepth>` has a default
depth of 1024 and no separate output-size template argument.

Supported common values are booleans, arithmetic values, semantic UTF-8
strings, explicit `nullptr`, `BytesValue`/`BytesView`, populated `optional`,
active `variant`, ranges (arrays), `Map` with equally sized key/value ranges,
and nested schemas, subject to each format's restrictions. The extended
values follow the [format support table](#extended-values-and-format-support).
A missing optional outside an omittable `Try` record member, a valueless
variant, an unsupported value, or `noop_encoder` produces an error. `Try`
alone controls decoding dispatch; omission requires the optional record member
described in [Basic schemas](#basic-schemas). Reader-only schemas must provide
encoder pipelines before they can be encoded.

MessagePack chooses the smallest integer and length/count representation;
float storage emits float32, double emits float64. Arrays/maps can be emitted
without knowing counts ahead of time: closing the container patches its header.
Map keys may be any supported value, including containers. Binary uses `bin`,
and `write_extension(int8_t, BytesView)` emits opaque extension data.
`v::Extension` owns extension payloads; `v::Instant` handles the
standard timestamp extension.

BSON requires a root document and string keys without NUL. Documents and arrays
patch their signed int32 byte lengths when closed; array indexes are generated
sequentially. Small integral storage emits int32; storage wider than 32 bits,
or unsigned values above INT32_MAX, emits int64. UINT64 values above INT64_MAX
are rejected. Floats emit IEEE binary64, with exact float32 widening. Null and
generic binary are supported. `Instant`, `ObjectId`, `BsonTimestamp`, and
`Decimal128` have native BSON codecs. Regex and code-with-scope have no
automatic C++ schema conversion or writer operation. Unsupported types are
not silently converted to strings or numbers.

TOML targets 1.0.0 and requires a root table. Each root member is one assignment,
keys are quoted and escaped, nested tables use inline tables, and arrays of
tables use arrays of inline tables. These forms preserve the same data values
as `[table]`/`[[table]]` declarations; source layout/comments are not preserved.
Floats retain a float spelling (e.g. `1.0`, `-0.0`, `inf`, `nan`); integers must
fit int64. Native calendar values and explicit `Instant` conversion follow
the format support table; null and binary are unsupported. Duplicate output
keys are errors. All three non-JSON writers reject invalid UTF-8 strings and
floating values that would lose precision when narrowed from long double to
binary64.

Writer strings are semantic characters. In particular, TOML's borrowed
`decode::string_view` remains raw: use `reader.read_string(state, std::string&)`
in a context-aware decoder step when storing values to re-encode with resolved
escapes. This keeps the existing borrowed-view API while enabling semantic
string round trips.

For manual emission, use `write_string`, `write_number`, `write_boolean`,
`write_null`, `write_binary`, plus structural `write(WriteRequest)` calls.
Low-level scalar reservations accept only semantic string bytes with context
`1`, followed by `commit(written)`; numbers use the typed methods. Output may
be partial after any error and must not be treated as a complete document.

## Other input formats

The following C++20 readers implement the same token, checkpoint, and schema
API as the JSON readers:

| Header | Reader | Input |
| --- | --- | --- |
| `hosq/msgpack/reader.hh` | `hosq::msgpack::Reader<>` | `std::string_view` of binary bytes |
| `hosq/bson/reader.hh` | `hosq::bson::Reader<>` | `std::string_view` of binary bytes |
| `hosq/toml/reader.hh` | `hosq::toml::Reader<>` | UTF-8 `std::string_view` |

For example, replace `json::FastReader<>` in `decode_document_into` with
`msgpack::Reader<>` to decode a MessagePack object into the same schema.
The optional template argument limits container depth (default: 64).
`v::Text` and `v::Bytes` own their data. Borrowed `RawText` and legacy
`string_view` values require the input to stay alive and unchanged.
`construct<std::string>` also provides owned string storage. As with the existing
JSON readers, the legacy `decode::string_view` exposes raw input without
resolving escapes; use `v::Text` for owned semantic text.

MessagePack supports all integer widths, IEEE floats, booleans, nil, strings,
arrays, maps (including non-string and container keys), binary payloads, and
opaque extensions. Constant object schemas require string keys and report a
mapping error for non-string keys. `decode::skip` can traverse all map key
shapes. `v::Bytes` handles binary values, `v::Extension` preserves
extension tags and payloads, and `v::Instant` handles timestamps.

BSON targets specification **1.1** and supports documents, arrays with
sequential decimal index keys, strings, int32/int64, doubles, booleans, null,
binary, and native BSON scalar types
(ObjectId, date, timestamp, decimal128, regex, code, symbol, DBPointer,
undefined, MinKey, MaxKey, code-with-scope). Document and payload lengths,
terminators, booleans, old binary lengths, and UTF-8 strings/keys are checked.
Code-with-scope additionally validates the code string, total size, and entire
scope document under the same depth limit. Common aliases decode and encode
ObjectId, UTC datetime, logical timestamp, and Decimal128. Other opaque BSON
values can be validated and skipped.

TOML targets **1.0.0**, including dotted assignments, bare/quoted/escaped keys,
all four string syntaxes, booleans, decimal/base-prefixed signed 64-bit integers,
floats (`inf`/`nan` included), nested arrays, inline tables, out-of-order table
headers, implicit parent tables, and arrays of tables. A structural index
reconciles table declarations before token traversal; it retains input spans
rather than constructing a general-purpose value DOM. Checkpoints only save
the cursor and current token. Duplicate/redefined/sealed table conflicts are
rejected according to TOML 1.0 rules.

Date/time tokens are syntactically validated (including calendar bounds).
The extended aliases decode native calendar fields or explicitly convert an
offset date-time to `Instant`; ordinary number, boolean, and string decoders
reject date/time tokens. Values can also be skipped or accessed as raw token
spans. Constant schema keys compare decoded TOML key names.
`decode::string_view` still returns borrowed raw key/value bytes, so escaped
dynamic keys, string escapes and multiline line continuations are not resolved
by that API. Multiline opening newlines and delimiters are removed from its
span. For semantic owned strings, `reader.read_string(state, output)` fills a
`std::string`, resolving escapes and multiline continuations (multiline CRLF
normalizes to LF). It also returns decoded dynamic key names and can be called
from a context-aware pipeline step returning `std::string`. Owned results
survive both the reader state and the input.

Wire syntax follows the primary specifications for
[MessagePack](https://github.com/msgpack/msgpack/blob/master/spec.md),
[BSON](https://bsonspec.org/spec.html), and
[TOML 1.0](https://toml.io/en/v1.0.0).

## Advanced: custom pipelines

Use `Elem<decoder, encoder>` to customize scanning, conversion, matching,
validation, and storage. The common aliases are compositions of these same
steps; most schemas can start with aliases and introduce custom elements only
where needed.

The following hand-written pipelines preserve the legacy `decode::number<T>`,
`decode::string_view`, and `decode::boolean` behavior. Native `Number<int>` may
accept an integral floating-point value; JSON `FastReader` rejects a floating
literal for it. Strict `v::Int<int>` is therefore not a drop-in replacement.
The legacy `String` copies raw string bytes without resolving escapes.
`decode_into` here uses the partial cursor API.

```cpp
#include <cassert>
#include <string>
#include <hosq/hosq.hh>
#include <hosq/json/fast_reader.hh>
#include <hosq/msgpack/writer.hh>

using namespace hosq;
namespace t = hosq::types;

template <class T>
using Number = Elem<scan::scalar | decode::number<T> | store::assign,
                    encode::value>;
using String = Elem<scan::scalar | decode::string_view | construct<std::string> |
                        store::once,
                    encode::value>;
using Boolean = Elem<scan::scalar | decode::boolean | store::assign,
                     encode::value>;

struct CustomRecord {
  t::Try<t::Field<"n", Number<int>>> n;
  t::Try<t::Field<"name", String>> name;
  t::Try<t::Field<"ok", Boolean>> ok;
};

int main() {
  CustomRecord record;
  const auto error = decode_into<json::FastReader<>>(
      record, R"({"ok":true,"name":"Ada","n":42})");
  if (error) return 1;
  assert(*record.n == 42);
  assert(*record.name == "Ada");
  assert(*record.ok);
  std::string output;
  msgpack::Writer writer{output};
  if (!encode_into(record, writer)) return 1;
}
```

`Try<E>` participates in dispatch when every field in an aggregate or tuple
is a `Try` wrapper. All-`Try` entry tuples dispatch object members by key;
missing members preserve their existing storage. Unknown members fail unless
a pipeline explicitly handles them. Duplicate handling depends on the
pipelines: for example, `store::assign` overwrites while `store::once` can reject
a repeat. This differs from the closed, duplicate-rejecting `v::Record<S>`.

All-`Try` element tuples dispatch array elements. Boolean-returning candidates
are tried left to right; `false` without a reader error permits the next
candidate. A reported reader error stops dispatch and is returned with its
original code, offset, and detail. A void-returning
typed decoder claims the element and reports failures through the reader's
sticky error, so it does not provide semantic-alternative fallback. A failed
candidate does not roll back storage, sticky errors, or the full reader state;
reader checkpoints only restore the cursor/token state they capture. `Try` is
not a standalone root schema, and this dispatch contract does not make
`v::Array<Try<E>>` supported.

Encoding a legacy all-`Try` aggregate writes every field, including fields
missing during decoding; `Try` does not track presence or choose an alternative
to emit. Empty optionals in this aggregate are errors. The omission described
in [Basic schemas](#basic-schemas) belongs specifically to `v::Record`/`v::Object`
and their optional `Try` members.

`encode::value` is a terminal encoder for semantic storage values. It uses
`ext::EncodeValue<W>` and writer-specific scalar operations, while `Encoder`
traverses `Elem`, `Entry`, `Try`, and aggregate/tuple schemas. Pass numeric
storage directly: formatting a number into a string first intentionally
encodes a string value. Existing text adapters (`encode::number`,
`encode::boolean`, `encode::string_view`) remain available. A decoder-only
custom element can use `noop_encoder`, but it must gain an encoder pipeline
before it can be encoded.

## Tests

```sh
cmake -S . -B build -DCXX_HOSQ_ENABLE_TEST=ON
cmake --build build --target hosq_tests_cxx20
ctest --test-dir build
```

### TOML corpus verification

The reproducible checker in [`tests/support`](tests/support) uses
[`toml-lang/toml-test`](https://github.com/toml-lang/toml-test), pinned for this
validation to `ff49d109861c1ad25af53f687f2aef19ab650600`.
It checks the TOML 1.0 list: invalid rejection, valid acceptance, decoded key
and container/type structure, and string/integer/float/boolean values
against golden JSON. String comparisons use the owned `read_string` API.
This corpus checker does not compare decoded calendar field values; those
codecs have separate unit tests. This is not a claim of full calendar-value
conformance.

With that corpus checked out in `/tmp/hosq-toml-test`:

```sh
nix develop --command clang++ -std=c++20 -O1 -Iinclude \
  tests/support/toml_conformance.cpp -o build/toml_conformance
python3 tests/support/check_toml_conformance.py \
  build/toml_conformance /tmp/hosq-toml-test
```

### Independent writer checks

[`tests/support/writer_interop.cpp`](tests/support/writer_interop.cpp)
generates synthetic fixtures. The companion
[`check_writer_interop.py`](tests/support/check_writer_interop.py) compares exact MessagePack/BSON bytes and decoded
values with Python `msgpack` and PyMongo, and checks TOML/JSON values with
`tomllib`/`json`, including keys, Unicode/control escapes, nested tables/table
arrays, numeric boundaries, float types and negative zero. It is separate from
reader/writer round-trip unit tests.

```sh
nix develop --command clang++ -std=c++20 -O1 -Iinclude \
  tests/support/writer_interop.cpp -o build/writer_interop
nix shell --impure --expr '
  let f = builtins.getFlake (toString ./.);
      pkgs = import f.inputs.nixpkgs { system = builtins.currentSystem; };
  in pkgs.python3.withPackages (ps: [ ps.msgpack ps.pymongo ])
' --command python3 tests/support/check_writer_interop.py build/writer_interop
```

This uses a temporary environment from the existing flake's Nixpkgs and does
not install anything into a global profile.

## File smoke tests and fetched conformance corpus

The normal CTest suite includes small fixed JSON, BSON, MessagePack and TOML
files in [`tests/fixtures`](tests/fixtures/README.md), including malformed inputs. Each valid file is decoded
into the same schema, checked against known values, encoded and decoded again.
These complement the existing typed-value encode/decode tests and wire-specific
boundary tests. Semantic equality is checked rather than requiring the same
formatting or integer-width choices on re-encoding. The fixtures are hand-authored
or constructed directly from the wire specifications, not generated by HOSQ.

For the official TOML 1.0 corpus and independent writer checks, use:

```sh
nix develop path:.#conformance --command cmake -S . -B build-conformance -DCXX_HOSQ_ENABLE_TEST=ON
nix develop path:.#conformance --command cmake --build build-conformance -j4
nix develop path:.#conformance --command ctest --test-dir build-conformance --output-on-failure
nix build path:.#checks.aarch64-darwin.format-conformance
```

Replace `aarch64-darwin` with the current Nix system on other hosts. The last
command runs the TOML corpus checker and independent MessagePack/BSON/TOML/JSON
writer checker; it is also available through `nix flake check path:.`.
`path:.` includes local untracked implementation files during development.

Nix fetches `toml-lang/toml-test` at the revision and content hash in [`flake.nix`](flake.nix)
into its store. No corpus files, Git history, global-profile installs or test-time
network downloads are needed. The first Nix setup needs network access; later
runs reuse the store. Update both the revision and hash intentionally when
updating the corpus. Plain CMake remains offline and dependency-light; the TOML
corpus test is added only when `HOSQ_TOML_CORPUS` is supplied as an environment
variable or CMake cache path. The conformance shell sets it automatically.

Only TOML currently uses an upstream official corpus. BSON and MessagePack use
the local file fixtures and independent codec checks; this does not claim full
upstream corpus coverage for those formats.

## Benchmarks

The maintained HOSQ/Glaze/yyjson comparisons are in
[`bench/comparison`](bench/comparison/README.md). Each case has its own source
file and Google Benchmark executable; the HOSQ schema is visible in that case
file.
