// Standalone fixture generator; compiled separately from *.cc unit tests.
#include <array>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

#include "hosq/bson/writer.hh"
#include "hosq/hosq.hh"
#include "hosq/json/compact_writer.hh"
#include "hosq/msgpack/writer.hh"
#include "hosq/toml/writer.hh"

namespace {
using namespace hosq;
void result(std::string_view format, std::string_view name,
            std::string_view output) {
  constexpr char hex[] = "0123456789abcdef";
  std::cout << format << '\t' << name << '\t';
  for (unsigned char c : output) std::cout << hex[c >> 4] << hex[c & 15];
  std::cout << '\n';
}
template <class W>
void token(W& writer, TokenType type) {
  (void)writer.write({0, type, 0});
  if (writer.error()) {
    std::cerr << writer.error().detail << '\n';
    std::exit(2);
  }
}
template <class T>
struct Storage {
  template <class Input>
  using storage_type = T;
  template <class Context>
  void operator()(Context&) const {}
};
template <class T>
using Value = Elem<step<Storage<T>>, encode::value>;
struct Child {
  Try<Entry<Key<"n">, Value<int>>> n;
};
struct Fixture {
  Try<Entry<Key<"n">, Value<int>>> n;
  Try<Entry<Key<"text">, Value<std::string>>> text;
  Try<Entry<Key<"ok">, Value<bool>>> ok;
  Try<Entry<Key<"items">, Value<std::vector<Child>>>> items;
  Try<Entry<Key<"meta">, Value<Child>>> meta;
  Try<Entry<Key<"edge">, Value<std::vector<std::int64_t>>>> edge;
  Try<Entry<Key<"floats">, Value<std::vector<double>>>> floats;
};
template <class W>
void nested(std::string_view format) {
  Fixture value;
  value.n.value().value() = 42;
  value.text.value().value() = "a\n\"\\😀";
  value.ok.value().value() = true;
  value.items.value().value().resize(2);
  value.items.value().value()[0].n.value().value() = 1;
  value.items.value().value()[1].n.value().value() = 2;
  value.meta.value().value().n.value().value() = -7;
  value.edge.value().value() = {std::numeric_limits<std::int64_t>::min(),
                                std::numeric_limits<std::int64_t>::max()};
  value.floats.value().value() = {1.0, -0.0, 0.1, 1e300};
  std::string output;
  W writer(output);
  if (!encode_into(value, writer)) {
    std::cerr << writer.error().detail << '\n';
    std::exit(2);
  }
  result(format, "nested", output);
}
template <class W, class T>
void number(std::string_view format, std::string name, T value,
            bool root = false) {
  std::string output;
  W writer(output);
  if (!root) {
    token(writer, TokenType::ObjectBegin);
    if (!writer.write_string("x")) std::exit(2);
  }
  if (!writer.write_number(value)) {
    std::cerr << writer.error().detail << '\n';
    std::exit(2);
  }
  if (!root) token(writer, TokenType::ObjectEnd);
  token(writer, TokenType::End);
  result(format, name, output);
}
std::string controls() {
  std::string out;
  for (int i = 0; i < 32; ++i) out += static_cast<char>(i);
  out += static_cast<char>(127);
  return out;
}
template <class W>
void strings(std::string_view format) {
  for (const auto& [name, text] :
       std::vector<std::pair<std::string, std::string>>{
           {"empty", ""},
           {"escapes", "a\n\"\\"},
           {"unicode", "日本語😀"},
           {"controls", controls()}}) {
    std::string output;
    W writer(output);
    token(writer, TokenType::ObjectBegin);
    if (!writer.write_string("x") || !writer.write_string(text)) std::exit(2);
    token(writer, TokenType::ObjectEnd);
    token(writer, TokenType::End);
    result(format, "text:" + name, output);
  }
  std::string output;
  W writer(output);
  token(writer, TokenType::ObjectBegin);
  if (!writer.write_string("a.b\n\"\\") || !writer.write_number(42))
    std::exit(2);
  token(writer, TokenType::ObjectEnd);
  token(writer, TokenType::End);
  result(format, "weird-key", output);
}
template <class W>
void document_numbers(std::string_view format) {
  number<W>(format, "i32:42", 42);
  number<W>(format, "i32:-2147483648", std::numeric_limits<std::int32_t>::min());
  number<W>(format, "i64:42", std::int64_t{42});
  number<W>(format, "i64:-9223372036854775808",
            std::numeric_limits<std::int64_t>::min());
  number<W>(format, "u32:4294967295", std::uint32_t{0xffffffff});
  number<W>(format, "u64:9223372036854775807",
            std::uint64_t{0x7fffffffffffffff});
  number<W>(format, "f32:0.1", 0.1f);
  number<W>(format, "f64:1.0", 1.0);
  number<W>(format, "f64:-0.0", -0.0);
  number<W>(format, "f64:inf", std::numeric_limits<double>::infinity());
  number<W>(format, "f64:nan", std::numeric_limits<double>::quiet_NaN());
  number<W>(format, "f64:denorm", std::numeric_limits<double>::denorm_min());
  number<W>(format, "f64:max", std::numeric_limits<double>::max());
}
}  // namespace
int main() {
  nested<msgpack::Writer<>>("msgpack");
  nested<bson::Writer<>>("bson");
  nested<toml::Writer<>>("toml");
  nested<json::CompactWriter<>>("json");
  strings<msgpack::Writer<>>("msgpack");
  strings<bson::Writer<>>("bson");
  strings<toml::Writer<>>("toml");
  document_numbers<bson::Writer<>>("bson");
  document_numbers<toml::Writer<>>("toml");
  for (std::uint64_t value :
       {0ULL, 127ULL, 128ULL, 255ULL, 256ULL, 65535ULL, 65536ULL, 0xffffffffULL,
        0x100000000ULL, 0xffffffffffffffffULL})
    number<msgpack::Writer<>>("msgpack", "u:" + std::to_string(value), value,
                              true);
  for (std::int64_t value : std::array<std::int64_t, 10>{
           -1, -32, -33, -128, -129, -32768, -32769, -2147483648LL,
           -2147483649LL, std::numeric_limits<std::int64_t>::min()})
    number<msgpack::Writer<>>("msgpack", "s:" + std::to_string(value), value,
                              true);
  number<msgpack::Writer<>>("msgpack", "f32:0.1", 0.1f, true);
  number<msgpack::Writer<>>("msgpack", "f64:1.5", 1.5, true);
  for (unsigned n : {0u, 31u, 32u, 255u, 256u, 65535u, 65536u}) {
    std::string output;
    msgpack::Writer writer(output);
    if (!writer.write_string(std::string(n, 'a'))) return 2;
    token(writer, TokenType::End);
    result("msgpack", "str:" + std::to_string(n), output);
  }
  for (unsigned n : {0u, 15u, 16u, 65535u, 65536u}) {
    std::string output;
    msgpack::Writer writer(output);
    token(writer, TokenType::ArrayBegin);
    for (unsigned i = 0; i < n; ++i)
      if (!writer.write_number(0)) return 2;
    token(writer, TokenType::ArrayEnd);
    token(writer, TokenType::End);
    result("msgpack", "array:" + std::to_string(n), output);
  }
  for (unsigned n : {0u, 1u, 2u, 3u, 4u, 8u, 16u, 256u}) {
    const std::string payload(n, 'x');
    std::string output;
    msgpack::Writer writer(output);
    if (!writer.write_extension(7, BytesView::from_string_view(payload)))
      return 2;
    token(writer, TokenType::End);
    result("msgpack", "ext:" + std::to_string(n), output);
  }
  const std::string payload("\0\xff", 2);
  std::string output;
  msgpack::Writer writer(output);
  if (!writer.write_binary(BytesView::from_string_view(payload))) return 2;
  token(writer, TokenType::End);
  result("msgpack", "binary", output);
  std::string bson;
  bson::Writer bw(bson);
  token(bw, TokenType::ObjectBegin);
  if (!bw.write_string("x") ||
      !bw.write_binary(BytesView::from_string_view(payload)))
    return 2;
  token(bw, TokenType::ObjectEnd);
  token(bw, TokenType::End);
  result("bson", "binary", bson);
}
