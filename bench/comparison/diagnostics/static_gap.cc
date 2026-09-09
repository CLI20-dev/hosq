// Diagnostic only: Hordered requires the generator's exact field order.
#define main comparison_suite_main
#include "../comparison.cc"
#undef main

#include "hosq/detail/compiler.hh"

struct FixedArrayStep {
  template <class>
  using storage_type = Accumulator;
  template <class C>
    requires(!std::is_const_v<C>)
  void operator()(C& c) const {
    auto& r = c.reader();
    auto& s = c.reader_state();
    ++c.value().out->v[9];
    for (int i = 0; i < 4; ++i) {
      if (!next_token<Expect::next_element_or_end>(r, s)) std::abort();
      TypedScalar<ScalarKind::uint>{}(c);
    }
    if (!next_token<Expect::next_element_or_end>(r, s) ||
        r.current_token(s).type != TokenType::ArrayEnd)
      std::abort();
  }
};
struct FixedPayload {
  Try<Entry<Key<"name">, TypedField<ScalarKind::string>>> name;
  Try<Entry<Key<"count">, TypedField<ScalarKind::uint>>> count;
  Try<Entry<Key<"ratio">, TypedField<ScalarKind::real>>> ratio;
  Try<Entry<Key<"ok">, TypedField<ScalarKind::boolean>>> ok;
  Try<Entry<Key<"none">, TypedField<ScalarKind::null>>> none;
  Try<Entry<Key<"values">, Elem<step<FixedArrayStep>, noop_encoder>>> values;
  void bind(Stats& out) {
    name.value().value().out = &out;
    count.value().value().out = &out;
    ratio.value().value().out = &out;
    ok.value().value().out = &out;
    none.value().value().out = &out;
    values.value().value().out = &out;
  }
};
struct FixedPayloadStep {
  template <class>
  using storage_type = Accumulator;
  template <class C>
    requires(!std::is_const_v<C>)
  void operator()(C& c) const {
    auto& out = *c.value().out;
    ++out.v[8];
    FixedPayload p;
    p.bind(out);
    if (!Decoder<R>::decode_nested(p, c.reader(), c.reader_state()).is_ok())
      std::abort();
  }
};
struct FixedRecord {
  Try<Entry<Key<"id">, TypedField<ScalarKind::uint>>> id;
  Try<Entry<Key<"payload">, Elem<step<FixedPayloadStep>, noop_encoder>>> payload;
  void bind(Stats& out) {
    id.value().value().out = &out;
    payload.value().value().out = &out;
  }
};
Stats hfixed(const Fixture& f) {
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
    FixedRecord obj;
    obj.bind(out);
    if (!Decoder<R>::decode_nested(obj, r, s).is_ok()) std::abort();
  }
  if (!next_token<Expect::document_end>(r, s) || !r.error(s).is_ok())
    std::abort();
  return out;
}
struct VariablePayload {
  std::string name;
  uint64_t count{};
  double ratio{};
  bool ok{};
  std::nullptr_t none{};
  std::vector<uint64_t> values;
};
struct VariableRecord {
  uint64_t id{};
  VariablePayload payload;
};
template <>
struct glz::meta<VariablePayload> {
  using T = VariablePayload;
  static constexpr auto value =
      object("name", &T::name, "count", &T::count, "ratio", &T::ratio, "ok",
             &T::ok, "none", &T::none, "values", &T::values);
};
template <>
struct glz::meta<VariableRecord> {
  using T = VariableRecord;
  static constexpr auto value = object("id", &T::id, "payload", &T::payload);
};
Stats gvariable(Fixture& f) {
  std::vector<VariableRecord> rows;
  glaze_bench::read(rows, f);
  Stats out;
  ++out.v[9];
  for (auto& row : rows) {
    ++out.v[8];
    ++out.v[0];
    out.v[1] += row.id;
    auto& p = row.payload;
    ++out.v[8];
    ++out.v[0];
    string_stats(p.name, false, out);
    ++out.v[0];
    out.v[1] += p.count;
    ++out.v[0];
    out.v[2] += std::bit_cast<uint64_t>(p.ratio);
    ++out.v[0];
    out.v[6] += p.ok;
    ++out.v[0];
    ++out.v[7];
    ++out.v[9];
    for (auto v : p.values) {
      ++out.v[0];
      out.v[1] += v;
    }
  }
  return out;
}
Stats gcounts(Fixture& f) {
  std::vector<glaze_bench::Record> rows;
  glaze_bench::read(rows, f);
  Stats out;
  ++out.v[9];
  for (auto& row : rows) {
    ++out.v[8];
    ++out.v[0];
    out.v[1] += row.id;
    auto& p = row.payload;
    ++out.v[8];
    ++out.v[0];
    string_stats(p.name, false, out);
    ++out.v[0];
    out.v[1] += p.count;
    ++out.v[0];
    out.v[2] += std::bit_cast<uint64_t>(p.ratio);
    ++out.v[0];
    out.v[6] += p.ok;
    ++out.v[0];
    ++out.v[7];
    ++out.v[9];
    for (auto v : p.values) {
      ++out.v[0];
      out.v[1] += v;
    }
  }
  return out;
}

template <StringLiteral Key>
HOSQ_ALWAYS_INLINE void ordered_key(R& r, R::state_type& s) {
  if (!next_token<Expect::next_member_or_end>(r, s)) std::abort();
  const auto& t = r.current_token(s);
  constexpr auto k = Key.view();
  if (t.type != TokenType::Scalar ||
      t.context != uint8_t(JsonTokenContext::string))
    std::abort();
  const char* start = s.begin + t.offset;
  if (size_t(s.end - start) <= k.size() ||
      std::string_view(start, k.size()) != k || start[k.size()] != '"')
    std::abort();
  t.length = k.size();
  s.pos = start + k.size() + 1;
  if (!next_token<ReadPosition::member_value>(r, s)) std::abort();
}
Stats hordered(const Fixture& f) {
  R r;
  R::state_type s;
  r.reset(s, std::string_view(f.input));
  Stats out;
  Accumulator a{&out};
  DecodeContext<Accumulator, R, TryPhase::first> c{a, r, s};
  if (!next_token<ReadPosition::value>(r, s)) std::abort();
  ++out.v[9];
  for (;;) {
    if (!next_token<Expect::next_element_or_end>(r, s)) std::abort();
    if (r.current_token(s).type == TokenType::ArrayEnd) break;
    ++out.v[8];
    ordered_key<"id">(r, s);
    TypedScalar<ScalarKind::uint>{}(c);
    ordered_key<"payload">(r, s);
    ++out.v[8];
    ordered_key<"name">(r, s);
    TypedScalar<ScalarKind::string>{}(c);
    ordered_key<"count">(r, s);
    TypedScalar<ScalarKind::uint>{}(c);
    ordered_key<"ratio">(r, s);
    TypedScalar<ScalarKind::real>{}(c);
    ordered_key<"ok">(r, s);
    TypedScalar<ScalarKind::boolean>{}(c);
    ordered_key<"none">(r, s);
    TypedScalar<ScalarKind::null>{}(c);
    ordered_key<"values">(r, s);
    TypedArray{}(c);
    if (!next_token<Expect::next_member_or_end>(r, s) ||
        r.current_token(s).type != TokenType::ObjectEnd)
      std::abort();
    if (!next_token<Expect::next_member_or_end>(r, s) ||
        r.current_token(s).type != TokenType::ObjectEnd)
      std::abort();
  }
  if (!next_token<Expect::document_end>(r, s) || !r.error(s).is_ok())
    std::abort();
  return out;
}
struct PlainPayload {
  Entry<Key<"name">, TypedField<ScalarKind::string>> name;
  Entry<Key<"count">, TypedField<ScalarKind::uint>> count;
  Entry<Key<"ratio">, TypedField<ScalarKind::real>> ratio;
  Entry<Key<"ok">, TypedField<ScalarKind::boolean>> ok;
  Entry<Key<"none">, TypedField<ScalarKind::null>> none;
  Entry<Key<"values">, Elem<step<TypedArray>, noop_encoder>> values;
  void bind(Stats& out) {
    name.value().value().out = &out;
    count.value().value().out = &out;
    ratio.value().value().out = &out;
    ok.value().value().out = &out;
    none.value().value().out = &out;
    values.value().value().out = &out;
  }
};
struct TypedPlainPayload {
  template <class>
  using storage_type = Accumulator;
  template <class C>
    requires(!std::is_const_v<C>)
  void operator()(C& c) const {
    auto& out = *c.value().out;
    ++out.v[8];
    PlainPayload p;
    p.bind(out);
    if (!Decoder<R>::decode_nested(p, c.reader(), c.reader_state()).is_ok())
      std::abort();
  }
};
struct PlainRecord {
  Entry<Key<"id">, TypedField<ScalarKind::uint>> id;
  Entry<Key<"payload">, Elem<step<TypedPlainPayload>, noop_encoder>> payload;
  void bind(Stats& out) {
    id.value().value().out = &out;
    payload.value().value().out = &out;
  }
};

Stats hplain(const Fixture& f) {
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
    PlainRecord obj;
    obj.bind(out);
    if (!Decoder<R>::decode_nested(obj, r, s).is_ok()) std::abort();
  }
  if (!next_token<Expect::document_end>(r, s) || !r.error(s).is_ok())
    std::abort();
  return out;
}

struct PlainFixedPayload {
  Entry<Key<"name">, TypedField<ScalarKind::string>> name;
  Entry<Key<"count">, TypedField<ScalarKind::uint>> count;
  Entry<Key<"ratio">, TypedField<ScalarKind::real>> ratio;
  Entry<Key<"ok">, TypedField<ScalarKind::boolean>> ok;
  Entry<Key<"none">, TypedField<ScalarKind::null>> none;
  Entry<Key<"values">, Elem<step<FixedArrayStep>, noop_encoder>> values;
  void bind(Stats& out) {
    name.value().value().out = &out;
    count.value().value().out = &out;
    ratio.value().value().out = &out;
    ok.value().value().out = &out;
    none.value().value().out = &out;
    values.value().value().out = &out;
  }
};
struct TypedPlainFixedPayload {
  template <class>
  using storage_type = Accumulator;
  template <class C>
    requires(!std::is_const_v<C>)
  void operator()(C& c) const {
    auto& out = *c.value().out;
    ++out.v[8];
    PlainFixedPayload p;
    p.bind(out);
    if (!Decoder<R>::decode_nested(p, c.reader(), c.reader_state()).is_ok())
      std::abort();
  }
};
struct PlainFixedRecord {
  Entry<Key<"id">, TypedField<ScalarKind::uint>> id;
  Entry<Key<"payload">, Elem<step<TypedPlainFixedPayload>, noop_encoder>>
      payload;
  void bind(Stats& out) {
    id.value().value().out = &out;
    payload.value().value().out = &out;
  }
};

Stats hplain_fixed4(const Fixture& f) {
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
    PlainFixedRecord obj;
    obj.bind(out);
    if (!Decoder<R>::decode_nested(obj, r, s).is_ok()) std::abort();
  }
  if (!next_token<Expect::document_end>(r, s) || !r.error(s).is_ok())
    std::abort();
  return out;
}

template <class Number, ScalarKind Kind>
struct AccumulateNumber {
  template <class>
  using storage_type = Accumulator;

  template <class C>
    requires(!std::is_const_v<C>)
  void operator()(Number value, C& c) const {
    auto& out = *c.value().out;
    ++out.v[0];
    if constexpr (Kind == ScalarKind::uint)
      out.v[1] += value;
    else
      out.v[2] += std::bit_cast<uint64_t>(value);
  }
};

inline constexpr auto canonical_uint =
    scan::scalar | decode::number<uint64_t> |
    step<AccumulateNumber<uint64_t, ScalarKind::uint>>;
inline constexpr auto canonical_real =
    scan::scalar | decode::number<double> |
    step<AccumulateNumber<double, ScalarKind::real>>;

template <ScalarKind Kind>
struct CanonicalScalarPipeline {
  static constexpr auto value = step<TypedScalar<Kind>>;
};

template <>
struct CanonicalScalarPipeline<ScalarKind::uint> {
  static constexpr auto value = canonical_uint;
};

template <>
struct CanonicalScalarPipeline<ScalarKind::real> {
  static constexpr auto value = canonical_real;
};

template <ScalarKind Kind>
using CanonicalTypedField =
    Elem<CanonicalScalarPipeline<Kind>::value, noop_encoder>;

template <bool Fixed4>
struct CanonicalArray {
  template <class>
  using storage_type = Accumulator;

  template <class C>
    requires(!std::is_const_v<C>)
  void operator()(C& c) const {
    auto& r = c.reader();
    auto& st = c.reader_state();
    ++c.value().out->v[9];
    if constexpr (Fixed4) {
      for (int i = 0; i < 4; ++i) {
        if (!next_token<Expect::next_element_or_end>(r, st)) std::abort();
        canonical_uint.invoke(c);
      }
      if (!next_token<Expect::next_element_or_end>(r, st) ||
          r.current_token(st).type != TokenType::ArrayEnd)
        std::abort();
    } else {
      for (;;) {
        if (!next_token<Expect::next_element_or_end>(r, st)) std::abort();
        if (r.current_token(st).type == TokenType::ArrayEnd) return;
        canonical_uint.invoke(c);
      }
    }
  }
};

struct AccumulateFixedArray {
  template <class>
  using storage_type = Accumulator;

  template <class C>
    requires(!std::is_const_v<C>)
  void operator()(const std::array<uint64_t, 4>& values, C& c) const {
    auto& out = *c.value().out;
    ++out.v[9];
    out.v[0] += values.size();
    for (auto value : values) out.v[1] += value;
  }
};

template <bool Fixed4, bool Native>
inline constexpr auto canonical_array = [] {
  if constexpr (Native)
    return scan::array |
           decode::collect<scan::scalar | decode::number<uint64_t>, 4> |
           step<AccumulateFixedArray>;
  else
    return step<CanonicalArray<Fixed4>>;
}();

template <bool Fixed4, bool Native = false>
struct CanonicalPayload {
  Entry<Key<"name">, CanonicalTypedField<ScalarKind::string>> name;
  Entry<Key<"count">, CanonicalTypedField<ScalarKind::uint>> count;
  Entry<Key<"ratio">, CanonicalTypedField<ScalarKind::real>> ratio;
  Entry<Key<"ok">, CanonicalTypedField<ScalarKind::boolean>> ok;
  Entry<Key<"none">, CanonicalTypedField<ScalarKind::null>> none;
  Entry<Key<"values">, Elem<canonical_array<Fixed4, Native>, noop_encoder>>
      values;
  void bind(Stats& out) {
    name.value().value().out = &out;
    count.value().value().out = &out;
    ratio.value().value().out = &out;
    ok.value().value().out = &out;
    none.value().value().out = &out;
    values.value().value().out = &out;
  }
};

template <bool Fixed4, bool Native = false>
struct CanonicalPayloadStep {
  template <class>
  using storage_type = Accumulator;

  template <class C>
    requires(!std::is_const_v<C>)
  void operator()(C& c) const {
    auto& out = *c.value().out;
    ++out.v[8];
    CanonicalPayload<Fixed4, Native> p;
    p.bind(out);
    if (!Decoder<R>::decode_nested(p, c.reader(), c.reader_state()).is_ok())
      std::abort();
  }
};

template <bool Fixed4, bool Native = false>
struct CanonicalRecord {
  Entry<Key<"id">, CanonicalTypedField<ScalarKind::uint>> id;
  Entry<Key<"payload">,
        Elem<step<CanonicalPayloadStep<Fixed4, Native>>, noop_encoder>>
      payload;
  void bind(Stats& out) {
    id.value().value().out = &out;
    payload.value().value().out = &out;
  }
};

template <bool Fixed4, bool Native = false>
Stats hcanonical(const Fixture& f) {
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
    CanonicalRecord<Fixed4, Native> obj;
    obj.bind(out);
    if (!Decoder<R>::decode_nested(obj, r, s).is_ok()) std::abort();
  }
  if (!next_token<Expect::document_end>(r, s) || !r.error(s).is_ok())
    std::abort();
  return out;
}

int main(int argc, char** argv) {
  if (argc < 3) return 2;
  const std::string fixture_dir = argv[1];
  const std::string mode = argv[2];
  std::string selected_case = "medium/all";
  bool case_seen = false;
  int benchmark_argc = 1;
  for (int i = 3; i < argc; ++i) {
    const std::string_view arg = argv[i];
    if (arg.starts_with("--case=")) {
      if (case_seen || arg.size() == std::string_view("--case=").size()) {
        std::cerr << "Specify one non-empty --case=NAME\n";
        return 2;
      }
      selected_case = arg.substr(std::string_view("--case=").size());
      case_seen = true;
    } else {
      argv[benchmark_argc++] = argv[i];
    }
  }
  argc = benchmark_argc;

  Fixture f{};
  std::ifstream m(fixture_dir + "/manifest.txt");
  std::string n, file, target_file;
  int sparse;
  bool found = false;
  while (m >> n >> file >> sparse) {
    Stats expected;
    for (auto& v : expected.v) m >> v;
    if (n == selected_case) {
      f.name = n;
      f.sparse = sparse;
      f.expected = expected;
      target_file = file;
      found = true;
    }
  }
  if (!found || f.sparse) {
    std::cerr << "Missing non-sparse static case: " << selected_case << "\n";
    return 2;
  }
  std::ifstream input(fixture_dir + "/" + target_file, std::ios::binary);
  f.input.assign(std::istreambuf_iterator<char>(input), {});
  if (f.input.empty()) return 2;

  auto run = [&]() {
    if (mode == "yyjson") return yy_run(f);
    if (mode == "Hplain") return hplain(f);
    if (mode == "HplainFixed4") return hplain_fixed4(f);
    if (mode == "Hcanonical") return hcanonical<false>(f);
    if (mode == "HcanonicalFixed4") return hcanonical<true>(f);
    if (mode == "Hnative4") return hcanonical<true, true>(f);
    if (mode == "Horiginal") return typed_run(f);
    if (mode == "Hordered") return hordered(f);
    if (mode == "Hfixed4") return hfixed(f);
    if (mode == "Glazeoriginal") return glaze_bench::typed_run(f);
    if (mode == "Glazevariable") return gvariable(f);
    if (mode == "Glazecounts") return gcounts(f);
    std::abort();
  };
  if (run() != f.expected) {
    std::cerr << "Mismatch " << mode << "\n";
    return 3;
  }
  benchmark::RegisterBenchmark(mode.c_str(), [&](benchmark::State& s) {
    for (auto _ : s) {
      auto out = run();
      benchmark::DoNotOptimize(out);
    }
  });
  benchmark::Initialize(&argc, argv);
  benchmark::RunSpecifiedBenchmarks();
  benchmark::Shutdown();
}
