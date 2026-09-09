#include <benchmark/benchmark.h>
#include <yyjson.h>

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
#include <vector>

#include "hosq/hosq.hh"
#include "hosq/json/fast_reader.hh"

static_assert(YYJSON_VERSION_MAJOR == 0 && YYJSON_VERSION_MINOR == 12 &&
              YYJSON_VERSION_PATCH == 0);
static_assert(glz::version.major == 8 && glz::version.minor == 3 &&
              glz::version.patch == 0);

namespace partial_fields {

auto make_input(std::size_t rows) -> std::string {
  std::string json{"["};
  json.reserve(rows * 112);
  std::mt19937_64 generator{0x8f3c5a1d7b9246e0ULL};
  for (std::size_t i = 0; i < rows; ++i) {
    if (i != 0) json.push_back(',');
    json += R"({"id":)" + std::to_string(generator());
    json += R"(,"payload":{"name":"record-)" + std::to_string(generator());
    json += R"(","count":)" + std::to_string(generator());
    json += R"(,"ratio":)" + std::to_string(generator() % 100'000'000 / 100.0);
    json += R"(,"ok":)";
    json += (generator() & 1) != 0 ? "true" : "false";
    json += R"(,"none":null,"values":[)";
    for (std::size_t value = 0; value < 4; ++value) {
      if (value != 0) json.push_back(',');
      json += std::to_string(generator());
    }
    json += "]}}";
  }
  json.push_back(']');
  return json;
}

std::string input = make_input(2048);

struct GlazePayload {
  std::string_view name;
  std::uint64_t count{};
  double ratio{};
  bool ok{};
};

struct GlazeRecord {
  std::uint64_t id{};
  GlazePayload payload;
};

}  // namespace partial_fields

template <>
struct glz::meta<partial_fields::GlazePayload> {
  using T = partial_fields::GlazePayload;
  static constexpr auto value = object("name", &T::name, "count", &T::count,
                                       "ratio", &T::ratio, "ok", &T::ok);
};

template <>
struct glz::meta<partial_fields::GlazeRecord> {
  using T = partial_fields::GlazeRecord;
  static constexpr auto value = object("id", &T::id, "payload", &T::payload);
};

namespace partial_fields {

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
using HosqSkipKey =
    hosq::Elem<hosq::scan::scalar | hosq::decode::skip, hosq::noop_encoder>;
using HosqSkipValue = hosq::Elem<hosq::decode::skip, hosq::noop_encoder>;

struct HosqPayload {
  hosq::Try<hosq::Entry<hosq::Key<"name">, HosqString>> name;
  hosq::Try<hosq::Entry<hosq::Key<"count">, HosqNumber<std::uint64_t>>> count;
  hosq::Try<hosq::Entry<hosq::Key<"ratio">, HosqNumber<double>>> ratio;
  hosq::Try<hosq::Entry<hosq::Key<"ok">, HosqBool>> ok;
  hosq::Try<hosq::Entry<HosqSkipKey, HosqSkipValue>> skip;
};

using HosqPayloadValue =
    hosq::Elem<hosq::scan::object | hosq::decode::to<HosqPayload> |
                   hosq::store::assign,
               hosq::noop_encoder>;

struct HosqRecord {
  hosq::Try<hosq::Entry<hosq::Key<"id">, HosqNumber<std::uint64_t>>> id;
  hosq::Try<hosq::Entry<hosq::Key<"payload">, HosqPayloadValue>> payload;
  hosq::Try<hosq::Entry<HosqSkipKey, HosqSkipValue>> skip;
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
      glz::opts{.error_on_unknown_keys = false, .partial_read = false};
  const auto error = glz::read<options>(rows, input);
  if (error || error.count != input.size()) std::abort();

  std::array<std::uint64_t, 4> checksum{};
  for (const auto& row : rows) {
    checksum[0] += row.id + row.payload.count;
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
    const auto& payload = row.payload.value().value();
    checksum[0] += row.id.value().value() + payload.count.value().value();
    checksum[1] += std::bit_cast<std::uint64_t>(payload.ratio.value().value());
    checksum[2] += payload.name.value().value().size();
    checksum[3] += payload.ok.value().value();
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
    yyjson_val* id = yyjson_obj_get(row, "id");
    yyjson_val* payload = yyjson_obj_get(row, "payload");
    yyjson_val* name = yyjson_obj_get(payload, "name");
    yyjson_val* number = yyjson_obj_get(payload, "count");
    yyjson_val* ratio = yyjson_obj_get(payload, "ratio");
    yyjson_val* ok = yyjson_obj_get(payload, "ok");
    if (!yyjson_is_uint(id) || !yyjson_is_obj(payload) || !yyjson_is_str(name) ||
        !yyjson_is_uint(number) || !yyjson_is_real(ratio) || !yyjson_is_bool(ok))
      std::abort();

    checksum[0] += yyjson_get_uint(id) + yyjson_get_uint(number);
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

}  // namespace partial_fields

int main(int argc, char** argv) {
  using namespace partial_fields;
  const auto expected = run_hosq();
  if (run_glaze() != expected || run_yyjson() != expected) {
    std::cerr << "partial_fields output mismatch\n";
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
