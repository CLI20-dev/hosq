// Companion to check_toml_conformance.py; intentionally .cpp so the unit-test
// CMake glob (*.cc) does not include this standalone program.
#include <charconv>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <type_traits>

#include "hosq/toml/reader.hh"

namespace {
void write_json_string(std::string_view text) {
  std::cout << '"';
  constexpr char hex[] = "0123456789abcdef";
  for (unsigned char c : text) {
    if (c == '"' || c == '\\')
      std::cout << '\\' << static_cast<char>(c);
    else if (c < 0x20)
      std::cout << "\\u00" << hex[c >> 4] << hex[c & 15];
    else
      std::cout << static_cast<char>(c);
  }
  std::cout << '"';
}
template <class State>
void tree(State& state, std::size_t id) {
  const auto& node = state.nodes[id];
  if (node.token.type == hosq::TokenType::ObjectBegin) {
    std::cout << '{';
    bool first = true;
    for (auto child : node.children) {
      if (!first) std::cout << ',';
      first = false;
      write_json_string(state.nodes[child].key);
      std::cout << ':';
      tree(state, state.nodes[child].children[0]);
    }
    std::cout << '}';
  } else if (node.token.type == hosq::TokenType::ArrayBegin) {
    std::cout << '[';
    bool first = true;
    for (auto child : node.children) {
      if (!first) std::cout << ',';
      first = false;
      tree(state, child);
    }
    std::cout << ']';
  } else {
    using C = hosq::detail::NativeContext;
    const auto context = static_cast<C>(node.token.context);
    std::string type, value;
    if (context == C::string) {
      type = "string";
      state.current = id;
      state.token = node.token;
      if (!hosq::toml::Reader<>{}.read_string(state, value)) std::exit(3);
    } else if (context == C::boolean) {
      type = "bool";
      value = std::get<bool>(node.scalar) ? "true" : "false";
    } else if (context == C::datetime) {
      const auto raw = state.input.substr(node.token.offset, node.token.length);
      if (raw.size() == 10)
        type = "date-local";
      else if (raw[2] == ':')
        type = "time-local";
      else if (raw.ends_with("z") || raw.ends_with("Z") ||
               raw.substr(19).find_first_of("+-") != std::string_view::npos)
        type = "datetime";
      else
        type = "datetime-local";
    } else {
      std::visit(
          [&](auto scalar) {
            using V = decltype(scalar);
            if constexpr (std::same_as<V, std::int64_t> ||
                          std::same_as<V, std::uint64_t>) {
              type = "integer";
              value = std::to_string(scalar);
            } else if constexpr (std::same_as<V, double>) {
              type = "float";
              char buf[128];
              const auto result = std::to_chars(buf, buf + sizeof(buf), scalar);
              value.assign(buf, result.ptr);
            }
          },
          node.scalar);
    }
    std::cout << "{\"type\":";
    write_json_string(type);
    std::cout << ",\"value\":";
    write_json_string(value);
    std::cout << '}';
  }
}
}  // namespace
int main(int argc, char** argv) {
  if (argc != 2) return 2;
  std::string path;
  while (std::getline(std::cin, path)) {
    if (!path.ends_with(".toml")) continue;
    std::ifstream in(std::string(argv[1]) + "/" + path, std::ios::binary);
    if (!in) return 2;
    const std::string text{std::istreambuf_iterator<char>(in), {}};
    hosq::toml::Reader<> reader;
    hosq::toml::Reader<>::state_type state;
    reader.reset(state, text);
    while (reader.next_token(state))
      if (state.token.type == hosq::TokenType::End) break;
    const auto error = reader.error(state);
    std::cout << "{\"file\":";
    write_json_string(path);
    std::cout << ",\"ok\":" << (error.is_ok() ? "true" : "false");
    if (error.is_ok()) {
      std::cout << ",\"tree\":";
      tree(state, 0);
    } else {
      std::cout << ",\"code\":" << static_cast<int>(error.code)
                << ",\"offset\":" << error.offset;
    }
    std::cout << "}\n";
  }
}
