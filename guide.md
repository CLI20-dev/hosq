@include/ser20/json/reader.hh のテストを tests/json/reader.cc に include/ser20/json/compact_writer.hh のテストを tests/json/compact_writer.cc
  に書きなさい
この test は

特定の固定入力に対して reader が特定の sequence を返す (特に json は toplevel value string array empty array, empty object, がparse可能か必要その後 object で nested object, nest は 5 階層くらいまででいい段階的にするのも良いがここまでくると大きい もの 小さい json の入力の2パターンで 大きい json の構造を複雑にするすだけでよい "foobar": [] のような empty array とか empty object を valueに持つこと それに [ と ] が離れている empty array とかも)

特定の固定入力に対して reader が特定の エラーを返す (基本的に構文エラーです ,, とか :: とか {{ とか連続するパターンは 空白を開ける開けないでそれぞれ 他に {   }a とか object の階層が正しくないとか array separater に : を使っている object の separater に , を使っている scalar に空白が混ざっている "value": 638 472, とか そういうものをチェクせよ、ただ string に new line が含まれているかどうかはこの Reader が **検査しない**、他にも null とか true とか false とかの scalar のエラーもチェック **しない**)

特定の固定入力に対して compact_writer が特定の出力を返す (reader と同様のパターンで、ただし reader と違って出力は空白を含まないことを注意する)

特定の固定入力に対して compact_writer が特定のエラーを返す (とはいっても エラーパターンは 間違った token 列を requestするか、 commit で間違ったサイズを渡すかです。他にも弾くべきエラーがあれば追加せよ)

私が指定しなかったテスト以外にも、追加してかまわないです。
むしろエッジケース漏れを良く確認して、積極的に追加しなさい

迷ったらこのガイドラインを繰り返すこと
バグが reader writer にあると感じる場合は修正する必要がありますが、エラーチェック等は意図的に省いているケースがあります。
そのため 仕様かどうか分からないものは、 ./maybe_bug.md に書いて私に確認すること
