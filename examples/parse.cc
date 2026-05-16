#include <fstream>
#include <iostream>
#include <string>

#include "ser20/parser.hh"

auto main() -> int {
  std::ifstream f("./test.json");
  if (!f) {
    std::cerr << "error: cannot open ./test.json\n";
    return 1;
  }
  const std::string input(std::istreambuf_iterator<char>(f), {});

  ser20::Parser<> parser;
  parser.parse(input);

  return 0;
}
