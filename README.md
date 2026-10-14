# HOSQ

HOSQ is a C++20 header-only library for decoding known parts of structured
input without first constructing a general-purpose DOM.

The public implementation is under `include/hosq`. A schema is a C++ type whose
entries compose scanning, decoding, matching, validation, and storage steps.
`Try<Entry<...>>` members provide key-order-independent object dispatch.

```cpp
#include <hosq/hosq.hh>
#include <hosq/json/fast_reader.hh>

using namespace hosq;

template <class T>
using Number = Elem<scan::scalar | decode::number<T> | store::assign,
                    noop_encoder>;

struct Record {
  Try<Entry<Key<"id">, Number<int>>> id;
};

Record record;
auto result = decode_into<json::FastReader<>>(record, R"({"id":42})");
```

## Tests

```sh
cmake -S . -B build -DCXX_HOSQ_ENABLE_TEST=ON
cmake --build build --target hosq_tests_cxx20
ctest --test-dir build
```

## Benchmarks

The maintained HOSQ/Glaze/yyjson comparisons are in
[`bench/comparison`](bench/comparison/README.md). Each case has its own source
file and Google Benchmark executable; the HOSQ schema is visible in that case
file.

## Other input formats

The following C++20 readers implement the same token, checkpoint, and schema
API as the JSON readers:

| Header | Reader | Input |
| --- | --- | --- |
| `hosq/msgpack/reader.hh` | `hosq::msgpack::Reader<>` | `std::string_view` of binary bytes |
| `hosq/bson/reader.hh` | `hosq::bson::Reader<>` | `std::string_view` of binary bytes |
| `hosq/toml/reader.hh` | `hosq::toml::Reader<>` | UTF-8 `std::string_view` |

For example, replace `json::FastReader<>` in `decode_into` with
`msgpack::Reader<>` to decode a MessagePack object into the same schema.
The optional template argument limits container depth (default: 64). Strings
borrow the input, so keep it alive while using decoded `string_view` values.
`construct<std::string>` provides owned string storage. As with the existing
JSON readers, string escapes are exposed as raw input, not resolved.

MessagePack supports all integer widths, IEEE floats, booleans, nil, strings,
arrays, maps (including non-string and container keys), binary payloads, and
opaque extensions. Constant object schemas require string keys and report a
mapping error for non-string keys. `decode::skip` can traverse all map key
shapes. Binary and extension values can be skipped but have no dedicated
application-specific conversion API.

BSON targets specification **1.1** and supports documents, arrays with sequential decimal index keys, strings,
int32/int64, doubles, booleans, null, binary, and opaque BSON scalar types
(ObjectId, date, timestamp, decimal128, regex, code, symbol, DBPointer,
undefined, MinKey, MaxKey, code-with-scope). Document and payload lengths,
terminators, booleans, old binary lengths, and UTF-8 strings/keys are checked.
Code-with-scope additionally validates the code string, total size, and entire
scope document under the same depth limit. Opaque values can be skipped;
no conversion to application-specific BSON types is provided because existing
schemas expose numbers, booleans, strings and containers.

TOML targets **1.0.0**, including dotted assignments, bare/quoted/escaped keys,
all four string syntaxes, booleans, decimal/base-prefixed signed 64-bit integers,
floats (`inf`/`nan` included), nested arrays, inline tables, out-of-order table
headers, implicit parent tables, and arrays of tables. A structural index
reconciles table declarations before token traversal; it retains input spans
rather than constructing a general-purpose value DOM. Checkpoints only save
the cursor and current token. Duplicate/redefined/sealed table conflicts are
rejected according to TOML 1.0 rules.

Date/time tokens are syntactically validated (including calendar bounds) and
can be skipped or accessed as raw token spans. They remain opaque scalars:
number, boolean and string decoders reject them rather than inventing a
calendar storage type. Constant schema keys compare decoded TOML key names.
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

### TOML corpus verification

The reproducible checker in `tests/support` uses
[`toml-lang/toml-test`](https://github.com/toml-lang/toml-test), pinned for this
validation to `ff49d109861c1ad25af53f687f2aef19ab650600`.
It checks the TOML 1.0 list: invalid rejection, valid acceptance, decoded key
and container/type structure, and string/integer/float/boolean values
against golden JSON. String comparisons use the owned `read_string` API. Date/time value
comparisons are intentionally excluded due to the opaque-calendar API above;
this is not a claim of full calendar-value conformance.

With that corpus checked out in `/tmp/hosq-toml-test`:

```sh
nix develop --command clang++ -std=c++20 -O1 -Iinclude \
  tests/support/toml_conformance.cpp -o build/toml_conformance
python3 tests/support/check_toml_conformance.py \
  build/toml_conformance /tmp/hosq-toml-test
```

## Encoding with the format writers

`encode::value` is a terminal encoder for semantic storage values. It uses
`ext::EncodeValue<W>` and writer-specific scalar operations, while `Encoder`
continues to traverse the same `Elem`, `Entry`, `Try`, and aggregate/tuple
schemas used by decoding. Existing text adapters (`encode::number`,
`encode::boolean`, `encode::string_view`) and custom JSON pipelines are
unchanged. Use `encode::value` directly on numeric storage: formatting a number
into a string first would intentionally encode a string value.

```cpp
#include <hosq/hosq.hh>
#include <hosq/msgpack/writer.hh>

template <class T>
using Number = hosq::Elem<
    hosq::scan::scalar | hosq::decode::number<T> | hosq::store::assign,
    hosq::encode::value>;

struct Record {
  hosq::Try<hosq::Entry<hosq::Key<"id">, Number<int>>> id;
};

Record record;
record.id.value().value() = 42;
std::string output;
hosq::msgpack::Writer writer{output};
if (!hosq::encode_into(record, writer)) {
  // Inspect writer.error(); discard partial output on failure.
}
```

Replace the writer with `bson::Writer` or `toml::Writer` for the same schema.
The writer headers are `hosq/{msgpack,bson,toml}/writer.hh`; each writer takes a
`std::string&`, returns `std::string_view` from `output()`, and implements the
existing `WriterConcept`. `Writer<MaxDepth, MaxOutputBytes>` bounds depth and
output size. The default depth is 64; BSON's default output limit is INT32_MAX,
while MessagePack/TOML default to the string's addressable size. Output limits
include any bytes already in the supplied string.

Supported common values are booleans, arithmetic values, semantic UTF-8
strings, explicit `nullptr`, `BytesView`, populated `optional`, active `variant`,
ranges (arrays), `Map` with equally sized key/value ranges, and nested schemas.
A missing optional, a valueless variant, an unsupported value, or
`noop_encoder` produces an error. `Try` controls decoding dispatch; it does
not imply omission during encoding. Reader-only schemas must provide encoder
pipelines before they can be encoded. No decoder, store or InputView behavior
is changed.

MessagePack chooses the smallest integer and length/count representation;
float storage emits float32, double emits float64. Arrays/maps can be emitted
without knowing counts ahead of time: closing the container patches its header.
Map keys may be any supported value, including containers. Binary uses `bin`,
and `write_extension(int8_t, BytesView)` emits opaque extension data. Extension
payload semantics (such as timestamps) belong to the application.

BSON requires a root document and string keys without NUL. Documents and arrays
patch their signed int32 byte lengths when closed; array indexes are generated
sequentially. Small integral storage emits int32; storage wider than 32 bits,
or unsigned values above INT32_MAX, emits int64. UINT64 values above INT64_MAX
are rejected. Floats emit IEEE binary64, with exact float32 widening. Null and
generic binary are supported; ObjectId, date, timestamp, decimal128, regex and
code-with-scope have no automatic C++ schema conversion or writer operation.
They are not silently converted to strings/numbers.

TOML targets 1.0.0 and requires a root table. Each root member is one assignment,
keys are quoted and escaped, nested tables use inline tables, and arrays of
tables use arrays of inline tables. These forms preserve the same data values
as `[table]`/`[[table]]` declarations; source layout/comments are not preserved.
Floats retain a float spelling (e.g. `1.0`, `-0.0`, `inf`, `nan`); integers must
fit int64. Null, binary and calendar-type conversions are unsupported. Duplicate
output keys are errors. All three writers reject invalid UTF-8 strings and
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

### Independent writer checks

`tests/support/writer_interop.cpp` generates synthetic fixtures. The companion
`check_writer_interop.py` compares exact MessagePack/BSON bytes and decoded
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
files in `tests/fixtures`, including malformed inputs. Each valid file is decoded
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

Nix fetches `toml-lang/toml-test` at the revision and content hash in `flake.nix`
into its store. No corpus files, Git history, global-profile installs or test-time
network downloads are needed. The first Nix setup needs network access; later
runs reuse the store. Update both the revision and hash intentionally when
updating the corpus. Plain CMake remains offline and dependency-light; the TOML
corpus test is added only when `HOSQ_TOML_CORPUS` is supplied as an environment
variable or CMake cache path. The conformance shell sets it automatically.

Only TOML currently uses an upstream official corpus. BSON and MessagePack use
the local file fixtures and independent codec checks; this does not claim full
upstream corpus coverage for those formats.
