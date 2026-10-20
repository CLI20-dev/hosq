namespace id_only {

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

auto run_hosq() -> std::uint64_t {
  HosqRoot root;
  const auto result =
      hosq::decode_into<hosq::json::FastReader<>>(root, std::string_view{input});
  if (!result.is_ok()) std::abort();

  std::uint64_t checksum{};
  for (const auto& row : root.value()) checksum += row.id.value().value();
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
