# scan

```cpp
json::scan::scalar
// input: json::Scanner&
// output: std::string_view
// scanner を進め、scalar value を取り出して渡す
// 対象: string / number / true / false / null
// string の quote 除去はするが、escape 解決はしない

json::scan::string
// input: json::Scanner&
// output: std::string_view
// scanner を進め、JSON string の中身だけを渡す
// quote は含めない
// escape 解決は decode 側で行う


json::scan::object_raw
// input: json::Scanner&
// output: std::string_view
// scanner を進め、object 全体の入力範囲を渡す
// token 化も parse も行わない

json::scan::object_tokens<N>
// input: json::Scanner&
// output: json::TokenView<N>
// scanner を進め、object 内の structural token を固定長 stack buffer に積んで渡す
// N を超えた場合は pack/decode 側に error として伝播する

json::scan::object_tokens
// input: json::Scanner&
// output: json::TokenVector
// scanner を進め、object 内の structural token を heap buffer に積んで渡す
// allocation を許可する scan primitive

json::scan::array_raw
// input: json::Scanner&
// output: std::string_view
// scanner を進め、array 全体の入力範囲を渡す
// token 化も parse も行わない

json::scan::array_tokens<N>
// input: json::Scanner&
// output: json::TokenView<N>
// scanner を進め、array 内の structural token を固定長 stack buffer に積んで渡す
// N を超えた場合は pack/decode 側に error として伝播する

json::scan::array_tokens
// input: json::Scanner&
// output: json::TokenVector
// scanner を進め、array 内の structural token を heap buffer に積んで渡す
// allocation を許可する scan primitive
```

# decode

```cpp
json::decode::integer<T>
// input: std::string_view
// output: T
// number 文字列を整数型 T に変換する

json::decode::floating<T>
// input: std::string_view
// output: T
// number 文字列を浮動小数点型 T に変換する

json::decode::boolean
// input: std::string_view
// output: bool
// true / false を bool に変換する

json::decode::string_view
// input: std::string_view
// output: std::string_view
// borrowed string_view のまま渡す
// escape が存在する場合の扱いは別 primitive に任せる

json::decode::string
// input: std::string_view
// output: std::string
// allocation して escape 解決済み string を作る

json::decode::parse_to<T>
// input: json::Scanner& | json::TokenView<N> | json::TokenVector
// output: T
// scanner または tokenized object / array 表現を T として再帰 parse する

json::decode::variant<Ts...>
// input: json::Scanner& | json::TokenView<N> | json::TokenVector
// output: json::Variant<Ts...>
// scanner または tokenized object / array 表現を候補型 Ts... のいずれかとして parse する

json::decode::member<P>
// input: json::Scanner& | json::TokenView<N> | json::TokenVector
// output: std::pair<std::string_view, pack_result_t<P>>
// object member を key:value として処理する
// key は JSON object の仕様上 string 固定なので std::string_view として渡す
// value には pipeline P を適用する
// P は decode と pack を含んでよい

json::decode::vector<P>
// input: json::Scanner& | json::TokenView<N> | json::TokenVector
// output: std::vector<pack_result_t<P>>
// scanner または tokenized array 表現を std::vector として decode する
// 各 element には pipeline P を適用する
// P は scalar decode でも object parse でもよい

json::decode::fixed_array<P, N>
// input: json::Scanner& | json::TokenView<M> | json::TokenVector
// output: std::array<pack_result_t<P>, N>
// scanner または tokenized array 表現を std::array として decode する
// 各 element には pipeline P を適用する

json::decode::tuple<Ps...>
// input: json::Scanner& | json::TokenView<N> | json::TokenVector
// output: std::tuple<pack_result_t<Ps>...>
// scanner または tokenized array 表現を positional tuple として decode する
// 各 position には対応する pipeline Ps を適用する

json::decode::optional<T>
// input: scan output
// output: std::optional<T>
// missing / null / decode failure を optional として扱う


json::decode::custom<F>
// input: previous decode output
// output: invoke_result_t<F, input>
// user-defined conversion を適用する
// enum 変換など、reflection が無い環境での名前解決もここに寄せる

json::decode::identity
// input: T
// output: T
// 変換せずそのまま渡す
```

# pack

```cpp
json::pack::assign
// input: T
// output: T
// decoded value T を field の保持型にする

json::pack::once
// input: T
// output: T
// decoded value T を field の保持型にする
// 代入は一度だけ許可し、重複時は pack の意味論として error にする

json::pack::overwrite
// input: T
// output: T
// decoded value T を field の保持型にする
// 重複時は後勝ちで上書きする

json::pack::first_wins
// input: T
// output: T
// decoded value T を field の保持型にする
// 重複時は最初の値を保持する

json::pack::push
// input: T
// output: std::vector<T>
// decoded value T を std::vector<T> に push_back して保持する

json::pack::emplace
// input: T
// output: std::vector<T>
// decoded value T から std::vector<T> に emplace_back して保持する

json::pack::insert
// input: T
// output: implementation-defined associative container for T
// decoded value T を associative container に insert して保持する

json::pack::append
// input: T
// output: appendable container/buffer for T
// decoded value T を append 可能な container/buffer に追加して保持する

json::pack::ignore
// input: T
// output: json::ignored
// decoded value T を保持しない

json::pack::callback<F>
// input: T
// output: json::callback_result_t<F, T>
// decoded value T を user callback に渡し、その callback の意味論で保持型を決める
```

json::unknown::reject                  // unknown key を error にする
json::unknown::reject_with_suggestion  // typo suggestion 付きで error にする
json::unknown::ignore                  // unknown key を無視する
json::unknown::callback<F>             // UnknownMemberContext を user callback に渡す
