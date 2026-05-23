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


