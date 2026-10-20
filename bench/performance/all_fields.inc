namespace all_fields {

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

auto run_hosq() -> std::array<std::uint64_t, 4> {
  HosqRoot root;
  const auto result =
      hosq::decode_into<hosq::json::FastReader<>>(root, std::string_view{input});
  if (!result.is_ok()) std::abort();

  std::array<std::uint64_t, 4> checksum{};
  for (const auto& row : root.value()) {
    const auto& payload = row.payload.value().value();
    checksum[0] += row.id.value().value() + payload.count.value().value();
    for (const auto value : payload.values.value().value()) checksum[0] += value;
    checksum[1] += std::bit_cast<std::uint64_t>(payload.ratio.value().value());
    checksum[2] += payload.name.value().value().size();
    checksum[3] += payload.ok.value().value();
  }
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

}  // namespace all_fields
