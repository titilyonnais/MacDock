#include "minitest.h"
#include "../src/core/json.h"

TEST_CASE(json_parse_object) {
    auto v = md::json::parse(R"({"a":1.5,"b":[true,null,"xé"],"c":{"d":-2e3}})");
    REQUIRE(v.has_value());
    CHECK_NEAR(v->find("a")->asNumber(0), 1.5, 1e-9);
    CHECK(v->find("b")->asArray()[0].asBool(false));
    CHECK(v->find("b")->asArray()[1].isNull());
    CHECK(v->find("b")->asArray()[2].asString("") == "x\xC3\xA9");
    CHECK_NEAR(v->find("c")->find("d")->asNumber(0), -2000, 1e-9);
    CHECK(v->find("missing") == nullptr);
}

TEST_CASE(json_rejects_garbage) {
    std::string err;
    CHECK(!md::json::parse("{\"a\":", &err).has_value());
    CHECK(!err.empty());
    CHECK(!md::json::parse("{} trailing").has_value());
    CHECK(!md::json::parse("").has_value());
    CHECK(!md::json::parse("[1,]").has_value());
    CHECK(!md::json::parse("\"unterminated").has_value());
}

TEST_CASE(json_roundtrip_preserves_order_and_escapes) {
    md::json::Value o = md::json::Object{};
    o.set("z", 1.0);
    o.set("a", std::string("quote\" back\\ nl\n"));
    auto text = md::json::serialize(o, false);
    CHECK(text == R"({"z":1,"a":"quote\" back\\ nl\n"})");
    CHECK(md::json::parse(text).has_value());
}

TEST_CASE(json_numbers_shortest_roundtrip) {
    // Fichiers de l'utilisateur : « 8.4 » et non « 8.4000000000000004 » (relu à l'identique quand même).
    md::json::Value o = md::json::Object{};
    o.set("a", 8.4);
    o.set("b", 0.1);
    o.set("c", 1.0 / 3);
    o.set("d", 1e-7);
    o.set("e", -2.5e20);
    o.set("f", 64.0);
    const auto text = md::json::serialize(o, false);
    CHECK(text == R"({"a":8.4,"b":0.1,"c":0.3333333333333333,"d":1e-07,"e":-2.5e+20,"f":64})");
    const auto back = md::json::parse(text);
    REQUIRE(back.has_value());
    CHECK(back->find("a")->asNumber(0) == 8.4);
    CHECK(back->find("c")->asNumber(0) == 1.0 / 3);
    CHECK(back->find("e")->asNumber(0) == -2.5e20);
}

TEST_CASE(json_surrogate_pair) {
    auto v = md::json::parse(R"("😀")");
    REQUIRE(v.has_value());
    CHECK(v->asString("") == "\xF0\x9F\x98\x80");
}

TEST_CASE(json_deep_nesting_is_rejected_not_crash) {
    std::string s(100000, '[');
    CHECK(!md::json::parse(s).has_value());
}

TEST_CASE(json_wrong_type_accessors_return_default) {
    auto v = md::json::parse(R"({"s":"x"})");
    REQUIRE(v.has_value());
    CHECK_NEAR(v->find("s")->asNumber(7), 7, 1e-9);
    CHECK(v->find("s")->asArray().empty());
    CHECK(v->find("s")->find("k") == nullptr);
}
