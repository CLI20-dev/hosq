// Compile-only regression workload for constant-key handler debug information.
// Keep wide schemas, nested mappings/arrays, captured JSON and multiple roots in
// one translation unit. No application code or external dependencies are needed.
#include <string>
#include <string_view>

#include "hosq/hosq.hh"
#include "hosq/json/dom.hh"

namespace compile_time {
namespace t = hosq::types;
namespace v = hosq::types::value;

template <class S>
using Object =
    hosq::Elem<hosq::scan::object | hosq::decode::to<S> | hosq::store::assign,
               hosq::encode::value>;
using Json = hosq::Elem<hosq::decode::json_dom<> | hosq::store::assign,
                        hosq::encode::json_dom>;
using Number = hosq::Elem<hosq::scan::scalar | hosq::decode::number<double> |
                              hosq::store::assign,
                          hosq::encode::value>;

struct Position {
  t::Optional<"x", Number> x;
  t::Optional<"y", Number> y;
  t::Optional<"hidden", v::Bool> hidden;
};
struct Ports {
  t::Optional<"inputs", Object<Position>> inputs;
  t::Optional<"outputs", Object<Position>> outputs;
};
struct Note {
  t::Optional<"id", v::String> id;
  t::Optional<"scope", v::String> scope;
  t::Optional<"text", v::String> text;
  t::Optional<"color", v::String> color;
  t::Optional<"x", Number> x;
  t::Optional<"y", Number> y;
  t::Optional<"width", Number> width;
  t::Optional<"height", Number> height;
  t::Optional<"fontSize", Number> font_size;
};
struct Layout {
  t::Optional<"flow", v::String> flow;
  t::Optional<"positions", v::Mapping<v::String, Object<Position>>> positions;
  t::Optional<"ports", v::Mapping<v::String, Object<Ports>>> ports;
  t::Optional<"boundary", Object<Ports>> boundary;
  t::Optional<"notes", v::Array<Object<Note>>> notes;
  t::Optional<"panels", v::Mapping<v::String, Object<Position>>> panels;
  t::Optional<"groups", v::Array<Object<Ports>>> groups;
};
struct Parameter {
  t::Optional<"name", v::String> name;
  t::Optional<"type", v::String> type;
  t::Optional<"data", Json> data;
  t::Optional<"enabled", v::Bool> enabled;
};
struct Definition {
  t::Optional<"name", v::String> name;
  t::Optional<"description", v::String> description;
  t::Optional<"language", v::String> language;
  t::Optional<"template", v::String> template_name;
  t::Optional<"parameters", v::Array<Object<Parameter>>> parameters;
  t::Optional<"inputs", v::Array<Object<Ports>>> inputs;
  t::Optional<"trigger", v::String> trigger;
  t::Optional<"limit", v::Uint64> limit;
  t::Optional<"timeout", Json> timeout;
  t::Optional<"buildTimeout", Json> build_timeout;
  t::Optional<"record", v::Bool> record;
  t::Optional<"retain", v::Bool> retain;
  t::Optional<"output", v::String> output;
  t::Optional<"filesystem", v::Bool> filesystem;
  t::Optional<"sandbox", v::Bool> sandbox;
  t::Optional<"reuse", v::Bool> reuse;
  t::Optional<"network", v::Bool> network;
  t::Optional<"markdown", v::Bool> markdown;
  t::Optional<"folders", v::Array<Object<Parameter>>> folders;
};
struct Project {
  t::Optional<"name", v::String> name;
  t::Optional<"path", v::String> path;
  t::Optional<"connections", v::Array<Object<Ports>>> connections;
  t::Optional<"save", v::Bool> save;
  t::Optional<"canvas", Object<Layout>> canvas;
  t::Optional<"boundary", Object<Ports>> boundary;
};
struct Metadata {
  t::Optional<"description", v::String> description;
  t::Optional<"content", v::String> content;
  t::Optional<"context", v::String> context;
  t::Optional<"origin", v::String> origin;
  t::Optional<"inputs", v::Array<Object<Parameter>>> inputs;
  t::Optional<"outputs", v::Array<Object<Parameter>>> outputs;
};
struct Link {
  t::Optional<"external", v::String> external;
};

template <class T>
auto Decode(std::string_view text, T& output) -> hosq::Error {
  hosq::json::GenericDom<> dom;
  if (auto error = dom.assign(std::string(text))) return error;
  return dom[""].decode_into(output);
}

template auto Decode(std::string_view, Project&) -> hosq::Error;
template auto Decode(std::string_view, Layout&) -> hosq::Error;
template auto Decode(std::string_view, Definition&) -> hosq::Error;
template auto Decode(std::string_view, Metadata&) -> hosq::Error;
template auto Decode(std::string_view, Link&) -> hosq::Error;
}  // namespace compile_time
