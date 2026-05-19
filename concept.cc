struct JsonVec2 {
  ser20::Entry<key<"x">, Elem<ser20::scan::scalar | ser20::decode::integer<int> |
                              ser20::pack::once>>
      x;

  ser20::Entry<key<"y">, Elem<ser20::scan::scalar | ser20::decode::integer<int> |
                              ser20::pack::once>>
      y;
};

constexpr auto to_vec2 = [](const JsonVec2& p) {
  return Vec2{
      .x = p.x,
      .y = p.y,
  };
};

struct Circle {
  ser20::Entry<key<"type">, Elem<ser20::scan::string |
                                 ser20::validate::match<"circle"_json_string> |
                                 ser20::pack::once>>
      type;

  ser20::Entry<key<"color">,
               Elem<ser20::decode::parse_to<JsonRGB> |
                    ser20::decode::custom<to_rgb> | ser20::pack::once>>
      color;

  ser20::Entry<key<"center">,
               Elem<ser20::decode::parse_to<JsonVec2> |
                    ser20::decode::custom<to_vec2> | ser20::pack::once>>
      center;

  ser20::Entry<key<"radius">,
               Elem<ser20::scan::scalar | ser20::decode::integer<int> |
                    ser20::pack::once>>
      radius;

  ser20::Entry<
      Elem<ser20::scan::string>,
      Elem<ser20::decode::member<ser20::pack::reject_unknown_with_suggestion> |
           ser20::pack::ignore>>
      unknown;
};

struct Rectangle {
  ser20::Entry<
      key<"type">,
      Elem<ser20::scan::string |
           ser20::validate::match<"rectangle"_json_string> | ser20::pack::once>>
      type;

  ser20::Entry<key<"color">,
               Elem<ser20::decode::parse_to<JsonRGB> |
                    ser20::decode::custom<to_rgb> | ser20::pack::once>>
      color;

  ser20::Entry<key<"top_left">,
               Elem<ser20::decode::parse_to<JsonVec2> |
                    ser20::decode::custom<to_vec2> | ser20::pack::once>>
      top_left;

  ser20::Entry<key<"width">,
               Elem<ser20::scan::scalar | ser20::decode::integer<int> |
                    ser20::pack::once>>
      width;

  ser20::Entry<key<"height">,
               Elem<ser20::scan::scalar | ser20::decode::integer<int> |
                    ser20::pack::once>>
      height;

  ser20::Entry<
      Elem<ser20::scan::string>,
      Elem<ser20::decode::member<ser20::pack::reject_unknown_with_suggestion> |
           ser20::pack::ignore>>
      unknown;
};

struct Triangle {
  ser20::Entry<key<"type">, Elem<ser20::scan::string |
                                 ser20::validate::match<"triangle"_json_string> |
                                 ser20::pack::once>>
      type;

  ser20::Entry<key<"color">,
               Elem<ser20::decode::parse_to<JsonRGB> |
                    ser20::decode::custom<to_rgb> | ser20::pack::once>>
      color;

  ser20::Entry<key<"vertex1">,
               Elem<ser20::decode::parse_to<JsonVec2> |
                    ser20::decode::custom<to_vec2> | ser20::pack::once>>
      vertex1;

  ser20::Entry<key<"vertex2">,
               Elem<ser20::decode::parse_to<JsonVec2> |
                    ser20::decode::custom<to_vec2> | ser20::pack::once>>
      vertex2;

  ser20::Entry<key<"vertex3">,
               Elem<ser20::decode::parse_to<JsonVec2> |
                    ser20::decode::custom<to_vec2> | ser20::pack::once>>
      vertex3;

  ser20::Entry<
      Elem<ser20::scan::string>,
      Elem<ser20::decode::member<ser20::pack::reject_unknown_with_suggestion> |
           ser20::pack::ignore>>
      unknown;
};

struct Metadata {
  ser20::Entry<
      key<"source">,
      Elem<ser20::scan::string | ser20::decode::string_view | ser20::pack::once>>
      source;

  ser20::Entry<key<"version">,
               Elem<ser20::scan::scalar | ser20::decode::integer<int> |
                    ser20::pack::once>>
      version;

  ser20::Entry<
      Elem<ser20::scan::string>,
      Elem<ser20::decode::member<ser20::pack::ignore> | ser20::pack::ignore>>
      unknown;
};

using Shape = std::OneOf<Circle, Rectangle, Triangle>;

struct Document {
  ser20::Entry<key<"data">,
               Elem<ser20::decode::vector<ser20::decode::parse_to<Shape>,
                                          ser20::pack::push> |
                    ser20::pack::once>>
      data;

  ser20::Entry<key<"metadata">,
               Elem<ser20::decode::parse_to<Metadata> | ser20::pack::once>>
      metadata;

  ser20::Entry<Elem<ser20::scan::string>,
               Elem<ser20::unknown::reject_with_suggestion>>
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
