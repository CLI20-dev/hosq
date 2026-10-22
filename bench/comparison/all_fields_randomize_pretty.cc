#include <benchmark/benchmark.h>
#include <yyjson.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <glaze/glaze.hpp>
#include <iostream>
#include <random>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "hosq/hosq.hh"
#include "hosq/json/fast_reader.hh"

static_assert(YYJSON_VERSION_MAJOR == 0 && YYJSON_VERSION_MINOR == 12 &&
              YYJSON_VERSION_PATCH == 0);
static_assert(glz::version.major == 8 && glz::version.minor == 3 &&
              glz::version.patch == 0);

namespace all_fields_randomize_pretty {

auto make_input(std::size_t rows) -> std::string {
  std::string json{"[\n"};
  json.reserve(rows * 180);
  std::mt19937_64 generator{0x8f3c5a1d7b9246e0ULL};
  std::mt19937_64 order_generator{0x21d4a76cb985e30fULL};
  for (std::size_t i = 0; i < rows; ++i) {
    if (i != 0) json += ",\n";
    const auto id = generator();
    const auto name = generator();
    const auto count = generator();
    const auto ratio = generator() % 100'000'000 / 100.0;
    const auto ok = (generator() & 1) != 0;
    std::array<std::uint64_t, 4> values{};
    for (auto& value : values) value = generator();

    std::array<std::string, 6> payload_members{
        R"(            "name": "record-)" + std::to_string(name) + '"',
        R"(            "count": )" + std::to_string(count),
        R"(            "ratio": )" + std::to_string(ratio),
        std::string{R"(            "ok": )"} + (ok ? "true" : "false"),
        R"(            "none": null)",
        R"(            "values": [
                )" +
            std::to_string(values[0]) + ",\n                " +
            std::to_string(values[1]) + ",\n                " +
            std::to_string(values[2]) + ",\n                " +
            std::to_string(values[3]) + "\n            ]",
    };
    std::shuffle(payload_members.begin(), payload_members.end(),
                 order_generator);

    std::string payload{"        \"payload\": {\n"};
    for (std::size_t member = 0; member < payload_members.size(); ++member) {
      if (member != 0) payload += ",\n";
      payload += payload_members[member];
    }
    payload += "\n        }";

    std::array<std::string, 2> record_members{
        R"(        "id": )" + std::to_string(id), std::move(payload)};
    if ((order_generator() & 1) != 0)
      std::swap(record_members[0], record_members[1]);

    json += "    {\n";
    json += record_members[0];
    json += ",\n";
    json += record_members[1];
    json += "\n    }";
  }
  json += "\n]";
  return json;
}

std::string input = make_input(2048);

struct GlazePayload {
  std::string_view name;
  std::uint64_t count{};
  double ratio{};
  bool ok{};
  std::nullptr_t none{};
  std::array<std::uint64_t, 4> values{};
};

struct GlazeRecord {
  std::uint64_t id{};
  GlazePayload payload;
};

}  // namespace all_fields_randomize_pretty

template <>
struct glz::meta<all_fields_randomize_pretty::GlazePayload> {
  using T = all_fields_randomize_pretty::GlazePayload;
  static constexpr auto value =
      object("name", &T::name, "count", &T::count, "ratio", &T::ratio, "ok",
             &T::ok, "none", &T::none, "values", &T::values);
};

template <>
struct glz::meta<all_fields_randomize_pretty::GlazeRecord> {
  using T = all_fields_randomize_pretty::GlazeRecord;
  static constexpr auto value = object("id", &T::id, "payload", &T::payload);
};

namespace all_fields_randomize_pretty {

template <class T>
using HosqNumber = hosq::Elem<hosq::scan::scalar | hosq::decode::number<T> |
                                  hosq::store::assign,
                              hosq::noop_encoder>;
using HosqString = hosq::Elem<hosq::scan::scalar | hosq::decode::string_view |
                                  hosq::store::assign,
                              hosq::noop_encoder>;
using HosqBool =
    hosq::Elem<hosq::scan::scalar | hosq::decode::boolean | hosq::store::assign,
               hosq::noop_encoder>;
using HosqNull = hosq::Elem<
    hosq::scan::scalar | hosq::decode::string_view |
        hosq::decode::custom<[](std::string_view value) -> std::nullptr_t {
          if (value != "null") std::abort();
          return nullptr;
        }> |
        hosq::store::assign,
    hosq::noop_encoder>;
using HosqValues = hosq::Elem<
    hosq::scan::array |
        hosq::decode::collect<
            hosq::scan::scalar | hosq::decode::number<std::uint64_t>, 4> |
        hosq::store::assign,
    hosq::noop_encoder>;

struct HosqPayload {
  hosq::Try<hosq::Entry<hosq::Key<"name">, HosqString>> name;
  hosq::Try<hosq::Entry<hosq::Key<"count">, HosqNumber<std::uint64_t>>> count;
  hosq::Try<hosq::Entry<hosq::Key<"ratio">, HosqNumber<double>>> ratio;
  hosq::Try<hosq::Entry<hosq::Key<"ok">, HosqBool>> ok;
  hosq::Try<hosq::Entry<hosq::Key<"none">, HosqNull>> none;
  hosq::Try<hosq::Entry<hosq::Key<"values">, HosqValues>> values;
};

using HosqPayloadValue =
    hosq::Elem<hosq::scan::object | hosq::decode::to<HosqPayload> |
                   hosq::store::assign,
               hosq::noop_encoder>;

struct HosqRecord {
  hosq::Try<hosq::Entry<hosq::Key<"id">, HosqNumber<std::uint64_t>>> id;
  hosq::Try<hosq::Entry<hosq::Key<"payload">, HosqPayloadValue>> payload;
};

using HosqRoot =
    hosq::Elem<hosq::scan::array |
                   hosq::decode::collect<hosq::scan::object |
                                         hosq::decode::to<HosqRecord>> |
                   hosq::store::assign,
               hosq::noop_encoder>;

auto run_glaze() -> std::array<std::uint64_t, 4> {
  std::vector<GlazeRecord> rows;
  constexpr auto options =
      glz::opts{.error_on_unknown_keys = true, .partial_read = false};
  const auto error = glz::read<options>(rows, input);
  if (error || error.count != input.size()) std::abort();

  std::array<std::uint64_t, 4> checksum{};
  for (const auto& row : rows) {
    checksum[0] += row.id + row.payload.count;
    for (const auto value : row.payload.values) checksum[0] += value;
    checksum[1] += std::bit_cast<std::uint64_t>(row.payload.ratio);
    checksum[2] += row.payload.name.size();
    checksum[3] += row.payload.ok;
  }
  return checksum;
}

auto run_hosq() -> std::array<std::uint64_t, 4> {
  HosqRoot root;
  const auto result =
      hosq::decode_into<hosq::json::FastReader<>>(root, std::string_view{input});
  if (!result.is_ok()) std::abort();

  std::array<std::uint64_t, 4> checksum{};
  for (const auto& row : root.value()) {
    const auto& payload = *row.payload;
    checksum[0] += *row.id + *payload.count;
    for (const auto value : *payload.values) checksum[0] += value;
    checksum[1] += std::bit_cast<std::uint64_t>(*payload.ratio);
    checksum[2] += payload.name->size();
    checksum[3] += *payload.ok;
  }
  return checksum;
}

auto run_yyjson() -> std::array<std::uint64_t, 4> {
  yyjson_doc* document = yyjson_read(input.data(), input.size(), 0);
  if (document == nullptr) std::abort();
  yyjson_val* root = yyjson_doc_get_root(document);
  if (!yyjson_is_arr(root)) std::abort();

  std::array<std::uint64_t, 4> checksum{};
  std::size_t index, count;
  yyjson_val* row;
  yyjson_arr_foreach(root, index, count, row) {
    if (!yyjson_is_obj(row) || yyjson_obj_size(row) != 2) std::abort();
    yyjson_val* id = yyjson_obj_get(row, "id");
    yyjson_val* payload = yyjson_obj_get(row, "payload");
    yyjson_val* name = yyjson_obj_get(payload, "name");
    yyjson_val* number = yyjson_obj_get(payload, "count");
    yyjson_val* ratio = yyjson_obj_get(payload, "ratio");
    yyjson_val* ok = yyjson_obj_get(payload, "ok");
    yyjson_val* none = yyjson_obj_get(payload, "none");
    yyjson_val* values = yyjson_obj_get(payload, "values");
    if (!yyjson_is_uint(id) || !yyjson_is_obj(payload) ||
        yyjson_obj_size(payload) != 6 || !yyjson_is_str(name) ||
        !yyjson_is_uint(number) || !yyjson_is_real(ratio) ||
        !yyjson_is_bool(ok) || !yyjson_is_null(none) || !yyjson_is_arr(values) ||
        yyjson_arr_size(values) != 4)
      std::abort();

    checksum[0] += yyjson_get_uint(id) + yyjson_get_uint(number);
    for (std::size_t value_index = 0; value_index < 4; ++value_index) {
      yyjson_val* value = yyjson_arr_get(values, value_index);
      if (!yyjson_is_uint(value)) std::abort();
      checksum[0] += yyjson_get_uint(value);
    }
    checksum[1] += std::bit_cast<std::uint64_t>(yyjson_get_real(ratio));
    checksum[2] += yyjson_get_len(name);
    checksum[3] += yyjson_get_bool(ok);
  }
  yyjson_doc_free(document);
  return checksum;
}

using Parser = std::array<std::uint64_t, 4> (*)();

void benchmark_parser(benchmark::State& state, Parser parser) {
  for (auto _ : state) {
    auto checksum = parser();
    benchmark::DoNotOptimize(checksum);
  }
  state.SetBytesProcessed(state.iterations() * input.size());
}

}  // namespace all_fields_randomize_pretty

int main(int argc, char** argv) {
  using namespace all_fields_randomize_pretty;
  const auto expected = run_hosq();
  if (run_glaze() != expected || run_yyjson() != expected) {
    std::cerr << "all_fields_randomize_pretty output mismatch\n";
    return 1;
  }

  benchmark::RegisterBenchmark("HOSQ", benchmark_parser, run_hosq);
  benchmark::RegisterBenchmark("Glaze", benchmark_parser, run_glaze);
  benchmark::RegisterBenchmark("yyjson", benchmark_parser, run_yyjson);
  benchmark::Initialize(&argc, argv);
  if (benchmark::ReportUnrecognizedArguments(argc, argv)) return 2;
  benchmark::RunSpecifiedBenchmarks();
  benchmark::Shutdown();
}
