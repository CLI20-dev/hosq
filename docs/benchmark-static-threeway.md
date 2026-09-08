# Typed HOSQ / Glaze / yyjson comparison — 2026-09-08

今回の比較では Glaze をすべて型付きスキーマで実行した。部分取得は必要なメンバーだけを宣言し、未知のキーを skip する。以前の generic DOM の比較とは区別する。

直前の Context bridge の inline 指定前後を、同じベンチマークソースで比較した。中サイズの全項目取得で HOSQ / Glaze は **1.293 → 1.242**、大サイズで **1.266 → 1.222**。HOSQ の処理時間はそれぞれ平均約 **3.9%、3.4%** 短縮した。この直接比較の対象はこの2ケースだけであり、全ケースの改善を示すものではない。

一方、5項目の部分取得、混合数値、エスケープ文字列では HOSQ は Glaze の約2倍以上の時間を要する。中・大サイズの id 取得と未エスケープの長い UTF-8 文字列は Glaze より約5〜6%短い。単一の順位ではなく処理ごとの差として読む必要がある。

## Results

CPU time per document. H / G and H / Y are time ratios; smaller is faster.

| Case | HOSQ µs | Glaze µs | yyjson µs | H / G | H / Y |
|---|---:|---:|---:|---:|---:|
| small/id | 0.130 | 0.117 | 0.186 | 1.106 | 0.699 |
| small/all | 0.107 | 0.088 | 0.206 | 1.209 | 0.518 |
| medium/id | 192.701 | 202.416 | 131.610 | 0.952 | 1.464 |
| medium/many | 314.986 | 137.598 | 164.363 | 2.289 | 1.917 |
| medium/all | 144.843 | 116.637 | 168.014 | 1.242 | 0.862 |
| large/id | 24707.000 | 26351.300 | 20805.214 | 0.938 | 1.188 |
| large/all | 18943.333 | 15498.500 | 21822.833 | 1.222 | 0.868 |
| wide_first/id | 317.074 | 220.516 | 111.391 | 1.438 | 2.848 |
| wide_last/id | 317.007 | 219.192 | 148.048 | 1.446 | 2.142 |
| numbers/all | 693.665 | 336.771 | 414.647 | 2.060 | 1.673 |
| strings_utf8/all | 604.214 | 639.085 | 678.802 | 0.946 | 0.890 |
| strings_escaped/all | 1451.243 | 633.627 | 879.266 | 2.290 | 1.651 |

図と生データは `bench/comparison/results/static-threeway-2026-09-08/` にローカル保存する。results は Git の対象外であり、コミットに含めない。

- `comparison.png`, `comparison.svg`: yyjson = 1 の棒グラフ。
- `chart-data.json`, `table.md`, `before-control.json`: 集計値。
- `run1/`, `run2/`: 全反復、実行順序、コマンド、電源状態、バイナリハッシュ。
- `source-sha256.json`, `versions.json`, `compile_commands.json`: 計測対象の記録。

棒は各 run の中央値から求めた比の2回平均。ひげは2回の比の最小・最大であり、信頼区間ではない。表の時間は2回の中央値の平均、比は各 run 内の比の平均なので、丸めた表の時間同士を割った値とはわずかに異なる。

## Comparison contract

- Glaze 8.3.0、yyjson 0.12.0、Clang 21.1.8、C++23、Release `-O3 -DNDEBUG`。HOSQ は現在の未コミットの InputView 版、bundled fast_float 有効。今回ライブラリ本体は変更していない。
- Glaze は部分取得も全取得も typed metadata を使う。部分取得では `error_on_unknown_keys=false`, `partial_read=false`。全配列要素と入力末尾まで処理する。`partial_read` は早期終了の設定であり、この比較では使用しない。[Unknown keys](https://stephenberry.github.io/glaze/unknown-keys/), [Partial read](https://stephenberry.github.io/glaze/partial-read/).
- HOSQ 部分取得は `Try<Entry<Key>>` と未知値の skip。全項目取得は Try なしの `Entry` スキーマで、**宣言されたキー順序が前提**。Glaze のオブジェクト読み込みは任意順に対応する。この保証の差は残る。
- 3者とも毎回新しい取得結果 vector を作り、共通の集計処理を実行して破棄する。HOSQ のスキーマ型から共通集計への変換は行単位のローカル値であり、2本目の vector は作らない。
- yyjson はデフォルトの `yyjson_read` で DOM を作り、選択した値の vector を作る。DOM と vector の割当・破棄も計測内。id だけなら `yyjson_obj_get` を1回使用し、複数項目は1回の走査で取得する。DOM で判明した要素数による reserve は計測内に行う。
- HOSQ と Glaze の文字列は借用 `string_view` で受け、共通の unescape/hash 処理に渡す。yyjson はデコード済み文字列を同じ集計へ渡す。混合数値は型付き tuple で uint64/int64/double を保持し、UINT64_MAX を double に変換しない。
- 入力生成・読み込みは計測外。同じ論理 JSON バイト列を繰り返す resident-input の比較。Glaze の入力 capacity の準備は計測外、読み込み時の一時 padding は計測内。HOSQ は bounded FastReader を使用する。
- Python が入力を読み直して選択結果の独立 digest を生成。全36組および before の2組で入力保持、全入力の消費、出力の一致を確認した。
- 入力は正常系。UTF-8 や skip 中の検証保証には差があり、同一の検証負荷や不正入力への同等性は主張しない。
- AC 電源下で、順序を変えた2回の実行。各ケース・各ライブラリは別プロセス、各5反復、各反復最低0.1秒。計測中にビルドを並行実行していない。以前のバッテリー使用時の絶対時間とは比較していない。

12ケースは小・中・30 MBの入力、少数・5項目・全項目取得、不要キー128個と一致位置、混合数値、同じ文字列の UTF-8 / escape 表現を分ける。many は id と payload の name/count/ratio/ok の5葉を取得する。全項目は入力ごとに全値の静的型を記述する。

## Before control

同じ `static_comparison.cc` を、inline 変更直前の保存済みヘッダーでもビルドした。現在との差は decoder の6箇所と decode pipeline の2箇所への `HOSQ_ALWAYS_INLINE` 追加のみ。before の include は `results/reader-view-specialization-2026-09-08/inputview-final/include`。過去に測った絶対時間の転記ではない。

| Case | run 1 after / before | run 2 after / before | Before H / G | After H / G |
|---|---:|---:|---:|---:|
| medium/all | 0.9717 | 0.9498 | 1.293 | 1.242 |
| large/all | 0.9625 | 0.9688 | 1.266 | 1.222 |

新しいコンテキストの独立監査で、型付き skip の実装、余分な HOSQ vector がないこと、計測範囲、全380サンプル、集計、ソースとバイナリのハッシュ、before の8箇所だけの差を確認した。

## Reproduce

Google Benchmark 1.9.5 と上記の固定バージョンを用意し、同じ Clang ツールチェーンを C/C++ に指定する。

```sh
cmake -S bench/comparison -B /tmp/hosq-static-build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DYYJSON_SOURCE=/path/to/yyjson.c \
  -DGLAZE_SOURCE_DIR=/path/to/glaze-8.3.0
cmake --build /tmp/hosq-static-build --target hosq_static_comparison static_comparison_fixtures
python3 bench/comparison/run_static.py /tmp/hosq-static-build/hosq_static_comparison /tmp/hosq-static-build/fixtures-static bench/comparison/results/static-rerun/run1 --seed 2026090801
python3 bench/comparison/run_static.py /tmp/hosq-static-build/hosq_static_comparison /tmp/hosq-static-build/fixtures-static bench/comparison/results/static-rerun/run2 --seed 2026090802
python3 bench/comparison/summarize_static.py bench/comparison/results/static-rerun/run1 bench/comparison/results/static-rerun/run2 bench/comparison/results/static-rerun
python3 bench/comparison/plot_static.py bench/comparison/results/static-rerun/chart-data.json bench/comparison/results/static-rerun/comparison
```

図の出力には Matplotlib が必要。before を再測定する場合は同じ benchmark source を保存済みヘッダーだけ差し替えてビルドし、両 run に `--before-executable PATH` を渡す。今回の正確なコンパイル・リンク指定はローカル results に保存した。
