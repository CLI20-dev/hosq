# HOSQ

**HOSQ** is **High Ordered Schema Query**.

HOSQ is a **schema query engine for structured data**.

It is built for people who are tired of pretending that building a full DOM is a reasonable first step.

HOSQ targets **modern C++20**.

HOSQ's full core feature set is available in C++20, and you can additionally opt into C++26 reflection support when you want schema derivation directly from reflected types.

If all you want is:

- three integers
- two strings
- one nested object
- strict validation
- predictable errors

then materializing an entire tree is wasteful, slow, and architecturally lazy.

HOSQ takes a different position:

**describe exactly what you want, and parse exactly that.**

---

## Who This Is For

HOSQ is not trying to be the universal answer to every structured-data problem.

It is built for teams that already know these things:

- the schema is known ahead of time
- correctness matters more than permissive guessing
- most payloads are larger than the subset you actually consume
- validation belongs in the parser, not as a cleanup job afterward
- generic DOM construction is often accidental work

If your system looks like:

- API ingestion
- ETL
- event pipelines
- protocol bridges
- config loading
- service boundaries with stable contracts

then HOSQ is exactly in its natural habitat.

That is not a weakness.

That is focus.

---

## Why HOSQ

Most parsers answer the wrong question.

They answer:

> "How do I turn this document into a generic representation?"

HOSQ answers:

> "What data do you actually need, and what guarantees do you want while extracting it?"

That difference matters.

With HOSQ, your parser is not a bag of callbacks, not a hand-written state machine, and not a post-parse cleanup pass over a giant dynamic object graph.

It is a **schema query**:

- scan the next token class
- decode it into a typed value
- validate it
- match exact choices
- store it with explicit semantics

You write the data contract once. HOSQ does the rest.

---

## What HOSQ Refuses To Be

HOSQ is deliberately not optimized for:

- browsing unknown ad-hoc documents
- generic JSON editing UIs
- REPL-style exploratory data poking
- "just parse everything into a variant tree and decide later"

There are already good tools for that.

HOSQ is for the opposite stance:

**the document is large, the contract is known, the failure mode matters, and the application only wants a precise typed slice.**

That clarity should be visible in the README because it is part of the product.

---

## What It Feels Like

HOSQ is built around composable pipelines:

```cpp
scan::scalar | decode::number<int> | validate::range<0, 100> | store::assign
```

That line says everything:

- the input must be a scalar
- decode it as `int`
- reject values outside `0..100`
- store the result

No hidden runtime registry. No stringly-typed visitor maze. No second pass.

Just a typed pipeline.

---

## A Smaller API for Real Work

You can still build schemas directly from the low-level pipeline pieces, but the everyday API is intentionally compact.

```cpp
#include <hosq/hosq.hh>
#include <hosq/json/fast_reader.hh>

using namespace hosq;

struct User {
  JsonUInt64Field<"id"> id;
  JsonStringField<"name", validate::non_empty> name;
  JsonIntField<"age", validate::range<0, 150>> age;
  JsonBoolField<"active"> active;
};

int main() {
  auto user = User{};

  const auto result = decode_into<json::FastReader<>>(
      user,
      R"({"id":42,"name":"Ada","age":31,"active":true})");

  if (!result.is_ok()) {
    return 1;
  }

  std::println("{} {} {}", *user.id, *user.name, *user.age);
}
```

This is the point of HOSQ:

- no DOM
- no manual field dispatch
- no handwritten conversion layer
- no ad-hoc validation phase after parsing

You declare the shape you want. The parser enforces it.

---

## Why The API Looks Like This

Some APIs look shorter because they hide decisions.

HOSQ makes those decisions explicit instead:

- what token shape is expected
- how it is decoded
- what values are accepted
- how duplicates behave
- how results accumulate

That is why a HOSQ schema can look denser than a generic binding macro.

The trade is intentional:

**more information in the type, less ambiguity in production.**

If you think that is "too explicit", the usual alternative is not simplicity.

It is moving the same complexity into undocumented runtime behavior.

---

## The Core Model

HOSQ is made from a few simple concepts.

### `scan`

`scan` says what kind of token is expected.

```cpp
scan::scalar
scan::object
scan::array
```

### `decode`

`decode` converts a scanned token into a typed value.

```cpp
decode::number<int>
decode::number<double>
decode::boolean
decode::string_view
decode::to<User>
decode::collect<...>
decode::each<...>
decode::skip
```

### `validate`

`validate` enforces domain rules while the parser still has full source context.

```cpp
validate::match<"admin">
validate::one_of<200, 201, 204>
validate::range<0, 65535>
validate::non_empty
validate::predicate<is_valid_email>
```

### `store`

`store` defines storage semantics explicitly.

```cpp
store::assign
store::once
store::first_wins
store::push
```

This is one of HOSQ's strongest design choices.

Most parser APIs blur together:

- decoding
- validation
- duplicate handling
- accumulation

HOSQ does not.

Those are different concerns, so they are different steps.

---

## Strict Where It Matters

HOSQ is not interested in being permissive just because permissiveness is fashionable.

If your input contract says:

- this field must exist
- this member must be unique
- this string must match one of three values
- this integer must stay in range

then HOSQ treats those as parser-level guarantees, not optional cleanup.

This has practical consequences:

- better error locality
- fewer invalid intermediate states
- less post-parse defensive code
- cleaner failure modes in production

And because validation runs while the parser still knows the exact token and offset, the error you get is the error you actually wanted.

---

## Partial Decode Is the Default, Not the Hack

Most real documents are larger than what your application actually needs.

HOSQ is built for that reality.

```cpp
struct Status {
  JsonUInt64Field<"id"> id;
  JsonIntField<"retweet_count"> retweet_count;
  JsonObjectField<"user", User> user;
  JsonUnknownField skip;
};
```

You can ignore what you do not care about without paying the conceptual cost of a DOM.

This is especially useful for:

- API gateways
- analytics ingestion
- ETL filters
- protocol bridges
- large nested payloads

`JsonUnknownField skip;` is a catch-all entry for members you choose not to model explicitly. The identifier `skip` is only a conventional field name in examples; it can be named however you want.

---

## Yes, You Can Still Build A Generic DOM

HOSQ is aggressively schema-first, but it is not dogmatic.

When you do need a generic in-memory representation, HOSQ can still produce one directly:

```cpp
using GenericObject =
    Elem<scan::object | decode::generics | store::assign, noop_encoder>;

auto value = GenericObject{};
auto result = decode_into<json::FastReader<>>(value, input);
```

Or equivalently as a field:

```cpp
struct Envelope {
  JsonGenericField<"metadata"> metadata;
  JsonUnknownField skip;
};
```

That gives you an escape hatch for:

- partially known documents
- gradual migration from legacy parser stacks
- schema discovery tools
- mixed strict/generic payloads

This matters because the real world is not pure.

Some parts of a payload deserve a hard contract.
Some parts are intentionally open-ended.

HOSQ supports both in the same schema.

You can parse most of the document with strict typed fields and keep only the unstable remainder as a generic value.

That is far more useful than choosing between "everything is a DOM" and "everything must be rigidly modeled up front."

---

## Query-Style Matching

HOSQ is not limited to exact field-name matches.

Schemas can also match structural patterns over the **key space** itself.

```cpp
struct Metrics {
  JsonStringField<RegexMatch<"id_[0-9]*">> ids;
  JsonUnknownField rest;
};
```

That field means:

> capture string-valued members whose **key** matches `id_[0-9]*`

This is different from value validation.

If you want to validate the **value** instead, that remains an ordinary pipeline:

```cpp
scan::scalar | decode::string_view | validate::match<"active">
```

This becomes powerful when documents contain:

- generated keys
- metric namespaces
- sharded field families
- protocol extensions with patterned names
- data that is logically grouped by naming convention

Instead of enumerating every possible key by hand, HOSQ lets the schema ask a more interesting question:

**which members of this document belong to the class of data I care about?**

That is where the "query" in High Ordered Schema Query becomes literal.

HOSQ is not just mapping fixed structs.
It can describe selective extraction over key spaces.

---

## One Schema, Multiple Formats

HOSQ is not a JSON-only idea.

The same schema model works across:

- JSON
- MessagePack
- CBOR

That means you can keep one semantic contract while swapping transport formats to match the system boundary you are integrating with.

```cpp
struct UserSchema {
  JsonUInt64Field<"id"> id;
  JsonStringField<"name", validate::non_empty> name;
  JsonIntField<"age", validate::range<0, 150>> age;
};

auto user = UserSchema{};
auto json_result = decode_into<json::FastReader<>>(user, json_bytes);
auto msgpack_result = decode_into<msgpack::Reader<>>(user, msgpack_bytes);
auto cbor_result = decode_into<cbor::Reader<>>(user, cbor_bytes);
```

This matters when:

- public APIs speak JSON
- internal queues speak MessagePack
- embedded or constrained systems speak CBOR

You should not need three parser stacks and three sets of invariants for the same logical data.

---

## Reflection When You Want It, Explicit Schemas When You Don't

If you like explicit schemas, HOSQ lets you be explicit.

If you want zero-boilerplate data binding, HOSQ also supports **opt-in C++26 reflection-driven schema derivation**.

```cpp
struct User {
  std::uint64_t id;
  std::string_view name;
  int age;
  bool active;
};

constexpr auto schema = hosq::reflect<User>();
```

Reflection support gives you:

- automatic field mapping
- generated aliases
- consistent encode/decode contracts
- less schema drift

The positioning is simple:

- **C++20 gives you the full HOSQ model**
- **C++26 reflection is an additional convenience layer**

Explicit schemas still remain first-class when you need:

- custom field names
- strict duplicate handling
- transport-specific tuning
- custom validators
- hand-optimized storage behavior

The important positioning is this:

- reflection is the shortest path when your C++ type already is the schema
- explicit schemas are the strongest path when transport behavior is part of the contract

HOSQ supports both because production code usually needs both.

---

## Schema Generation and Help Generation

HOSQ schemas are not just parser definitions.

They are structured metadata.

That means HOSQ can generate:

- JSON Schema
- MessagePack/CBOR schema descriptions
- CLI help
- API field documentation
- config reference output

```cpp
constexpr auto schema = hosq::schema::generate<User>();

std::println("{}", hosq::schema::to_json_schema(schema));
std::println("{}", hosq::help::usage(schema));
```

This is where HOSQ becomes more than a parser.

It becomes a single source of truth.

When your parsing rules, validation rules, documentation, and command help all come from the same schema, whole classes of drift disappear.

And because schema metadata can be resolved through runtime localization hooks, the same schema can produce locale-specific output without duplicating validation logic.

```cpp
JsonIntField<"age", validate::range<0, 150>> age{
    .description = i18n("field.age"),
    .requirement = hosq::required
};
```

That means one schema definition can generate:

- English JSON Schema
- Japanese JSON Schema
- locale-specific CLI help
- localized OpenAPI field descriptions
- translated config reference output

The important part is that only the human-facing metadata changes.

The structural contract stays the same:

- same field name
- same type
- same validation
- same required/optional semantics

This is a genuinely different value proposition from "parser plus docs generator."

HOSQ can act as a **localized contract source** for systems that need both machine-readable guarantees and human-readable documentation across multiple locales.

---

## Schema Evolution Is Part Of The Model

Schema-driven parsing is only interesting if schema changes remain manageable.

HOSQ treats schema evolution as normal engineering work, not as an edge case.

That means you can express:

- newly added optional fields
- deprecated fields
- field renames
- forward-compatible unknown-member absorption
- stricter validation in new versions
- dual-format migration windows

A typical strategy looks like this:

```cpp
struct UserV2 {
  JsonUInt64Field<"id"> id;
  JsonStringField<"name", validate::non_empty> name;
  JsonStringField<"display_name"> display_name;
  JsonUnknownField rest;
};
```

If your schema changes, you change the contract in one place.

That is a better failure mode than letting drift leak into dozens of hand-written field lookups.

---

## Nested Collections Without Ceremony

Arrays and objects are not bolted on as special cases.

They compose naturally.

```cpp
using Scores =
    JsonArrayField<"scores", JsonInt<validate::range<0, 100>>>;

using Tags =
    JsonArrayField<"tags", JsonString<validate::non_empty>>;

using Metrics =
    JsonMapField<"metrics", JsonString<>, JsonDouble<>>;
```

For more control, use the low-level pipelines directly:

```cpp
using ScoresValue =
    Elem<scan::array |
             decode::collect<
                 scan::scalar |
                 decode::number<int> |
                 validate::range<0, 100>> |
             store::assign,
         noop_encoder>;
```

The important part is that the mental model does not change.

HOSQ is still:

**scan -> decode -> validate -> store**

---

## FastReader and SimpleReader

HOSQ gives you two reader styles out of the box.

### `json::SimpleReader`

Use it when you want:

- straightforward behavior
- easier debugging
- a more direct reference implementation

### `json::FastReader`

Use it when you want:

- lower overhead
- tighter hot paths
- bounded depth control
- production-oriented throughput

Your schema does not change.

That is the important part.

You can prototype with one reader and deploy with another without rewriting your data contract.

```cpp
auto a = decode_into<json::SimpleReader>(schema, input);
auto b = decode_into<json::FastReader<>>(schema, input);
```

The parser backend is a policy choice, not an application rewrite.

---

## Errors With Useful Semantics

HOSQ reports structured errors, not vague failure booleans.

Errors include:

- code
- kind
- offset
- detail

That makes them usable for:

- diagnostics
- logging
- test assertions
- UI feedback
- protocol rejection paths

```cpp
if (!result.is_ok()) {
  std::println("code={} kind={} offset={} detail={}",
               int(result.code),
               int(result.kind()),
               result.offset,
               result.detail);
}
```

HOSQ distinguishes between:

- input errors
- syntax errors
- schema mismatches
- conversion failures
- validation failures

That is a better contract than "parse failed".

`detail` is a `std::string_view`, designed to be cheap to move through the system and stable enough to log, assert on, surface in tooling, and feed into generated help or diagnostics.

When you want richer diagnostics, readers can also be configured to record the failing path.

That lets HOSQ produce errors in the style of:

```text
type mismatch at $.meta.id.name: expected string, got number
```

This is especially useful in:

- API boundary diagnostics
- config validation
- developer-facing error reporting
- generated help and schema tooling

---

## Encoding Matters Too

HOSQ is not decode-only.

The same schema model supports structured output through the encoding pipeline.

Decode is built from:

- `scan`
- `decode`
- `validate`
- `store`

Encode follows the same philosophy on the way out:

- select the stored value
- transform it with `encode`
- emit it through the writer

In other words, HOSQ is not just "a decoder with a serializer attached".

It is a schema contract that can be executed in both directions.

```cpp
std::string out;
json::CompactWriter writer{out};

encode_into(user, writer);
```

And when you need more control, you can build explicit encode pipelines the same way you build decode pipelines:

```cpp
encode::number<int>
encode::boolean
encode::string_view
```

That means generated aliases, reflected schemas, and hand-written field schemas can all share the same encode/decode contract.

This gives you:

- symmetric contracts
- easier round-trip testing
- consistent field naming
- fewer custom serializer branches

---

## Where HOSQ Wins

HOSQ is strongest when all of these are true:

- you do not want a DOM
- you do want typed values
- you need parser-time validation
- duplicate semantics matter
- partial extraction is the common case
- the schema is an asset, not an inconvenience

That is why HOSQ is compelling in places where generic parsers are merely adequate.

---

## A More Honest Comparison

If you want a generic JSON value type, there are already plenty of libraries.

If you want:

- selective extraction
- schema-bound decoding
- integrated validation
- duplicate-key semantics
- nested typed collection handling
- transport-agnostic schemas

then HOSQ is solving a different problem.

That problem is closer to:

- query planning
- structured ingestion
- typed protocol decoding

than to "parse some JSON into a map of variants".

This is why the name is **High Ordered Schema Query**.

It is not just parsing bytes.

It is asking a document a typed question.

The shortest way to position HOSQ against the usual ecosystem is this:

- `simdjson` asks: how fast can we navigate JSON?
- DOM libraries ask: how can we represent everything?
- binding libraries ask: how can we map this struct quickly?
- **HOSQ asks: what exact contract does the application want to enforce while extracting data?**

---

## Installation

### CMake

```cmake
find_package(hosq CONFIG REQUIRED)
target_link_libraries(your_target PRIVATE hosq::hosq)
```

HOSQ requires a compiler with solid **C++20** support.

That is a deliberate floor, not an accident. HOSQ relies on modern compile-time facilities because the schema is part of the program, not just a runtime string.

### Header-only usage

```cpp
#include <hosq/hosq.hh>
```

Format-specific components are available under their own namespaces:

```cpp
#include <hosq/json/fast_reader.hh>
#include <hosq/json/simple_reader.hh>
#include <hosq/json/compact_writer.hh>
#include <hosq/msgpack/reader.hh>
#include <hosq/cbor/reader.hh>
```

---

## A Real Example

This is the style HOSQ is optimized for.

```cpp
#include <hosq/hosq.hh>
#include <hosq/json/fast_reader.hh>

using namespace hosq;

struct User {
  JsonUInt64Field<"id"> id;
  JsonStringField<"name", validate::non_empty> name;
};

struct Status {
  JsonUInt64Field<"id"> id;
  JsonIntField<"retweet_count"> retweet_count;
  JsonIntField<"favorite_count"> favorite_count;
  JsonObjectField<"user", User> user;
  JsonUnknownField skip;
};

struct Timeline {
  JsonArrayField<"statuses", Status> statuses;
  JsonUnknownField skip;
};

int main() {
  auto timeline = Timeline{};

  const auto result = decode_into<json::FastReader<>>(timeline, input);
  if (!result.is_ok()) {
    return 1;
  }

  for (const auto& status : *timeline.statuses) {
    std::println("{} {}", *status.id, *status.user->name);
  }
}
```

No DOM.

No dynamic lookup.

No post-parse reinterpretation.

Just the fields you asked for, in the types you asked for, with the guarantees you asked for.

---

## When To Use HOSQ

Use HOSQ when:

- you know the shape you want
- you want parser-time validation
- you only need part of a document
- you want one schema across multiple formats
- you care about explicit duplicate and storage semantics
- you want documentation and help generated from the same source as parsing

Do not use HOSQ if your primary goal is to freely mutate arbitrary untyped documents after parsing.

That is a different problem.

And frankly, a much less interesting one.

---

## Design Principles

- **Schema-first**: parsing should express a contract, not a hope.
- **Partial by default**: extracting less data should be easier, not harder.
- **Typed all the way through**: no generic value detour unless you ask for one.
- **Composable semantics**: scanning, decoding, validation, and storage are separate on purpose.
- **Backend independence**: one schema, many readers and writers.
- **Useful errors**: failure must be structurally meaningful.
- **Explicit tradeoffs**: HOSQ would rather be precise for the right workloads than vague for all workloads.

---

## In One Sentence

**HOSQ lets you parse structured data the way high-performance systems actually use it: as a typed schema query, not as a generic tree you regret building five microseconds later.**
