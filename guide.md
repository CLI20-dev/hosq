include/ser20/decoder.hh

を作成しました。
これで reader と併せて json を parse することができるはずです。
汎用 dom を作っていないので、完全な json を parse することはできませんが、必要な部分だけを parse することはできます。

test を作ってみなさい
tests/json/decode_test.cc に作成せよ

イメージとしては以下のような test を複数作ります
decode のみを検証するので、encode は noop_encoder を使います

```cpp

TEST_xxxxxxx(xxxx, xxx) {
    struct {
      ser20::Entry<
        ser20::Elem<ser20::scan::string | ser20::match::literal<"name"_literal>, ser20::noop_encoder>,
        ser20::Elem<ser20::scan::string | ser20::decode::string_view | ser20::store::once, ser20::noop_encoder>
      > name;
      ser20::Entry<
        ser20::Elem<ser20::scan::string | ser20::match::literal<"id"_literal>, ser20::noop_encoder>,
        ser20::Elem<ser20::scan::literal | ser20::decode::number<int> | ser20::store::once, ser20::noop_encoder>
      > id;
    } json;

    auto json_str = R"({"name": "John", "id": 123})";
    auto result = ser20::decode(json, json_str);

    EXPECT_TRUE(result);
    EXPECT_EQ(json.name.value, "John");
    EXPECT_EQ(json.id.value, 123);
}

```
