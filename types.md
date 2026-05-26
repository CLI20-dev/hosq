# Types

## Json

literal
null | true | false | number | string


## MsgPack

nil
positive fixint
fixstr
false
true
bin 8
bin 16
bin 32
ext 8
ext 16
ext 32
float 32
float 64
uint 8
uint 16
uint 32
uint 64
int 8
int 16
int 32
int 64
fixext 1
fixext 2
fixext 4
fixext 8
fixext 16
str 8
str 16
str 32
array 16
array 32
map 16
map 32
negative fixint

## Cbor

0	unsigned integer N	-
1	negative integer -1-N	-
2	byte string	N bytes
3	text string	N bytes (UTF-8 text)
4	array	N data items (elements)
5	map	2N data items (key/value pairs)
6	tag of number N	1 data item
7	simple/float	-

## BSON

0x01 Double 64-bit IEEE 754 浮動小数点数

0x02 String UTF-8 文字列

0x03 Document / Object 埋め込みドキュメント

0x04 Array 配列

0x05 Binary バイナリデータ

0x06 Undefined 非推奨

0x07 ObjectId 12 byte の ObjectId

0x08 Boolean true / false

0x09 UTC DateTime Unix epoch からのミリ秒

0x0A Null null

0x0B Regex 正規表現

0x0C DBPointer 非推奨

0x0D JavaScript Code JavaScript コード

0x0E Symbol 非推奨

0x0F JavaScript Code with Scope 非推奨

0x10 Int32 32-bit signed integer

0x11 Timestamp MongoDB 内部用 timestamp

0x12 Int64 64-bit signed integer

0x13 Decimal128 128-bit decimal floating point

0xFF MinKey 最小比較値

0x7F MaxKey 最大比較値

## Flatbuffer


## Flexbuffer

# Basic Pipelines

## scan

```cpp
constexpr auto scalar;

constexpr auto string;

constexpr auto literal;

constexpr auto object;

constexpr auto array;
```

## decode
```cpp
template <detail::Number T>
constexpr auto number;

constexpr auto boolean;

constexpr auto string_view;

constexpr auto identity;

template <auto F>
constexpr auto custom;

template <auto... Pipelines>
constexpr auto each;
// each<P> or each<keyP, valueP>

template <auto... Pipelines>
constexpr auto collect;
// collect<P> or collect<keyP, valueP>
```

## match
```cpp
template <auto V>
constexpr auto literal;
```

## store
```cpp
constexpr auto assign;

constexpr auto push;

constexpr auto ignore;

constexpr auto once;

constexpr auto first_wins;
```


# Example

## primitives
```cpp
scan::literal | decode::number<int> | store::assign; -> int (overwrite)

scan::literal | decode::number<int> | store::once; -> optional

scan::literal | decode::boolean | store::push; -> std::vector<bool> (push_back)

scan::literal | decode::number<int> | store::emplace; -> std::vector (emplace)

scan::literal | decode::number<int> | store::first_wins; -> std::optional (first value is stored, subsequent values are ignored)

scan::scalar | decode::number<int> | store::assign; -> int (overwrite)

scan::scalar | decode::number<int> | store::once; -> optional

scan::scalar | decode::number<int> | store::push; -> std::vector (push_back)

scan::scalar | decode::number<int> | store::emplace; -> std::vector (emplace)

scan::scalar | decode::number<int> | store::first_wins; -> std::optional (first value is stored, subsequent values are ignored)
```

## objects and arrays
```cpp
scan::array | decode::each<
  scan::string | decode::string_view | store::assign,
>; -> std::vector<std::string>

scan::object | decode::each<
  scan::string | decode::string_view | store::assign,
  scan::literal | decode::number<int> | store::assign
>; -> hosq::Map<std::vector<std::string>, std::vector<int>>

scan::array | decode::collect<
  scan::literal | decode::number<int>,
> | store::assign; -> std::vector<int>

scan::object | decode::each<
  scan::string | decode::string_view,
  scan::literal | decode::boolean
> | store::assign; -> hosq::Map<std::vector<std::string>, std::vector<bool>>
```

## matches
```cpp
scan::literal | match::literal<42>

scan::string | match::literal<"hello">

scan::literal | match::literal<42> | store::assign; -> bool (true if value is 42, false otherwise)
```


