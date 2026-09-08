# 照合済みキーからMemberInputへ — 2026-09-08

任意順のMedium/5 fieldsを維持したまま、現行版から14.2%短縮した。handlerのinlineだけの版からも8.4%短縮。全項目取得はほぼ同水準、Wideとmedium/idは変更前との差が1%以内。Glazeとの差はまだ残る。

## 採用した変更

- `meta_match.hh`: Handler::operator()に既存のHOSQ_ALWAYS_INLINEを指定する。lambdaは元から具体型であり、関数ポインターへ変換していたわけではない。
- `decoder.hh`: 定数キー照合後、boolで成否を返さない値デコーダーについて、既存EntryInputの任意のdecode_matchedフックを試す。非対応の場合は従来経路。boolで成否を返すTryのcheckpoint/rollback経路は変更しない。
- `json/fast_reader.hh`: 照合済みキーの後ろからコロンとscalar開始を確認し、既存の空タグMemberInputを渡す。値トークンを先に作る経路を省き、既存の数値・文字列・真偽値の特殊化を使う。コンテナなどは従来経路。

EntryInputの実装はMatchedというコンパイル時のboolで既存の順序付き入口と共有する。新しい実行時フラグや状態フィールドはない。キー照合は元のmeta-matchのまま。キー順序の制約も加えていない。

## 同時比較

AC電源、同じClang21 -O3、既存のCompact入力。順序を変えた2巡、各5反復、最低0.1秒。7ケース×3版にmedium/manyのGlazeを加え、44プロセス・220反復。ビルドと測定は別。数値は巡ごとのCPU時間中央値の平均。

| ケース | 変更前 µs | handlerのみ µs | 採用版 µs | 変更前比 |
|---|---:|---:|---:|---:|
| medium/many | 280.217 | 262.435 | 240.359 | -14.22% |
| medium/id | 183.988 | 184.822 | 185.416 | +0.78% |
| medium/all | 137.408 | 137.524 | 136.758 | -0.47% |
| large/id | 23581.250 | 24058.967 | 23538.000 | -0.18% |
| large/all | 18046.937 | 17906.875 | 17967.063 | -0.44% |
| wide_first/id | 130.943 | 130.714 | 131.786 | +0.64% |
| small/id | 0.122 | 0.123 | 0.120 | -1.76% |

同じ測定のGlaze medium/manyは138.243 µs。HOSQ/Glazeの実時間平均比は約2.03から1.74になった。微小差の有意性や未測定ケースへの一般化は主張しない。

## 不採用の比較

handler改善版を基準に、既知matcherのinline追加、fallbackのinline追加、SkipImplラッパーのinline追加も比較した。matcher単独の追加は約2%に留まり、すべてをinlineしてもmedium/manyの改善は伸びなかった。これらは採用していない。

最初のMemberInput共有化では入口をlambdaで渡し、medium/allの生成コードが悪化した。その版も不採用。入口をコンパイル時に選ぶ実装に整理し、既存のdecodeメソッドを直接共有する最も小さい版を採用した。

## 検証と限界

- 採用後、C++20/C++26各307テスト、合計614テスト合格。
- 6,282入力×3 whitespace modes×3 padding指定＝56,538通りで基準版と値・エラー・カーソル・トークン・最終状態が一致。
- 2,048レコードの外側・payloadキーを逆順／レコードごとにランダム順にした入力でも、同じ独立digestと全文書処理を確認。
- 測定バイナリに使ったincludeスナップショットと採用後の全includeファイルが一致。
- `git diff --check`合格。
- boolで成否を返す独自値パイプラインやコンテナ値は今回の新しい入口の対象外。Glazeとの残差全体を解消したわけではない。

Git対象外の `bench/comparison/results/matched-input-2026-09-08/` に最終測定、各試作の差分とコンパイルコマンド、採用差分、hash、検証ログを保存。コンパイル依存・スナップショット・バイナリは `/tmp/hosq-dispatch-inline/` と `/tmp/hosq-handler-inline/` に保持している。今回コミット・pushはしていない。
