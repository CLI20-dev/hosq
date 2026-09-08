# Handlerの呼び出し境界 — 2026-09-08

現行Handlerは `Handler<Key, Fn>` に具体的なlambda型を格納している。Decoderからの経路は関数ポインターではなく、assemblyも直接bl。lambda本体にはalways_inline指定があるが、Handler::operator()にはなかった。

一時includeコピーだけで、Handler::operator()に既存のHOSQ_ALWAYS_INLINEを付けて測定した。本体はこの実験では変更していない。meta-matchの照合方式・スキーマ・入力・出力・任意順対応は同一。

生成コードではlambda Handlerへの直接blが10箇所から0箇所へ減少。Compactのpayload matcherのスタック領域は208 Bから112 B。4handler用の参照を書き込む8組のstpもなくなった。呼び出し先の型は元から静的に確定しており、NTTP lambda用の新APIはこの変化に不要だった。

AC電源、同じClang21 -O3、別プロセスで順序を変えた2巡・各5反復・最低0.1秒。値は巡ごとのCPU時間中央値の平均。

| ケース | 現行 µs | 試作 µs | 時間変化 |
|---|---:|---:|---:|
| medium/many | 279.503 | 256.524 | -8.22% |
| wide_first/id | 131.324 | 130.617 | -0.54% |
| medium/all | 136.009 | 136.513 | +0.37% |

medium/manyは両巡で短縮した。他の微小差について有意性を主張しない。測定は同じ独立digest照合を通過。6,282入力×9条件の56,538通りで、以前の基準版と取得値・エラー・最終状態が一致した。全テストsuiteはこの一時コピーでは実行していないため、採用検証完了とはしない。

静的lambdaのテンプレート引数化はruntime captureを別引数に分離する設計に使えるが、それ自体はinlineの保証ではない。まず現在の具体型を保ってラッパー境界を消す変更を優先する根拠が得られた。Glazeとの残差全体が解消したという結果ではない。

生結果と差分はGit対象外の `bench/comparison/results/handler-inline-2026-09-08/`。試作バイナリ・include・disassemblyは `/tmp/hosq-handler-inline/` に保存。
