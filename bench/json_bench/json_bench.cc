#include <benchmark/benchmark.h>
#include <yyjson.h>

#include <cmath>
#include <cstdint>
#include <hosq/hosq.hh>
#include <hosq/json/fast_reader.hh>
#include <hosq/json/simple_reader.hh>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

using namespace hosq;
using namespace std::string_view_literals;

namespace {

[[maybe_unused]] constexpr std::string_view json_whitespace = R"(
{
   "fixed_object": {
      "int_array": [0, 1, 2, 3, 4, 5, 6],
      "float_array": [0.1, 0.2, 0.3, 0.4, 0.5, 0.6],
      "double_array": [3288398.238, 233e22, 289e-1, 0.928759872, 0.22222848, 0.1, 0.2, 0.3, 0.4]
   },
   "fixed_name_object": {
      "name0": "James",
      "name1": "Abraham",
      "name2": "Susan",
      "name3": "Frank",
      "name4": "Alicia"
   },
   "another_object": {
      "string": "here is some text",
      "another_string": "Hello World",
      "escaped_text": "{\"some key\":\"some string value\"}",
      "boolean": false,
      "nested_object": {
         "v3s": [[0.12345, 0.23456, 0.001345],
                  [0.3894675, 97.39827, 297.92387],
                  [18.18, 87.289, 2988.298]],
         "id": "298728949872"
      }
   },
   "string_array": ["Cat", "Dog", "Elephant", "Tiger"],
   "string": "Hello world",
   "number": 3.14,
   "boolean": true,
   "another_bool": false
}
)";

[[maybe_unused]] constexpr std::string_view json_minified =
    R"({"fixed_object":{"int_array":[0,1,2,3,4,5,6],"float_array":[0.1,0.2,0.3,0.4,0.5,0.6],"double_array":[3288398.238,2.33e+24,28.9,0.928759872,0.22222848,0.1,0.2,0.3,0.4]},"fixed_name_object":{"name0":"James","name1":"Abraham","name2":"Susan","name3":"Frank","name4":"Alicia"},"another_object":{"string":"here is some text","another_string":"Hello World","escaped_text":"{\"some key\":\"some string value\"}","boolean":false,"nested_object":{"v3s":[[0.12345,0.23456,0.001345],[0.3894675,97.39827,297.92387],[18.18,87.289,2988.298]],"id":"298728949872"}},"string_array":["Cat","Dog","Elephant","Tiger"],"string":"Hello world","number":3.14,"boolean":true,"another_bool":false})";

constexpr int kIntArraySum = 21;
constexpr double kFloatArraySum = 2.1;
constexpr size_t kDoubleArrayCount = 9;
constexpr double kDoubleArraySecond = 2.33e24;
constexpr size_t kNameTotalLength = 28;
constexpr size_t kStringArrayTotalLength = 19;
constexpr double kV3Sum = 3489.8379625;
constexpr std::string_view kNestedId = "298728949872"sv;
constexpr std::string_view kTopLevelString = "Hello world"sv;
constexpr std::string_view kAnotherString = "Hello World"sv;
constexpr bool kTopLevelBoolean = true;
constexpr bool kAnotherBoolean = false;

using SkipKey = Elem<scan::scalar | decode::skip, noop_encoder>;
using SkipVal = Elem<decode::skip, noop_encoder>;

template <StringLiteral Name>
using FieldKey = Elem<scan::scalar | decode::string_view | match::literal<Name>,
                      noop_encoder>;

template <class T>
using NumberField =
    Elem<scan::scalar | decode::number<T> | store::assign, noop_encoder>;

using StringField =
    Elem<scan::scalar | decode::string_view | store::assign, noop_encoder>;

using BoolField =
    Elem<scan::scalar | decode::boolean | store::assign, noop_encoder>;

template <class T, size_t N>
using NumberArrayField =
    Elem<scan::array | decode::each<scan::scalar | decode::number_into<T>, N>,
         noop_encoder>;

using StringArrayField =
    Elem<scan::array |
             decode::each<scan::scalar | decode::string_view | store::assign, 4>,
         noop_encoder>;

using V3ArrayField =
    Elem<scan::array |
             decode::each<
                 scan::array |
                     decode::each<scan::scalar | decode::number_into<double>, 3>,
                 3>,
         noop_encoder>;

struct FixedObjectSchema {
  Entry<FieldKey<"int_array">, NumberArrayField<int, 7>> int_array;
  Entry<FieldKey<"float_array">, NumberArrayField<double, 6>> float_array;
  Entry<FieldKey<"double_array">, NumberArrayField<double, 9>> double_array;
};

struct FixedNameObjectSchema {
  Entry<FieldKey<"name0">, StringField> name0;
  Entry<FieldKey<"name1">, StringField> name1;
  Entry<FieldKey<"name2">, StringField> name2;
  Entry<FieldKey<"name3">, StringField> name3;
  Entry<FieldKey<"name4">, StringField> name4;
};

struct NestedObjectSchema {
  Entry<FieldKey<"v3s">, V3ArrayField> v3s;
  Entry<FieldKey<"id">, StringField> id;
};

using NestedObjectField =
    Elem<scan::object | decode::to<NestedObjectSchema> | store::assign,
         noop_encoder>;

struct AnotherObjectSchema {
  Entry<FieldKey<"string">, StringField> string;
  Entry<FieldKey<"another_string">, StringField> another_string;
  Entry<FieldKey<"escaped_text">, StringField> escaped_text;
  Entry<FieldKey<"boolean">, BoolField> boolean;
  Entry<FieldKey<"nested_object">, NestedObjectField> nested_object;
};

using FixedObjectField =
    Elem<scan::object | decode::to<FixedObjectSchema> | store::assign,
         noop_encoder>;
using FixedNameObjectField =
    Elem<scan::object | decode::to<FixedNameObjectSchema> | store::assign,
         noop_encoder>;
using AnotherObjectField =
    Elem<scan::object | decode::to<AnotherObjectSchema> | store::assign,
         noop_encoder>;

struct RootSchema {
  Entry<FieldKey<"fixed_object">, FixedObjectField> fixed_object;
  Entry<FieldKey<"fixed_name_object">, FixedNameObjectField> fixed_name_object;
  Entry<FieldKey<"another_object">, AnotherObjectField> another_object;
  Entry<FieldKey<"string_array">, StringArrayField> string_array;
  Entry<FieldKey<"string">, StringField> string;
  Entry<FieldKey<"number">, NumberField<double>> number;
  Entry<FieldKey<"boolean">, BoolField> boolean;
  Entry<FieldKey<"another_bool">, BoolField> another_bool;
  // Entry<SkipKey, SkipVal> skip;
};

struct Stats {
  int int_sum{0};
  double float_sum{0.0};
  size_t double_count{0};
  double second_double{0.0};
  size_t name_total_len{0};
  size_t string_array_total_len{0};
  double v3_sum{0.0};
  std::string nested_id{};
  std::string top_level_string{};
  std::string another_string{};
  bool top_level_boolean{false};
  bool another_boolean{false};
};

auto unquote_json_string(std::string_view value) -> std::string_view {
  if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
    return value.substr(1, value.size() - 2);
  }
  return value;
}

auto make_stats(const RootSchema& root) -> Stats {
  Stats stats;

  for (const auto value : *root.fixed_object->int_array) {
    stats.int_sum += value;
  }
  for (const auto value : *root.fixed_object->float_array) {
    stats.float_sum += value;
  }

  const auto& doubles = *root.fixed_object->double_array;
  stats.double_count = doubles.size();
  if (doubles.size() > 1) {
    stats.second_double = doubles[1];
  }

  const auto& names = *root.fixed_name_object;
  stats.name_total_len += unquote_json_string(*names.name0).size();
  stats.name_total_len += unquote_json_string(*names.name1).size();
  stats.name_total_len += unquote_json_string(*names.name2).size();
  stats.name_total_len += unquote_json_string(*names.name3).size();
  stats.name_total_len += unquote_json_string(*names.name4).size();

  for (const auto value : *root.string_array) {
    stats.string_array_total_len += unquote_json_string(value).size();
  }

  for (const auto& row : *root.another_object->nested_object->v3s) {
    for (const auto value : row) {
      stats.v3_sum += value;
    }
  }

  stats.nested_id =
      std::string{unquote_json_string(*root.another_object->nested_object->id)};
  stats.top_level_string = std::string{unquote_json_string(*root.string)};
  stats.another_string =
      std::string{unquote_json_string(*root.another_object->another_string)};
  stats.top_level_boolean = *root.boolean;
  stats.another_boolean = *root.another_bool;
  return stats;
}

template <class Reader>
auto collect_hosq(std::string_view input) -> Stats {
  RootSchema root;
  const auto result = decode_into<Reader>(root, input);
  if (!result.is_ok()) {
    std::cerr << "hosq parse error: code=" << static_cast<int>(result.code)
              << " offset=" << result.offset << "\n";
    return {};
  }
  return make_stats(root);
}

auto collect_yyjson(std::string_view input) -> Stats {
  yyjson_doc* doc = yyjson_read(input.data(), input.size(), 0);
  if (!doc) {
    std::cerr << "yyjson parse failed\n";
    return {};
  }

  Stats stats;
  yyjson_val* root = yyjson_doc_get_root(doc);
  yyjson_val* fixed_object = yyjson_obj_get(root, "fixed_object");
  yyjson_val* fixed_name_object = yyjson_obj_get(root, "fixed_name_object");
  yyjson_val* another_object = yyjson_obj_get(root, "another_object");
  yyjson_val* nested_object = yyjson_obj_get(another_object, "nested_object");

  size_t idx = 0;
  size_t max = 0;
  yyjson_val* value = nullptr;

  yyjson_arr_foreach(yyjson_obj_get(fixed_object, "int_array"), idx, max,
                     value) {
    stats.int_sum += static_cast<int>(yyjson_get_int(value));
  }

  idx = 0;
  max = 0;
  yyjson_arr_foreach(yyjson_obj_get(fixed_object, "float_array"), idx, max,
                     value) {
    stats.float_sum += yyjson_get_num(value);
  }

  idx = 0;
  max = 0;
  yyjson_arr_foreach(yyjson_obj_get(fixed_object, "double_array"), idx, max,
                     value) {
    if (idx == 1) {
      stats.second_double = yyjson_get_num(value);
    }
    ++stats.double_count;
  }

  stats.name_total_len += std::string_view{yyjson_get_str(yyjson_obj_get(
                                               fixed_name_object, "name0"))}
                              .size();
  stats.name_total_len += std::string_view{yyjson_get_str(yyjson_obj_get(
                                               fixed_name_object, "name1"))}
                              .size();
  stats.name_total_len += std::string_view{yyjson_get_str(yyjson_obj_get(
                                               fixed_name_object, "name2"))}
                              .size();
  stats.name_total_len += std::string_view{yyjson_get_str(yyjson_obj_get(
                                               fixed_name_object, "name3"))}
                              .size();
  stats.name_total_len += std::string_view{yyjson_get_str(yyjson_obj_get(
                                               fixed_name_object, "name4"))}
                              .size();

  idx = 0;
  max = 0;
  yyjson_arr_foreach(yyjson_obj_get(root, "string_array"), idx, max, value) {
    stats.string_array_total_len +=
        std::string_view{yyjson_get_str(value)}.size();
  }

  idx = 0;
  max = 0;
  yyjson_val* row = nullptr;
  yyjson_arr_foreach(yyjson_obj_get(nested_object, "v3s"), idx, max, row) {
    size_t inner_idx = 0;
    size_t inner_max = 0;
    yyjson_val* inner = nullptr;
    yyjson_arr_foreach(row, inner_idx, inner_max, inner) {
      stats.v3_sum += yyjson_get_num(inner);
    }
  }

  stats.nested_id = yyjson_get_str(yyjson_obj_get(nested_object, "id"));
  stats.top_level_string = yyjson_get_str(yyjson_obj_get(root, "string"));
  stats.another_string =
      yyjson_get_str(yyjson_obj_get(another_object, "another_string"));
  stats.top_level_boolean = yyjson_get_bool(yyjson_obj_get(root, "boolean"));
  stats.another_boolean = yyjson_get_bool(yyjson_obj_get(root, "another_bool"));

  yyjson_doc_free(doc);
  return stats;
}

auto is_expected(const Stats& stats) -> bool {
  return stats.int_sum == kIntArraySum &&
         std::abs(stats.float_sum - kFloatArraySum) < 1e-12 &&
         stats.double_count == kDoubleArrayCount &&
         std::abs((stats.second_double - kDoubleArraySecond) /
                  kDoubleArraySecond) < 1e-12 &&
         stats.name_total_len == kNameTotalLength &&
         stats.string_array_total_len == kStringArrayTotalLength &&
         std::abs(stats.v3_sum - kV3Sum) < 1e-12 &&
         stats.nested_id == kNestedId &&
         stats.top_level_string == kTopLevelString &&
         stats.another_string == kAnotherString &&
         stats.top_level_boolean == kTopLevelBoolean &&
         stats.another_boolean == kAnotherBoolean;
}

void report_stats(std::string_view label, const Stats& stats) {
  std::cerr << label << ": int_sum=" << stats.int_sum
            << " float_sum=" << stats.float_sum
            << " double_count=" << stats.double_count
            << " second_double=" << stats.second_double
            << " name_total_len=" << stats.name_total_len
            << " string_array_total_len=" << stats.string_array_total_len
            << " v3_sum=" << stats.v3_sum << " nested_id=" << stats.nested_id
            << " top_level_string=" << stats.top_level_string
            << " another_string=" << stats.another_string
            << " top_level_boolean=" << stats.top_level_boolean
            << " another_boolean=" << stats.another_boolean << "\n";
}

auto verify_case(std::string_view label, const Stats& stats) -> bool {
  if (is_expected(stats)) {
    return true;
  }
  report_stats(label, stats);
  return false;
}

template <class Reader>
void bench_hosq(benchmark::State& state, std::string_view input) {
  for (auto _ : state) {
    RootSchema root;
    const auto result = decode_into<Reader>(root, input);
    benchmark::DoNotOptimize(root);
    if (!result.is_ok()) {
      state.SkipWithError("hosq parse failed");
      break;
    }
  }
  state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) *
                          static_cast<int64_t>(input.size()));
}

void bench_yyjson(benchmark::State& state, std::string_view input) {
  for (auto _ : state) {
    auto stats = collect_yyjson(input);
    benchmark::DoNotOptimize(stats);
  }
  state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) *
                          static_cast<int64_t>(input.size()));
}

static void BM_Minified_SimpleReader(benchmark::State& state) {
  bench_hosq<json::SimpleReader>(state, json_minified);
}

static void BM_Minified_FastReader(benchmark::State& state) {
  bench_hosq<json::FastReader<>>(state, json_minified);
}

static void BM_Minified_YYJSON(benchmark::State& state) {
  bench_yyjson(state, json_minified);
}

static void BM_Whitespace_SimpleReader(benchmark::State& state) {
  bench_hosq<json::SimpleReader>(state, json_whitespace);
}

static void BM_Whitespace_FastReader(benchmark::State& state) {
  bench_hosq<json::FastReader<>>(state, json_whitespace);
}

static void BM_Whitespace_YYJSON(benchmark::State& state) {
  bench_yyjson(state, json_whitespace);
}

BENCHMARK(BM_Minified_SimpleReader);
BENCHMARK(BM_Minified_FastReader);
BENCHMARK(BM_Minified_YYJSON);
BENCHMARK(BM_Whitespace_SimpleReader);
BENCHMARK(BM_Whitespace_FastReader);
BENCHMARK(BM_Whitespace_YYJSON);

}  // namespace

auto main(int argc, char** argv) -> int {
  if (!verify_case("simple/minified",
                   collect_hosq<json::SimpleReader>(json_minified)) ||
      !verify_case("fast/minified",
                   collect_hosq<json::FastReader<>>(json_minified)) ||
      !verify_case("yyjson/minified", collect_yyjson(json_minified)) ||
      !verify_case("simple/whitespace",
                   collect_hosq<json::SimpleReader>(json_whitespace)) ||
      !verify_case("fast/whitespace",
                   collect_hosq<json::FastReader<>>(json_whitespace)) ||
      !verify_case("yyjson/whitespace", collect_yyjson(json_whitespace))) {
    std::cerr << "json_bench verification failed\n";
    return 1;
  }

  ::benchmark::Initialize(&argc, argv);
  ::benchmark::RunSpecifiedBenchmarks();
  ::benchmark::Shutdown();
  return 0;
}
