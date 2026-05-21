# ser20 Pipeline I/O Model

このメモは、ser20 の各 primitive について、**pipeline input** と **Context 由来の Reader / Writer / value** を明確に分離した整理です。

基本ルール:

```cpp
// pipeline input
Fn(input) または Fn(input, ctx)

// context input
Fn(ctx)
```

`scan::*` は原則として pipeline input を取らず、`DecodeContext` から `reader` を読む primitive です。
`decode::*` / `validate::*` / `match::*` / `encode::*` は多くが前段の pipeline output を input として受けます。
`store::*` / `write::*` は Context に副作用を持つ終端寄り primitive です。

---

# scan

`scan` は **DecodeContext 由来の Reader を消費する段階**。
原則として pipeline input は `void`。

```cpp
[x] ser20::scan::scalar
// pipeline input: void
// context input: DecodeContext<T, R>& または const DecodeContext<T, R>&
// context source: ctx.reader()
// output: std::string_view
// reader を進め、現在 token の scalar value を取り出して渡す
// 対象: string / number / true / false / null
// string の quote 除去はするが、escape 解決はしない
```

```cpp
[x] ser20::scan::string
// pipeline input: void
// context input: DecodeContext<T, R>& または const DecodeContext<T, R>&
// context source: ctx.reader()
// output: std::string_view
// reader を進め、現在 token が JSON string であることを要求し、中身だけを渡す
// quote は含めない
// escape 解決は decode 側で行う
```

```cpp
[x] ser20::scan::literal
// pipeline input: void
// context input: DecodeContext<T, R>& または const DecodeContext<T, R>&
// context source: ctx.reader()
// output: std::string_view
// literal のみ受ける
// number / true / false / null
// string が来たら type_mismatch
```

ここまでは `Reader::advance()` だけで済むので、const context path も成立し得る。

```cpp
[x] ser20::scan::object
// pipeline input: void
// context input: DecodeContext<T, R>&
// context source: ctx.reader()
// output: std::string_view
// reader.next_token() を使って object 全体を消費し、入力範囲を返す
// token 化も parse も行わない
// mutable context 必須
```

```cpp
[x] ser20::scan::array
// pipeline input: void
// context input: DecodeContext<T, R>&
// context source: ctx.reader()
// output: std::string_view
// reader.next_token() を使って array 全体を消費し、入力範囲を返す
// token 化も parse も行わない
// mutable context 必須
```

```cpp
[-] ser20::scan::object_tokens<N>
// pipeline input: void
// context input: DecodeContext<T, R>&
// context source: ctx.reader()
// output: ser20::TokenView<N>
// reader.next_token() を使って object 全体を消費し、object 内 token を固定長 buffer に積む
// mutable context 必須
```

```cpp
[-] ser20::scan::object_tokens
// pipeline input: void
// context input: DecodeContext<T, R>&
// context source: ctx.reader()
// output: ser20::TokenVector
// allocation を許可する tokenized object scan
// mutable context 必須
```

```cpp
[-] ser20::scan::array_tokens<N>
// pipeline input: void
// context input: DecodeContext<T, R>&
// context source: ctx.reader()
// output: ser20::TokenView<N>
// reader.next_token() を使って array 全体を消費し、array 内 token を固定長 buffer に積む
// mutable context 必須
```

```cpp
[-] ser20::scan::array_tokens
// pipeline input: void
// context input: DecodeContext<T, R>&
// context source: ctx.reader()
// output: ser20::TokenVector
// allocation を許可する tokenized array scan
// mutable context 必須
```

---

# decode_into

```cpp
[ ] ser20::decode_into<T>
// pipeline input: none
// API input: T& output_object, typename R::input_type input
// context source: internal Decoder<R>::reader_
// output: Result<void, Error>
// top-level API。Reader を reset し、T が ElemT なら scalar/value decode、AsEntryTupleT なら object decode する
// 直接構築 TODO
```

---

# decode

`decode` は基本的に **前段 pipeline output を変換する段階**。
ただし `parse_to<T>` / `vector<T>` のような再帰 parse 系は context reader/token view を使う。

```cpp
[x] ser20::decode::number<T>
// pipeline input: std::string_view
// context input: optional
// output: T
// number 文字列を arithmetic T に変換する
```

```cpp
[x] ser20::decode::boolean
// pipeline input: std::string_view
// context input: optional
// output: bool
// true / false を bool に変換する
```

```cpp
[x] ser20::decode::string_view
// pipeline input: std::string_view
// context input: none
// output: std::string_view
// borrowed string_view のまま渡す
// escape が存在する場合の扱いは decode::string / decode::json_string に任せる
```

```cpp
[ ] ser20::decode::string
// pipeline input: std::string_view
// context input: optional allocator / context
// output: std::string
// allocation して escape 解決済み string を作る
```

```cpp
[-] ser20::decode::variant<Ts...>
// Ts: ElemT または AsEntryTupleT
// pipeline input: void
// context input: DecodeContext<Storage, R>&
// context source: ctx.reader()
// output: ser20::Variant<decoded_result_t<Ts>...>
// 現在 token から候補型 Ts... のいずれかとして parse する
// Reader に直接触る decoder なので scan を兼任する
// つまり decode::variant<Ts...> の前に scan::* は不要
// 各候補 Ts に対して parse/decode を try し、成功した decoded value を Variant に保持する
```

```cpp
[x] ser20::decode::vector<T>
// T: ElemT または AsEntryTupleT
// pipeline input: void
// context input: DecodeContext<Storage, R>&
// context source: ctx.reader()
// output: std::vector<decoded_result_t<T>>
// 現在 token が ArrayBegin であることを要求し、ctx.reader() を消費する
// Reader に直接触る decoder なので scan を兼任する
// つまり decode::vector<T> の前に scan::array は不要
// 各 array element に対して T の parse/decode を try し、成功した decoded value を vector に保持する
// 例: T が ElemT なら各 element をその Elem pipeline で decode する
// 例: T が AsEntryTupleT なら各 element を object schema として再帰 decode する
// これにより、object schema の member として std::vector<ChildSchema> や std::vector<decoded_result_t<Elem>> を持てる
```

```cpp
[x] ser20::decode::fixed_array<T, N>
// T: ElemT または AsEntryTupleT
// pipeline input: void
// context input: DecodeContext<Storage, R>&
// context source: ctx.reader()
// output: std::array<decoded_result_t<T>, N>
// 現在 token が ArrayBegin であることを要求し、固定長 array として decode する
// Reader に直接触る decoder なので scan を兼任する
// つまり decode::fixed_array<T, N> の前に scan::array は不要
```

```cpp
[-] ser20::decode::tuple<Ts...>
// Ts: ElemT または AsEntryTupleT
// pipeline input: void
// context input: DecodeContext<Storage, R>&
// context source: ctx.reader()
// output: std::tuple<decoded_result_t<Ts>...>
// 現在 token が ArrayBegin であることを要求し、positional tuple として decode する
// Reader に直接触る decoder なので scan を兼任する
// つまり decode::tuple<Ts...> の前に scan::array は不要
```

```cpp
[ ] ser20::decode::optional<T>
// pipeline input: scan output | void
// context input: DecodeContext<Storage, R>& when input is void
// output: std::optional<decoded_result_t<T>>
// missing / null / candidate_rejected を optional として扱う
```

```cpp
[x] ser20::decode::custom<F>
// pipeline input: previous output
// context input: optional
// output: invoke_result_t<F, input> または Result<..., Error>
// user-defined conversion を適用する
```

```cpp
[x] ser20::decode::identity
// pipeline input: T
// context input: none
// output: T
// 変換せずそのまま渡す
```

---

# validate

`validate` は原則 **前段 pipeline output を検査して同じ値を返す段階**。
Reader/Writer は基本使わない。必要なら custom/predicate だけ context-aware を許す。

```cpp
[x] ser20::validate::match<V>
// pipeline input: T
// context input: none
// output: T
// decoded value T が V と等しいことを検証する
// 失敗時 validation_failed / invalid_choice
```

```cpp
[x] ser20::validate::one_of<Vs...>
// pipeline input: T
// context input: none
// output: T
// T が Vs... のいずれかと等しいことを検証する
```

```cpp
[x] ser20::validate::range<Min, Max>
// pipeline input: arithmetic T
// context input: none
// output: T
// T が [Min, Max] の範囲内であることを検証する
```

```cpp
[x] ser20::validate::min<Min>
// pipeline input: arithmetic T
// context input: none
// output: T
```

```cpp
[x] ser20::validate::max<Max>
// pipeline input: arithmetic T
// context input: none
// output: T
```

```cpp
[-] ser20::validate::not_null
// pipeline input: nullable/null-aware value
// context input: none
// output: input
// JSON null / CBOR null / MessagePack nil などを拒否する
```

```cpp
[x] ser20::validate::non_empty
// pipeline input: string-like | container-like T
// context input: none
// output: T
```

```cpp
[x] ser20::validate::size<N>
// pipeline input: string-like | container-like T
// context input: none
// output: T
```

```cpp
[x] ser20::validate::size_range<Min, Max>
// pipeline input: string-like | container-like T
// context input: none
// output: T
```

```cpp
[x] ser20::validate::predicate<F>
// pipeline input: T
// context input: optional
// output: T
// F(value) または F(value, ctx) を適用する
```

```cpp
[x] ser20::validate::custom<F>
// pipeline input: T
// context input: optional
// output: T または Result<T, Error> または Result<void, Error>
// user-defined validation hook
```

```cpp
[x] ser20::validate::identity
// pipeline input: T
// context input: none
// output: T
```

---

# store

`store` は **前段 pipeline output を field storage に反映する段階**。
`DecodeContext<Storage, R>` の `value()` / `occurrences()` を使うので context-aware。

```cpp
[x] ser20::store::assign
// pipeline input: T
// context input: DecodeContext<Storage, R>&
// context target: ctx.value()
// output: void
// decoded value T を field storage に代入する
```

```cpp
[x] ser20::store::once
// pipeline input: T
// context input: DecodeContext<Storage, R>&
// context target: ctx.value(), ctx.occurrences()
// output: void
// 代入は一度だけ許可し、重複時は duplicate_member
```

```cpp
[x] ser20::store::overwrite
// pipeline input: T
// context input: DecodeContext<Storage, R>&
// output: void
// 重複時は後勝ちで上書きする
```

```cpp
[ ] ser20::store::first_wins
// pipeline input: T
// context input: DecodeContext<Storage, R>&
// output: void
// 重複時は最初の値を保持し、後続値は消費だけして無視する
```

```cpp
[x] ser20::store::push
// pipeline input: T
// context input: DecodeContext<std::vector<T>, R>&
// output: void
// ctx.value().push_back(decoded)
```

```cpp
[x] ser20::store::emplace
// pipeline input: T
// context input: DecodeContext<std::vector<T>, R>&
// output: void
// ctx.value().emplace_back(decoded)
```

```cpp
[-] ser20::store::insert
// pipeline input: T
// context input: DecodeContext<AssociativeContainer, R>&
// output: void
// ctx.value().insert(decoded)
```

```cpp
[-] ser20::store::append
// pipeline input: T
// context input: DecodeContext<Appendable, R>&
// output: void
// append 可能 container/buffer に追加
```

```cpp
[x] ser20::store::ignore
// pipeline input: T
// context input: optional
// output: void
// decoded value T を保持しない
```

```cpp
[-] ser20::store::callback<F>
// pipeline input: T
// context input: DecodeContext<Storage, R>&
// output: void
// decoded value T を callback に渡す
```

---

# unknown

`unknown` は pipeline primitive ではなく、**object decoder policy**。

```cpp
ser20::unknown::reject
// object decoder policy
// unknown key を error にする
```

```cpp
ser20::unknown::reject_with_suggestion
// object decoder policy
// unknown key を typo suggestion 付きで error にする
```

```cpp
ser20::unknown::ignore
// object decoder policy
// unknown member の value 全体を reader/token stream から skip する
// scalar/object/array すべて skip 対象
```

```cpp
ser20::unknown::callback<F>
// object decoder policy
// UnknownMemberContext を user callback に渡す
// callback が skip / reject / custom handling を選ぶ
```

---

# encode

`encode` は **前段 pipeline input を出力用表現へ変換する段階**。
Writer への実書き込みは `write::*`。

```cpp
[x] ser20::encode::number<T>
// pipeline input: T
// context input: none
// output: std::string
// arithmetic T を number 文字列に変換する
```

```cpp
[x] ser20::encode::boolean
// pipeline input: bool
// context input: none
// output: std::string_view
// true / false の出力表現に変換する
```

```cpp
[x] ser20::encode::string_view
// pipeline input: std::string_view
// context input: none
// output: std::string_view
// すでに出力可能な borrowed string として渡す
```

```cpp
[ ] ser20::encode::string
// pipeline input: string-like
// context input: optional allocator/context
// output: std::string
// 必要に応じて escape 済み string を作る
```

```cpp
[-] ser20::encode::null
// pipeline input: null-like value
// context input: none
// output: std::string_view
```

```cpp
[-] ser20::encode::custom<F>
// pipeline input: T
// context input: optional
// output: invoke_result_t<F, T>
```

```cpp
[x] ser20::encode::identity
// pipeline input: T
// context input: none
// output: T
```

---

# write

`write` は **EncodeContext 由来の Writer に出力する終端段階**。
原則 output は `void`。

```cpp
[-] ser20::write::scalar
// pipeline input: std::string_view
// context input: EncodeContext<T, W>&
// context target: ctx.writer()
// output: void
// writer に scalar 表現を書き込む
```

```cpp
[-] ser20::write::string
// pipeline input: std::string_view
// context input: EncodeContext<T, W>&
// context target: ctx.writer()
// output: void
// writer に string 表現を書き込む
// quote/escape の責務は Writer または encode::string と整合させる
```

```cpp
[-] ser20::write::object_begin
// pipeline input: void
// context input: EncodeContext<T, W>&
// context target: ctx.writer()
// output: void
```

```cpp
[-] ser20::write::object_end
// pipeline input: void
// context input: EncodeContext<T, W>&
// context target: ctx.writer()
// output: void
```

```cpp
[-] ser20::write::array_begin
// pipeline input: void
// context input: EncodeContext<T, W>&
// context target: ctx.writer()
// output: void
```

```cpp
[-] ser20::write::array_end
// pipeline input: void
// context input: EncodeContext<T, W>&
// context target: ctx.writer()
// output: void
```

---

# load

`load` は **EncodeContext または object value から encode pipeline input を取り出す段階**。

```cpp
[-] ser20::load::value
// pipeline input: void
// context input: EncodeContext<T, W>& または const EncodeContext<T, W>&
// context source: ctx.value()
// output: T& または const T&
// encode 対象 value を取り出して渡す
```

```cpp
[-] ser20::load::member<Ptr>
// pipeline input: object-like T
// context input: none
// output: member reference
// member pointer Ptr を使って object から field value を取り出す
```

```cpp
[-] ser20::load::custom<F>
// pipeline input: T
// context input: optional
// output: invoke_result_t<F, T>
// user-defined getter
```

```cpp
[-] ser20::load::identity
// pipeline input: T
// context input: none
// output: T
```

---

# match

`match` は **candidate selection 用 validation**。
失敗時は `candidate_rejected` を返す点が `validate::*` と違う。

```cpp
[x] ser20::match::literal<V>
// pipeline input: T
// context input: none
// output: T
// input が V と等しいことを検証する
// V は T と比較可能な NTTP
// 等しくない場合は candidate_rejected
```

```cpp
[x] ser20::match::any
// pipeline input: T
// context input: none
// output: T
// 常に candidate accepted
// candidate matching の placeholder / fallback 用
```

---

# Summary

最重要の区別:

```text
scan / write / load::value:
  Context から Reader / Writer / value を取る

decode / validate / store / encode / match:
  基本は前段 pipeline output を input として受ける

store / write:
  Context に副作用を持つ終端寄り primitive

unknown:
  pipeline ではなく object decoder policy
```



---> Fixed Object
struct FixedObject {
  Entry<Elem<scan::string | match::literal<"name>>, Elem<scan::string | decode::number | store::once>>;
  Entry<Elem<scan::string | match::literal<"id">>, Elem<scan::literal | decode::number | store::once>>;
  Entry<Elem<scan::string | match::literal<"data">>, Elem<scan::literal | decode::number | store::once>>;
  ...
}; -> fixed number of object (order not aware)


struct OrderedFixedObject {
  Entry<Elem<scan::string | store::once>, Elem<scan::string | decode::number | store::once>>;
  Entry<Elem<scan::string | store::once>, Elem<scan::literal | decode::number | store::once>>;
  Entry<Elem<scan::string | store::once>, Elem<scan::literal | decode::number | store::once>>;
  ...
}; -> fixed number of object (order aware)


---> Fixed Array
struct OrderedTuple {
  Elem<scan::string | decode::number | scan::push>
  Elem<scan::string | decode::number | scan::push>
  Elem<scan::string | decode::number | scan::push>
}; -> variable array

  Branch<Elem<scan::string | decode::number | scan::push>>


struct VariableObject {
  Entry<Elem<scan::string | decode::number | scan::push>, Elem<scan::string | decode::number | scan::push>>
}; -> variable object

