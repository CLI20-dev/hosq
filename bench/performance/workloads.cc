// Fixed HOSQ-only copies of bench/comparison valid-input workloads.
#include <benchmark/benchmark.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <random>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "hosq/hosq.hh"
#include "hosq/json/fast_reader.hh"

// Workload definitions require HOSQ declarations above.
#include "all_fields.inc"
#include "all_fields_pretty.inc"
#include "all_fields_randomize.inc"
#include "all_fields_randomize_pretty.inc"
#include "id_only.inc"
#include "id_only_pretty.inc"
#include "partial_fields.inc"
#include "partial_fields_pretty.inc"

template <class T>
void print_checksum(const T& value) {
  if constexpr (std::is_integral_v<T>)
    std::cout << value;
  else {
    for (auto v : value) std::cout << v << ',';
  }
}
std::uint64_t input_hash(std::string_view s) {
  std::uint64_t h = 14695981039346656037ULL;
  for (unsigned char c : s) {
    h ^= c;
    h *= 1099511628211ULL;
  }
  return h;
}
int main(int argc, char** argv) {
  bool verify = argc == 2 && std::string_view(argv[1]) == "--verify-only";
  std::cout << "id_only bytes=" << id_only::input.size()
            << " fnv1a64=" << input_hash(id_only::input) << " checksum=";
  print_checksum(id_only::run_hosq());
  std::cout << "\n";
  benchmark::RegisterBenchmark("id_only", id_only::benchmark_parser,
                               id_only::run_hosq);
  std::cout << "partial_fields bytes=" << partial_fields::input.size()
            << " fnv1a64=" << input_hash(partial_fields::input) << " checksum=";
  print_checksum(partial_fields::run_hosq());
  std::cout << "\n";
  benchmark::RegisterBenchmark("partial_fields",
                               partial_fields::benchmark_parser,
                               partial_fields::run_hosq);
  std::cout << "all_fields bytes=" << all_fields::input.size()
            << " fnv1a64=" << input_hash(all_fields::input) << " checksum=";
  print_checksum(all_fields::run_hosq());
  std::cout << "\n";
  benchmark::RegisterBenchmark("all_fields", all_fields::benchmark_parser,
                               all_fields::run_hosq);
  std::cout << "all_fields_randomize bytes="
            << all_fields_randomize::input.size()
            << " fnv1a64=" << input_hash(all_fields_randomize::input)
            << " checksum=";
  print_checksum(all_fields_randomize::run_hosq());
  std::cout << "\n";
  benchmark::RegisterBenchmark("all_fields_randomize",
                               all_fields_randomize::benchmark_parser,
                               all_fields_randomize::run_hosq);
  std::cout << "id_only_pretty bytes=" << id_only_pretty::input.size()
            << " fnv1a64=" << input_hash(id_only_pretty::input) << " checksum=";
  print_checksum(id_only_pretty::run_hosq());
  std::cout << "\n";
  benchmark::RegisterBenchmark("id_only_pretty",
                               id_only_pretty::benchmark_parser,
                               id_only_pretty::run_hosq);
  std::cout << "partial_fields_pretty bytes="
            << partial_fields_pretty::input.size()
            << " fnv1a64=" << input_hash(partial_fields_pretty::input)
            << " checksum=";
  print_checksum(partial_fields_pretty::run_hosq());
  std::cout << "\n";
  benchmark::RegisterBenchmark("partial_fields_pretty",
                               partial_fields_pretty::benchmark_parser,
                               partial_fields_pretty::run_hosq);
  std::cout << "all_fields_pretty bytes=" << all_fields_pretty::input.size()
            << " fnv1a64=" << input_hash(all_fields_pretty::input)
            << " checksum=";
  print_checksum(all_fields_pretty::run_hosq());
  std::cout << "\n";
  benchmark::RegisterBenchmark("all_fields_pretty",
                               all_fields_pretty::benchmark_parser,
                               all_fields_pretty::run_hosq);
  std::cout << "all_fields_randomize_pretty bytes="
            << all_fields_randomize_pretty::input.size()
            << " fnv1a64=" << input_hash(all_fields_randomize_pretty::input)
            << " checksum=";
  print_checksum(all_fields_randomize_pretty::run_hosq());
  std::cout << "\n";
  benchmark::RegisterBenchmark("all_fields_randomize_pretty",
                               all_fields_randomize_pretty::benchmark_parser,
                               all_fields_randomize_pretty::run_hosq);
  if (verify) return 0;
  benchmark::Initialize(&argc, argv);
  if (benchmark::ReportUnrecognizedArguments(argc, argv)) return 2;
  benchmark::RunSpecifiedBenchmarks();
  benchmark::Shutdown();
}
