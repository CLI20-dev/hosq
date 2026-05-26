#include <hosq/hosq.hh>
#include <hosq/json/simple_reader.hh>
#include <iostream>
#include <string_view>

using namespace hosq;
using namespace std::string_view_literals;

namespace {

constexpr std::string_view json_whitespace = R"(
{
   "fixed_object": {
      "int_array": [0, 1, 2, 3, 4, 5, 6],
      "float_array": [0.1, 0.2, 0.3, 0.4, 0.5, 0.6],
      "double_array": [3288398.238, 233e22, 289e-1, 0.928759872, 0.22222848, 0.1, 0.2, 0.3, 0.4]
   },
   "fixed_name_object": {
      "name0": "James",
      "name1": "Abraham",
      "name2": "Susan",
      "name3": "Frank",
      "name4": "Alicia"
   },
   "another_object": {
      "string": "here is some text",
      "another_string": "Hello World",
      "escaped_text": "{\"some key\":\"some string value\"}",
      "boolean": false,
      "nested_object": {
         "v3s": [[0.12345, 0.23456, 0.001345],
                  [0.3894675, 97.39827, 297.92387],
                  [18.18, 87.289, 2988.298]],
         "id": "298728949872"
      }
   },
   "string_array": ["Cat", "Dog", "Elephant", "Tiger"],
   "string": "Hello world",
   "number": 3.14,
   "boolean": true,
   "another_bool": false
}
)";

template <StringLiteral Name>                               //
using FieldKey = Elem<                                      //
    scan::scalar                                            //
        | decode::string_view                               //
        | match::literal<Name>,                             //
    noop_encoder>;                                          //
                                                            //
template <class T>                                          //
using NumberField = Elem<                                   //
    scan::scalar                                            //
        | decode::number<T>                                 //
        | store::assign,                                    //
    noop_encoder>;                                          //
                                                            //
using StringField = Elem<                                   //
    scan::scalar                                            //
        | decode::string_view                               //
        | store::assign,                                    //
    noop_encoder>;                                          //
                                                            //
using BoolField = Elem<                                     //
    scan::scalar                                            //
        | decode::boolean                                   //
        | store::assign,                                    //
    noop_encoder>;                                          //
                                                            //
template <class T, size_t N>                                //
using NumberArrayField = Elem<                              //
    scan::array                                             //
        | decode::each<                                     //
              scan::scalar                                  //
                  | decode::number_into<T>,                 //
              N>,                                           //
    noop_encoder>;                                          //
                                                            //
using StringArrayField = Elem<                              //
    scan::array                                             //
        | decode::each<                                     //
              scan::scalar                                  //
                  | decode::string_view                     //
                  | store::assign,                          //
              4>,                                           //
    noop_encoder>;                                          //
                                                            //
using V3ArrayField = Elem<                                  //
    scan::array                                             //
        | decode::each<                                     //
              scan::array                                   //
                  | decode::each<                           //
                        scan::scalar                        //
                            | decode::number_into<double>,  //
                        3>,                                 //
              3>,                                           //
    noop_encoder>;                                          //

struct FixedObjectSchema {
  Entry<FieldKey<"int_array">, NumberArrayField<int, 7>> int_array;
  Entry<FieldKey<"float_array">, NumberArrayField<double, 6>> float_array;
  Entry<FieldKey<"double_array">, NumberArrayField<double, 9>> double_array;
};

struct FixedNameObjectSchema {
  Entry<FieldKey<"name0">, StringField> name0;
  Entry<FieldKey<"name1">, StringField> name1;
  Entry<FieldKey<"name2">, StringField> name2;
  Entry<FieldKey<"name3">, StringField> name3;
  Entry<FieldKey<"name4">, StringField> name4;
};

struct NestedObjectSchema {
  Entry<FieldKey<"v3s">, V3ArrayField> v3s;
  Entry<FieldKey<"id">, StringField> id;
};

using NestedObjectField = Elem<           //
    scan::object                          //
        | decode::to<NestedObjectSchema>  //
        | store::assign,                  //
    noop_encoder>;

struct AnotherObjectSchema {
  Entry<FieldKey<"string">, StringField> string;
  Entry<FieldKey<"another_string">, StringField> another_string;
  Entry<FieldKey<"escaped_text">, StringField> escaped_text;
  Entry<FieldKey<"boolean">, BoolField> boolean;
  Entry<FieldKey<"nested_object">, NestedObjectField> nested_object;
};

using FixedObjectField = Elem<               //
    scan::object                             //
        | decode::to<FixedObjectSchema>      //
        | store::assign,                     //
    noop_encoder>;                           //
using FixedNameObjectField = Elem<           //
    scan::object                             //
        | decode::to<FixedNameObjectSchema>  //
        | store::assign,                     //
    noop_encoder>;                           //
using AnotherObjectField = Elem<             //
    scan::object                             //
        | decode::to<AnotherObjectSchema>    //
        | store::assign,
    noop_encoder>;

struct RootSchema {
  Entry<FieldKey<"fixed_object">, FixedObjectField> fixed_object;
  Entry<FieldKey<"fixed_name_object">, FixedNameObjectField> fixed_name_object;
  Entry<FieldKey<"another_object">, AnotherObjectField> another_object;
  Entry<FieldKey<"string_array">, StringArrayField> string_array;
  Entry<FieldKey<"string">, StringField> string;
  Entry<FieldKey<"number">, NumberField<double>> number;
  Entry<FieldKey<"boolean">, BoolField> boolean;
  Entry<FieldKey<"another_bool">, BoolField> another_bool;
};

constexpr int kIterations = 10000;

}  // namespace

auto main() -> int {
  using Reader = json::SimpleReader;

  for (int i = 0; i < kIterations; ++i) {
    RootSchema root;
    const auto result = decode_into<Reader>(root, json_whitespace);
    if (!result.is_ok()) {
      std::cerr << "parse error: code=" << static_cast<int>(result.code)
                << " offset=" << result.offset << "\n";
      return 1;
    }
  }

  return 0;
}
