# scan

```cpp
[x] ser20::scan::scalar
// input: ser20::Scanner&
// output: std::string_view
// scanner を進め、scalar value を取り出して渡す
// 対象: string / number / true / false / null
// string の quote 除去はするが、escape 解決はしない

[x] ser20::scan::string
// input: ser20::Scanner&
// output: std::string_view
// scanner を進め、JSON string の中身だけを渡す
// quote は含めない
// escape 解決は decode 側で行う

[x] ser20::scan::literal
// input: void
// output: std::string_view
// literal のみ受ける
// number / true / false / null
// string が来たら type_mismatch or unexpected_token

[x] ser20::scan::object
// input: ser20::Scanner&
// output: std::string_view
// scanner を進め、object 全体の入力範囲を渡す
// token 化も parse も行わない

[-] ser20::scan::object_tokens<N>
// input: ser20::Scanner&
// output: ser20::TokenView<N>
// scanner を進め、object 内の structural token を固定長 stack buffer に積んで渡す
// N を超えた場合は store/decode 側に error として伝播する

[-] ser20::scan::object_tokens
// input: ser20::Scanner&
// output: ser20::TokenVector
// scanner を進め、object 内の structural token を heap buffer に積んで渡す
// allocation を許可する scan primitive

[x] ser20::scan::array
// input: ser20::Scanner&
// output: std::string_view
// scanner を進め、array 全体の入力範囲を渡す
// token 化も parse も行わない

[-] ser20::scan::array_tokens<N>
// input: ser20::Scanner&
// output: ser20::TokenView<N>
// scanner を進め、array 内の structural token を固定長 stack buffer に積んで渡す
// N を超えた場合は store/decode 側に error として伝播する

[-] ser20::scan::array_tokens
// input: ser20::Scanner&
// output: ser20::TokenVector
// scanner を進め、array 内の structural token を heap buffer に積んで渡す
// allocation を許可する scan primitive
```
# decode_into
直接構築 TODO

# decode

```cpp
[x] ser20::decode::number<T>
// input: std::string_view
// output: T
// number 文字列を浮動小数点型 T に変換する

[x] ser20::decode::boolean
// input: std::string_view
// output: bool
// true / false を bool に変換する

[x] ser20::decode::string_view
// input: std::string_view
// output: std::string_view
// borrowed string_view のまま渡す
// escape が存在する場合の扱いは別 primitive に任せる

[ ] ser20::decode::string
// input: std::string_view
// output: std::string
// allocation して escape 解決済み string を作る

[ ] ser20::decode::parse_to<T>
// input: ser20::Scanner& | ser20::TokenView<N> | ser20::TokenVector
// output: T
// scanner または tokenized object / array 表現を T として再帰 parse する

[-] ser20::decode::variant<Ts...>
// input: ser20::Scanner& | ser20::TokenView<N> | ser20::TokenVector
// output: ser20::Variant<Ts...>
// scanner または tokenized object / array 表現を候補型 Ts... のいずれかとして parse する

[-] ser20::decode::member<P>
// input: ser20::Scanner& | ser20::TokenView<N> | ser20::TokenVector
// output: std::pair<std::string_view, store_result_t<P>>
// object member を key:value として処理する
// key は JSON object の仕様上 string 固定なので std::string_view として渡す
// value には pipeline P を適用する
// P は decode と store を含んでよい

[-] ser20::decode::vector<P>
// input: ser20::Scanner& | ser20::TokenView<N> | ser20::TokenVector
// output: std::vector<store_result_t<P>>
// scanner または tokenized array 表現を std::vector として decode する
// 各 element には pipeline P を適用する
// P は scalar decode でも object parse でもよい

[-] ser20::decode::fixed_array<P, N>
// input: ser20::Scanner& | ser20::TokenView<M> | ser20::TokenVector
// output: std::array<store_result_t<P>, N>
// scanner または tokenized array 表現を std::array として decode する
// 各 element には pipeline P を適用する

[-] ser20::decode::tuple<Ps...>
// input: ser20::Scanner& | ser20::TokenView<N> | ser20::TokenVector
// output: std::tuple<store_result_t<Ps>...>
// scanner または tokenized array 表現を positional tuple として decode する
// 各 position には対応する pipeline Ps を適用する

[ ] ser20::decode::optional<T>
// input: scan output
// output: std::optional<T>
// missing / null / decode failure を optional として扱う


[x] ser20::decode::custom<F>
// input: previous decode output
// output: invoke_result_t<F, input>
// user-defined conversion を適用する
// enum 変換など、reflection が無い環境での名前解決もここに寄せる

[x] ser20::decode::identity
// input: T
// output: T
// 変換せずそのまま渡す
```

# validate

```cpp
[x] ser20::validate::match<V>
// input: T
// output: T
// decoded value T が V と等しいことを検証する
// 一致しない場合は validation_failed または invalid_choice を返す
// 例: type tag の確認など
//   ser20::validate::match<"circle"_json_string>

[x] ser20::validate::one_of<Vs...>
// input: T
// output: T
// decoded value T が Vs... のいずれかと等しいことを検証する
// enum / string literal / numeric literal などの allowed choices に使う
// 一致しない場合は invalid_choice を返す

[x] ser20::validate::range<Min, Max>
// input: arithmetic T
// output: T
// decoded value T が [Min, Max] の範囲内であることを検証する
// 範囲外の場合は validation_failed または out_of_range を返す
// Min / Max は閉区間

[x] ser20::validate::min<Min>
// input: arithmetic T
// output: T
// decoded value T が Min 以上であることを検証する

[x] ser20::validate::max<Max>
// input: arithmetic T
// output: T
// decoded value T が Max 以下であることを検証する

[-] ser20::validate::not_null
// input: nullable/null-aware value
// output: input
// null ではないことを検証する
// JSON null / CBOR null / MessagePack nil などを拒否するために使う

[x] ser20::validate::non_empty
// input: string-like | container-like T
// output: T
// decoded value T が empty ではないことを検証する
// string / vector / array-like container などに使う

[x] ser20::validate::size<N>
// input: string-like | container-like T
// output: T
// decoded value T の size() が N と等しいことを検証する

[x] ser20::validate::size_range<Min, Max>
// input: string-like | container-like T
// output: T
// decoded value T の size() が [Min, Max] の範囲内であることを検証する

[x] ser20::validate::predicate<F>
// input: T
// output: T
// user-defined predicate F(value) を適用する
// F(value) が true を返せば通過
// false を返せば validation_failed
// F は constexpr function object / function pointer / stateless lambda などを想定

[x] ser20::validate::custom<F>
// input: T
// output: T
// user-defined validation F(value) を適用する
// F は bool / Result<void, Error> / Result<T, Error> などを返せる設計にしてよい
// predicate より自由度の高い validation hook

[x] ser20::validate::identity
// input: T
// output: T
// 何も検証せずそのまま渡す
// 条件付き pipeline 生成や placeholder 用
```

# store

```cpp
[x] ser20::store::assign
// input: T
// output: T
// decoded value T を field の保持型にする

[ ] ser20::store::once
// input: T
// output: T
// decoded value T を field の保持型にする
// 代入は一度だけ許可し、重複時は store の意味論として error にする

[ ] ser20::store::overwrite
// input: T
// output: T
// decoded value T を field の保持型にする
// 重複時は後勝ちで上書きする

[ ] ser20::store::first_wins
// input: T
// output: T
// decoded value T を field の保持型にする
// 重複時は最初の値を保持する

[x] ser20::store::push
// input: T
// output: std::vector<T>
// decoded value T を std::vector<T> に push_back して保持する

[x] ser20::store::emplace
// input: T
// output: std::vector<T>
// decoded value T から std::vector<T> に emplace_back して保持する

[-] ser20::store::insert
// input: T
// output: implementation-defined associative container for T
// decoded value T を associative container に insert して保持する

[-] ser20::store::append
// input: T
// output: appendable container/buffer for T
// decoded value T を append 可能な container/buffer に追加して保持する

[x] ser20::store::ignore
// input: T
// output: ser20::ignored
// decoded value T を保持しない

[-] ser20::store::callback<F>
// input: T
// output: ser20::callback_result_t<F, T>
// decoded value T を user callback に渡し、その callback の意味論で保持型を決める
```

ser20::unknown::reject                  // unknown key を error にする
ser20::unknown::reject_with_suggestion  // typo suggestion 付きで error にする
ser20::unknown::ignore                  // unknown key を無視する
ser20::unknown::callback<F>             // UnknownMemberContext を user callback に渡す



# encode

```
[ ] ser20::encode::integer<T>
// input: T
// output: std::string
// 整数値 T を出力表現用の number 文字列に変換する

[ ] ser20::encode::floating<T>
// input: T
// output: std::string
// 浮動小数点値 T を出力表現用の number 文字列に変換する

[ ] ser20::encode::boolean
// input: bool
// output: std::string_view
// true / false の出力表現に変換する

[ ] ser20::encode::string_view
// input: std::string_view
// output: std::string_view
// すでに出力可能な borrowed string として渡す
// escape は write 側または別 primitive に任せる

[ ] ser20::encode::string
// input: string-like
// output: std::string
// 必要に応じて escape 済み string を作る

[-] ser20::encode::null
// input: null-like value
// output: std::string_view
// null の出力表現に変換する

[-] ser20::encode::custom<F>
// input: T
// output: invoke_result_t<F, T>
// user-defined conversion を適用する
// enum -> string などをここに寄せる

[ ] ser20::encode::identity
// input: T
// output: T
// 変換せずそのまま渡す
```

# write
```
[-] ser20::write::scalar
// input: std::string_view
// output: void
// writer.write(TokenType::SCALAR, value) を呼ぶ

[-] ser20::write::string
// input: std::string_view
// output: void
// writer.write(TokenType::SCALAR, escaped/quoted representation) を呼ぶ
// quote を含めるかどうかは Writer の責務に合わせる

[-] ser20::write::object_begin
// input: void
// output: void
// writer.write(TokenType::ObjectBegin, {}) を呼ぶ

[-] ser20::write::object_end
// input: void
// output: void
// writer.write(TokenType::ObjectEnd, {}) を呼ぶ

[-] ser20::write::array_begin
// input: void
// output: void

[-] ser20::write::array_end
// input: void
// output: void
```

# load

```
[-] ser20::load::value
// input: EncodeContext<T, W>
// output: T& または const T&
// context が保持している encode 対象 value を取り出して渡す

[-] ser20::load::member<Ptr>
// input: object-like T
// output: member reference
// member pointer Ptr を使って object から field value を取り出す
// 例: ser20::load::member<&Circle::radius>

[-] ser20::load::custom<F>
// input: T
// output: invoke_result_t<F, T>
// user-defined getter を適用する
// private member / computed property / external mapping に使う

[-] ser20::load::identity
// input: T
// output: T
// 変換せずそのまま渡す
```
