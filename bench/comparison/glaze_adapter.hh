// Included after the shared Fixture/Stats definitions in comparison.cc.
// Public Glaze APIs only; pinned to the version recorded by CMake.
namespace glaze_bench {
struct Id {
  uint64_t id{};
};
#ifdef HOSQ_COMPARISON_DIAGNOSTICS
struct Payload {
  std::string name;
  uint64_t count{};
  double ratio{};
  bool ok{};
  std::nullptr_t none{};
  std::array<uint64_t, 4> values{};
};
struct Record {
  uint64_t id{};
  Payload payload;
};
#endif

}  // namespace glaze_bench
// Explicit metadata avoids compiler-dependent aggregate reflection (nullptr_t).
template <>
struct glz::meta<glaze_bench::Id> {
  using T = glaze_bench::Id;
  static constexpr auto value = object("id", &T::id);
};
#ifdef HOSQ_COMPARISON_DIAGNOSTICS
template <>
struct glz::meta<glaze_bench::Payload> {
  using T = glaze_bench::Payload;
  static constexpr auto value =
      object("name", &T::name, "count", &T::count, "ratio", &T::ratio, "ok",
             &T::ok, "none", &T::none, "values", &T::values);
};
template <>
struct glz::meta<glaze_bench::Record> {
  using T = glaze_bench::Record;
  static constexpr auto value = object("id", &T::id, "payload", &T::payload);
};
#endif
namespace glaze_bench {
void visit(const glz::generic_u64& value, Stats& out) {
  if (value.is_object()) {
    ++out.v[8];
    for (const auto& [key, child] : value.get_object()) visit(child, out);
  } else if (value.is_array()) {
    ++out.v[9];
    for (const auto& child : value.get_array()) visit(child, out);
  } else {
    ++out.v[0];
    if (value.is_string())
      string_stats(value.get<std::string>(), false, out);
    else if (value.is_uint64())
      out.v[1] += value.get<uint64_t>();
    else if (value.is_int64())
      out.v[1] += uint64_t(value.get<int64_t>());
    else if (value.is_double())
      out.v[2] += std::bit_cast<uint64_t>(value.get<double>());
    else if (value.is_boolean())
      out.v[6] += value.get<bool>();
    else if (value.is_null())
      ++out.v[7];
    else
      std::abort();
  }
}

template <bool Sparse = false, class T>
void read(T& output, Fixture& f) {
  constexpr auto options = glz::opts{.error_on_unknown_keys = !Sparse};
  const auto original_size = f.input.size();
  // Official recommended mutable std::string path: Glaze temporarily adds
  // padding and restores size. That work is inside the timed call.
  const auto ec = glz::read<options>(output, f.input);
  if (ec || ec.count != original_size || f.input.size() != original_size) {
    std::cerr << "Glaze failed/full input not consumed: " << f.name << ": "
              << glz::format_error(ec, f.input) << '\n';
    std::abort();
  }
}

Stats run(Fixture& f) {
  Stats out;
  if (f.sparse) {
    std::vector<Id> rows;
    read<true>(rows, f);
    out.v[0] = rows.size();
    for (const auto& row : rows) out.v[1] += row.id;
  } else {
    glz::generic_u64 root;
    read(root, f);
    visit(root, out);
  }
  return out;
}

#ifdef HOSQ_COMPARISON_DIAGNOSTICS
Stats typed_run(Fixture& f) {
  std::vector<Record> rows;
  read(rows, f);
  Stats out;
  out.v[9] = 1 + rows.size();
  out.v[8] = 2 * rows.size();
  out.v[0] = 10 * rows.size();
  for (const auto& row : rows) {
    const auto& p = row.payload;
    out.v[1] += row.id + p.count;
    for (auto v : p.values) out.v[1] += v;
    out.v[2] += std::bit_cast<uint64_t>(p.ratio);
    string_stats(p.name, false, out);
    out.v[6] += p.ok;
    ++out.v[7];
  }
  return out;
}
#endif
}  // namespace glaze_bench
