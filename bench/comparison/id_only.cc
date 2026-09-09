#include <benchmark/benchmark.h>
#include <yyjson.h>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <glaze/glaze.hpp>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "hosq/hosq.hh"
#include "hosq/json/fast_reader.hh"

static_assert(YYJSON_VERSION_MAJOR == 0 && YYJSON_VERSION_MINOR == 12 &&
              YYJSON_VERSION_PATCH == 0);
static_assert(glz::version.major == 8 && glz::version.minor == 3 &&
              glz::version.patch == 0);

namespace id_only {

auto next_random(std::uint64_t& state) -> std::uint64_t {
  state += 0x9e3779b97f4a7c15ULL;
  auto value = state;
  value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
  value = (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
  return value ^ (value >> 31);
}

auto make_input(std::size_t rows) -> std::string {
  std::string json{"["};
  json.reserve(rows * 112);
  std::uint64_t random_state = 0x8f3c5a1d7b9246e0ULL;
  for (std::size_t i = 0; i < rows; ++i) {
    if (i != 0) json.push_back(',');
    json += R"({"id":)" + std::to_string(next_random(random_state));
    json += R"(,"payload":{"name":"record-)" +
            std::to_string(next_random(random_state));
    json += R"(","count":)" + std::to_string(next_random(random_state));
    json += R"(,"ratio":)" +
            std::to_string(next_random(random_state) % 100'000'000 / 100.0);
    json += R"(,"ok":)";
    json += (next_random(random_state) & 1) != 0 ? "true" : "false";
    json += R"(,"none":null,"values":[)";
    for (std::size_t value = 0; value < 4; ++value) {
      if (value != 0) json.push_back(',');
      json += std::to_string(next_random(random_state));
    }
    json += "]}}";
  }
  json.push_back(']');
  return json;
}

std::string input = make_input(2048);

struct GlazeRecord {
  std::uint64_t id{};
};

}  // namespace id_only

template <>
struct glz::meta<id_only::GlazeRecord> {
  using T = id_only::GlazeRecord;
  static constexpr auto value = object("id", &T::id);
};

namespace id_only {

using HosqId =
    hosq::Elem<hosq::scan::scalar | hosq::decode::number<std::uint64_t> |
                   hosq::store::assign,
               hosq::noop_encoder>;
using HosqSkipKey =
    hosq::Elem<hosq::scan::scalar | hosq::decode::skip, hosq::noop_encoder>;
using HosqSkipValue = hosq::Elem<hosq::decode::skip, hosq::noop_encoder>;

struct HosqRecord {
  hosq::Try<hosq::Entry<hosq::Key<"id">, HosqId>> id;
  hosq::Try<hosq::Entry<HosqSkipKey, HosqSkipValue>> skip;
};

using HosqRoot =
    hosq::Elem<hosq::scan::array |
                   hosq::decode::collect<hosq::scan::object |
                                         hosq::decode::to<HosqRecord>> |
                   hosq::store::assign,
               hosq::noop_encoder>;

auto run_glaze() -> std::uint64_t {
  std::vector<GlazeRecord> rows;
  constexpr auto options =
      glz::opts{.error_on_unknown_keys = false, .partial_read = false};
  const auto error = glz::read<options>(rows, input);
  if (error || error.count != input.size()) std::abort();

  std::uint64_t checksum{};
  for (const auto& row : rows) checksum += row.id;
  return checksum;
}

auto run_hosq() -> std::uint64_t {
  HosqRoot root;
  const auto result =
      hosq::decode_into<hosq::json::FastReader<>>(root, std::string_view{input});
  if (!result.is_ok()) std::abort();

  std::uint64_t checksum{};
  for (const auto& row : root.value()) checksum += row.id.value().value();
  return checksum;
}

auto run_yyjson() -> std::uint64_t {
  yyjson_doc* document = yyjson_read(input.data(), input.size(), 0);
  if (document == nullptr) std::abort();
  yyjson_val* root = yyjson_doc_get_root(document);
  if (!yyjson_is_arr(root)) std::abort();

  std::uint64_t checksum{};
  std::size_t index, count;
  yyjson_val* row;
  yyjson_arr_foreach(root, index, count, row) {
    yyjson_val* id = yyjson_obj_get(row, "id");
    if (!yyjson_is_uint(id)) std::abort();
    checksum += yyjson_get_uint(id);
  }
  yyjson_doc_free(document);
  return checksum;
}

using Parser = std::uint64_t (*)();

void benchmark_parser(benchmark::State& state, Parser parser) {
  for (auto _ : state) {
    auto checksum = parser();
    benchmark::DoNotOptimize(checksum);
  }
  state.SetBytesProcessed(state.iterations() * input.size());
}

}  // namespace id_only

int main(int argc, char** argv) {
  using namespace id_only;
  const auto expected = run_hosq();
  if (run_glaze() != expected || run_yyjson() != expected) {
    std::cerr << "id_only output mismatch\n";
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
