# HOSQ

HOSQ is a C++20 header-only library for decoding structured input into typed
schemas without first constructing a general-purpose DOM. Common
`hosq::types` describe integers, strings, arrays, and maps once; JSON,
BSON, MessagePack, and TOML readers and writers handle their wire formats.
Custom scanning, decoding, validation, and storage pipelines remain available
when an application needs more control.

The public implementation is under `include/hosq`. Add `include` to your
compiler's header search path; the examples below use C++20.

- [Quick start](#quick-start)
- [Basic schemas](#basic-schemas)
- [Common types](#common-types)
- [Decoding complete documents](#decoding-complete-documents)
- [Error locations](#error-locations)
- [Encoding with the format writers](#encoding-with-the-format-writers)
- [Other input formats](#other-input-formats)
- [Advanced: custom pipelines](#advanced-custom-pipelines)
- [Tests](#tests)
- [Benchmarks](#benchmarks)

## Quick start

Use `hosq::types` for named fields and `hosq::types::value` for unkeyed
values. A structure of ordinary entries can be decoded directly. This traversal
reads members in declaration order; see [Advanced: custom pipelines](#advanced-custom-pipelines)
for key-based Try dispatch and its missing-member behavior.

```cpp
#include <cassert>
#include <hosq/hosq.hh>
#include <hosq/json/fast_reader.hh>

namespace t = hosq::types;

struct User {
  t::Uint64<"id"> id;
  t::String<"name"> name;
};

int main() {
  User user;
  const auto error = hosq::decode_document_into<hosq::json::FastReader<>>(
      user, R"({"id":42,"name":"a\nb"})");
  if (error) return 1;  // Inspect error.code, offset, detail, and path.
  assert(user.id.value().value() == 42);
  assert(user.name.value().value() == "a\nb");
}
```

`String` owns the decoded string and resolves escapes through its composed
pipeline. `Uint64` checks the integer category and range; it does not accept
`42.0` or `"42"` as an integer. An entry's first `.value()` accesses its Elem;
the second accesses the stored C++ value.

## Basic schemas

Named aliases take the key first; their element arguments come from `v`.
An `Entry` also accepts an existing aggregate schema directly as its key or
value. For nested objects, it connects that schema to the ordinary
`scan::object | decode::to<S> | store::assign` and `encode::value` pipelines.

```cpp
#include <cassert>
#include <hosq/hosq.hh>
#include <hosq/json/fast_reader.hh>

namespace t = hosq::types;
namespace v = hosq::types::value;

struct Address {
  t::String<"city"> city;
};
struct Profile {
  hosq::Entry<hosq::Key<"address">, Address> address;
  t::Array<"tags", v::String> tags;
  t::Mapping<"scores", v::String, v::Int64> scores;
};

int main() {
  Profile profile;
  const auto error = hosq::decode_document_into<hosq::json::FastReader<>>(
      profile, R"({"address":{"city":"Kyoto"},
                   "tags":["cpp"],"scores":{"tests":3}})");
  if (error) return 1;
  assert(profile.address.value().value().city.value().value() == "Kyoto");
  assert(profile.tags.value().value().at(0) == "cpp");
  const auto& scores = profile.scores.value().value();
  assert(scores.entries().at(0).first == "tests" &&
         scores.entries().at(0).second == 3);
}
```

The first `.value()` still returns the Elem used by the Entry; the second
returns the stored `Address`. Existing Elem arguments retain their exact
types. An array-shaped aggregate schema uses `scan::array` instead. Accepted
schemas follow the existing Decoder contract: their fields must be Elems or
Entries, or an aggregate with actual Try dispatch. Plain C++ data structures
such as `struct { int n; }` do not acquire automatic parsing rules.

For example, `Entry<Address, v::Int64>` uses an object as its key. MessagePack
can represent such keys; JSON, BSON and TOML retain their string-key
restrictions. All traversal, validation and matching behavior comes from the
existing schema and pipeline steps. Entry adds no unknown-key, duplicate-key,
missing-field or omission policy.

To collect an array of structures, compose the traversal directly:

```cpp
using Users = hosq::Elem<
    hosq::scan::array
    | hosq::decode::collect<hosq::scan::object | hosq::decode::to<User>>
    | hosq::store::assign,
    hosq::encode::value>;
```

Its storage is `std::vector<User>`. The nested `User` keeps the decoding and
encoding rules of its existing schema; wrapping it does not select additional
unknown-key, duplicate-key, missing-field, or omission policies.

`Constrained` composes the existing `validate` rules with a value alias. Reuse
the resulting value type in fields and containers, or use the key-first alias:

```cpp
namespace check = hosq::validate;
constexpr auto even = [](const auto& n) { return n % 2 == 0; };
using Age = v::Constrained<v::Int8,
                          check::range<0, 120> | check::predicate<even>>;
using Name = v::Constrained<v::String, check::non_empty>;
using Code = v::Constrained<v::String, check::size<3>>;
struct ValidatedUser {
  t::Field<"age", Age> age;
  t::Constrained<"name", v::String, check::non_empty> name;
  t::Constrained<"code", v::String, check::size<3>> code;
  t::Array<"children", Age> children;
};
```

These remain exact `Elem`/`Entry` aliases with the original storage and strict
scalar categories (`Age` still rejects `42.0`). Accepted rules are `range`,
`min`, `max`, `non_empty`, `size`, `size_range`, and `predicate`, composed with
`|`. Rules must accept const storage; transforming or context-dependent
`validate::custom` steps are not accepted. String size counts decoded UTF-8
bytes. Predicates run at decode and encode, so use stable, side-effect-free
predicates.

Numeric rules keep their existing comparison semantics: native floating-point
NaN passes `range`/`min`/`max` because it compares neither below nor above a
bound. For finite bounds, include `<cmath>` and compose an explicit predicate:
`check::range<-2, 2> | check::predicate<[](double n) { return std::isfinite(n); }>`.

Constraints check stored values after successful decoding and before the
underlying encoder runs, stopping at the first failed rule. Failed validation
is not transactional: newly decoded storage may remain available. Default
construction and mutable `.value()`/`*` access are unchanged; encoding rechecks
them and rejects invalid values. An
invalid standalone value writes no payload; an enclosing object or array may
already have written a prefix when a child fails. Decode errors retain the
existing validation codes and details. Built-in writers retain those codes and
details on encode as well. Check the error/result and discard failed output.

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
| `String` | Owned `std::string`, with escapes resolved |
| `Bytes` | Owned `BytesValue` payload and BSON binary subtype |
| `Array<E>` | `std::vector<E::storage_type>` |
| `Mapping<K, V>` | `hosq::MappingValue<K::storage_type, V::storage_type>` with paired entries |
| `Optional<E>` | `std::optional<E::storage_type>`, for observable absence |
| `Instant` | `InstantValue`: Unix seconds and a nonnegative nanosecond remainder |
| `OffsetDateTime` | `OffsetDateTimeValue`: local date/time and offset in minutes |
| `LocalDateTime`, `LocalDate`, `LocalTime` | Corresponding `*Value` calendar fields, with no implicit time zone |
| `Decimal128` | `Decimal128Value`: 16 owned BSON wire bytes, without arithmetic or conversion to double |
| `ObjectId` | `ObjectIdValue`: 12 owned BSON wire bytes |
| `BsonTimestamp` | `BsonTimestampValue`: logical timestamp seconds and increment, distinct from `Instant` |
| `Extension` | `ExtensionValue`: signed MessagePack type tag and owned byte payload |

Use a structure directly, or an Elem such as `v::Array<v::Uint64>`, at the root. Named fields
are in `t = hosq::types`; every scalar and extended alias above has a
key-first counterpart, such as `t::Uint64<"id">`, `t::String<"name">`, and
`t::Instant<"created_at">`. Generic numeric fields are `t::Int<"n", T>`,
`t::Uint<"n", T>`, and `t::Float<"n", T>`, with the same default storage types
as their unkeyed counterparts.

| Named alias | Meaning |
| --- | --- |
| `t::Array<"ids", v::Uint64>` | A named array |
| `t::Mapping<"scores", v::String, v::Int64>` | A named dynamic map |
| `t::Optional<"nickname", v::String>` | `t::Field<"nickname", v::Optional<v::String>, t::presence::Optional>` |
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

`String` is an alias for an ordinary `Elem` pipeline with `std::string`
storage. Its convenience behavior comes from independently composable steps:

```cpp
using String = hosq::Elem<
    hosq::scan::scalar
    | hosq::decode::string_source
    | hosq::construct<std::string>
    | hosq::decode::json_unescape
    | hosq::decode::toml_unescape
    | hosq::validate::utf8
    | hosq::store::assign,
    hosq::encode::value>;
```

`decode::string_source` checks the string category and returns a borrowed
`std::string_view`: a JSON string body, a complete quoted TOML literal (or a
bare key), or a BSON/MessagePack string payload. The input must remain alive and
unchanged while that view is used. `construct<std::string>` makes an owned copy;
it performs no unescaping or character encoding conversion.

`decode::json_unescape` resolves JSON escapes and UTF-16 surrogate pairs only
for JSON Readers; it passes strings through unchanged for other Readers.
`decode::toml_unescape` independently handles TOML quotation, escapes, multiline
newlines and continuations, and is a no-op for other Readers. Each conversion
processes its pipeline input. `validate::utf8` is a separate validation step.
To retain source bytes, omit those conversions and validation; to borrow them,
also omit `construct<std::string>`. Storage types themselves do not perform
these transformations. `encode::value` escapes stored string content once for
the destination format.

`Mapping` rejects duplicate decoded keys. Its `MappingValue` storage owns an
ordered `std::vector<std::pair<K, V>>`: use `entries()` to inspect, erase or
reorder pairs and `emplace_back(key, value)` to append. These operations preserve
key/value correspondence. The legacy parallel-vector `Map` remains available
for low-level pipeline scratch storage.

Fields use `presence::Required` by default. `t::Optional<K, E>` combines
optional presence with `std::optional` storage. In the key-based aggregate
traversal, every field must be a Try candidate or an optional-presence field;
a missing member preserves its existing storage. A present member must decode
as `E`: `null` does not become absence unless `E` explicitly accepts it.

Encoding writes the aggregate's fields. An empty optional has no encoded value
and is an error; it is not automatically omitted or replaced with null.
Explicit `Null`, a missing member, and an empty optional remain separate
states. To control omission, supply an explicit encoder pipeline.

Mapping key comparisons use decoded values, compare nested object members
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

## Error locations

`Error::path` is an owning `ErrorPath` (`std::vector<PathSegment>`), ordered
from the input root to the byte position in `Error::offset`. `code`, `offset`,
and `detail` retain their existing meanings. Existing positional and designated
`Error` initializers remain valid; their path defaults to empty.

Internally, decoding propagates a code-only status. Built-in Reader and Writer
states keep the original byte offset and detail without owning a path. The
public `Error` is assembled at the API boundary; its name and existing fields
are unchanged. The original custom Reader/Writer protocols remain supported.

Successful decoding does not collect paths. After a failure, a separate Reader
walks the input to locate the offset. The schema and its user pipeline are not
executed again, and the original Reader's cursor and error remain unchanged.
JSON diagnostics use `SimpleReader`; other formats use a fresh Reader of the
same type. Diagnosis requires an independently constructible Reader/state and
an input reconstructible from the Reader's byte view. No Reader protocol
methods are added.

```cpp
const auto error = hosq::decode_document_into<hosq::json::FastReader<>>(
    user, R"({"id":42.0,"name":"Ada"})");
assert(error.path[0] == hosq::PathSegment::key("id"));

// Skip the diagnostic traversal even when decoding fails.
const auto plain = hosq::decode_document_into<hosq::json::FastReader<>>(
    user, R"({"id":42.0,"name":"Ada"})",
    hosq::DecodeOptions{.error_path = false});
assert(plain.path.empty());
```

`DecodeOptions` is also accepted by `decode_into`, `Decoder::decode`,
`decode_document`, and `decode_nested`. Built-in Readers retain only code,
offset, and detail when `report_error` is called; a supplied public path is not
an internal error carrier. Prefailed Readers return immediately without a
diagnostic pass. A custom Reader's existing nonempty public path is preserved.

Arrays use `PathSegment::element(index)`. Objects use `PathSegment::key(name,
ordinal)`, with the input's zero-based member ordinal. The path is identical
whether an object is decoded as an Entry schema, Mapping, or custom pipeline.
A key transformed from `"abc"` to `"ABC"` by a pipeline still appears as `"abc"`
in the diagnostic. String key escapes are decoded by the Reader's scalar
codec; names own their bytes, and punctuation is literal, not a separator.
Non-text keys, unsupported key codecs, and noncopyable Reader states use
`key_at(ordinal)` without stringifying values. Empty text keys have
`has_name == true`. `PathSegment::field` remains available for explicitly
supplied paths; the input traversal emits key and index segments.

For example, a nested error can produce `{PathSegment::key("items"),
PathSegment::element(1), PathSegment::key("a/b"), PathSegment::key("count")}`.
Returned paths survive input destruction, decoder reuse, copying, and moving.
Only key names and array/member positions are retained, never value payloads.

Paths are best effort: an unlocatable offset, a Reader reset failure, an error
at the root, or an error after the root can leave the path empty. A syntax
failure can identify only the position reached by the diagnostic Reader.
The offset is authoritative even for user-reported errors; an offset outside
the input does not acquire a schema-derived path. Directly invoking a pipeline
with a `DecodeContext` does not add a diagnostic pass. Custom Reader extensions
used during replay must support independent state; scalar codec hooks can be
called to interpret keys. Encoding does not diagnose paths automatically.

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
  t::String<"name"> name;
};

int main() {
  User user;
  user.id.value().value() = 42;
  user.name.value().value() = "Ada";
  std::string output;
  hosq::msgpack::Writer writer{output};
  if (!hosq::encode_into(user, writer)) {
    // Inspect writer.error(); discard partial output on failure.
    return 1;
  }
  User decoded;
  const auto error = hosq::decode_document_into<hosq::msgpack::Reader<>>(
      decoded, output);
  if (error) return 1;
  assert(decoded.name.value().value() == "Ada");
  assert(decoded.id.value().value() == 42);
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
An empty optional, a valueless variant, an unsupported value, or `noop_encoder`
produces an error. `Try` controls decoding dispatch and does not provide an
encoding omission policy. Reader-only schemas must provide encoder pipelines
before they can be encoded.

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
`v::String` and `v::Bytes` own their data. Borrowed `string_view` values require
the input to stay alive and unchanged.
`construct<std::string>` also provides owned string storage. As with the existing
JSON readers, the legacy `decode::string_view` exposes raw input without
resolving escapes; use `v::String` for owned semantic text.

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
#include <hosq/json/simple_reader.hh>
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
  assert(record.n.value().value() == 42);
  assert(record.name.value().value() == "Ada");
  assert(record.ok.value().value());
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
a repeat.

All-`Try` element tuples dispatch array elements. Matching candidates
are tried left to right; an unmatched `MatchResult` without a reader error
restores the captured reader position before trying the next candidate. A
reported reader error stops dispatch without rollback and is returned with its
original code, offset, and detail. A void-returning
typed decoder claims the element and reports failures through the reader's
sticky error, so it does not provide semantic-alternative fallback. On an
error-free rejection, reader checkpoints restore only the parsing state they
capture. Storage and callback side effects are never rolled back by `Try`.
`Try` is not a standalone root schema, and this dispatch contract does not make
`v::Array<Try<E>>` supported.

`Decoder<R>::decode_nested(value, reader, state)` returns an existing Reader
error immediately, before dispatching or decoding any schema. It preserves
the error's code, offset and detail, the Reader position, and the destination
storage. This check happens when decoding starts; it does not interrupt the
remaining steps of a pipeline that has already started.

To start reading a new input with the same Reader and state, call
`reader.reset(state, input)`. Reset discards the previous error and parsing
state; invalid new input may produce a new error. `Decoder::decode` and
`Decoder::decode_document` reset their internal Reader for each new input, as
do the `decode_into` and `decode_document_into` convenience functions.

Encoding an all-`Try` aggregate writes every field, including fields missing
during decoding; `Try` does not track presence or choose an alternative to
emit. Empty optionals in this aggregate are errors.

Matching is a distinct pipeline effect. `match::literal<V>` and
`match::predicate<F>` produce library-controlled `MatchResult<T>` values;
users cannot construct these results directly or implicitly convert them to
bool. `if (result)` and `result.matched()` inspect the status. A predicate must
return exactly bool; value/context and context-only signatures are supported.
The input payload continues after a successful match, and the final pipeline
result retains the matching type even after `store::assign`.

```cpp
using Odd = Elem<scan::scalar | decode::integer<int> |
                     match::predicate<[](int n) { return n % 2 != 0; }> |
                     store::assign,
                 encode::value>;
struct Choices {
  Try<Odd> odd;
  Try<types::value::Int64> fallback;
};
Choices choices;
auto error = decode_into<json::SimpleReader>(choices, "[1,2]");
// choices.odd.value() == 1; choices.fallback.value() == 2
```

A matching Elem is rejected at compile time as a direct root, an ordinary
fixed tuple/member, or a typed Array/Mapping/Optional value. Both
`decode_into` and `decode_document_into` enforce this restriction. Use a
validation step (for example `validate::match<1>`) for a required value that
must report a validation error, and Try for candidate selection. Field
presence alone does not enable candidate selection.

`decode::each` and `decode::collect` preserve matching through array/object
traversal. If a collect operand returns `MatchResult<T>`, the collected values
contain `T`, and collect returns a `MatchResult` wrapping the whole collection.
For object collection, either the key or value pipeline can introduce matching;
fixed-size collections propagate it too. A mismatch rejects the whole candidate
so the surrounding Try can retry from its checkpoint. Recorded Reader errors,
including fixed-size length errors, terminate decoding without rollback.

```cpp
using Ones = Elem<
    scan::array
    | decode::collect<scan::scalar | decode::integer<int> | match::literal<1>>
    | store::assign,
    encode::value>;
using Integers = Elem<
    scan::array | decode::collect<scan::scalar | decode::integer<int>>
    | store::assign,
    encode::value>;
struct Choices {
  Try<Ones> ones;
  Try<Integers> fallback;
};
Choices choices;
auto error = decode_document_into<json::SimpleReader>(choices, "[[1,1],[1,2]]");
// choices.ones.value() == std::vector<int>{1,1}
// choices.fallback.value() == std::vector<int>{1,2}
```

Ordinary collect operands retain their data types, including bool. A bool
returned by `decode::custom` is data; use `match::predicate` when that bool is
intended to control candidate selection. Elem decoder terminals must be void
(ordinary decoding) or MatchResult (candidate decoding).

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

### Scalar codec boundary

Common typed decoders request semantic values through
`hosq::ext::DecodeSemanticScalar<R>`. A reader may implement
`read_semantic(state, output)` or specialize the adapter without exposing its
state layout. Numeric codecs produce `SemanticNumber` (signed/unsigned 64-bit
integer or binary64); common pipelines then enforce the requested category,
range and precision. Bool, null and bytes use the same boundary. String source
extraction uses `ext::DecodeStringSource<R>` (or the Reader hook
`read_string_source(state, std::string_view&)`); explicit string conversions use
format-owned adapters independently of scalar interpretation.
A codec conversion failure must record a Reader error. Existing dedicated
string/binary hooks remain supported.

JSON and TOML own their string-literal parsing helpers. Decoding does not
extend borrowed input life.
