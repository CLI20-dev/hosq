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
  ser20::ElemImpl<
      "r",
      ser20::scan::scalar |
      ser20::decode::integer<int> |
      ser20::pack::once
  > r;

  ser20::ElemImpl<
      "g",
      ser20::scan::scalar |
      ser20::decode::integer<int> |
      ser20::pack::once
  > g;

  ser20::ElemImpl<
      "b",
      ser20::scan::scalar |
      ser20::decode::integer<int> |
      ser20::pack::once
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
  ser20::ElemImpl<
      "x",
      ser20::scan::scalar |
      ser20::decode::integer<int> |
      ser20::pack::once
  > x;

  ser20::ElemImpl<
      "y",
      ser20::scan::scalar |
      ser20::decode::integer<int> |
      ser20::pack::once
  > y;
};

constexpr auto to_vec2 = [](const JsonVec2& p) {
  return Vec2{
      .x = p.x,
      .y = p.y,
  };
};

struct Circle {
  ser20::Constant<"type", "circle"_json_string> constant;

  ser20::ElemImpl<
      "color",
      ser20::decode::parse_to<JsonRGB> |
      ser20::decode::custom<to_rgb> |
      ser20::pack::once
  > color;

  ser20::ElemImpl<
      "center",
      ser20::decode::parse_to<JsonVec2> |
      ser20::decode::custom<to_vec2> |
      ser20::pack::once
  > center;

  ser20::ElemImpl<
      "radius",
      ser20::scan::scalar |
      ser20::decode::integer<int> |
      ser20::pack::once
  > radius;

  ser20::ElemImpl<
      "",
      ser20::decode::member<ser20::pack::reject_unknown_with_suggestion> |
      ser20::pack::ignore
  > unknown;
};

struct Rectangle {
  ser20::Constant<"type", "rectangle"_json_string> constant;
  ser20::ElemImpl<
      "color",
      ser20::decode::parse_to<JsonRGB> |
      ser20::decode::custom<to_rgb> |
      ser20::pack::once
  > color;

  ser20::ElemImpl<
      "top_left",
      ser20::decode::parse_to<JsonVec2> |
      ser20::decode::custom<to_vec2> |
      ser20::pack::once
  > top_left;

  ser20::ElemImpl<
      "width",
      ser20::scan::scalar |
      ser20::decode::integer<int> |
      ser20::pack::once
  > width;

  ser20::ElemImpl<
      "height",
      ser20::scan::scalar |
      ser20::decode::integer<int> |
      ser20::pack::once
  > height;

  ser20::ElemImpl<
      "",
      ser20::decode::member<ser20::pack::reject_unknown_with_suggestion> |
      ser20::pack::ignore
  > unknown;
};

struct Triangle {
  ser20::Constant<"type", "triangle"_json_string> constant;
  ser20::ElemImpl<
      "color",
      ser20::decode::parse_to<JsonRGB> |
      ser20::decode::custom<to_rgb> |
      ser20::pack::once
  > color;

  ser20::ElemImpl<
      "vertex1",
      ser20::decode::parse_to<JsonVec2> |
      ser20::decode::custom<to_vec2> |
      ser20::pack::once
  > vertex1;

  ser20::ElemImpl<
      "vertex2",
      ser20::decode::parse_to<JsonVec2> |
      ser20::decode::custom<to_vec2> |
      ser20::pack::once
  > vertex2;

  ser20::ElemImpl<
      "vertex3",
      ser20::decode::parse_to<JsonVec2> |
      ser20::decode::custom<to_vec2> |
      ser20::pack::once
  > vertex3;

  ser20::ElemImpl<
      "",
      ser20::decode::member<ser20::pack::reject_unknown_with_suggestion> |
      ser20::pack::ignore
  > unknown;
};

using Shape =
    ser20::Variant<
        Circle,
        Rectangle,
        Triangle
    >;

struct Metadata {
  ser20::ElemImpl<
      "source",
      ser20::scan::string |
      ser20::decode::string_view |
      ser20::pack::once
  > source;

  ser20::ElemImpl<
      "version",
      ser20::scan::scalar |
      ser20::decode::integer<int> |
      ser20::pack::once
  > version;

  ser20::ElemImpl<
      "",
      ser20::decode::member<ser20::pack::ignore> |
      ser20::pack::ignore
  > unknown;
};

struct Document {
  ser20::ElemImpl<
      "data",
      ser20::decode::vector<
          ser20::decode::variant<
              Circle,
              Rectangle,
              Triangle
          > |
          ser20::pack::assign
      > |
      ser20::pack::once
  > data;

  ser20::ElemImpl<
      "metadata",
      ser20::decode::parse_to<Metadata> |
      ser20::pack::once
  > metadata;

  ser20::ElemImpl<
      "",
      ser20::unknown::reject_with_suggestion
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

  auto document = ser20::parse<app::Document>(text);

  return 0;
}
