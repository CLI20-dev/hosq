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
    const auto& payload = *row.payload;
    checksum[0] += *row.id + *payload.count;
    for (const auto value : *payload.values) checksum[0] += value;
    checksum[1] += std::bit_cast<std::uint64_t>(*payload.ratio);
    checksum[2] += payload.name->size();
    checksum[3] += *payload.ok;
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

}  // namespace all_fields_randomize_pretty
