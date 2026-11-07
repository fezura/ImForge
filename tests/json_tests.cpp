#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "imforge/imforge_json.h"

#include <clocale>
#include <cmath>
#include <string>

using imforge::DumpJson;
using imforge::Json;
using imforge::JsonError;
using imforge::ParseJson;

static Json Parse(const std::string& text) {
    Json j;
    JsonError err;
    bool ok = ParseJson(text, j, &err);
    if (!ok) FAIL("parse failed: " << err.message << " at " << err.line << ":" << err.col);
    return j;
}

static JsonError ParseFail(const std::string& text) {
    Json j;
    JsonError err;
    bool ok = ParseJson(text, j, &err);
    REQUIRE_FALSE(ok);
    REQUIRE_FALSE(err.message.empty());
    return err;
}

TEST_CASE("json constructors and predicates") {
    CHECK(Json().IsNull());
    CHECK(Json(true).IsBool());
    CHECK(Json(true).AsBool() == true);
    CHECK(Json(42).IsNumber());
    CHECK(Json(42).AsInt() == 42);
    CHECK(Json(3.5).AsDouble() == 3.5);
    CHECK(Json(7u).AsInt() == 7);
    CHECK(Json(1234567890123LL).AsInt64() == 1234567890123LL);
    CHECK(Json(static_cast<size_t>(9)).AsInt() == 9);
    CHECK(Json(1.5f).AsDouble() == doctest::Approx(1.5));
    CHECK(Json("hi").IsString());
    CHECK(Json("hi").AsString() == "hi");
    CHECK(Json(std::string("yo")).AsString() == "yo");
    CHECK(Json(static_cast<const char*>(nullptr)).AsString().empty());
    CHECK(Json::Array().IsArray());
    CHECK(Json::Object().IsObject());
    CHECK(Json::Array().Size() == 0);
}

TEST_CASE("json accessors with defaults") {
    Json s("text");
    CHECK(s.AsBool(true) == true);
    CHECK(s.AsInt(5) == 5);
    CHECK(s.AsDouble(2.5) == 2.5);
    CHECK(Json(3).AsString("def") == "def");
    CHECK(Json(3).AsString().empty());
    CHECK(Json(1e12).AsInt() == 2147483647);
    CHECK(Json(-1e12).AsInt() == (-2147483647 - 1));
    CHECK(Json(2.9).AsInt() == 2);
}

TEST_CASE("json object operations keep insertion order") {
    Json o = Json::Object();
    o.Set("zeta", 1);
    o.Set("alpha", 2);
    o.Set("mid", "m");
    REQUIRE(o.Size() == 3);
    CHECK(o.obj[0].first == "zeta");
    CHECK(o.obj[1].first == "alpha");
    CHECK(o.obj[2].first == "mid");

    o.Set("alpha", 99);
    REQUIRE(o.Size() == 3);
    CHECK(o["alpha"].AsInt() == 99);
    CHECK(o.obj[1].first == "alpha");

    CHECK(o.Has("zeta"));
    CHECK_FALSE(o.Has("nope"));
    CHECK(o["nope"].IsNull());
    CHECK(o.Find("mid") != nullptr);
    CHECK(o.Find("nope") == nullptr);
    const Json& co = o;
    CHECK(co.Find("mid")->AsString() == "m");

    Json* f = o.Find("zeta");
    REQUIRE(f);
    *f = Json("changed");
    CHECK(o["zeta"].AsString() == "changed");

    o.Erase("zeta");
    CHECK(o.Size() == 2);
    CHECK_FALSE(o.Has("zeta"));
    o.Erase("missing");
    CHECK(o.Size() == 2);

    CHECK(DumpJson(o, 0, false) == "{\"alpha\":99,\"mid\":\"m\"}");
}

TEST_CASE("json Set on non-object converts, Push on non-array converts") {
    Json j(5);
    j.Set("k", 1);
    CHECK(j.IsObject());
    CHECK(j["k"].AsInt() == 1);
    Json a("s");
    a.Push(1).Push(2);
    CHECK(a.IsArray());
    CHECK(a.Size() == 2);
    CHECK(a[1].AsInt() == 2);
    CHECK(a[2].IsNull());
    CHECK(Json(3)[static_cast<size_t>(0)].IsNull());
    CHECK(Json(3)["x"].IsNull());
}

TEST_CASE("json equality") {
    CHECK(Json() == Json());
    CHECK(Json(1) == Json(1.0));
    CHECK(Json(1) != Json(2));
    CHECK(Json(1) != Json("1"));
    CHECK(Json("a") == Json(std::string("a")));
    Json a = Parse("{\"x\":1,\"y\":[1,2,{\"z\":null}]}");
    Json b = Parse("{\"y\":[1,2,{\"z\":null}],\"x\":1}");
    CHECK(a == b);
    Json c = Parse("{\"y\":[1,2,{\"z\":null}],\"x\":2}");
    CHECK(a != c);
    Json d = Parse("{\"x\":1}");
    CHECK(a != d);
    CHECK(Parse("[1,2]") != Parse("[2,1]"));
    CHECK(Parse("[1,2]") == Parse("[1, 2]"));
}

TEST_CASE("json parse scalars") {
    CHECK(Parse("null").IsNull());
    CHECK(Parse("true").AsBool() == true);
    CHECK(Parse("false").AsBool(true) == false);
    CHECK(Parse("0").AsDouble() == 0.0);
    CHECK(Parse("-0").AsDouble() == 0.0);
    CHECK(Parse("123").AsInt() == 123);
    CHECK(Parse("-17").AsInt() == -17);
    CHECK(Parse("1.5").AsDouble() == 1.5);
    CHECK(Parse("1e3").AsDouble() == 1000.0);
    CHECK(Parse("1E+3").AsDouble() == 1000.0);
    CHECK(Parse("2.5e-3").AsDouble() == doctest::Approx(0.0025));
    CHECK(Parse("0.1").AsDouble() == 0.1);
    CHECK(Parse("  \n\t 42 \r\n").AsInt() == 42);
    CHECK(Parse("\"\"").AsString().empty());
    CHECK(Parse("\"abc\"").AsString() == "abc");
    CHECK(Parse("123456789012345678").AsDouble() == 123456789012345678.0);
}

TEST_CASE("json parse string escapes and unicode") {
    CHECK(Parse("\"a\\\"b\"").AsString() == "a\"b");
    CHECK(Parse("\"a\\\\b\"").AsString() == "a\\b");
    CHECK(Parse("\"a\\/b\"").AsString() == "a/b");
    CHECK(Parse("\"\\b\\f\\n\\r\\t\"").AsString() == "\b\f\n\r\t");
    CHECK(Parse("\"\\u0041\"").AsString() == "A");
    CHECK(Parse("\"\\u00e9\"").AsString() == "\xC3\xA9");
    CHECK(Parse("\"\\u20AC\"").AsString() == "\xE2\x82\xAC");
    CHECK(Parse("\"\\uD83D\\uDE00\"").AsString() == "\xF0\x9F\x98\x80");
    CHECK(Parse("\"\\u0000\"").AsString() == std::string(1, '\0'));
    CHECK(Parse("\"caf\xC3\xA9\"").AsString() == "caf\xC3\xA9");
    CHECK(Parse("\"\xF0\x9F\x98\x80\"").AsString() == "\xF0\x9F\x98\x80");
}

TEST_CASE("json parse containers") {
    Json j = Parse("{\"a\": [1, 2.5, \"x\", true, null, {\"b\": {}}], \"c\": {\"d\": []}}");
    REQUIRE(j.IsObject());
    REQUIRE(j["a"].IsArray());
    CHECK(j["a"].Size() == 6);
    CHECK(j["a"][0].AsInt() == 1);
    CHECK(j["a"][1].AsDouble() == 2.5);
    CHECK(j["a"][2].AsString() == "x");
    CHECK(j["a"][3].AsBool() == true);
    CHECK(j["a"][4].IsNull());
    CHECK(j["a"][5]["b"].IsObject());
    CHECK(j["a"][5]["b"].Size() == 0);
    CHECK(j["c"]["d"].IsArray());
    CHECK(j["c"]["d"].Size() == 0);
    CHECK(Parse("[]").Size() == 0);
    CHECK(Parse("{}").Size() == 0);
    CHECK(Parse(" [ ] ").IsArray());
    CHECK(Parse(" { } ").IsObject());
}

TEST_CASE("json parse keeps key order and duplicate keys") {
    Json j = Parse("{\"z\":1,\"a\":2,\"m\":3}");
    REQUIRE(j.Size() == 3);
    CHECK(j.obj[0].first == "z");
    CHECK(j.obj[1].first == "a");
    CHECK(j.obj[2].first == "m");
    Json d = Parse("{\"k\":1,\"k\":2}");
    CHECK(d.Size() == 2);
    CHECK(d["k"].AsInt() == 1);
}

TEST_CASE("json parse BOM and raw buffer") {
    Json j = Parse("\xEF\xBB\xBF{\"a\":1}");
    CHECK(j["a"].AsInt() == 1);
    const char buf[] = "[1,2,3]garbage";
    Json k;
    CHECK(ParseJson(buf, 7, k));
    CHECK(k.Size() == 3);
    CHECK_FALSE(ParseJson(buf, sizeof(buf) - 1, k));
    CHECK(ParseJson(nullptr, 0, k) == false);
}

TEST_CASE("json parse errors with positions") {
    JsonError e = ParseFail("");
    CHECK(e.line == 1);
    CHECK(e.col == 1);

    e = ParseFail("[1,2,]");
    CHECK(e.line == 1);
    CHECK(e.col == 6);

    e = ParseFail("{\"a\":1,}");
    CHECK(e.line == 1);
    CHECK(e.col == 8);

    e = ParseFail("{\n  \"a\": 1,\n  \"b\": tru\n}");
    CHECK(e.line == 3);
    CHECK(e.col == 8);

    e = ParseFail("{\"a\" 1}");
    CHECK(e.col == 6);

    e = ParseFail("[1 2]");
    CHECK(e.col == 4);

    e = ParseFail("\"abc");
    CHECK(e.message == "unterminated string");

    e = ParseFail("\"a\nb\"");
    CHECK(e.message == "control character in string");
    CHECK(e.line == 1);
    CHECK(e.col == 3);

    e = ParseFail("\"\\x\"");
    CHECK(e.message == "invalid escape sequence");

    e = ParseFail("\"\\u12\"");
    e = ParseFail("\"\\uZZZZ\"");
    CHECK(e.message == "invalid hex digit in \\u escape");

    e = ParseFail("\"\\uD83D\"");
    CHECK(e.message == "missing low surrogate");
    e = ParseFail("\"\\uD83D\\u0041\"");
    CHECK(e.message == "invalid low surrogate");
    e = ParseFail("\"\\uDE00\"");
    CHECK(e.message == "unexpected low surrogate");

    e = ParseFail("01");
    CHECK(e.message == "trailing characters after JSON value");
    e = ParseFail("1.");
    e = ParseFail(".5");
    e = ParseFail("-");
    e = ParseFail("1e");
    e = ParseFail("+1");
    e = ParseFail("nul");
    e = ParseFail("True");
    e = ParseFail("{a:1}");
    CHECK(e.message == "expected string key");
    e = ParseFail("[1,2] x");
    CHECK(e.message == "trailing characters after JSON value");
    CHECK(e.col == 7);
    e = ParseFail("{\"a\":1} {}");
    CHECK(e.message == "trailing characters after JSON value");
    e = ParseFail("[");
    e = ParseFail("{");
    e = ParseFail("{\"a\"");
    e = ParseFail("{\"a\":");
    e = ParseFail("]");
    e = ParseFail("}");
    e = ParseFail("\x01");

    Json j;
    CHECK_FALSE(ParseJson(std::string("[1,"), j, nullptr));
}

TEST_CASE("json nesting depth limit") {
    std::string deep256(256, '[');
    deep256 += std::string(256, ']');
    Json ok;
    JsonError err;
    CHECK(ParseJson(deep256, ok, &err));
    std::string deep257(257, '[');
    deep257 += std::string(257, ']');
    CHECK_FALSE(ParseJson(deep257, ok, &err));
    CHECK(err.message == "nesting too deep");
    CHECK(err.col == 257);
    std::string mixed;
    for (int i = 0; i < 300; ++i) mixed += "{\"a\":";
    mixed += "1";
    for (int i = 0; i < 300; ++i) mixed += "}";
    CHECK_FALSE(ParseJson(mixed, ok, &err));
}

TEST_CASE("json dump scalars") {
    CHECK(DumpJson(Json()) == "null");
    CHECK(DumpJson(Json(true)) == "true");
    CHECK(DumpJson(Json(false)) == "false");
    CHECK(DumpJson(Json(0)) == "0");
    CHECK(DumpJson(Json(-0.0)) == "0");
    CHECK(DumpJson(Json(42)) == "42");
    CHECK(DumpJson(Json(-7)) == "-7");
    CHECK(DumpJson(Json(1.0)) == "1");
    CHECK(DumpJson(Json(100.0)) == "100");
    CHECK(DumpJson(Json(1e14)) == "100000000000000");
    CHECK(DumpJson(Json(1e15)) == "1e+15");
    CHECK(DumpJson(Json(123456789012345.0)) == "123456789012345");
    CHECK(DumpJson(Json(0.5)) == "0.5");
    CHECK(DumpJson(Json(0.1)) == "0.1");
    CHECK(DumpJson(Json(1.5)) == "1.5");
    CHECK(DumpJson(Json(-2.25)) == "-2.25");
    CHECK(DumpJson(Json(3.14159)) == "3.14159");
    CHECK(DumpJson(Json(1.0 / 3.0)) == "0.3333333333333333");
    CHECK(DumpJson(Json(0.1 + 0.2)) == "0.30000000000000004");
    CHECK(DumpJson(Json(1e-7)) == "1e-07");
    CHECK(DumpJson(Json(2.5e20)) == "2.5e+20");
    CHECK(DumpJson(Json(static_cast<double>(0.1f))) == "0.10000000149011612");
    CHECK(DumpJson(Json(std::nan(""))) == "null");
    CHECK(DumpJson(Json(HUGE_VAL)) == "null");
    CHECK(DumpJson(Json("")) == "\"\"");
    CHECK(DumpJson(Json("plain")) == "\"plain\"");
}

TEST_CASE("json dump string escaping") {
    CHECK(DumpJson(Json("a\"b")) == "\"a\\\"b\"");
    CHECK(DumpJson(Json("a\\b")) == "\"a\\\\b\"");
    CHECK(DumpJson(Json("a/b")) == "\"a/b\"");
    CHECK(DumpJson(Json("\b\f\n\r\t")) == "\"\\b\\f\\n\\r\\t\"");
    CHECK(DumpJson(Json(std::string("\x01\x1f"))) == "\"\\u0001\\u001f\"");
    CHECK(DumpJson(Json(std::string(1, '\0'))) == "\"\\u0000\"");
    CHECK(DumpJson(Json("caf\xC3\xA9")) == "\"caf\xC3\xA9\"");
    CHECK(DumpJson(Json("\xF0\x9F\x98\x80")) == "\"\xF0\x9F\x98\x80\"");
    CHECK(DumpJson(Json("\x7f")) == "\"\x7f\"");
}

TEST_CASE("json dump containers pretty and compact") {
    Json o = Json::Object();
    o.Set("b", Json::Array().Push(1).Push(Json::Object().Set("y", true).Set("x", Json())));
    o.Set("a", "s");
    o.Set("e", Json::Object());
    o.Set("d", Json::Array());

    CHECK(DumpJson(o, 2, true) ==
          "{\n"
          "  \"a\": \"s\",\n"
          "  \"b\": [\n"
          "    1,\n"
          "    {\n"
          "      \"x\": null,\n"
          "      \"y\": true\n"
          "    }\n"
          "  ],\n"
          "  \"d\": [],\n"
          "  \"e\": {}\n"
          "}");
    CHECK(DumpJson(o, 2, false) ==
          "{\n"
          "  \"b\": [\n"
          "    1,\n"
          "    {\n"
          "      \"y\": true,\n"
          "      \"x\": null\n"
          "    }\n"
          "  ],\n"
          "  \"a\": \"s\",\n"
          "  \"e\": {},\n"
          "  \"d\": []\n"
          "}");
    CHECK(DumpJson(o, 0, true) == "{\"a\":\"s\",\"b\":[1,{\"x\":null,\"y\":true}],\"d\":[],\"e\":{}}");
    CHECK(DumpJson(o, 0, false) == "{\"b\":[1,{\"y\":true,\"x\":null}],\"a\":\"s\",\"e\":{},\"d\":[]}");
    CHECK(DumpJson(o, 4, true).find("\n    \"a\": \"s\"") != std::string::npos);
    CHECK(DumpJson(o, -3, true) == DumpJson(o, 0, true));
    CHECK(DumpJson(o) == DumpJson(o, 2, true));
}

TEST_CASE("json sorted dump is recursive and byte-wise") {
    Json j = Parse("{\"b\":{\"z\":1,\"B\":2,\"a\":3},\"A\":[{\"y\":0,\"x\":0}],\"a\":1,\"_\":2}");
    CHECK(DumpJson(j, 0, true) == "{\"A\":[{\"x\":0,\"y\":0}],\"_\":2,\"a\":1,\"b\":{\"B\":2,\"a\":3,\"z\":1}}");
    CHECK(j.obj[0].first == "b");
}

TEST_CASE("json round trips") {
    const char* docs[] = {
        "null",
        "true",
        "-12.5",
        "\"s\\n\\\"q\\\"\"",
        "[]",
        "{}",
        "[1,[2,[3,[4]]]]",
        "{\"a\":{\"b\":{\"c\":[1,2,3,{\"d\":\"e\"}]}},\"f\":false,\"g\":null,\"h\":0.125}",
        "{\"schemaVersion\":1,\"kind\":\"panel\",\"name\":\"Main\",\"root\":{\"id\":\"window_1\",\"type\":\"Window\",\"props\":{\"size\":{\"x\":400,\"y\":300},\"flags\":[\"NoResize\"]},\"children\":[]}}",
    };
    for (const char* d : docs) {
        Json j = Parse(d);
        std::string compact = DumpJson(j, 0, false);
        CHECK(compact == d);
        std::string pretty = DumpJson(j, 2, true);
        Json again = Parse(pretty);
        CHECK(again == j);
        CHECK(DumpJson(again, 2, true) == pretty);
        Json third = Parse(DumpJson(again, 0, true));
        CHECK(third == j);
    }
}

TEST_CASE("json number round trip precision") {
    double values[] = {0.1, 0.2, 0.3, 1.0 / 3.0, 2.0 / 3.0, 1e-300, 1e300, 123456.789, 5e-324,
                       1.7976931348623157e308, 3.141592653589793, 2.718281828459045, 1e15 + 0.5,
                       -9007199254740993.0, 4503599627370497.0, 0.000001, 1234567.1234567};
    for (double v : values) {
        std::string s = DumpJson(Json(v));
        Json back = Parse(s);
        CHECK(back.AsDouble() == v);
    }
}

TEST_CASE("json big document") {
    Json root = Json::Object();
    Json items = Json::Array();
    for (int i = 0; i < 5000; ++i) {
        Json it = Json::Object();
        it.Set("id", i);
        it.Set("name", "item_" + std::to_string(i));
        it.Set("value", i * 0.25);
        it.Set("tags", Json::Array().Push("a").Push("b").Push(i % 2 == 0));
        Json pos = Json::Object();
        pos.Set("x", static_cast<double>(i));
        pos.Set("y", -static_cast<double>(i) / 3.0);
        it.Set("pos", pos);
        items.Push(it);
    }
    root.Set("items", items);
    root.Set("count", 5000);
    std::string text = DumpJson(root, 2, true);
    CHECK(text.size() > 100000);
    Json parsed;
    JsonError err;
    REQUIRE(ParseJson(text, parsed, &err));
    CHECK(parsed == root);
    CHECK(parsed["items"].Size() == 5000);
    CHECK(parsed["items"][4999]["name"].AsString() == "item_4999");
    CHECK(parsed["items"][1234]["pos"]["y"].AsDouble() == -1234.0 / 3.0);
    CHECK(DumpJson(parsed, 2, true) == text);
    std::string compact = DumpJson(root, 0, true);
    CHECK(compact.find('\n') == std::string::npos);
    Json parsed2;
    REQUIRE(ParseJson(compact, parsed2, &err));
    CHECK(parsed2 == root);
}

TEST_CASE("json long numbers and long strings") {
    std::string longnum = "0." + std::string(100, '1');
    Json j = Parse(longnum);
    CHECK(j.AsDouble() == doctest::Approx(0.1111111111111111));
    std::string longstr(100000, 'x');
    Json s = Parse("\"" + longstr + "\"");
    CHECK(s.AsString() == longstr);
    CHECK(DumpJson(s, 0) == "\"" + longstr + "\"");
}

TEST_CASE("json error struct is reset on success") {
    JsonError err;
    err.message = "stale";
    err.line = 9;
    Json j;
    CHECK(ParseJson(std::string("1"), j, &err));
    CHECK(err.message.empty());
    CHECK(err.line == 0);
    CHECK(err.col == 0);
}

TEST_CASE("json numbers are locale independent") {
    const char* names[] = {"de_DE.UTF-8", "de_DE.utf8", "de_DE", "German_Germany.1252", "fr_FR.UTF-8",
                           "French_France.1252"};
    bool comma = false;
    for (const char* n : names) {
        if (std::setlocale(LC_NUMERIC, n) && std::localeconv()->decimal_point[0] == ',') {
            comma = true;
            break;
        }
    }
    if (comma) {
        CHECK(DumpJson(Json(0.1)) == "0.1");
        CHECK(DumpJson(Json(2.5)) == "2.5");
        CHECK(Parse("1.5").AsDouble() == 1.5);
        CHECK(Parse("[0.25, 1e-3]")[1].AsDouble() == 1e-3);
        std::string third = DumpJson(Json(1.0 / 3.0));
        CHECK(third.find(',') == std::string::npos);
        CHECK(Parse(third).AsDouble() == 1.0 / 3.0);
    }
    std::setlocale(LC_NUMERIC, "C");
    CHECK(DumpJson(Json(0.1)) == "0.1");
    CHECK(DumpJson(Json(1.0 / 3.0)) == "0.3333333333333333");
}
