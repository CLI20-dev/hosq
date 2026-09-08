#include <benchmark/benchmark.h>
#include <glaze/glaze.hpp>
#include <hosq/hosq.hh>
#include <hosq/json/fast_reader.hh>
#include <yyjson.h>

#include <array>
#include <bit>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

using namespace hosq;

namespace {

using Reader = json::FastReader<>;

struct Stats {
  std::array<uint64_t, 10> v{};
  bool operator==(const Stats&) const = default;
};

[[noreturn]] void fatal(std::string_view message) {
  std::cerr << "static comparison: " << message << '\n';
  std::abort();
}

void string_stats(std::string_view s, bool escaped, Stats& out) {
  ++out.v[3];
  uint64_t hash = 14695981039346656037ULL;
  auto byte = [&](unsigned char c) {
    ++out.v[4];
    hash = (hash ^ c) * 1099511628211ULL;
  };
  auto hex = [](char c) -> unsigned {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    fatal("invalid JSON Unicode escape");
  };
  auto utf8 = [&](unsigned u) {
    if (u < 0x80) {
      byte(u);
    } else if (u < 0x800) {
      byte(0xc0 | (u >> 6));
      byte(0x80 | (u & 63));
    } else if (u < 0x10000) {
      byte(0xe0 | (u >> 12));
      byte(0x80 | ((u >> 6) & 63));
      byte(0x80 | (u & 63));
    } else {
      byte(0xf0 | (u >> 18));
      byte(0x80 | ((u >> 12) & 63));
      byte(0x80 | ((u >> 6) & 63));
      byte(0x80 | (u & 63));
    }
  };
  for (size_t i = 0; i < s.size(); ++i) {
    unsigned char c = s[i];
    if (!escaped || c != '\\') {
      byte(c);
      continue;
    }
    if (++i == s.size()) fatal("truncated JSON escape");
    c = s[i];
    switch (c) {
      case '"': case '\\': case '/': byte(c); break;
      case 'b': byte(8); break;
      case 'f': byte(12); break;
      case 'n': byte(10); break;
      case 'r': byte(13); break;
      case 't': byte(9); break;
      case 'u': {
        if (i + 4 >= s.size()) fatal("truncated JSON Unicode escape");
        unsigned u = 0;
        for (int j = 0; j < 4; ++j) u = (u << 4) | hex(s[++i]);
        if (u >= 0xd800 && u <= 0xdbff) {
          if (i + 6 >= s.size() || s[++i] != '\\' || s[++i] != 'u')
            fatal("invalid JSON surrogate pair");
          unsigned low = 0;
          for (int j = 0; j < 4; ++j) low = (low << 4) | hex(s[++i]);
          if (low < 0xdc00 || low > 0xdfff) fatal("invalid low surrogate");
          u = 0x10000 + ((u - 0xd800) << 10) + (low - 0xdc00);
        }
        utf8(u);
        break;
      }
      default: fatal("invalid JSON escape");
    }
  }
  out.v[5] += hash;
}

enum class Mode { id = 0, many = 1, all = 2, numbers = 3, strings = 4 };

struct Fixture {
  std::string name;
  std::string input;
  Mode mode{};
  Stats expected;
};

struct IdRecord { uint64_t id{}; };
struct ManyPayload {
  std::string_view name;
  uint64_t count{};
  double ratio{};
  bool ok{};
};
struct ManyRecord { uint64_t id{}; ManyPayload payload; };
struct AllPayload {
  std::string_view name;
  uint64_t count{};
  double ratio{};
  bool ok{};
  std::nullptr_t none{};
  std::array<uint64_t, 4> values{};
};
struct AllRecord { uint64_t id{}; AllPayload payload; };
struct NumberPayload {
  uint64_t u{};
  int64_t i{};
  double a{}, b{}, c{};
  uint64_t maximum{};
};
struct NumberRecord { uint64_t id{}; NumberPayload payload; };
struct StringRecord { uint64_t id{}; std::string_view payload; };

const IdRecord& project(const IdRecord& row) { return row; }
const ManyRecord& project(const ManyRecord& row) { return row; }
const AllRecord& project(const AllRecord& row) { return row; }
const NumberRecord& project(const NumberRecord& row) { return row; }
const StringRecord& project(const StringRecord& row) { return row; }

template <class Rows>
Stats consume_ids(const Rows& rows) {
  Stats out;
  out.v[9] = 1;
  out.v[8] = rows.size();
  out.v[0] = rows.size();
  for (const auto& native : rows) out.v[1] += project(native).id;
  return out;
}

template <class Rows>
Stats consume_many(const Rows& rows, bool escaped) {
  Stats out;
  out.v[9] = 1;
  out.v[8] = 2 * rows.size();
  out.v[0] = 5 * rows.size();
  for (const auto& native : rows) {
    const auto row = project(native);
    out.v[1] += row.id + row.payload.count;
    out.v[2] += std::bit_cast<uint64_t>(row.payload.ratio);
    string_stats(row.payload.name, escaped, out);
    out.v[6] += row.payload.ok;
  }
  return out;
}

template <class Rows>
Stats consume_all(const Rows& rows, bool escaped) {
  Stats out;
  out.v[9] = 1 + rows.size();
  out.v[8] = 2 * rows.size();
  out.v[0] = 10 * rows.size();
  for (const auto& native : rows) {
    const auto row = project(native);
    out.v[1] += row.id + row.payload.count;
    out.v[2] += std::bit_cast<uint64_t>(row.payload.ratio);
    string_stats(row.payload.name, escaped, out);
    out.v[6] += row.payload.ok;
    ++out.v[7];
    for (auto n : row.payload.values) out.v[1] += n;
  }
  return out;
}

template <class Rows>
Stats consume_numbers(const Rows& rows) {
  Stats out;
  out.v[9] = 1 + rows.size();
  out.v[8] = rows.size();
  out.v[0] = 7 * rows.size();
  for (const auto& native : rows) {
    const auto row = project(native);
    out.v[1] += row.id + row.payload.u + uint64_t(row.payload.i) +
                row.payload.maximum;
    out.v[2] += std::bit_cast<uint64_t>(row.payload.a) +
                std::bit_cast<uint64_t>(row.payload.b) +
                std::bit_cast<uint64_t>(row.payload.c);
  }
  return out;
}

template <class Rows>
Stats consume_strings(const Rows& rows, bool escaped) {
  Stats out;
  out.v[9] = 1;
  out.v[8] = rows.size();
  out.v[0] = 2 * rows.size();
  for (const auto& native : rows) {
    const auto row = project(native);
    out.v[1] += row.id;
    string_stats(row.payload, escaped, out);
  }
  return out;
}

template <class T>
using HNumber = Elem<scan::scalar | decode::number<T> | store::assign,
                     noop_encoder>;
using HString = Elem<scan::scalar | decode::string_view | store::assign,
                     noop_encoder>;
using HBool = Elem<scan::scalar | decode::boolean | store::assign, noop_encoder>;
using HNull = Elem<scan::scalar | decode::string_view |
                       decode::custom<[](std::string_view value) -> std::nullptr_t {
                         if (value != "null") fatal("HOSQ expected null");
                         return nullptr;
                       }> |
                       store::assign,
                   noop_encoder>;
using HSkipKey = Elem<scan::scalar | decode::skip, noop_encoder>;
using HSkipValue = Elem<decode::skip, noop_encoder>;

struct HId {
  Try<Entry<Key<"id">, HNumber<uint64_t>>> id;
  Try<Entry<HSkipKey, HSkipValue>> skip;
};
struct HManyPayload {
  Try<Entry<Key<"name">, HString>> name;
  Try<Entry<Key<"count">, HNumber<uint64_t>>> count;
  Try<Entry<Key<"ratio">, HNumber<double>>> ratio;
  Try<Entry<Key<"ok">, HBool>> ok;
  Try<Entry<HSkipKey, HSkipValue>> skip;
};
using HManyPayloadValue =
    Elem<scan::object | decode::to<HManyPayload> | store::assign, noop_encoder>;
struct HMany {
  Try<Entry<Key<"id">, HNumber<uint64_t>>> id;
  Try<Entry<Key<"payload">, HManyPayloadValue>> payload;
  Try<Entry<HSkipKey, HSkipValue>> skip;
};
struct HAllPayload {
  Entry<Key<"name">, HString> name;
  Entry<Key<"count">, HNumber<uint64_t>> count;
  Entry<Key<"ratio">, HNumber<double>> ratio;
  Entry<Key<"ok">, HBool> ok;
  Entry<Key<"none">, HNull> none;
  Entry<Key<"values">,
        Elem<scan::array | decode::collect<scan::scalar |
                                                decode::number<uint64_t>, 4> |
                 store::assign,
             noop_encoder>>
      values;
};
using HAllPayloadValue =
    Elem<scan::object | decode::to<HAllPayload> | store::assign, noop_encoder>;
struct HAll {
  Entry<Key<"id">, HNumber<uint64_t>> id;
  Entry<Key<"payload">, HAllPayloadValue> payload;
};
struct HNumberTuple {
  HNumber<uint64_t> u;
  HNumber<int64_t> i;
  HNumber<double> a, b, c;
  HNumber<uint64_t> maximum;
};
using HNumberTupleValue =
    Elem<scan::array | decode::to<HNumberTuple> | store::assign, noop_encoder>;
struct HNumbers {
  Entry<Key<"id">, HNumber<uint64_t>> id;
  Entry<Key<"payload">, HNumberTupleValue> payload;
};
struct HStrings {
  Entry<Key<"id">, HNumber<uint64_t>> id;
  Entry<Key<"payload">, HString> payload;
};

IdRecord project(const HId& row) {
  return {row.id.value().value()};
}
ManyRecord project(const HMany& row) {
  const auto& p = row.payload.value().value();
  return {row.id.value().value(),
          {p.name.value().value(), p.count.value().value(),
           p.ratio.value().value(), p.ok.value().value()}};
}
AllRecord project(const HAll& row) {
  const auto& p = row.payload.value().value();
  return {row.id.value().value(),
          {p.name.value().value(), p.count.value().value(),
           p.ratio.value().value(), p.ok.value().value(), nullptr,
           p.values.value().value()}};
}
NumberRecord project(const HNumbers& row) {
  const auto& p = row.payload.value().value();
  return {row.id.value().value(),
          {p.u.value(), p.i.value(), p.a.value(), p.b.value(), p.c.value(),
           p.maximum.value()}};
}
StringRecord project(const HStrings& row) {
  return {row.id.value().value(), row.payload.value().value()};
}

template <class Schema>
using HRoot = Elem<scan::array |
                       decode::collect<scan::object | decode::to<Schema>> |
                       store::assign,
                   noop_encoder>;

template <class Schema>
HRoot<Schema> hosq_decode(const Fixture& f) {
  HRoot<Schema> root;
  Reader reader;
  Reader::state_type state;
  reader.reset(state, std::string_view{f.input});
  if (!next_token<ReadPosition::value>(reader, state) ||
      !Decoder<Reader>::decode_nested(root, reader, state).is_ok() ||
      !next_token<Expect::document_end>(reader, state) ||
      !reader.error(state).is_ok())
    fatal("HOSQ decode/full input check failed for " + f.name);
  return root;
}

Stats hosq_run(const Fixture& f) {
  switch (f.mode) {
    case Mode::id: {
      auto root = hosq_decode<HId>(f);
      return consume_ids(root.value());
    }
    case Mode::many: {
      auto root = hosq_decode<HMany>(f);
      return consume_many(root.value(), true);
    }
    case Mode::all: {
      auto root = hosq_decode<HAll>(f);
      return consume_all(root.value(), true);
    }
    case Mode::numbers: {
      auto root = hosq_decode<HNumbers>(f);
      return consume_numbers(root.value());
    }
    case Mode::strings: {
      auto root = hosq_decode<HStrings>(f);
      return consume_strings(root.value(), true);
    }
  }
  fatal("unknown mode");
}

}  // namespace

template <> struct glz::meta<IdRecord> {
  using T = IdRecord;
  static constexpr auto value = object("id", &T::id);
};
template <> struct glz::meta<ManyPayload> {
  using T = ManyPayload;
  static constexpr auto value = object("name", &T::name, "count", &T::count,
                                       "ratio", &T::ratio, "ok", &T::ok);
};
template <> struct glz::meta<ManyRecord> {
  using T = ManyRecord;
  static constexpr auto value = object("id", &T::id, "payload", &T::payload);
};
template <> struct glz::meta<AllPayload> {
  using T = AllPayload;
  static constexpr auto value = object(
      "name", &T::name, "count", &T::count, "ratio", &T::ratio, "ok", &T::ok,
      "none", &T::none, "values", &T::values);
};
template <> struct glz::meta<AllRecord> {
  using T = AllRecord;
  static constexpr auto value = object("id", &T::id, "payload", &T::payload);
};
template <> struct glz::meta<NumberPayload> {
  using T = NumberPayload;
  static constexpr auto value = array(&T::u, &T::i, &T::a, &T::b, &T::c,
                                      &T::maximum);
};
template <> struct glz::meta<NumberRecord> {
  using T = NumberRecord;
  static constexpr auto value = object("id", &T::id, "payload", &T::payload);
};
template <> struct glz::meta<StringRecord> {
  using T = StringRecord;
  static constexpr auto value = object("id", &T::id, "payload", &T::payload);
};

namespace {

template <bool AllowUnknown, class T>
void glaze_read(T& output, Fixture& f) {
  constexpr auto options = glz::opts{
      .error_on_unknown_keys = !AllowUnknown,
      .partial_read = false,
  };
  const auto size = f.input.size();
  const auto ec = glz::read<options>(output, f.input);
  if (ec || ec.count != size || f.input.size() != size) {
    std::cerr << "Glaze failed/full input not consumed for " << f.name << ": "
              << glz::format_error(ec, f.input) << '\n';
    std::abort();
  }
}

Stats glaze_run(Fixture& f) {
  switch (f.mode) {
    case Mode::id: {
      std::vector<IdRecord> rows;
      glaze_read<true>(rows, f);
      return consume_ids(rows);
    }
    case Mode::many: {
      std::vector<ManyRecord> rows;
      glaze_read<true>(rows, f);
      return consume_many(rows, true);
    }
    case Mode::all: {
      std::vector<AllRecord> rows;
      glaze_read<false>(rows, f);
      return consume_all(rows, true);
    }
    case Mode::numbers: {
      std::vector<NumberRecord> rows;
      glaze_read<false>(rows, f);
      return consume_numbers(rows);
    }
    case Mode::strings: {
      std::vector<StringRecord> rows;
      glaze_read<false>(rows, f);
      return consume_strings(rows, true);
    }
  }
  fatal("unknown mode");
}

std::string_view ykey(yyjson_val* key) {
  return {yyjson_get_str(key), yyjson_get_len(key)};
}

uint64_t yuint(yyjson_val* v) {
  if (!v || !yyjson_is_uint(v)) fatal("yyjson expected uint64");
  return yyjson_get_uint(v);
}
int64_t ysint(yyjson_val* v) {
  if (!v) fatal("yyjson expected int64");
  if (yyjson_is_sint(v)) return yyjson_get_sint(v);
  if (yyjson_is_uint(v) &&
      yyjson_get_uint(v) <= uint64_t(std::numeric_limits<int64_t>::max()))
    return int64_t(yyjson_get_uint(v));
  fatal("yyjson integer is outside int64 range");
}
double yreal(yyjson_val* v) {
  if (!v || !yyjson_is_real(v)) fatal("yyjson expected real");
  return yyjson_get_real(v);
}
std::string_view ystring(yyjson_val* v) {
  if (!v || !yyjson_is_str(v)) fatal("yyjson expected string");
  return {yyjson_get_str(v), yyjson_get_len(v)};
}

template <class F>
void each_member(yyjson_val* object, F&& f) {
  if (!yyjson_is_obj(object)) fatal("yyjson expected object");
  size_t i, n;
  yyjson_val *key, *value;
  yyjson_obj_foreach(object, i, n, key, value) f(ykey(key), value);
}

Stats yyjson_run(const Fixture& f) {
  yyjson_doc* doc = yyjson_read(f.input.data(), f.input.size(), 0);
  if (!doc) fatal("yyjson parse failed for " + f.name);
  yyjson_val* root = yyjson_doc_get_root(doc);
  if (!yyjson_is_arr(root)) fatal("yyjson expected root array");

  Stats out;
  if (f.mode == Mode::id) {
    std::vector<IdRecord> rows;
    rows.reserve(yyjson_arr_size(root));
    size_t i, n; yyjson_val* row;
    yyjson_arr_foreach(root, i, n, row) {
      rows.push_back({yuint(yyjson_obj_get(row, "id"))});
    }
    out = consume_ids(rows);
  } else if (f.mode == Mode::many || f.mode == Mode::all) {
    std::vector<ManyRecord> many;
    std::vector<AllRecord> all;
    const auto count = yyjson_arr_size(root);
    if (f.mode == Mode::many) many.reserve(count); else all.reserve(count);
    size_t i, n; yyjson_val* row;
    yyjson_arr_foreach(root, i, n, row) {
      yyjson_val *id = nullptr, *payload = nullptr;
      each_member(row, [&](std::string_view k, yyjson_val* v) {
        if (k == "id") id = v; else if (k == "payload") payload = v;
      });
      yyjson_val *name = nullptr, *number = nullptr, *ratio = nullptr,
                 *ok = nullptr, *none = nullptr, *values = nullptr;
      each_member(payload, [&](std::string_view k, yyjson_val* v) {
        if (k == "name") name = v;
        else if (k == "count") number = v;
        else if (k == "ratio") ratio = v;
        else if (k == "ok") ok = v;
        else if (k == "none") none = v;
        else if (k == "values") values = v;
      });
      if (!ok || !yyjson_is_bool(ok)) fatal("yyjson expected boolean");
      if (f.mode == Mode::many) {
        many.push_back({yuint(id), {ystring(name), yuint(number), yreal(ratio),
                                    yyjson_get_bool(ok)}});
      } else {
        if (!none || !yyjson_is_null(none) || !values ||
            yyjson_arr_size(values) != 4)
          fatal("yyjson invalid full payload");
        std::array<uint64_t, 4> a{};
        size_t j, m; yyjson_val* v;
        yyjson_arr_foreach(values, j, m, v) a[j] = yuint(v);
        all.push_back({yuint(id), {ystring(name), yuint(number), yreal(ratio),
                                  yyjson_get_bool(ok), nullptr, a}});
      }
    }
    out = f.mode == Mode::many ? consume_many(many, false)
                               : consume_all(all, false);
  } else if (f.mode == Mode::numbers) {
    std::vector<NumberRecord> rows;
    rows.reserve(yyjson_arr_size(root));
    size_t i, n; yyjson_val* row;
    yyjson_arr_foreach(root, i, n, row) {
      yyjson_val *id = nullptr, *payload = nullptr;
      each_member(row, [&](std::string_view k, yyjson_val* v) {
        if (k == "id") id = v; else if (k == "payload") payload = v;
      });
      if (!payload || yyjson_arr_size(payload) != 6)
        fatal("yyjson invalid number tuple");
      rows.push_back({yuint(id),
                      {yuint(yyjson_arr_get(payload, 0)),
                       ysint(yyjson_arr_get(payload, 1)),
                       yreal(yyjson_arr_get(payload, 2)),
                       yreal(yyjson_arr_get(payload, 3)),
                       yreal(yyjson_arr_get(payload, 4)),
                       yuint(yyjson_arr_get(payload, 5))}});
    }
    out = consume_numbers(rows);
  } else {
    std::vector<StringRecord> rows;
    rows.reserve(yyjson_arr_size(root));
    size_t i, n; yyjson_val* row;
    yyjson_arr_foreach(root, i, n, row) {
      yyjson_val *id = nullptr, *payload = nullptr;
      each_member(row, [&](std::string_view k, yyjson_val* v) {
        if (k == "id") id = v; else if (k == "payload") payload = v;
      });
      rows.push_back({yuint(id), ystring(payload)});
    }
    out = consume_strings(rows, false);
  }
  yyjson_doc_free(doc);
  return out;
}

Stats run_glaze(Fixture& fixture) { return glaze_run(fixture); }
Stats run_hosq(Fixture& fixture) { return hosq_run(fixture); }
Stats run_yyjson(Fixture& fixture) { return yyjson_run(fixture); }

}  // namespace

int main(int argc, char** argv) {
  if (argc < 4) fatal("usage: hosq_static_comparison FIXTURE_DIRECTORY CASE ENGINE [benchmark options]");
  const std::string directory = argv[1];
  const std::string selected_case = argv[2];
  const std::string engine = argv[3];
  if (engine != "Glaze" && engine != "HOSQ" && engine != "yyjson")
    fatal("ENGINE must be Glaze, HOSQ, or yyjson");
  using EngineRun = Stats (*)(Fixture&);
  const EngineRun run = engine == "Glaze" ? run_glaze
                      : engine == "HOSQ"  ? run_hosq
                                           : run_yyjson;

  std::ifstream manifest(directory + "/manifest_static.txt");
  if (!manifest) fatal("cannot open manifest_static.txt");
  Fixture fixture;
  std::string name, file;
  int mode;
  size_t manifest_count = 0;
  bool found = false;
  while (manifest >> name >> file >> mode) {
    Stats expected;
    for (auto& value : expected.v)
      if (!(manifest >> value)) fatal("malformed manifest_static.txt");
    ++manifest_count;
    if (name != selected_case) continue;
    if (found || mode < 0 || mode > 4) fatal("duplicate case or invalid mode");
    fixture.name = name;
    fixture.mode = static_cast<Mode>(mode);
    fixture.expected = expected;
    std::ifstream input(directory + "/" + file, std::ios::binary);
    if (!input) fatal("cannot open fixture " + file);
    fixture.input.assign(std::istreambuf_iterator<char>(input), {});
    found = true;
  }
  if (manifest_count != 12) fatal("manifest must contain exactly 12 cases");
  if (!found || fixture.input.empty()) fatal("selected case missing or empty");

  fixture.input.reserve(fixture.input.size() + glz::padding_bytes);
  const std::string original = fixture.input;
  const auto actual = run(fixture);
  if (actual != fixture.expected) {
    std::cerr << "digest mismatch for " << fixture.name << '/' << engine << '\n';
    std::cerr << "expected:";
    for (auto x : fixture.expected.v) std::cerr << ' ' << x;
    std::cerr << "\nactual:  ";
    for (auto x : actual.v) std::cerr << ' ' << x;
    std::cerr << '\n';
    std::abort();
  }
  if (fixture.input != original) fatal("engine changed logical fixture bytes");
  std::cout << "verified " << fixture.name << '/' << engine
            << " bytes=" << fixture.input.size()
            << " leaves=" << fixture.expected.v[0] << '\n';

  benchmark::RegisterBenchmark(
      (fixture.name + "/" + engine).c_str(), [&](benchmark::State& state) {
        for (auto _ : state) {
          auto result = run(fixture);
          benchmark::DoNotOptimize(result);
        }
        state.SetBytesProcessed(state.iterations() * fixture.input.size());
        state.counters["input_bytes"] = double(fixture.input.size());
      });

  for (int i = 1; i < argc - 3; ++i) argv[i] = argv[i + 3];
  argc -= 3;
  benchmark::Initialize(&argc, argv);
  if (benchmark::ReportUnrecognizedArguments(argc, argv)) {
    benchmark::Shutdown();
    return 1;
  }
  benchmark::RunSpecifiedBenchmarks();
  benchmark::Shutdown();
  return 0;
}
