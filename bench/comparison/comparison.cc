#include <benchmark/benchmark.h>
#include <yyjson.h>

#include <array>
#include <bit>
#include <fstream>
#include <glaze/glaze.hpp>
#include <hosq/hosq.hh>
#include <hosq/json/fast_reader.hh>
#include <iostream>
#include <string>
using namespace hosq;
using R = json::FastReader<>;
struct Stats {
  std::array<uint64_t, 10> v{};
  void add(const Stats& s) {
    for (size_t i = 0; i < v.size(); ++i) v[i] += s.v[i];
  }
  bool operator==(const Stats&) const = default;
};
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
    std::abort();
  };
  auto utf8 = [&](unsigned u) {
    if (u < 128)
      byte(u);
    else if (u < 2048) {
      byte(0xc0 | (u >> 6));
      byte(0x80 | (u & 63));
    } else if (u < 65536) {
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
    if (++i == s.size()) std::abort();
    c = s[i];
    switch (c) {
      case '"':
      case '\\':
      case '/':
        byte(c);
        break;
      case 'b':
        byte(8);
        break;
      case 'f':
        byte(12);
        break;
      case 'n':
        byte(10);
        break;
      case 'r':
        byte(13);
        break;
      case 't':
        byte(9);
        break;
      case 'u': {
        if (i + 4 >= s.size()) std::abort();
        unsigned u = 0;
        for (int j = 0; j < 4; ++j) u = (u << 4) | hex(s[++i]);
        if (u >= 0xd800 && u <= 0xdbff) {
          if (i + 6 >= s.size() || s[++i] != '\\' || s[++i] != 'u') std::abort();
          unsigned low = 0;
          for (int j = 0; j < 4; ++j) low = (low << 4) | hex(s[++i]);
          if (low < 0xdc00 || low > 0xdfff) std::abort();
          u = 0x10000 + ((u - 0xd800) << 10) + (low - 0xdc00);
        }
        utf8(u);
        break;
      }
      default:
        std::abort();
    }
  }
  out.v[5] += hash;
}
void hvisit(R& r, R::state_type& s, Stats& out) {
  auto type = r.current_token(s).type;
  if (type == TokenType::ObjectBegin) {
    ++out.v[8];
    for (;;) {
      if (!next_token<Expect::next_member_or_end>(r, s)) std::abort();
      if (r.current_token(s).type == TokenType::ObjectEnd) return;
      DecodeContext<void, R, TryPhase::first> c{r, s};
      ext::ScanScalar<R>{}(c);
      if (!next_token<ReadPosition::member_value>(r, s)) std::abort();
      hvisit(r, s, out);
    }
  }
  if (type == TokenType::ArrayBegin) {
    ++out.v[9];
    for (;;) {
      if (!next_token<Expect::next_element_or_end>(r, s)) std::abort();
      if (r.current_token(s).type == TokenType::ArrayEnd) return;
      hvisit(r, s, out);
    }
  }
  if (type != TokenType::Scalar) std::abort();
  ++out.v[0];
  auto& t = r.current_token(s);
  if (t.context == uint8_t(JsonTokenContext::string)) {
    DecodeContext<void, R, TryPhase::first> c{r, s};
    ext::ScanScalar<R>{}(c);
    string_stats({s.begin + t.offset, t.length}, true, out);
    return;
  }
  const char ch = s.begin[t.offset];
  if (ch == 't' || ch == 'f' || ch == 'n') {
    r.resolve_skip(s);
    out.v[6] += ch == 't';
    out.v[7] += ch == 'n';
    return;
  }
  const char* first = s.begin + t.offset;
  const char* end = s.end;
  const char* last;
  uint64_t integer = 0;
  if (ch == '-') {
    int64_t n{};
    auto z = R::parse_number(first, end, n);
    if (z.ec != std::errc{}) std::abort();
    integer = uint64_t(n);
    last = z.ptr;
  } else {
    auto z = R::parse_number(first, end, integer);
    if (z.ec != std::errc{}) std::abort();
    last = z.ptr;
  }
  if (last < end && (*last == '.' || *last == 'e' || *last == 'E')) {
    double n{};
    auto z = R::parse_number(first, end, n);
    if (z.ec != std::errc{}) std::abort();
    out.v[2] += std::bit_cast<uint64_t>(n);
    last = z.ptr;
  } else
    out.v[1] += integer;
  t.length = last - first;
  s.pos = last;
}
void yvisit(yyjson_val* v, Stats& out) {
  if (yyjson_is_obj(v)) {
    ++out.v[8];
    size_t i, m;
    yyjson_val *k, *x;
    yyjson_obj_foreach(v, i, m, k, x) yvisit(x, out);
    return;
  }
  if (yyjson_is_arr(v)) {
    ++out.v[9];
    size_t i, m;
    yyjson_val* x;
    yyjson_arr_foreach(v, i, m, x) yvisit(x, out);
    return;
  }
  ++out.v[0];
  if (yyjson_is_str(v)) {
    string_stats({yyjson_get_str(v), yyjson_get_len(v)}, false, out);
    return;
  }
  if (yyjson_is_uint(v))
    out.v[1] += yyjson_get_uint(v);
  else if (yyjson_is_sint(v))
    out.v[1] += uint64_t(yyjson_get_sint(v));
  else if (yyjson_is_real(v))
    out.v[2] += std::bit_cast<uint64_t>(yyjson_get_real(v));
  else if (yyjson_is_bool(v))
    out.v[6] += yyjson_get_bool(v);
  else if (yyjson_is_null(v))
    ++out.v[7];
  else
    std::abort();
}

// Shared consumer: count values/containers and hash decoded string values.
// Keys are used for selection, but are not part of the output digest.
struct Accumulator {
  Stats* out{};
};
struct VisitNumber {
  template <class>
  using storage_type = Accumulator;
  template <class C>
    requires(!std::is_const_v<C>)
  void operator()(C& c) const {
    uint64_t n{};
    if (!c.reader().template read_number<uint64_t>(c.reader_state(), n))
      std::abort();
    ++c.value().out->v[0];
    c.value().out->v[1] += n;
  }
};
using NumericField = Elem<step<VisitNumber>, noop_encoder>;
using SkipKey = Elem<scan::scalar | decode::skip, noop_encoder>;
using SkipVal = Elem<decode::skip, noop_encoder>;
struct Selected {
  Try<Entry<Key<"id">, NumericField>> id;
  Try<Entry<SkipKey, SkipVal>> skip;
};

// Fixed-schema control for medium/all: same digest as the generic visitor.
// It isolates application traversal from the Reader's static execution path.
enum class ScalarKind { uint, real, string, boolean, null };
template <ScalarKind K>
struct TypedScalar {
  template <class>
  using storage_type = Accumulator;
  template <class C>
    requires(!std::is_const_v<C>)
  void operator()(C& c) const {
    auto& out = *c.value().out;
    auto& r = c.reader();
    auto& st = c.reader_state();
    ++out.v[0];
    if constexpr (K == ScalarKind::uint) {
      uint64_t n{};
      if (!r.template read_number<uint64_t>(st, n)) std::abort();
      out.v[1] += n;
    } else if constexpr (K == ScalarKind::real) {
      double n{};
      if (!r.template read_number<double>(st, n)) std::abort();
      out.v[2] += std::bit_cast<uint64_t>(n);
    } else if constexpr (K == ScalarKind::string) {
      ext::ScanScalar<R>{}(c);
      auto& t = r.current_token(st);
      string_stats({st.begin + t.offset, t.length}, true, out);
    } else {
      const auto& t = r.current_token(st);
      if constexpr (K == ScalarKind::boolean)
        out.v[6] += st.begin[t.offset] == 't';
      else
        ++out.v[7];
      r.resolve_skip(st);
    }
  }
};
template <ScalarKind K>
using TypedField = Elem<step<TypedScalar<K>>, noop_encoder>;
struct TypedArray {
  template <class>
  using storage_type = Accumulator;
  template <class C>
    requires(!std::is_const_v<C>)
  void operator()(C& c) const {
    auto& r = c.reader();
    auto& st = c.reader_state();
    ++c.value().out->v[9];
    for (;;) {
      if (!next_token<Expect::next_element_or_end>(r, st)) std::abort();
      if (r.current_token(st).type == TokenType::ArrayEnd) return;
      TypedScalar<ScalarKind::uint>{}(c);
    }
  }
};
struct Payload {
  Try<Entry<Key<"name">, TypedField<ScalarKind::string>>> name;
  Try<Entry<Key<"count">, TypedField<ScalarKind::uint>>> count;
  Try<Entry<Key<"ratio">, TypedField<ScalarKind::real>>> ratio;
  Try<Entry<Key<"ok">, TypedField<ScalarKind::boolean>>> ok;
  Try<Entry<Key<"none">, TypedField<ScalarKind::null>>> none;
  Try<Entry<Key<"values">, Elem<step<TypedArray>, noop_encoder>>> values;
  void bind(Stats& out) {
    name.value().value().out = &out;
    count.value().value().out = &out;
    ratio.value().value().out = &out;
    ok.value().value().out = &out;
    none.value().value().out = &out;
    values.value().value().out = &out;
  }
};
struct TypedPayload {
  template <class>
  using storage_type = Accumulator;
  template <class C>
    requires(!std::is_const_v<C>)
  void operator()(C& c) const {
    auto& out = *c.value().out;
    ++out.v[8];
    Payload p;
    p.bind(out);
    if (!Decoder<R>::decode_nested(p, c.reader(), c.reader_state()).is_ok())
      std::abort();
  }
};
struct TypedRecord {
  Try<Entry<Key<"id">, TypedField<ScalarKind::uint>>> id;
  Try<Entry<Key<"payload">, Elem<step<TypedPayload>, noop_encoder>>> payload;
  void bind(Stats& out) {
    id.value().value().out = &out;
    payload.value().value().out = &out;
  }
};
struct Fixture {
  std::string name, input;
  bool sparse;
  Stats expected;
};
#include "glaze_adapter.hh"

std::vector<Fixture> fixtures;
Stats hosq_run(const Fixture& f) {
  R r;
  R::state_type s;
  r.reset(s, std::string_view(f.input));
  Stats out;
  if (!next_token<ReadPosition::value>(r, s)) std::abort();
  if (!f.sparse)
    hvisit(r, s, out);
  else {
    if (r.current_token(s).type != TokenType::ArrayBegin) std::abort();
    for (;;) {
      if (!next_token<Expect::next_element_or_end>(r, s)) std::abort();
      if (r.current_token(s).type == TokenType::ArrayEnd) break;
      Selected obj;
      obj.id.value().value().out = &out;
      if (!Decoder<R>::decode_nested(obj, r, s).is_ok()) std::abort();
    }
  }
  if (!next_token<Expect::document_end>(r, s) || !r.error(s).is_ok())
    std::abort();
  return out;
}

Stats typed_run(const Fixture& f) {
  R r;
  R::state_type s;
  r.reset(s, std::string_view(f.input));
  Stats out;
  if (!next_token<ReadPosition::value>(r, s)) std::abort();
  ++out.v[9];
  for (;;) {
    if (!next_token<Expect::next_element_or_end>(r, s)) std::abort();
    if (r.current_token(s).type == TokenType::ArrayEnd) break;
    ++out.v[8];
    TypedRecord obj;
    obj.bind(out);
    if (!Decoder<R>::decode_nested(obj, r, s).is_ok()) std::abort();
  }
  if (!next_token<Expect::document_end>(r, s) || !r.error(s).is_ok())
    std::abort();
  return out;
}
Stats yy_run(const Fixture& f) {
  auto* d = yyjson_read(f.input.data(), f.input.size(), 0);
  if (!d) std::abort();
  Stats out;
  auto* root = yyjson_doc_get_root(d);
  if (!f.sparse)
    yvisit(root, out);
  else {
    size_t i, n;
    yyjson_val* v;
    yyjson_arr_foreach(root, i, n, v) {
      auto* id = yyjson_obj_get(v, "id");
      if (!id) std::abort();
      yvisit(id, out);
    }
  }
  yyjson_doc_free(d);
  return out;
}
int main(int argc, char** argv) {
  if (argc < 2) {
    std::cerr << "usage: comparison FIXTURE_DIRECTORY [benchmark options]\n";
    return 2;
  }
  const auto env = [](const char* key) -> std::string {
    const auto* value = std::getenv(key);
    return value ? value : "";
  };
  const auto selected_case = env("HOSQ_COMPARE_CASE");
  const auto selected_engine = env("HOSQ_COMPARE_ENGINE");
  const auto check_engine = [&](std::string_view engine) {
    return selected_engine.empty() || engine == selected_engine;
  };
  std::string dir = argv[1];
  std::ifstream manifest(dir + "/manifest.txt");
  std::string name, file;
  int sparse;
  while (manifest >> name >> file >> sparse) {
    Fixture f;
    f.name = name;
    f.sparse = sparse;
    for (auto& n : f.expected.v)
      if (!(manifest >> n)) return 3;
    if (!selected_case.empty() && name != selected_case) continue;
    std::ifstream input(dir + "/" + file, std::ios::binary);
    f.input.assign(std::istreambuf_iterator<char>(input), {});
    if (f.input.empty()) return 4;
    fixtures.push_back(std::move(f));
  }
  if (fixtures.size() != (selected_case.empty() ? 12 : 1)) {
    std::cerr << "Expected 12 cases\n";
    return 5;
  }
  for (size_t i = 0; i < fixtures.size(); ++i) {
    auto& f = fixtures[i];
    const auto original_input = f.input;
    if ((check_engine("Glaze") && glaze_bench::run(f) != f.expected) ||
        (check_engine("HOSQ") && hosq_run(f) != f.expected) ||
        (check_engine("yyjson") && yy_run(f) != f.expected) ||
        f.input != original_input) {
      std::cerr << "Mismatch " << f.name << "\n";
      return 6;
    }
    std::cout << "verified " << f.name << " bytes=" << f.input.size()
              << " leaves=" << f.expected.v[0] << "\n";

    if (f.name == "medium/all") {
      if ((check_engine("HOSQtyped") && typed_run(f) != f.expected) ||
          (check_engine("Glazetyped") &&
           glaze_bench::typed_run(f) != f.expected)) {
        std::cerr << "Typed mismatch\n";
        return 8;
      }
      benchmark::RegisterBenchmark(
          "medium/all/HOSQtyped", [i](benchmark::State& s) {
            for (auto _ : s) {
              auto out = typed_run(fixtures[i]);
              benchmark::DoNotOptimize(out);
            }
            s.SetBytesProcessed(s.iterations() * fixtures[i].input.size());
          });
    }
    benchmark::RegisterBenchmark(
        (f.name + "/Glaze").c_str(), [i](benchmark::State& s) {
          for (auto _ : s) {
            auto out = glaze_bench::run(fixtures[i]);
            benchmark::DoNotOptimize(out);
          }
          s.SetBytesProcessed(s.iterations() * fixtures[i].input.size());
        });
    if (f.name == "medium/all") {
      benchmark::RegisterBenchmark(
          "medium/all/Glazetyped", [i](benchmark::State& s) {
            for (auto _ : s) {
              auto out = glaze_bench::typed_run(fixtures[i]);
              benchmark::DoNotOptimize(out);
            }
            s.SetBytesProcessed(s.iterations() * fixtures[i].input.size());
          });
    }
    for (bool hosq : {true, false}) {
      benchmark::RegisterBenchmark(
          (f.name + (hosq ? "/HOSQ" : "/yyjson")).c_str(),
          [i, hosq](benchmark::State& s) {
            const auto& f = fixtures[i];
            for (auto _ : s) {
              auto out = hosq ? hosq_run(f) : yy_run(f);
              benchmark::DoNotOptimize(out);
            }
            s.SetBytesProcessed(s.iterations() * f.input.size());
            s.counters["input_bytes"] = double(f.input.size());
          });
    }
  }
  // Remove our positional argument before Google Benchmark parses its flags.
  for (int i = 1; i < argc - 1; ++i) argv[i] = argv[i + 1];
  --argc;
  benchmark::Initialize(&argc, argv);
  if (benchmark::ReportUnrecognizedArguments(argc, argv)) return 7;
  benchmark::RunSpecifiedBenchmarks();
  benchmark::Shutdown();
}
