# FastReader whitespace modes

`FastReader` の第3テンプレート引数に空白処理のヒントを指定できる。デフォルトは `Auto`。既存の第1引数（深さ）と第2引数（padding）の意味は変わらない。

```cpp
using namespace hosq::json;
using Automatic = FastReader<>;
using PrettyReader = FastReader<128, InputPadding::none, WhitespaceMode::Pretty>;
using CompactReader = FastReader<128, InputPadding::none, WhitespaceMode::Compact>;
```

3モードとも、スペース・タブ・CR・LFのすべてを受け付ける。入力形式を強制するオプションではなく、空白処理の最適化を選ぶヒントである。文字列内の空白には影響しない。

- `Compact`: 空白がなければ直ちに戻る。空白があれば通常の1 byteずつの走査。
- `Pretty`: 改行やタブも読み進め、連続する4スペースをまとめて処理する。`memcmp` の固定長比較を使い、明示的なSIMD・アラインメント要求は追加しない。
- `Auto`: 文書先頭の空白を読み飛ばし、`{` または `[` の直後2文字がJSON空白ならPretty、それ以外ならCompactを選ぶ。選ばれた具体型のポインターでdecode scope全体を実行する。

選択は `if constexpr` によってコンパイル時に解決する。Readerは引き続き空の型で、stateも3モードで同じ型。保持するモード変数、関数ポインター、仮想関数はない。通常読み込み、固定長数値配列、EntryInput、汎用コンテナskipは同じ空白ヘルパーを使う。

`Decoder::decode` / `decode_into` と `Decoder::decode_nested` は `ext::ReaderDispatch` を経由する。通常Readerはそのまま実行し、Autoの特殊化だけがPretty/Compactの具体型を選ぶ。ネストした処理のReader型は既に具体型なので、再判定しない。stateを共有し、ポインターのcast、heap allocation、virtual callは使わない。coreにはこの小さな汎用dispatchフックを追加した。

低水準のトークンループを書く場合は、そのループ全体を `with_reader` に渡す。

```cpp
FastReader<> reader;
FastReader<>::state_type state;
reader.reset(state, input);
reader.with_reader(state, [&](auto* actual) {
    // actual は Pretty または Compact の具体型へのポインター。
    // state は共有され、ループ中にモードの再判定はない。
    while (actual->next_token(state)) {
        if (actual->current_token(state).type == hosq::TokenType::End) break;
    }
});
```

ポインターはcallbackの実行中だけ有効で、外へ保存しない。Autoオブジェクトへ直接 `next_token` を繰り返す既存の使い方も残しているが、その場合は従来の局所的な空白処理となる。1回の切替を得るにはDecoderか上のscopeを使う。複数回独立に `decode_nested` をAutoで呼べば、各scopeで判定する。

この選択規則はyyjson 0.12.0の `read_root_pretty` / `read_root_minify` と同じ先頭2文字のヒューリスティック。誤って別の形式を選んでも両モードが全JSON空白を受け付けるため、正しさは変わらない。`{ ` のように開き括弧の後の空白が1文字だけならCompactになる。

ユーザー定義のpipeline stepに渡るContextのReader型も、選択されたPretty/Compactとなる。Readerに依存する拡張はその具体型にも対応させる。

## One-time dispatch measurement — 2026-09-08

同じベンチマークソース・入力・出力のdigest検証を使用。旧Auto（局所判定）、新Auto、明示Pretty/Compactを別バイナリで測定した。AC電源、Clang21 -O3、2ラウンド×5反復、別プロセスで順序を変え、ビルドやassembly生成と速度測定を並行していない。表はラウンド中央値の平均（µs）、比は同ラウンド内の比の平均。

| 入力 / 取得 | 旧Auto | 新Auto | 選択先の明示モード | 新Auto / 明示 |
|---|---:|---:|---:|---:|
| Pretty / medium id | 245.735 | 240.089 | 239.942 | 1.001 |
| Pretty / medium 5項目 | 362.792 | 361.668 | 358.956 | 1.008 |
| Pretty / medium 全項目 | 282.328 | 308.322 | 289.412 | 1.065 |
| Pretty / small 全項目 | 0.186 | 0.183 | 0.174 | 1.052 |
| Compact / medium id | 180.578 | 184.442 | 180.624 | 1.021 |
| Compact / medium 5項目 | 295.329 | 297.072 | 296.047 | 1.003 |
| Compact / medium 全項目 | 140.275 | 136.258 | 139.533 | 0.977 |
| Compact / small 全項目 | 0.106 | 0.103 | 0.104 | 0.988 |

assemblyの切り分け用wrapperでは、先頭の選択から具体型のconsume関数への直接分岐/callとなった。具体型のトークンループにモード分岐も間接callもない。確認のためconsumeだけをnoinlineにした診断用wrapperであり、ライブラリにはnoinlineを追加していない。

**選択後のモードdispatchコストは除去できたが、明示モードと実行時間が完全一致することまでは証明されていない。** Prettyの全項目取得では約6.5%の差が残る。コンパイラのコード生成・配置の差を疑う余地はあるが、今回その因果を特定したとは言わない。先頭の判定自体は実行時の仕事であり、特に小入力では無視できるとは限らない。

C++20/C++26で各303件、計606件のテストが成功。Decoder内部で実際のReader型がAutoでないこと、同じDecoderの入力ごとの再選択、shared state、保護ページに接した短い入力で先頭判定が範囲外を読まないことを検証する。

証拠は `bench/comparison/results/auto-dispatch-2026-09-08/` に保存し、Gitには含めない。

## Historical local-Auto measurement (superseded)

以下は先頭dispatch導入前の局所判定版の測定。現在のAutoの性能として引用しない。


変更前と同じベンチマークソースを使い、4版をClang21 / Release / -O3でビルド。中サイズJSONとwide JSONを、同じ値・同じキー順序のCompact形式と4スペースindentのPretty形式にした。各形式の中で入力バイト列と取得結果を一致させ、独立digest検証を通してから測定した。

AC電源、2ラウンド、各5反復、各反復最低0.1秒。各測定は別プロセス、順序を変え、ビルドと並行実行していない。表は各ラウンド中央値の平均（µs）。短縮率は各ラウンド内の比の平均で求める。

| 入力形式 / 取得 | 変更前 | Auto | Pretty | Compact |
|---|---:|---:|---:|---:|
| Pretty / medium id | 250.1 | 250.5 | 239.8 | 312.3 |
| Pretty / medium 5項目 | 409.8 | 369.1 | 378.7 | 438.1 |
| Pretty / medium 全項目 | 318.5 | 287.8 | 282.0 | 315.7 |
| Pretty / wide id | 357.8 | 336.5 | 342.8 | 383.6 |
| Compact / medium id | 193.6 | 177.0 | 181.1 | 194.5 |
| Compact / medium 5項目 | 314.3 | 293.2 | 310.9 | 306.7 |
| Compact / medium 全項目 | 147.2 | 153.4 | 149.5 | 140.4 |
| Compact / wide id | 313.5 | 311.8 | 311.8 | 303.0 |

AutoはPretty入力の5項目で9.9%、全項目で9.5%、wideで6.0%短縮。Compact入力のidで8.5%、5項目で6.7%短縮した。一方、Compact入力の全項目ではAutoが4.2%遅く、明示Compactは4.6%短縮した。入力形式だけでなく、使う処理とコード配置の影響もある。2ラウンドの記述統計であり、微小差の統計的有意性は主張しない。

明示モードが常にAutoより速くなる結果ではない。形式がわかっていても、対象pipelineで測って選ぶのが適切。

## Validation and scope

- C++20とC++26で各300件、計600件のテストが成功。
- 3モード、none/4/8 padding、混在空白、EOF、エラー位置、checkpoint rollback、skip、EntryInputの数値・文字列・bool、固定長数値配列を確認。
- 0〜24 byteの空白と正確なpaddingを保護ページに接して配置し、範囲外読み込みがないことを確認。
- assembly用の小さいwrapperでは、Compactに4スペース比較がなく、Pretty/Autoに32 bitの固定長比較が生成された。モード値を読む命令はない。Autoの空白チェックそのものは実行時に残る。
- この旧測定の時点ではcore変更なし。現在は上記のdispatchフックを追加している。前の構造文字だけでskipする試作は採用していない。

生データ、ビルド指定、assembly、変更差分、ソース・バイナリのハッシュ、テストログは `bench/comparison/results/whitespace-modes-2026-09-08/` に保存。resultsはGit管理外でコミットしない。計測のために既存の標準ベンチマークへ恒久的なケースを追加していない。
