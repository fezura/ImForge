#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "imforge/imforge_protocol.h"

#include <string>
#include <vector>

using namespace imforge;

namespace {

Json RoundTrip(const Json& m, const char* expected_type) {
    std::string wire = Frame(m);
    CHECK(wire.find('\n') == std::string::npos);
    CHECK(wire.front() == '{');
    std::vector<unsigned char> payload(wire.begin(), wire.end());
    Json back;
    REQUIRE(Unframe(payload, back));
    CHECK(std::string(MsgType(back)) == expected_type);
    CHECK(IsMsg(back, expected_type));
    CHECK(back == m);
    CHECK(Frame(back) == wire);
    return back;
}

}  // namespace

TEST_CASE("Frame/Unframe basics") {
    Json bad;
    std::vector<unsigned char> garbage = {'{', 'x'};
    CHECK_FALSE(Unframe(garbage, bad));
    std::string arr = "[1,2]";
    CHECK_FALSE(Unframe(std::vector<unsigned char>(arr.begin(), arr.end()), bad));
    std::string no_t = "{\"a\":1}";
    CHECK_FALSE(Unframe(std::vector<unsigned char>(no_t.begin(), no_t.end()), bad));
    std::string ok = "{\"t\":\"ping\"}";
    Json j;
    REQUIRE(Unframe(ok.data(), ok.size(), j));
    CHECK(std::string(MsgType(j)) == "ping");
    CHECK(std::string(MsgType(Json())) == "");
    CHECK(std::string(MsgType(Json::Object())) == "");
    CHECK(Frame(MsgPing()) == "{\"t\":\"ping\"}");
    CHECK(Frame(MsgPong()) == "{\"t\":\"pong\"}");
    CHECK(Frame(MsgSelect("Main", "b1")) == "{\"id\":\"b1\",\"panel\":\"Main\",\"t\":\"select\"}");
}

TEST_CASE("hello") {
    Json m = MsgHello("CheatMenu", 4242, 19190, "dx11", "windows", {"imb.knob", "imb.gauge"}, {"logo", "atlas"});
    Json back = RoundTrip(m, msg::kHello);
    HelloMsg h;
    REQUIRE(ParseHello(back, h));
    CHECK(h.app == "CheatMenu");
    CHECK(h.pid == 4242);
    CHECK(h.imgui_version == 19190);
    CHECK(h.backend == "dx11");
    CHECK(h.platform == "windows");
    REQUIRE(h.widgets.size() == 2);
    CHECK(h.widgets[1] == "imb.gauge");
    REQUIRE(h.textures.size() == 2);
    CHECK(h.textures[0] == "logo");
    CHECK_FALSE(ParseHello(MsgPing(), h));
    Json empty = MsgHello(nullptr, 0, 0, nullptr, nullptr, {}, {});
    REQUIRE(ParseHello(empty, h));
    CHECK(h.app.empty());
    CHECK(h.widgets.empty());
    CHECK_FALSE(h.pairing);
    CHECK(h.nonce_a.empty());
}

TEST_CASE("pairing messages") {
    const std::string na(32, 'a'), nb = "0123456789abcdef0123456789abcdef", sid(32, 'f'), mac(64, '9');
    HelloMsg h;
    Json hello = RoundTrip(MsgPairingHello(na), msg::kHello);
    REQUIRE(ParseHello(hello, h));
    CHECK(h.pairing);
    CHECK(h.nonce_a == na);
    CHECK(h.version == kProtocolVersion);
    CHECK(h.app.empty());
    CHECK(h.pid == 0);
    CHECK(h.widgets.empty());
    CHECK(Frame(hello) == "{\"nonce_a\":\"" + na + "\",\"pairing\":true,\"t\":\"hello\",\"v\":2}");
    Json upper = MsgPairingHello(std::string(32, 'A'));
    REQUIRE(ParseHello(upper, h));
    CHECK(h.pairing);
    CHECK(h.nonce_a.empty());
    Json shortn = MsgPairingHello("abcd");
    REQUIRE(ParseHello(shortn, h));
    CHECK(h.nonce_a.empty());
    Json legacy_no_app = Json::Object();
    legacy_no_app.Set("t", "hello");
    CHECK_FALSE(ParseHello(legacy_no_app, h));

    PairChallengeMsg c;
    REQUIRE(ParsePairChallenge(RoundTrip(MsgPairChallenge(nb, sid, mac), msg::kPairChallenge), c));
    CHECK(c.nonce_b == nb);
    CHECK(c.session_id == sid);
    CHECK(c.mac_b == mac);
    CHECK_FALSE(ParsePairChallenge(MsgPairChallenge(nb, sid, mac.substr(1)), c));
    CHECK_FALSE(ParsePairChallenge(MsgPairChallenge(nb + "0", sid, mac), c));
    CHECK_FALSE(ParsePairChallenge(MsgPairChallenge(nb, "g" + sid.substr(1), mac), c));
    CHECK_FALSE(ParsePairChallenge(MsgPairResponse(sid, mac), c));

    PairResponseMsg r;
    REQUIRE(ParsePairResponse(RoundTrip(MsgPairResponse(sid, mac), msg::kPairResponse), r));
    CHECK(r.session_id == sid);
    CHECK(r.mac_a == mac);
    CHECK_FALSE(ParsePairResponse(MsgPairResponse(sid, std::string(64, 'Z')), r));

    std::string got;
    REQUIRE(ParsePairOk(RoundTrip(MsgPairOk(sid), msg::kPairOk), got));
    CHECK(got == sid);
    CHECK_FALSE(ParsePairOk(MsgPairOk("x"), got));

    PairFailMsg f;
    REQUIRE(ParsePairFail(RoundTrip(MsgPairFail(3, "wrong pairing code"), msg::kPairFail), f));
    CHECK(f.attempts_left == 3);
    CHECK(f.reason == "wrong pairing code");
    REQUIRE(ParsePairFail(MsgPairFail(-4, nullptr), f));
    CHECK(f.attempts_left == 0);
    Json evil = MsgPairFail(2, "x");
    evil.Set("reason", std::string(5000, 'r') + "[2J");
    evil.Set("attempts_left", 1 << 30);
    REQUIRE(ParsePairFail(evil, f));
    CHECK(f.reason.size() <= kProtocolMaxName);
    CHECK(f.attempts_left == 1000);
}

TEST_CASE("blocks") {
    std::vector<BlockHeight> blocks = {{"hdr", "a1b2c3d4"}, {"body", ""}, {"foot", "ffffffff"}};
    Json back = RoundTrip(MsgBlocks("Main", blocks), msg::kBlocks);
    BlocksMsg b;
    REQUIRE(ParseBlocks(back, b));
    CHECK(b.panel == "Main");
    REQUIRE(b.blocks.size() == 3);
    CHECK(b.blocks[0].id == "hdr");
    CHECK(b.blocks[0].h == "a1b2c3d4");
    CHECK(b.blocks[1].h.empty());
    CHECK(b.blocks[2].id == "foot");
    CHECK(b.blocks[2].h == "ffffffff");
    CHECK_FALSE(ParseBlocks(MsgSelect("Main", "x"), b));
    Json broken = MsgBlocks("Main", {});
    broken.Set("blocks", Json(3));
    CHECK_FALSE(ParseBlocks(broken, b));
}

TEST_CASE("select and hover") {
    TargetMsg t;
    Json s = RoundTrip(MsgSelect("Main", "btn_1"), msg::kSelect);
    REQUIRE(ParseSelect(s, t));
    CHECK(t.panel == "Main");
    CHECK(t.id == "btn_1");
    CHECK_FALSE(ParseHover(s, t));
    Json h = RoundTrip(MsgHover("Side", "slider"), msg::kHover);
    REQUIRE(ParseHover(h, t));
    CHECK(t.panel == "Side");
    CHECK(t.id == "slider");
    CHECK_FALSE(ParseSelect(h, t));
    Json none = RoundTrip(MsgSelect("Main", ""), msg::kSelect);
    REQUIRE(ParseSelect(none, t));
    CHECK(t.id.empty());
}

TEST_CASE("log and stats") {
    LogMsg l;
    Json lm = RoundTrip(MsgLog(2, "quote \" and \\ and \n newline \x01"), msg::kLog);
    REQUIRE(ParseLog(lm, l));
    CHECK(l.level == 2);
    CHECK(l.msg == "quote \" and \\ and \n newline \x01");
    CHECK_FALSE(ParseLog(MsgPong(), l));

    StatsMsg s;
    Json sm = RoundTrip(MsgStats(59.94f, {"Main", "Side"}), msg::kStats);
    REQUIRE(ParseStats(sm, s));
    CHECK(s.fps == doctest::Approx(59.94f));
    REQUIRE(s.panels.size() == 2);
    CHECK(s.panels[1] == "Side");
    CHECK_FALSE(ParseStats(lm, s));
}

TEST_CASE("ping and pong") {
    RoundTrip(MsgPing(), msg::kPing);
    RoundTrip(MsgPong(), msg::kPong);
    CHECK(MsgPing().Size() == 1);
    CHECK_FALSE(IsMsg(MsgPing(), msg::kPong));
}

TEST_CASE("project, panel and theme") {
    Json project = Json::Object();
    project.Set("schemaVersion", 1);
    project.Set("kind", "project");
    project.Set("name", "Demo");
    Json themes = Json::Array();
    Json th = Json::Object();
    th.Set("name", "Dark");
    th.Set("alpha", 0.5);
    themes.Push(th);
    project.Set("themes", themes);

    Json out;
    Json pm = RoundTrip(MsgProject(project), msg::kProject);
    REQUIRE(ParseProject(pm, out));
    CHECK(out == project);
    CHECK(out["themes"][0]["name"].AsString() == "Dark");
    CHECK_FALSE(ParsePanel(pm, out));

    Json panel = Json::Object();
    panel.Set("kind", "panel");
    panel.Set("name", "Main");
    Json nodes = Json::Array();
    Json n = Json::Object();
    n.Set("id", "b1");
    n.Set("type", "Button");
    nodes.Push(n);
    panel.Set("nodes", nodes);
    Json pn = RoundTrip(MsgPanel(panel), msg::kPanel);
    REQUIRE(ParsePanel(pn, out));
    CHECK(out == panel);
    CHECK(out["nodes"][0]["id"].AsString() == "b1");
    CHECK_FALSE(ParseProject(pn, out));

    Json theme = Json::Object();
    theme.Set("kind", "theme");
    theme.Set("name", "Dark");
    Json tm = RoundTrip(MsgTheme(theme), msg::kTheme);
    REQUIRE(ParseTheme(tm, out));
    CHECK(out == theme);
    CHECK_FALSE(ParseTheme(pn, out));
    Json not_object = MsgTheme(Json(5));
    CHECK_FALSE(ParseTheme(not_object, out));
}

TEST_CASE("components and widgets") {
    std::vector<Json> comps;
    Json c1 = Json::Object();
    c1.Set("name", "Card");
    Json c2 = Json::Object();
    c2.Set("name", "Row");
    comps.push_back(c1);
    comps.push_back(c2);
    std::vector<Json> out;
    Json cm = RoundTrip(MsgComponents(comps), msg::kComponents);
    REQUIRE(ParseComponents(cm, out));
    REQUIRE(out.size() == 2);
    CHECK(out[1]["name"].AsString() == "Row");
    CHECK_FALSE(ParseWidgets(cm, out));

    std::vector<Json> manifests;
    Json w = Json::Object();
    w.Set("type", "imb.knob");
    w.Set("version", 2);
    manifests.push_back(w);
    Json wm = RoundTrip(MsgWidgets(manifests), msg::kWidgets);
    REQUIRE(ParseWidgets(wm, out));
    REQUIRE(out.size() == 1);
    CHECK(out[0]["type"].AsString() == "imb.knob");
    CHECK(out[0]["version"].AsInt() == 2);
    CHECK_FALSE(ParseComponents(wm, out));

    Json em = RoundTrip(MsgComponents({}), msg::kComponents);
    REQUIRE(ParseComponents(em, out));
    CHECK(out.empty());
}

TEST_CASE("remove_panel and mode") {
    std::string name;
    Json rm = RoundTrip(MsgRemovePanel("Old"), msg::kRemovePanel);
    REQUIRE(ParseRemovePanel(rm, name));
    CHECK(name == "Old");
    CHECK_FALSE(ParseRemovePanel(MsgPing(), name));

    ModeMsg mode;
    Json mm = RoundTrip(MsgMode(true, "b1", "Main"), msg::kMode);
    REQUIRE(ParseMode(mm, mode));
    CHECK(mode.picker);
    CHECK(mode.selected == "b1");
    CHECK(mode.panel == "Main");
    Json off = RoundTrip(MsgMode(false, nullptr, nullptr), msg::kMode);
    REQUIRE(ParseMode(off, mode));
    CHECK_FALSE(mode.picker);
    CHECK(mode.selected.empty());
    CHECK(mode.panel.empty());
    CHECK_FALSE(ParseMode(rm, mode));
}

TEST_CASE("client_info carries the app metadata that used to travel in hello") {
    Json m = MsgClientInfo("CheatMenu", 4242, 19190, "dx11", "windows", {"imb.knob"}, {"logo"});
    Json back = RoundTrip(m, msg::kClientInfo);
    ClientInfoMsg ci;
    REQUIRE(ParseClientInfo(back, ci));
    CHECK(ci.app == "CheatMenu");
    CHECK(ci.pid == 4242);
    CHECK(ci.imgui_version == 19190);
    CHECK(ci.backend == "dx11");
    CHECK(ci.platform == "windows");
    REQUIRE(ci.widgets.size() == 1);
    CHECK(ci.widgets[0] == "imb.knob");
    REQUIRE(ci.textures.size() == 1);
    CHECK(ci.textures[0] == "logo");
    CHECK_FALSE(ParseClientInfo(MsgHello("x", 1, 2, "b", "p", {}, {}), ci));
    Json no_app = MsgClientInfo("x", 1, 2, "b", "p", {}, {});
    no_app.Set("app", 5);
    CHECK_FALSE(ParseClientInfo(no_app, ci));
    Json huge = MsgClientInfo(std::string(5000, 'n').c_str(), 1, 2, "b", "p", {}, {});
    REQUIRE(ParseClientInfo(huge, ci));
    CHECK(ci.app.size() == kProtocolMaxName);
}

TEST_CASE("type dispatch over every builder") {
    struct Case {
        Json m;
        const char* type;
    };
    std::vector<Case> cases = {
        {MsgHello("a", 1, 2, "b", "p", {}, {}), msg::kHello},
        {MsgPairingHello(std::string(32, 'a')), msg::kHello},
        {MsgClientInfo("a", 1, 2, "b", "p", {}, {}), msg::kClientInfo},
        {MsgBlocks("p", {}), msg::kBlocks},
        {MsgSelect("p", "i"), msg::kSelect},
        {MsgHover("p", "i"), msg::kHover},
        {MsgLog(0, "m"), msg::kLog},
        {MsgStats(1.0f, {}), msg::kStats},
        {MsgPong(), msg::kPong},
        {MsgProject(Json::Object()), msg::kProject},
        {MsgPanel(Json::Object()), msg::kPanel},
        {MsgComponents({}), msg::kComponents},
        {MsgWidgets({}), msg::kWidgets},
        {MsgRemovePanel("p"), msg::kRemovePanel},
        {MsgTheme(Json::Object()), msg::kTheme},
        {MsgMode(false, "", ""), msg::kMode},
        {MsgPing(), msg::kPing},
        {MsgPairChallenge(std::string(32, 'a'), std::string(32, 'b'), std::string(64, 'c')), msg::kPairChallenge},
        {MsgPairResponse(std::string(32, 'b'), std::string(64, 'd')), msg::kPairResponse},
        {MsgPairOk(std::string(32, 'b')), msg::kPairOk},
        {MsgPairFail(1, "r"), msg::kPairFail},
    };
    for (const Case& c : cases) {
        Json back = RoundTrip(c.m, c.type);
        for (const Case& other : cases) {
            CHECK(IsMsg(back, other.type) == (std::string(other.type) == c.type));
        }
    }
}
