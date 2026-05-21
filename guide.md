include/hosq/decoder.hh

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
      hosq::Entry<
        hosq::Elem<hosq::scan::string | hosq::match::literal<"name"_literal>, hosq::noop_encoder>,
        hosq::Elem<hosq::scan::string | hosq::decode::string_view | hosq::store::once, hosq::noop_encoder>
      > name;
      hosq::Entry<
        hosq::Elem<hosq::scan::string | hosq::match::literal<"id"_literal>, hosq::noop_encoder>,
        hosq::Elem<hosq::scan::literal | hosq::decode::number<int> | hosq::store::once, hosq::noop_encoder>
      > id;
    } json;

    auto json_str = R"({"name": "John", "id": 123})";
    auto result = hosq::decode(json, json_str);

    EXPECT_TRUE(result);
    EXPECT_EQ(json.name.value, "John");
    EXPECT_EQ(json.id.value, 123);
}

```
