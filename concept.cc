enum class ShapeKind {
  circle,
  rectangle,
  triangle,
  unknown,
};

struct Vec2 {
  int x;
  int y;
};

struct RGB {
  int r;
  int g;
  int b;
};

struct JsonRGB {
  json::ElemImpl<
      "r",
      json::scan::scalar |
      json::decode::integer<int> |
      json::pack::once
  > r;

  json::ElemImpl<
      "g",
      json::scan::scalar |
      json::decode::integer<int> |
      json::pack::once
  > g;

  json::ElemImpl<
      "b",
      json::scan::scalar |
      json::decode::integer<int> |
      json::pack::once
  > b;
};

constexpr auto to_rgb = [](const JsonRGB& c) {
  return RGB{
      .r = c.r,
      .g = c.g,
      .b = c.b,
  };
};

struct JsonVec2 {
  json::ElemImpl<
      "x",
      json::scan::scalar |
      json::decode::integer<int> |
      json::pack::once
  > x;

  json::ElemImpl<
      "y",
      json::scan::scalar |
      json::decode::integer<int> |
      json::pack::once
  > y;
};

constexpr auto to_vec2 = [](const JsonVec2& p) {
  return Vec2{
      .x = p.x,
      .y = p.y,
  };
};

struct Circle : json::Constant<"type", "circle"_json_string> {
  json::ElemImpl<
      "color",
      json::decode::parse_to<JsonRGB> |
      json::decode::custom<to_rgb> |
      json::pack::once
  > color;

  json::ElemImpl<
      "center",
      json::decode::parse_to<JsonVec2> |
      json::decode::custom<to_vec2> |
      json::pack::once
  > center;

  json::ElemImpl<
      "radius",
      json::scan::scalar |
      json::decode::integer<int> |
      json::pack::once
  > radius;

  json::ElemImpl<
      "",
      json::decode::member<json::pack::reject_unknown_with_suggestion> |
      json::pack::ignore
  > unknown;
};

struct Rectangle : json::Constant<"type", "rectangle"_json_string> {
  json::ElemImpl<
      "color",
      json::decode::parse_to<JsonRGB> |
      json::decode::custom<to_rgb> |
      json::pack::once
  > color;

  json::ElemImpl<
      "top_left",
      json::decode::parse_to<JsonVec2> |
      json::decode::custom<to_vec2> |
      json::pack::once
  > top_left;

  json::ElemImpl<
      "width",
      json::scan::scalar |
      json::decode::integer<int> |
      json::pack::once
  > width;

  json::ElemImpl<
      "height",
      json::scan::scalar |
      json::decode::integer<int> |
      json::pack::once
  > height;

  json::ElemImpl<
      "",
      json::decode::member<json::pack::reject_unknown_with_suggestion> |
      json::pack::ignore
  > unknown;
};

struct Triangle : json::Constant<"type", "triangle"_json_string> {
  json::ElemImpl<
      "color",
      json::decode::parse_to<JsonRGB> |
      json::decode::custom<to_rgb> |
      json::pack::once
  > color;

  json::ElemImpl<
      "vertex1",
      json::decode::parse_to<JsonVec2> |
      json::decode::custom<to_vec2> |
      json::pack::once
  > vertex1;

  json::ElemImpl<
      "vertex2",
      json::decode::parse_to<JsonVec2> |
      json::decode::custom<to_vec2> |
      json::pack::once
  > vertex2;

  json::ElemImpl<
      "vertex3",
      json::decode::parse_to<JsonVec2> |
      json::decode::custom<to_vec2> |
      json::pack::once
  > vertex3;

  json::ElemImpl<
      "",
      json::decode::member<json::pack::reject_unknown_with_suggestion> |
      json::pack::ignore
  > unknown;
};

using Shape =
    json::Variant<
        Circle,
        Rectangle,
        Triangle
    >;

struct Metadata {
  json::ElemImpl<
      "source",
      json::scan::string |
      json::decode::string_view |
      json::pack::once
  > source;

  json::ElemImpl<
      "version",
      json::scan::scalar |
      json::decode::integer<int> |
      json::pack::once
  > version;

  json::ElemImpl<
      "",
      json::decode::member<json::pack::ignore> |
      json::pack::ignore
  > unknown;
};

struct Document {
  json::ElemImpl<
      "data",
      json::decode::vector<
          json::decode::variant<
              Circle,
              Rectangle,
              Triangle
          > |
          json::pack::assign
      > |
      json::pack::once
  > data;

  json::ElemImpl<
      "metadata",
      json::decode::parse_to<Metadata> |
      json::pack::once
  > metadata;

  json::ElemImpl<
      "",
      json::unknown::reject_with_suggestion
  > unknown;
};

int main() {
  constexpr auto text = R"json(
  {
    "metadata": {
      "source": "generated-example",
      "version": 1
    },
    "data": [
      {
        "type": "circle",
        "center": { "x": 0, "y": 0 },
        "radius": 5,
        "color": { "r": 255, "g": 0, "b": 0 }
      },
      {
        "type": "rectangle",
        "top_left": { "x": -1, "y": -1 },
        "width": 2,
        "height": 3,
        "color": { "r": 0, "g": 255, "b": 0 }
      },
      {
        "type": "triangle",
        "vertex1": { "x": 0, "y": 0 },
        "vertex2": { "x": 1, "y": 0 },
        "vertex3": { "x": 0, "y": 1 },
        "color": { "r": 0, "g": 0, "b": 255 }
      }
    ]
  }
  )json";

  auto document = json::parse<app::Document>(text);

  return 0;
}
