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
