# scan

```cpp
ser20::scan::scalar
// input: ser20::Scanner&
// output: std::string_view
// scanner を進め、scalar value を取り出して渡す
// 対象: string / number / true / false / null
// string の quote 除去はするが、escape 解決はしない

ser20::scan::string
// input: ser20::Scanner&
// output: std::string_view
// scanner を進め、JSON string の中身だけを渡す
// quote は含めない
// escape 解決は decode 側で行う


ser20::scan::object_raw
// input: ser20::Scanner&
// output: std::string_view
// scanner を進め、object 全体の入力範囲を渡す
// token 化も parse も行わない

ser20::scan::object_tokens<N>
// input: ser20::Scanner&
// output: ser20::TokenView<N>
// scanner を進め、object 内の structural token を固定長 stack buffer に積んで渡す
// N を超えた場合は pack/decode 側に error として伝播する

ser20::scan::object_tokens
// input: ser20::Scanner&
// output: ser20::TokenVector
// scanner を進め、object 内の structural token を heap buffer に積んで渡す
// allocation を許可する scan primitive

ser20::scan::array_raw
// input: ser20::Scanner&
// output: std::string_view
// scanner を進め、array 全体の入力範囲を渡す
// token 化も parse も行わない

ser20::scan::array_tokens<N>
// input: ser20::Scanner&
// output: ser20::TokenView<N>
// scanner を進め、array 内の structural token を固定長 stack buffer に積んで渡す
// N を超えた場合は pack/decode 側に error として伝播する

ser20::scan::array_tokens
// input: ser20::Scanner&
// output: ser20::TokenVector
// scanner を進め、array 内の structural token を heap buffer に積んで渡す
// allocation を許可する scan primitive
```

# decode

```cpp
ser20::decode::integer<T>
// input: std::string_view
// output: T
// number 文字列を整数型 T に変換する

ser20::decode::floating<T>
// input: std::string_view
// output: T
// number 文字列を浮動小数点型 T に変換する

ser20::decode::boolean
// input: std::string_view
// output: bool
// true / false を bool に変換する

ser20::decode::string_view
// input: std::string_view
// output: std::string_view
// borrowed string_view のまま渡す
// escape が存在する場合の扱いは別 primitive に任せる

ser20::decode::string
// input: std::string_view
// output: std::string
// allocation して escape 解決済み string を作る

ser20::decode::parse_to<T>
// input: ser20::Scanner& | ser20::TokenView<N> | ser20::TokenVector
// output: T
// scanner または tokenized object / array 表現を T として再帰 parse する

ser20::decode::variant<Ts...>
// input: ser20::Scanner& | ser20::TokenView<N> | ser20::TokenVector
// output: ser20::Variant<Ts...>
// scanner または tokenized object / array 表現を候補型 Ts... のいずれかとして parse する

ser20::decode::member<P>
// input: ser20::Scanner& | ser20::TokenView<N> | ser20::TokenVector
// output: std::pair<std::string_view, pack_result_t<P>>
// object member を key:value として処理する
// key は JSON object の仕様上 string 固定なので std::string_view として渡す
// value には pipeline P を適用する
// P は decode と pack を含んでよい

ser20::decode::vector<P>
// input: ser20::Scanner& | ser20::TokenView<N> | ser20::TokenVector
// output: std::vector<pack_result_t<P>>
// scanner または tokenized array 表現を std::vector として decode する
// 各 element には pipeline P を適用する
// P は scalar decode でも object parse でもよい

ser20::decode::fixed_array<P, N>
// input: ser20::Scanner& | ser20::TokenView<M> | ser20::TokenVector
// output: std::array<pack_result_t<P>, N>
// scanner または tokenized array 表現を std::array として decode する
// 各 element には pipeline P を適用する

ser20::decode::tuple<Ps...>
// input: ser20::Scanner& | ser20::TokenView<N> | ser20::TokenVector
// output: std::tuple<pack_result_t<Ps>...>
// scanner または tokenized array 表現を positional tuple として decode する
// 各 position には対応する pipeline Ps を適用する

ser20::decode::optional<T>
// input: scan output
// output: std::optional<T>
// missing / null / decode failure を optional として扱う


ser20::decode::custom<F>
// input: previous decode output
// output: invoke_result_t<F, input>
// user-defined conversion を適用する
// enum 変換など、reflection が無い環境での名前解決もここに寄せる

ser20::decode::identity
// input: T
// output: T
// 変換せずそのまま渡す
```

# pack

```cpp
ser20::pack::assign
// input: T
// output: T
// decoded value T を field の保持型にする

ser20::pack::once
// input: T
// output: T
// decoded value T を field の保持型にする
// 代入は一度だけ許可し、重複時は pack の意味論として error にする

ser20::pack::overwrite
// input: T
// output: T
// decoded value T を field の保持型にする
// 重複時は後勝ちで上書きする

ser20::pack::first_wins
// input: T
// output: T
// decoded value T を field の保持型にする
// 重複時は最初の値を保持する

ser20::pack::push
// input: T
// output: std::vector<T>
// decoded value T を std::vector<T> に push_back して保持する

ser20::pack::emplace
// input: T
// output: std::vector<T>
// decoded value T から std::vector<T> に emplace_back して保持する

ser20::pack::insert
// input: T
// output: implementation-defined associative container for T
// decoded value T を associative container に insert して保持する

ser20::pack::append
// input: T
// output: appendable container/buffer for T
// decoded value T を append 可能な container/buffer に追加して保持する

ser20::pack::ignore
// input: T
// output: ser20::ignored
// decoded value T を保持しない

ser20::pack::callback<F>
// input: T
// output: ser20::callback_result_t<F, T>
// decoded value T を user callback に渡し、その callback の意味論で保持型を決める
```

ser20::unknown::reject                  // unknown key を error にする
ser20::unknown::reject_with_suggestion  // typo suggestion 付きで error にする
ser20::unknown::ignore                  // unknown key を無視する
ser20::unknown::callback<F>             // UnknownMemberContext を user callback に渡す
