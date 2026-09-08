# FastReader処理フローの中間メモ

2026-09-08。`matched-member-input-study.md` の採用版時点。ここに挙げる次の改善は未実装・未計測。

## 現在の任意順メンバー処理

Medium/5 fieldsのpayloadにある `"count":123` を例にする。Autoは文書入口でPretty/Compactの具体型を選ぶ。配列collectが各レコードを組み立て、外側のid/payloadを照合し、payloadのメンバーループへ入る。

1. Decoderが `next_member_or_end` を要求。FastReaderは空白・カンマ・開始引用符を処理し、キー先頭にposを置く。currentは文字列・長さ未確定、expectはコロン待ち。
2. meta-matchが先頭文字から候補を絞り、`count`と閉じ引用符を照合する。一致後はキートークンの長さを5に確定し、posを閉じ引用符の直後へ移す。
3. 型で決まるhandlerが `EntryInput::decode_matched` を試す。FastReaderはコロンとscalar開始を確認し、posを数値先頭へ移す。この時点でcurrentはまだキートークンのまま。空のMemberInputタグがpending-member状態を表す。
4. `scan::scalar` は入力を走査せずscan済みの空タグへ変える。`decode::number` の特殊化がposから直接変換する。成功後、確定済み値トークン・値末尾pos・カンマ待ちexpect・OKエラー状態を書き戻し、`store::assign`へ数値を渡す。
5. handler→matcher→Decoderのメンバーループへ処理結果を返す。次の `next_member_or_end` が状態・未解決トークンの有無・空白・区切りを確認し、次の未確定キートークンを作る。

これは論理的な処理段階。すべての段階が実際の関数呼び出しやメモリ書き込みとして残るという意味ではない。Handler::operator()のinline指定でhandler用参照の保持と呼び出しの一部は既に消えている。

## 未知項目

- `none:null`: matcher不一致後、標準skipの組であればFastReader内でキーとscalarをskipし、次のキー照合へ直接進む。
- `values:[1,2,3,4]`: scalar用高速経路では扱えず、Decoderのfallbackへ戻る。キーscan→配列開始トークン→汎用コンテナskip→メンバーループという経路。
- 最後の未知メンバー、不確かな入力、独自fallbackは従来経路を使う。
- boolで成否を返すTry値パイプラインのcheckpoint/rollback経路は、今回のdecode_matched高速入口の対象外。

## 次に検討すること

現在のMemberInputは値を読み始めるまでの中間トークンを省く。一方、キー一致直後と値読込直後では通常のトークン状態へ戻している。

1. 既知キーの位置・長さなどを、その場でキートークンへ確定する必要があるか。一般経路へ戻る時まで具体化を遅らせられるか。
2. 値の格納後に完全な値トークンを作り、次の遷移ですぐ確認する往復を減らせるか。状態を観測する利用者stepには従来の状態を提示する。
3. 数値終端で得た区切り情報を次の遷移で再利用できるか。検証を省略するのではなく、確認済み情報を使う。
4. 未知コンテナへのfallbackでも、照合・キー終端・値開始について既に得た情報を活かせるか。キー終端を単にキャッシュするだけの過去の試作では改善しなかった。

順序制約、perfect hash、新しい実行時状態を導入することが前提ではない。既存InputViewとReader特殊化の範囲で、状態が必要になる境界まで具体化を遅らせることを優先して検討する。独自step、fallback、Tryの巻き戻し、エラー位置・最終状態の契約は維持する。

ソース上の処理を削るだけでなく、assemblyで消えた命令を確認し、任意順の同一スキーマ・入力・取得結果で比較する。順序付きHOSQの診断を任意順処理の改善率として使わない。

## 現在の検証地点

採用後のC++20/C++26各307テスト合格。56,538条件の新旧比較、逆順・ランダム順入力のdigest照合も一致。Medium/5 fieldsは同時比較で280.2→240.4 µs。これは現在までの採用変更の結果であり、上の次の改善候補の見積もりではない。

測定詳細: [matched-member-input-study.md](matched-member-input-study.md)。生のbenchmark結果はGit対象外の `bench/comparison/results/` に置く。
