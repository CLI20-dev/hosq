# Medium / 5 fields: 現行版profile — 2026-09-08

Glazeは今回の設定でキー順序を要求していない。順序付きHOSQの以前の診断を、任意順の比較の改善率として使用しない。今回ライブラリ・スキーマ・benchmarkソースは変更していない。

## 順序の確認

`static_comparison.cc` のGlaze設定は `error_on_unknown_keys=false`、`partial_read=false`。ManyRecord/ManyPayloadのメタデータを使い、Glaze 8.3.0 `read.hpp:3386` から `parse_and_invoke` に進む。デフォルトではlinear_searchは無効。`read.hpp:441` 以降で入力キーをdecode_hashし、得られたindexでフィールドを選ぶ。宣言順に次のフィールドを読む処理ではない。

追加確認として、2,048レコードの外側・payload双方のキーを逆順にした入力、およびレコードごとにランダム順にした入力を作成した。HOSQ・Glazeとも元の独立digestと一致し、全文書の処理も確認できた。元と同じ230,031 B、取得leaf数10,240。これは順序に関する正しさの確認で、並べ替え入力の性能比較ではない。

## 取得条件

前の三者グラフと同じ `/tmp/hosq-member-resume/top` を使用。Release/-O3の実バイナリにmacOS `sample` を接続した。HOSQとGlazeを別々に2回ずつ、各3秒・1 ms間隔で取得。profile中のbenchmark時間を速度比較値として使わない。最初のsandbox内での接続は失敗したため、権限を拡張して成功したサンプルのみ使用した。

HOSQのmain threadは2,539 / 2,531サンプル。以下はtop-of-stackのself集計。親子のinclusive値は加算していない。5サンプル未満の省略分はその他とする。

| 実行場所 | 1回目 | 2回目 |
|---|---:|---:|
| デコードループ（inlineされた処理を含む） | 30.9% | 30.2% |
| payloadの定数キーmatcher | 19.2% | 20.9% |
| 既知キーの値handler | 17.6% | 17.6% |
| skip本体 | 14.4% | 15.8% |
| 未知キーfallback | 5.6% | 5.3% |
| 外出しされた浮動小数点変換 | 5.9% | 4.1% |
| 文字列の結果集計 | 4.8% | 4.8% |

matcherには、キー照合だけでなく前回追加した未知スカラーメンバーのskipもinlineされている。handlerにはトークン遷移と値の読込を含む。したがってmatcherの20%を「文字の比較だけ」、handlerの18%を「関数呼び出しだけ」と解釈しない。デコードループの30%にも複数の処理が混在し、この全部を状態書き戻しの費用と断定しない。

Glazeは2,534 / 2,538サンプル。`run_glaze`内のinlineされた処理が44.4 / 40.7%、UTF-8検証16.0 / 16.1%、未知キー処理11.5 / 12.4%、memchr本体10.3 / 9.7%、文字列集計9.3 / 10.5%、外出しされた浮動小数点変換6.3 / 7.3%。inlineのされ方が違うため、関数別割合をHOSQと直接対応させない。

## assemblyとの照合

sampleのロードアドレスを差し引き、測定バイナリのdisassemblyに対応付けた。

- Compactのpayload matcherは `0x100030fdc`。`sub sp, sp, #0xd0`、すなわち208 Bのフレームが残る。直後に4handler用の参照をスタックへ書き込む8組の `stp` がある。
- 個別の値handlerへ実際の `bl` が残る。一つの局所ループへすべて消えているわけではない。
- HOSQデコードループのselfサンプルに現れる `+7572 / +7620` は `0x100017a30 / 0x100017a60`。周辺にはトークンコピー、エラー状態の初期化、現在トークンの種別確認、matcher呼び出しが並ぶ。
- matcherが失敗した場合には別のfallback matcherへ `bl` する。今回の入力では `none` のスカラーskipから `values` のコンテナskipへ移る。

これは接続部分の処理が残っている証拠であって、各命令が何%を占めるかの測定ではない。stackサイズ自体やbranch miss数を性能差に換算していない。

## 判断

skip本体は約15%を占め、改善対象だが、それだけでGlazeとの約2倍差を説明するのは不十分。現在のprofileは、任意順を維持したまま、matcher・handler・トークン状態の受け渡しを簡素化する方向を支持する。次の実験でも元の任意順スキーマ、未知キーfallback、同じ入力・同じ取得結果を保つ。

順序固定で速くなるという過去の診断は、今回の比較に対する解決策ではない。Glazeが任意順のまま速いため、任意順の機能自体を不可避のコストとして扱うべきではない。

## 成果物

Git対象外の `bench/comparison/results/many-profile-2026-09-08/` に4本の生profile、self集計、キー順序変更の入力と照合ログ、assemblyの対応範囲、バイナリ・ソースhashを保存。元の実時間は [三者比較](benchmark-current-threeway.md) を参照。
