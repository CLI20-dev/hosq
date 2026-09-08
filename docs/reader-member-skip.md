# FastReader内の未知スカラーメンバーskip — 2026-09-08

定数キーに一致せず、fallbackが標準の `scan::scalar | decode::skip` と `decode::skip` の組である場合、FastReader内でキー・コロン・スカラー値・カンマを続けて読み、次のキー照合へ戻る。中間の値トークン構築と汎用fallbackへの往復を省く。

今回変更したライブラリヘッダーは `include/hosq/json/fast_reader.hh` のみ。core、InputView、状態のフィールド、benchmarkの処理契約は変更していない。以前からある `DeferredKey` の通知を使用する。明示的SIMDや入力paddingの追加条件もない。

## 範囲と互換性

- 既存の定数キー照合のうち、ASCIIの通常キーに使われる高速経路を対象とする。
- スカラーの未知メンバーを連続処理する。既知キーは従来handlerへ渡す。
- コンテナ、最後のメンバー、不確かな区切りでは従来経路に戻る。次のキーの開始まで確認できた時だけ状態を更新する。
- 最後のメンバーを残すのは、既存matcherがhandlerの実行結果を返す契約を維持するため。coreに「skip完了」の新しい返り値は導入しない。
- 利用者独自のfallbackは対象外。さらにbuiltinのScanScalarとDecodeSkipValueだけに型aliasを置き、利用者による完全特殊化を勝手に迂回しない。判定はコンパイル時。
- skip中の字句検証は従来と同じ。従来が読み飛ばせた不正リテラルを新経路だけで拒否する変更はしていない。

## 測定

AC電源、Clang 21.1.8 / -O3 / C++23、fast_float有効。HOSQはAuto、bounded入力でpaddingなし。変更前後のバイナリを同一セッションでランダム順に測定した。2ラウンド、各5反復、各反復最低0.1秒、合計44プロセス・220反復。ビルドは測定前に終了した。表は各ラウンドのCPU時間中央値の平均で、負の変化が短縮を示す。

| 入力 / 取得 | 変更前 µs | 変更後 µs | 時間変化 |
|---|---:|---:|---:|
| compact wide_first/id | 306.852 | 131.235 | -57.23% |
| compact wide_last/id | 306.348 | 128.606 | -58.02% |
| pretty wide_first/id | 339.669 | 160.532 | -52.74% |
| compact medium/id | 181.942 | 187.069 | +2.82% |
| compact medium/many | 296.579 | 280.016 | -5.58% |
| compact medium/all | 136.626 | 136.376 | -0.18% |
| pretty medium/many | 362.209 | 347.042 | -4.19% |
| compact large/id | 23346.417 | 23716.083 | +1.58% |
| compact small/id | 0.123 | 0.121 | -1.53% |

Wideは128レコード×129メンバーで、id以外の128個の整数メンバーをskipする。先頭／末尾とも約57〜58%短縮し、Prettyでも約53%短縮した。medium/manyは約5.6%短縮。一方、medium/idは両ラウンドで約2.8%、large/idは約1〜2%遅くなった。コンテナに対する先読み後の従来経路への復帰が候補だが、この測定だけで原因は確定していない。全ケースで無条件に速くなる変更ではない。

同じ測定でのCompact Wide比較:

| 取得位置 | HOSQ µs | Glaze µs | yyjson µs |
|---|---:|---:|---:|
| 先頭id | 131.235 | 218.496 | 113.695 |
| 末尾id | 128.606 | 209.478 | 149.061 |

HOSQは両方でGlazeより短い。yyjson比では先頭が約15%長く、末尾が約14%短い。yyjsonのDOM構築後のキー検索位置の差も含む結果であり、字句解析器単独の比較ではない。Glaze 8.3.0は必要なフィールドだけ宣言する型付き読込・未知キーskip・全文書処理。yyjson 0.12.0はDOM構築と取得・破棄を含む。詳細な契約と検証保証の差は [既存の比較記録](benchmark-auto-threeway.md) を参照。

## 検証と再現

- C++20 / C++26: それぞれ307テスト合格、合計614。
- 正常・不正・途中切れ6,282入力×3 whitespace modes×3 padding指定＝56,538通りで新旧を比較。取得値、エラー内容・位置、カーソル、トークン、コンテキスト状態が完全一致。
- 追加テストは混在値・重複id・空白、独自fallbackの呼出し、独自DecodeSkipValueの呼出し、既知キーhandlerへ到達する際の状態を確認する。
- このターン開始時のincludeスナップショットとの比較で、変更ヘッダーがFastReaderだけであることを確認した。

生結果・集計表・コンパイル／リンクコマンド・差分・hash・再測定スクリプトは、Git対象外の `bench/comparison/results/member-skip-2026-09-08/` に保存。`run.py` は `/tmp/hosq-member-skip/{before,after}` と既存fixtureディレクトリを使用する。今回の直前版は未コミットの変更を含むため、HEADだけではbeforeを再現できない。ヘッダースナップショットは `/tmp/hosq-member-skip/before-include` に保持した。
