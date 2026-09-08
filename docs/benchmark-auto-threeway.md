# Auto document dispatch: HOSQ / Glaze / yyjson — 2026-09-08

現在のHOSQ Auto（文書先頭でPretty/Compactを選択する版）で、三者を同じ実行環境で再測定した。Compactの中・大サイズ全項目取得では、HOSQはyyjsonより約17%短い時間で処理し、Glazeより約18〜19%長い。Prettyの中サイズ全項目取得では、HOSQはGlazeの1.55倍、yyjsonの1.27倍の時間を要する。Autoの切替だけで形式間の性能差が解消したわけではない。

今回ライブラリ本体は変更していない。旧バイナリの時間の流用ではなく、現在のソースと一致するバイナリで全ライブラリを測り直した。これ単独では、直前の変更による因果的な改善率を測るbefore/after比較ではない。

## Compact inputs

従来の最小12ケース。時間は文書あたりのCPU時間（µs）。H/G、H/Yは時間比で、小さいほど高速。

| Case | HOSQ µs | Glaze µs | yyjson µs | H / G | H / Y |
|---|---:|---:|---:|---:|---:|
| small/id | 0.125 | 0.114 | 0.180 | 1.101 | 0.697 |
| small/all | 0.104 | 0.086 | 0.201 | 1.202 | 0.516 |
| medium/id | 182.912 | 201.773 | 131.624 | 0.907 | 1.390 |
| medium/many | 299.359 | 138.392 | 163.084 | 2.163 | 1.836 |
| medium/all | 137.385 | 115.170 | 166.477 | 1.193 | 0.825 |
| large/id | 23337.850 | 26250.100 | 20952.500 | 0.889 | 1.114 |
| large/all | 18009.500 | 15247.762 | 21638.250 | 1.181 | 0.832 |
| wide_first/id | 306.546 | 219.346 | 112.443 | 1.398 | 2.726 |
| wide_last/id | 308.220 | 208.871 | 150.725 | 1.476 | 2.045 |
| numbers/all | 673.254 | 331.387 | 417.072 | 2.032 | 1.614 |
| strings_utf8/all | 590.355 | 636.900 | 681.537 | 0.927 | 0.866 |
| strings_escaped/all | 1441.588 | 632.075 | 883.348 | 2.281 | 1.632 |

## Pretty input

中サイズと同じ2,048レコード・同じキー順序・同じ値を、4スペースindentで整形した647,824 BのJSON。1項目・5項目・全項目取得を比較する。Compactは230,031 Bなので、形式間で入力byte数が同じという意味ではない。それぞれの形式の中では三者が同じ入力byte列を読む。

| Case | HOSQ µs | Glaze µs | yyjson µs | H / G | H / Y |
|---|---:|---:|---:|---:|---:|
| medium/id | 248.924 | 243.550 | 194.454 | 1.022 | 1.280 |
| medium/many | 357.598 | 197.595 | 227.193 | 1.810 | 1.574 |
| medium/all | 290.697 | 188.126 | 229.434 | 1.546 | 1.267 |

## Fairness and measurement

- Glaze 8.3.0は全ケースで型付きスキーマ。部分取得は必要なメンバーだけ宣言し、`error_on_unknown_keys=false`で残りをskip。`partial_read=false`で全文書を処理する。generic DOMとの比較ではない。
- HOSQはデフォルトAutoで、Decoder::decode_nestedの入口からPretty/Compactの具体型を選択する。入力はboundedでpadding指定なし。全項目取得はTryなし・宣言順のEntryスキーマ。Glazeは任意のキー順序に対応する。この保証差は残る。
- yyjson 0.12.0はデフォルトのreadでDOMを作り、取得値のvectorを作る。DOM/vectorの割当・破棄も計測内。id取得は1回のyyjson_obj_get、複数項目は1回のオブジェクト走査で取得する。
- 三者とも新しい取得結果vectorを作り、同じ集計を行って破棄する。HOSQは行単位で集計に渡し、余分な結果vectorを作らない。文字列・数値・UINT64_MAXの扱いは[型付き比較の契約](benchmark-static-threeway.md)と同じ。
- 正常なresident入力を反復する。読み込み後の集計も含むend-to-end比較であり、純粋な字句解析速度ではない。UTF-8やskip中の検証保証は三者で完全に同一ではない。
- Clang21.1.8 / Release -O3、bundled fast_float有効。AC電源。順序を変えた2ラウンド、各5反復、各反復最低0.1秒、各ケース・ライブラリは別プロセス。ビルドやグラフ描画を測定と並行していない。
- 15ケース×3ライブラリの45組で独立digestが一致。90プロセスすべて正常終了、450反復。生の90ファイルから45本の棒の値を再計算し、集計と一致した。
- 棒は同ラウンド内でyyjson=1とした比の2回平均。ひげは2回の比の範囲で、信頼区間ではない。表の時間は各ラウンド中央値の平均。微小差の統計的有意性は主張しない。

## Artifacts

ローカルの `bench/comparison/results/auto-threeway-2026-09-08/` に保存し、Gitには含めない。

- `compact/comparison.png`, `.svg`: Compact 12ケースの棒グラフ。
- `pretty/comparison.png`, `.svg`: Pretty 3ケースの棒グラフ。
- 各形式の`run1/`, `run2/`: 生のGoogle Benchmark出力、実行コマンド、電源状態、入力metadata、バイナリhash。
- 各形式の`chart-data.json`, `table.md`: 集計結果。
- `source-and-binary-hashes.json`, `versions.json`, `compile-command.json`: ソースとビルドの記録。
- `plot_compact.py`, `plot_pretty.py`: 再描画スクリプト。Matplotlibが必要。

実行は既存の `run_static.py`、集計は `summarize_static.py` を使う。Pretty用は既存manifestを保ったままmediumの3件だけをmetadataで選び、整形後のbyte数とhashを更新した。標準suiteに重複ケースは追加していない。
