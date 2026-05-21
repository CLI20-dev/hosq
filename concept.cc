struct JsonVec2 {
  hosq::Entry<key<"x">, Elem<hosq::scan::scalar | hosq::decode::integer<int> |
                             hosq::pack::once>>
      x;

  hosq::Entry<key<"y">, Elem<hosq::scan::scalar | hosq::decode::integer<int> |
                             hosq::pack::once>>
      y;
};

constexpr auto to_vec2 = [](const JsonVec2& p) {
  return Vec2{
      .x = p.x,
      .y = p.y,
  };
};

struct Circle {
  hosq::Entry<key<"type">, Elem<hosq::scan::string |
                                hosq::validate::match<"circle"_json_string> |
                                hosq::pack::once>>
      type;

  hosq::Entry<key<"color">,
              Elem<hosq::decode::parse_to<JsonRGB> |
                   hosq::decode::custom<to_rgb> | hosq::pack::once>>
      color;

  hosq::Entry<key<"center">,
              Elem<hosq::decode::parse_to<JsonVec2> |
                   hosq::decode::custom<to_vec2> | hosq::pack::once>>
      center;

  hosq::Entry<key<"radius">, Elem<hosq::scan::scalar |
                                  hosq::decode::integer<int> | hosq::pack::once>>
      radius;

  hosq::Entry<
      Elem<hosq::scan::string>,
      Elem<hosq::decode::member<hosq::pack::reject_unknown_with_suggestion> |
           hosq::pack::ignore>>
      unknown;
};

struct Rectangle {
  hosq::Entry<key<"type">, Elem<hosq::scan::string |
                                hosq::validate::match<"rectangle"_json_string> |
                                hosq::pack::once>>
      type;

  hosq::Entry<key<"color">,
              Elem<hosq::decode::parse_to<JsonRGB> |
                   hosq::decode::custom<to_rgb> | hosq::pack::once>>
      color;

  hosq::Entry<key<"top_left">,
              Elem<hosq::decode::parse_to<JsonVec2> |
                   hosq::decode::custom<to_vec2> | hosq::pack::once>>
      top_left;

  hosq::Entry<key<"width">, Elem<hosq::scan::scalar |
                                 hosq::decode::integer<int> | hosq::pack::once>>
      width;

  hosq::Entry<key<"height">, Elem<hosq::scan::scalar |
                                  hosq::decode::integer<int> | hosq::pack::once>>
      height;

  hosq::Entry<
      Elem<hosq::scan::string>,
      Elem<hosq::decode::member<hosq::pack::reject_unknown_with_suggestion> |
           hosq::pack::ignore>>
      unknown;
};

struct Triangle {
  hosq::Entry<key<"type">, Elem<hosq::scan::string |
                                hosq::validate::match<"triangle"_json_string> |
                                hosq::pack::once>>
      type;

  hosq::Entry<key<"color">,
              Elem<hosq::decode::parse_to<JsonRGB> |
                   hosq::decode::custom<to_rgb> | hosq::pack::once>>
      color;

  hosq::Entry<key<"vertex1">,
              Elem<hosq::decode::parse_to<JsonVec2> |
                   hosq::decode::custom<to_vec2> | hosq::pack::once>>
      vertex1;

  hosq::Entry<key<"vertex2">,
              Elem<hosq::decode::parse_to<JsonVec2> |
                   hosq::decode::custom<to_vec2> | hosq::pack::once>>
      vertex2;

  hosq::Entry<key<"vertex3">,
              Elem<hosq::decode::parse_to<JsonVec2> |
                   hosq::decode::custom<to_vec2> | hosq::pack::once>>
      vertex3;

  hosq::Entry<
      Elem<hosq::scan::string>,
      Elem<hosq::decode::member<hosq::pack::reject_unknown_with_suggestion> |
           hosq::pack::ignore>>
      unknown;
};

struct Metadata {
  hosq::Entry<key<"source">, Elem<hosq::scan::string |
                                  hosq::decode::string_view | hosq::pack::once>>
      source;

  hosq::Entry<
      key<"version">,
      Elem<hosq::scan::scalar | hosq::decode::integer<int> | hosq::pack::once>>
      version;

  hosq::Entry<
      Elem<hosq::scan::string>,
      Elem<hosq::decode::member<hosq::pack::ignore> | hosq::pack::ignore>>
      unknown;
};

using Shape = std::OneOf<Circle, Rectangle, Triangle>;

struct Document {
  hosq::Entry<key<"data">,
              Elem<hosq::decode::vector<hosq::decode::parse_to<Shape>,
                                        hosq::pack::push> |
                   hosq::pack::once>>
      data;

  hosq::Entry<key<"metadata">,
              Elem<hosq::decode::parse_to<Metadata> | hosq::pack::once>>
      metadata;

  hosq::Entry<Elem<hosq::scan::string>,
              Elem<hosq::unknown::reject_with_suggestion>>
      unknown;
};

// Tier 1: core abstraction validation
//   JSON
//   MessagePack
//   CBOR
//
// Tier 2: practical config / ecosystem formats
//   TOML
//   YAML
//   BSON
//
// Tier 3: edge cases / enterprise / compatibility formats
//   XML
//   CSV / TSV
//   INI
//   URL query / form-urlencoded
//   NDJSON / JSON Lines
//   UBJSON
//   FlexBuffers / FlatBuffers-ish bridge
//   Ion
