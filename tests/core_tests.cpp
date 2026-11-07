#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include <clocale>
#include <cmath>

#include "core_test_support.h"

using namespace imforge;
using namespace imforge::core_test;

TEST_CASE("SizeSpec parses every form") {
    SizeSpec s;
    CHECK(SizeSpec::Parse(Json(120), s));
    CHECK(s.mode == SizeSpec::Mode::Px);
    CHECK(s.value == doctest::Approx(120));
    CHECK(SizeSpec::Parse(Json("auto"), s));
    CHECK(s.mode == SizeSpec::Mode::Auto);
    CHECK(SizeSpec::Parse(Json("fill"), s));
    CHECK(s.mode == SizeSpec::Mode::Fill);
    CHECK(SizeSpec::Parse(Json("50%"), s));
    CHECK(s.mode == SizeSpec::Mode::Percent);
    CHECK(s.value == doctest::Approx(50));
    CHECK(SizeSpec::Parse(Json("fill-20"), s));
    CHECK(s.mode == SizeSpec::Mode::FillMinus);
    CHECK(s.value == doctest::Approx(20));
    CHECK(SizeSpec::Parse(Json("12.5"), s));
    CHECK(s.mode == SizeSpec::Mode::Px);
    CHECK(s.value == doctest::Approx(12.5));
    CHECK(SizeSpec::Parse(Json("33.3%"), s));
    CHECK(s.value == doctest::Approx(33.3));

    CHECK_FALSE(SizeSpec::Parse(Json("wide"), s));
    CHECK_FALSE(SizeSpec::Parse(Json("fill-"), s));
    CHECK_FALSE(SizeSpec::Parse(Json("fill-x"), s));
    CHECK_FALSE(SizeSpec::Parse(Json("%"), s));
    CHECK_FALSE(SizeSpec::Parse(Json("a%"), s));
    CHECK_FALSE(SizeSpec::Parse(Json(true), s));
    CHECK_FALSE(SizeSpec::Parse(Json(), s));
    CHECK_FALSE(SizeSpec::Parse(Json::Object(), s));
}

TEST_CASE("SizeSpec ToJson and ToString") {
    SizeSpec px;
    px.mode = SizeSpec::Mode::Px;
    px.value = 120;
    CHECK(px.ToJson().IsNumber());
    CHECK(DumpJson(px.ToJson(), 0) == "120");
    CHECK(px.ToString() == "120");
    px.value = 12.5f;
    CHECK(DumpJson(px.ToJson(), 0) == "12.5");
    px.value = 0.1f;
    CHECK(DumpJson(px.ToJson(), 0) == "0.1");

    SizeSpec a;
    CHECK(a.ToJson().AsString() == "auto");
    CHECK(a.ToString() == "auto");
    SizeSpec f;
    f.mode = SizeSpec::Mode::Fill;
    CHECK(f.ToJson().AsString() == "fill");
    SizeSpec pc;
    pc.mode = SizeSpec::Mode::Percent;
    pc.value = 50;
    CHECK(pc.ToJson().AsString() == "50%");
    CHECK(pc.ToString() == "50%");
    SizeSpec fm;
    fm.mode = SizeSpec::Mode::FillMinus;
    fm.value = 20;
    CHECK(fm.ToJson().AsString() == "fill-20");
    CHECK(fm.ToString() == "fill-20");

    const char* forms[] = {"auto", "fill", "50%", "fill-20"};
    for (const char* t : forms) {
        SizeSpec s;
        REQUIRE(SizeSpec::Parse(Json(t), s));
        CHECK(s.ToJson().AsString() == t);
    }
}

TEST_CASE("ParseColor accepts #rrggbb and #rrggbbaa") {
    bool ok = false;
    Vec4 c = ParseColor("#2ecc71ff", &ok);
    CHECK(ok);
    CHECK(c.x == doctest::Approx(0x2e / 255.0f));
    CHECK(c.y == doctest::Approx(0xcc / 255.0f));
    CHECK(c.z == doctest::Approx(0x71 / 255.0f));
    CHECK(c.w == doctest::Approx(1.0f));

    c = ParseColor("#2ecc71", &ok);
    CHECK(ok);
    CHECK(c.w == doctest::Approx(1.0f));
    CHECK(c.y == doctest::Approx(0xcc / 255.0f));

    c = ParseColor("#00000080", &ok);
    CHECK(ok);
    CHECK(c.w == doctest::Approx(128 / 255.0f));

    c = ParseColor("#2ECC71FF", &ok);
    CHECK(ok);
    CHECK(c.x == doctest::Approx(0x2e / 255.0f));

    const char* bad[] = {"2ecc71ff", "#2ecc7", "#2ecc71f", "#2ecc71ff0", "#gg0000ff", "", "#", "#2ecc71fg"};
    for (const char* b : bad) {
        ok = true;
        Vec4 z = ParseColor(b, &ok);
        CHECK_MESSAGE(!ok, b);
        CHECK(z.x == 0);
        CHECK(z.w == 0);
    }
    ParseColor("#ffffff");
}

TEST_CASE("ColorToHex emits 8 lowercase hex digits") {
    CHECK(ColorToHex(Vec4{1, 1, 1, 1}) == "#ffffffff");
    CHECK(ColorToHex(Vec4{0, 0, 0, 0}) == "#00000000");
    CHECK(ColorToHex(Vec4{0, 0, 0, 128 / 255.0f}) == "#00000080");
    CHECK(ColorToHex(Vec4{2, -1, 0.5f, 1}) == "#ff0080ff");
    const char* samples[] = {"#2ecc71ff", "#c0392bff", "#00000080", "#123456ab", "#ffffff00"};
    for (const char* s : samples) CHECK(ColorToHex(ParseColor(s)) == s);
    CHECK(ColorToHex(ParseColor("#2ECC71")) == "#2ecc71ff");
}

TEST_CASE("Project::ImGuiVersionNum") {
    Project p;
    p.imgui_version = "1.91.9";
    CHECK(p.ImGuiVersionNum() == 19190);
    p.imgui_version = "1.85";
    CHECK(p.ImGuiVersionNum() == 18500);
    p.imgui_version = "1.89.9";
    CHECK(p.ImGuiVersionNum() == 18990);
    p.imgui_version = "1.92.1";
    CHECK(p.ImGuiVersionNum() == 19210);
    p.imgui_version = "1.90.1";
    CHECK(p.ImGuiVersionNum() == 19010);
    p.imgui_version = "1.91.9b";
    CHECK(p.ImGuiVersionNum() == 19190);
    p.imgui_version = "1.90";
    CHECK(p.ImGuiVersionNum() == 19000);
    p.imgui_version = "";
    CHECK(p.ImGuiVersionNum() == 0);
    p.imgui_version = "garbage";
    CHECK(p.ImGuiVersionNum() == 0);
    CHECK(Project::VersionString(19190) == "1.91.9");
    CHECK(Project::VersionString(18500) == "1.85");
    CHECK(Project::VersionString(18990) == "1.89.9");
    CHECK(Project::VersionString(19010) == "1.90.1");
    CHECK(Project::VersionString(19000) == "1.90");
    CHECK(Project().ImGuiVersionNum() == 19190);
}

TEST_CASE("PropValue literal / expr / param forms") {
    PropValue lit(Json("hello"));
    CHECK_FALSE(lit.IsExpr());
    CHECK_FALSE(lit.IsParam());
    CHECK(lit.Expr().empty());
    PropValue ex(P(R"({"expr": "player.health > 0"})"));
    CHECK(ex.IsExpr());
    CHECK_FALSE(ex.IsParam());
    CHECK(ex.Expr() == "player.health > 0");
    PropValue pr(P(R"({"param": "label"})"));
    CHECK(pr.IsParam());
    CHECK_FALSE(pr.IsExpr());
    CHECK(pr.Param() == "label");
    PropValue size(P(R"({"w": "fill", "h": 20})"));
    CHECK_FALSE(size.IsExpr());
    CHECK_FALSE(size.IsParam());
}

TEST_CASE("Node prop accessors") {
    Node n;
    CHECK(n.Prop("label") == nullptr);
    n.SetProp("label", Json("A"));
    n.SetProp("size", P(R"({"w":"fill","h":"auto"})"));
    REQUIRE(n.Prop("label"));
    CHECK(n.Prop("label")->raw.AsString() == "A");
    n.SetProp("label", Json("B"));
    CHECK(n.props.size() == 2);
    CHECK(n.Prop("label")->raw.AsString() == "B");
    CHECK(n.props[0].first == "label");
    CHECK(n.props[1].first == "size");
    n.RemoveProp("label");
    CHECK(n.props.size() == 1);
    CHECK(n.Prop("label") == nullptr);
    n.RemoveProp("nothing");
    CHECK(n.props.size() == 1);
    const Node& cn = n;
    CHECK(cn.Prop("size") != nullptr);
    CHECK(cn.Bind("value") == nullptr);
    n.bind.emplace_back("value", PropValue(Json("player.hp")));
    REQUIRE(cn.Bind("value"));
    CHECK(cn.Bind("value")->raw.AsString() == "player.hp");
}

TEST_CASE("NodeFromJson reads every field of the example node") {
    Node n = ParseNode(kNodeExample);
    CHECK(n.id == "btn_play");
    CHECK(n.type == "Button");
    CHECK(n.name == "Play button");
    REQUIRE(n.props.size() == 2);
    CHECK(n.props[0].first == "label");
    CHECK(n.props[0].second.raw.AsString() == "{icon:fa-play} Play");
    CHECK(n.props[1].first == "size");
    CHECK(n.props[1].second.raw["w"].AsString() == "fill");
    CHECK(n.props[1].second.raw["h"].AsString() == "auto");
    REQUIRE(n.Bind("value"));
    CHECK(n.Bind("value")->raw.AsString() == "player.health");
    CHECK(n.visible_if == "cfg.esp.enabled");
    CHECK(n.enabled_if == "player.health > 0");
    CHECK(n.tooltip == "Starts the game");
    CHECK(n.font == "Bold");
    REQUIRE(n.style.colors.size() == 1);
    CHECK(n.style.colors[0].first == "Button");
    CHECK(ColorToHex(n.style.colors[0].second) == "#2ecc71ff");
    REQUIRE(n.style.vars.size() == 1);
    CHECK(n.style.vars[0].first == "FrameRounding");
    CHECK(n.style.vars[0].second.AsDouble() == 8);
    REQUIRE(n.style.classes.size() == 1);
    CHECK(n.style.classes[0] == "danger");
    REQUIRE(n.events.size() == 1);
    CHECK(n.events[0].first == "onClick");
    CHECK(n.events[0].second.handler);
    REQUIRE(n.events[0].second.actions.size() == 1);
    CHECK(n.events[0].second.actions[0].type == "toggle");
    CHECK(n.events[0].second.actions[0].target == "cfg.esp.enabled");
    CHECK(n.events[0].second.actions[0].value.IsNull());
    REQUIRE(n.anim.transitions.size() == 1);
    CHECK(n.anim.transitions[0].prop == "color.Button");
    CHECK(n.anim.transitions[0].on == "hover");
    CHECK(n.anim.transitions[0].to.AsString() == "#27ae60ff");
    CHECK(n.anim.transitions[0].duration == doctest::Approx(0.15f));
    CHECK(n.anim.transitions[0].ease == "OutCubic");
    CHECK(n.anim.enter_preset == "FadeSlideUp");
    CHECK(n.anim.enter_duration == doctest::Approx(0.25f));
    CHECK(n.anim.stagger == doctest::Approx(0.03f));
    CHECK(n.anim.exit_preset.empty());
    REQUIRE(n.fx.size() == 2);
    CHECK(n.fx[0].type == "shadow");
    CHECK(n.fx[0].params["size"].AsDouble() == 12);
    CHECK(n.fx[0].params["color"].AsString() == "#00000080");
    CHECK_FALSE(n.fx[0].params.Has("type"));
    CHECK(n.fx[1].type == "blur");
    CHECK(n.fx[1].params["radius"].AsDouble() == 8);
    CHECK(n.layout.anchor == "TopRight");
    CHECK(n.layout.offset.x == doctest::Approx(-10));
    CHECK(n.layout.offset.y == doctest::Approx(10));
    CHECK_FALSE(n.locked);
    CHECK(n.children.empty());
    CHECK(n.slots.empty());
}

TEST_CASE("NodeToJson round-trips the example node and omits defaults") {
    std::string dumped = NodeRoundTrip(kNodeExample);
    Json expected = P(kNodeExample);
    expected.Erase("children");
    expected.Erase("locked");
    CHECK(dumped == DumpJson(expected));
    CHECK(dumped.find("\"children\"") == std::string::npos);
    CHECK(dumped.find("\"locked\"") == std::string::npos);

    Node n = ParseNode(kNodeExample);
    Json out = NodeToJson(n);
    CHECK(out["props"].obj[0].first == "label");
    CHECK(out["props"].obj[1].first == "size");
    CHECK(out["style"]["colors"]["Button"].AsString() == "#2ecc71ff");
    CHECK(out["anim"]["transitions"][0]["duration"].AsDouble() == 0.15);
    CHECK(out["anim"]["enter"]["stagger"].AsDouble() == 0.03);
    CHECK(out["fx"][0]["type"].AsString() == "shadow");
    CHECK(out["fx"][0]["size"].AsDouble() == 12);
    CHECK(out["layout"]["offset"]["x"].AsDouble() == -10);
}

TEST_CASE("NodeToJson emits props in sorted order and keeps expr/param raw forms") {
    Node n = ParseNode(R"json({"id":"t","type":"Text","props":{"wrapped":true,"color":{"expr":"cfg.col"},"text":{"param":"label"}}})json");
    CHECK(n.props[0].first == "wrapped");
    CHECK(n.props[1].second.IsExpr());
    CHECK(n.props[2].second.IsParam());
    Json out = NodeToJson(n);
    REQUIRE(out["props"].obj.size() == 3);
    CHECK(out["props"].obj[0].first == "color");
    CHECK(out["props"].obj[1].first == "text");
    CHECK(out["props"].obj[2].first == "wrapped");
    CHECK(DumpJson(out, 0) == R"({"id":"t","props":{"color":{"expr":"cfg.col"},"text":{"param":"label"},"wrapped":true},"type":"Text"})");
}

TEST_CASE("NodeToJson minimal node has only id and type") {
    Node n = ParseNode(R"({"id":"sep_1","type":"Separator"})");
    CHECK(DumpJson(NodeToJson(n), 0) == R"({"id":"sep_1","type":"Separator"})");
    Node d;
    d.id = "x";
    d.type = "Dummy";
    d.layout.anchor = "TopLeft";
    CHECK(DumpJson(NodeToJson(d), 0) == R"({"id":"x","type":"Dummy"})");
    d.locked = true;
    d.layout.offset.x = 5;
    CHECK(DumpJson(NodeToJson(d), 0) == R"({"id":"x","layout":{"anchor":"TopLeft","offset":{"x":5,"y":0}},"locked":true,"type":"Dummy"})");
}

TEST_CASE("Node serialization of nested children, slots, exit anim and spring transitions") {
    const char* text = R"json({"id":"root","type":"VStack","children":[
      {"id":"c1","type":"Text","props":{"text":"a"}},
      {"id":"cmp","type":"Component","component":"LabeledSlider","props":{"label":"Speed","value":"player.speed"},
       "slots":{"content":[{"id":"inner","type":"Text","props":{"text":"b"}}]}},
      {"id":"c3","type":"Button","props":{"label":"x"},
       "anim":{"transitions":[{"prop":"var.Alpha","on":"active","to":0.5,"duration":0.3,"ease":"Spring","stiffness":170,"damping":26}],
               "enter":{"preset":"Fade","duration":0.2},"exit":{"preset":"SlideDown","duration":0.1}},
       "events":{"onClick":{"actions":[{"type":"set","target":"state.selectedTab","value":{"expr":"1 + 1"}},{"type":"closePopup"}]}}}
    ]})json";
    std::string dumped = NodeRoundTrip(text);
    CHECK(dumped == DumpJson(P(text)));
    Node n = ParseNode(text);
    REQUIRE(n.children.size() == 3);
    CHECK(n.children[1].component == "LabeledSlider");
    REQUIRE(n.children[1].slots.size() == 1);
    CHECK(n.children[1].slots[0].first == "content");
    REQUIRE(n.children[1].slots[0].second.size() == 1);
    CHECK(n.children[1].slots[0].second[0].id == "inner");
    const Node& c3 = n.children[2];
    CHECK(c3.anim.transitions[0].stiffness == doctest::Approx(170));
    CHECK(c3.anim.transitions[0].damping == doctest::Approx(26));
    CHECK(c3.anim.exit_preset == "SlideDown");
    CHECK(c3.anim.exit_duration == doctest::Approx(0.1f));
    CHECK(c3.anim.stagger == 0);
    REQUIRE(c3.events[0].second.actions.size() == 2);
    CHECK(c3.events[0].second.actions[0].value["expr"].AsString() == "1 + 1");
    CHECK(c3.events[0].second.actions[1].type == "closePopup");
    CHECK_FALSE(c3.events[0].second.handler);
}

TEST_CASE("Transition defaults are filled and emitted") {
    Node n = ParseNode(R"json({"id":"b","type":"Button","anim":{"transitions":[{"prop":"color.Button","to":"#000000ff"}]}})json");
    CHECK(n.anim.transitions[0].duration == doctest::Approx(0.15f));
    CHECK(n.anim.transitions[0].ease == "OutCubic");
    CHECK(DumpJson(NodeToJson(n), 0) ==
          R"({"anim":{"transitions":[{"duration":0.15,"ease":"OutCubic","prop":"color.Button","to":"#000000ff"}]},"id":"b","type":"Button"})");
}

TEST_CASE("NodeFromJson rejects malformed input") {
    Node n;
    Error e;
    CHECK_FALSE(NodeFromJson(P(R"({"id":"a"})"), n, &e));
    CHECK(e.message.find("type") != std::string::npos);
    CHECK_FALSE(NodeFromJson(P(R"({"type":"Button"})"), n, &e));
    CHECK(e.message.find("id") != std::string::npos);
    CHECK_FALSE(NodeFromJson(P(R"([1,2])"), n, &e));
    CHECK_FALSE(NodeFromJson(P(R"({"id":"a","type":"Button","style":{"colors":{"Button":"red"}}})"), n, &e));
    CHECK(e.message.find("Button") != std::string::npos);
    CHECK_FALSE(NodeFromJson(P(R"({"id":"a","type":"Button","style":{"vars":{"FrameRounding":"big"}}})"), n, &e));
    CHECK_FALSE(NodeFromJson(P(R"({"id":"a","type":"Button","children":{}})"), n, &e));
    CHECK_FALSE(NodeFromJson(P(R"({"id":"a","type":"Button","children":[{"id":"b"}]})"), n, &e));
    CHECK_FALSE(NodeFromJson(P(R"({"id":"a","type":"Button","events":{"onClick":{"actions":[{"target":"x"}]}}})"), n, &e));
    CHECK_FALSE(NodeFromJson(P(R"({"id":"a","type":"Button","fx":[{"size":3}]})"), n, &e));
    CHECK_FALSE(NodeFromJson(P(R"({"id":"a","type":"Button","bind":{"value":3}})"), n, &e));
    CHECK_FALSE(NodeFromJson(P(R"({"id":"a","type":"Button","locked":"yes"})"), n, &e));
    CHECK_FALSE(NodeFromJson(P(R"({"id":"a","type":"Button","layout":{"offset":[1,2]}})"), n, &e));
    CHECK_FALSE(NodeFromJson(P(R"({"id":"a","type":"Button","slots":{"content":{}}})"), n, &e));
    CHECK(NodeFromJson(P(R"({"id":"a","type":"Button","props":{"label":"ok"}})"), n, nullptr));
    CHECK_FALSE(NodeFromJson(P(R"({"id":"a"})"), n, nullptr));
}

TEST_CASE("Panel round trip") {
    Panel p;
    Error e;
    p.file = "panels/MainMenu.imb.json";
    REQUIRE_MESSAGE(PanelFromJson(P(kPanelExample), p, &e), e.message);
    CHECK(p.file == "panels/MainMenu.imb.json");
    CHECK(p.name == "MainMenu");
    REQUIRE(p.params.size() == 2);
    CHECK(p.params[0].name == "player");
    CHECK(p.params[0].type == "Player");
    CHECK(p.params[0].def.IsNull());
    REQUIRE(p.state.size() == 2);
    CHECK(p.state[0].name == "selectedTab");
    CHECK(p.state[0].def.AsInt() == 0);
    CHECK(p.state[1].def.AsBool());
    CHECK(p.root.type == "Window");
    CHECK(p.root.Prop("flags")->raw[0].AsString() == "NoCollapse");
    CHECK(p.root.Bind("open")->raw.AsString() == "state.open");
    REQUIRE(p.root.children.size() == 1);
    CHECK(p.root.children[0].children.size() == 2);

    std::string d1 = DumpJson(PanelToJson(p));
    CHECK(d1 == DumpJson(P(kPanelExample)));
    Panel p2;
    REQUIRE(PanelFromJson(P(d1.c_str()), p2, &e));
    CHECK(DumpJson(PanelToJson(p2)) == d1);
    Json out = PanelToJson(p);
    CHECK(out["schemaVersion"].AsInt() == 1);
    CHECK(out["kind"].AsString() == "panel");

    CHECK_FALSE(PanelFromJson(P(R"({"kind":"panel","name":"X"})"), p2, &e));
    CHECK(e.message.find("root") != std::string::npos);
    CHECK_FALSE(PanelFromJson(P(R"({"kind":"component","name":"X","root":{"id":"a","type":"Text"}})"), p2, &e));
    CHECK_FALSE(PanelFromJson(P(R"({"kind":"panel","root":{"id":"a","type":"Text"}})"), p2, &e));
    CHECK_FALSE(PanelFromJson(P(R"({"schemaVersion":2,"kind":"panel","name":"X","root":{"id":"a","type":"Text"}})"), p2, &e));
    CHECK_FALSE(PanelFromJson(P(R"({"kind":"panel","name":"X","params":[{"name":"p"}],"root":{"id":"a","type":"Text"}})"), p2, &e));
    CHECK(PanelFromJson(P(R"({"name":"Min","root":{"id":"a","type":"Text"}})"), p2, &e));
    CHECK(DumpJson(PanelToJson(p2), 0) == R"({"kind":"panel","name":"Min","root":{"id":"a","type":"Text"},"schemaVersion":1})");
}

TEST_CASE("Component round trip") {
    Component c;
    Error e;
    REQUIRE_MESSAGE(ComponentFromJson(P(kComponentExample), c, &e), e.message);
    CHECK(c.name == "LabeledSlider");
    REQUIRE(c.params.size() == 4);
    CHECK(c.params[0].def.AsString() == "Value");
    CHECK(c.params[1].type == "binding<float>");
    CHECK(c.params[1].def.IsNull());
    CHECK(c.params[2].def.AsDouble() == 0);
    REQUIRE(c.slots.size() == 1);
    CHECK(c.slots[0] == "content");
    REQUIRE(c.root.children.size() == 3);
    CHECK(c.root.children[0].Prop("text")->IsParam());
    CHECK(c.root.children[0].Prop("text")->Param() == "label");
    CHECK(c.root.children[1].Bind("value")->Param() == "value");
    CHECK(c.root.children[2].type == "Slot");

    std::string d1 = DumpJson(ComponentToJson(c));
    CHECK(d1 == DumpJson(P(kComponentExample)));
    Component c2;
    REQUIRE(ComponentFromJson(P(d1.c_str()), c2, &e));
    CHECK(DumpJson(ComponentToJson(c2)) == d1);
    CHECK_FALSE(ComponentFromJson(P(R"({"kind":"component","name":"X","slots":"content","root":{"id":"a","type":"Text"}})"), c2, &e));
    CHECK_FALSE(ComponentFromJson(P(R"({"kind":"component","name":"X"})"), c2, &e));
}

TEST_CASE("Theme round trip") {
    Theme t;
    Error e;
    REQUIRE_MESSAGE(ThemeFromJson(P(kThemeExample), t, &e), e.message);
    CHECK(t.name == "Dark");
    REQUIRE(t.colors.size() == 3);
    CHECK(t.colors[0].first == "WindowBg");
    CHECK(ColorToHex(t.colors[0].second) == "#1e1e1eff");
    CHECK(t.colors[2].first == "FutureCol");
    REQUIRE(t.vars.size() == 3);
    CHECK(t.vars[0].second.AsDouble() == 4);
    CHECK(t.vars[1].second["x"].AsDouble() == 8);
    CHECK(t.vars[2].second.AsDouble() == 0.95);
    std::string d1 = DumpJson(ThemeToJson(t));
    CHECK(d1 == DumpJson(P(kThemeExample)));
    Theme t2;
    REQUIRE(ThemeFromJson(P(d1.c_str()), t2, &e));
    CHECK(DumpJson(ThemeToJson(t2)) == d1);
    CHECK_FALSE(ThemeFromJson(P(R"({"kind":"theme","name":"Bad","colors":{"Text":"#12"}})"), t2, &e));
    CHECK(e.message.find("Text") != std::string::npos);
    CHECK_FALSE(ThemeFromJson(P(R"({"kind":"theme","name":"Bad","vars":{"Alpha":"x"}})"), t2, &e));
    CHECK_FALSE(ThemeFromJson(P(R"({"kind":"theme","name":"Bad","vars":{"WindowPadding":{"x":1}}})"), t2, &e));
    CHECK(ThemeFromJson(P(R"({"kind":"theme","name":"Empty"})"), t2, &e));
    CHECK(DumpJson(ThemeToJson(t2), 0) == R"({"colors":{},"kind":"theme","name":"Empty","schemaVersion":1,"vars":{}})");
}

TEST_CASE("Project round trip") {
    Project p;
    Error e;
    p.dir = "C:/proj";
    REQUIRE_MESSAGE(ProjectFromJson(P(kProjectExample), p, &e), e.message);
    CHECK(p.dir == "C:/proj");
    CHECK(p.name == "CheatMenu");
    CHECK(p.imgui_version == "1.91.9");
    CHECK(p.imgui_branch == "docking");
    CHECK(p.ImGuiVersionNum() == 19190);
    CHECK(p.window == "win32");
    CHECK(p.renderer == "dx11");
    REQUIRE(p.features.size() == 6);
    CHECK(p.Feature("animations"));
    CHECK_FALSE(p.Feature("docking"));
    CHECK(p.Feature("missing", true));
    CHECK(p.codegen.cpp_standard == 17);
    CHECK(p.codegen.nmspace == "ui");
    CHECK(p.codegen.split_header);
    CHECK(p.codegen.function_case == "pascal");
    CHECK(p.codegen.variable_case == "snake");
    CHECK(p.codegen.indent == 4);
    CHECK_FALSE(p.codegen.use_tabs);
    CHECK(p.codegen.output_dir == "generated");
    CHECK(p.codegen.markers);
    CHECK(p.codegen.dpi_mode == "fontSize");
    CHECK(p.codegen.base_font_size == 13);
    CHECK(p.codegen.runtime_header == "imb_runtime.h");
    CHECK(p.codegen.std_string);
    CHECK(p.compile_commands == "build/compile_commands.json");
    CHECK(p.include_paths.empty());
    REQUIRE(p.binding_types.size() == 2);
    CHECK(p.binding_types[1] == "Settings");
    CHECK(p.types_json == "generated/types.json");
    CHECK(p.live.enabled);
    CHECK(p.live.transport == "tcp");
    CHECK(p.live.tcp_port == 7777);
    CHECK(p.live.pipe_name == "ImForge");
    CHECK(p.live.shm_name == "ImForge");
    REQUIRE(p.fonts.size() == 1);
    CHECK(p.fonts[0].name == "Default");
    CHECK(p.fonts[0].file == "fonts/Inter-Regular.ttf");
    CHECK(p.fonts[0].size == 16);
    CHECK(p.fonts[0].ranges == "default");
    CHECK(p.fonts[0].merge_icons == "fontawesome6");
    REQUIRE(p.theme_files.size() == 1);
    CHECK(p.theme_files[0] == "themes/Dark.theme.json");
    CHECK(p.themes.empty());
    CHECK(p.active_theme == "Dark");
    REQUIRE(p.style_classes.size() == 1);
    CHECK(p.style_classes[0].first == "danger");
    CHECK(ColorToHex(p.style_classes[0].second.colors[0].second) == "#c0392bff");
    CHECK(p.style_classes[0].second.vars[0].second.AsDouble() == 6);
    REQUIRE(p.panel_files.size() == 1);
    CHECK(p.panel_files[0] == "panels/MainMenu.imb.json");
    REQUIRE(p.component_files.size() == 1);
    CHECK(p.component_files[0] == "components/LabeledSlider.imc.json");
    CHECK(p.panels.empty());
    REQUIRE(p.libraries.size() == 1);
    CHECK(p.libraries[0] == "libs/essentials.imblib");

    std::string d1 = DumpJson(ProjectToJson(p));
    CHECK(d1 == DumpJson(P(kProjectExample)));
    Project p2;
    REQUIRE(ProjectFromJson(P(d1.c_str()), p2, &e));
    CHECK(DumpJson(ProjectToJson(p2)) == d1);

    Panel loaded;
    loaded.name = "Other";
    loaded.file = "panels/Other.imb.json";
    p.panels.push_back(loaded);
    Theme th;
    th.name = "Light";
    th.file = "themes/Light.theme.json";
    p.themes.push_back(th);
    Json out = ProjectToJson(p);
    REQUIRE(out["panels"].Size() == 1);
    CHECK(out["panels"][0].AsString() == "panels/Other.imb.json");
    CHECK(out["themes"][0].AsString() == "themes/Light.theme.json");
    CHECK(out["components"][0].AsString() == "components/LabeledSlider.imc.json");

    Project defaults;
    defaults.name = "Empty";
    Json dj = ProjectToJson(defaults);
    CHECK(dj["imgui"]["version"].AsString() == "1.91.9");
    CHECK(dj["codegen"]["baseFontSize"].AsDouble() == 13);
    CHECK(dj["panels"].IsArray());
    CHECK(dj["features"].IsObject());
    Project d2;
    REQUIRE(ProjectFromJson(dj, d2, &e));
    CHECK(DumpJson(ProjectToJson(d2)) == DumpJson(dj));

    CHECK_FALSE(ProjectFromJson(P(R"({"kind":"project"})"), p2, &e));
    CHECK_FALSE(ProjectFromJson(P(R"({"kind":"panel","name":"X"})"), p2, &e));
    CHECK_FALSE(ProjectFromJson(P(R"({"kind":"project","name":"X","features":{"animations":"yes"}})"), p2, &e));
    CHECK_FALSE(ProjectFromJson(P(R"({"kind":"project","name":"X","codegen":{"indent":"four"}})"), p2, &e));
    CHECK_FALSE(ProjectFromJson(P(R"({"kind":"project","name":"X","panels":"panels/a.json"})"), p2, &e));
}

TEST_CASE("FindNode / FindParent / Walk / CollectIds visit children then slots in order") {
    Node root = ParseNode(R"json({"id":"root","type":"VStack","children":[
      {"id":"a","type":"HStack","children":[{"id":"a1","type":"Text"},{"id":"a2","type":"Text"}]},
      {"id":"cmp","type":"Component","component":"LabeledSlider",
       "slots":{"content":[{"id":"s1","type":"Text"},{"id":"s2","type":"Group","children":[{"id":"s2a","type":"Text"}]}]}},
      {"id":"b","type":"Button"}
    ]})json");
    std::vector<std::string> ids;
    CollectIds(root, ids);
    std::vector<std::string> expected = {"root", "a", "a1", "a2", "cmp", "s1", "s2", "s2a", "b"};
    CHECK(ids == expected);

    std::vector<std::string> walked;
    Walk(root, [&](Node& n) { walked.push_back(n.id); });
    CHECK(walked == expected);
    const Node& croot = root;
    walked.clear();
    Walk(croot, [&](const Node& n) { walked.push_back(n.type); });
    CHECK(walked.size() == 9);
    CHECK(walked[4] == "Component");

    REQUIRE(FindNode(root, "s2a"));
    CHECK(FindNode(root, "s2a")->type == "Text");
    CHECK(FindNode(root, "root") == &root);
    CHECK(FindNode(root, "nope") == nullptr);
    CHECK(FindNode(croot, "a2") == &croot.children[0].children[1]);

    size_t idx = 99;
    Node* parent = FindParent(root, "a2", &idx);
    REQUIRE(parent);
    CHECK(parent->id == "a");
    CHECK(idx == 1);
    parent = FindParent(root, "s2a", &idx);
    REQUIRE(parent);
    CHECK(parent->id == "s2");
    CHECK(idx == 0);
    parent = FindParent(root, "s2", &idx);
    REQUIRE(parent);
    CHECK(parent->id == "cmp");
    CHECK(idx == 1);
    parent = FindParent(root, "b", nullptr);
    REQUIRE(parent);
    CHECK(parent->id == "root");
    CHECK(FindParent(root, "root", &idx) == nullptr);
    CHECK(FindParent(root, "missing", &idx) == nullptr);

    Walk(root, [](Node& n) { n.locked = true; });
    CHECK(root.children[1].slots[0].second[1].children[0].locked);
}

TEST_CASE("GenerateId picks the smallest unused lowerCamel suffix") {
    Node root = ParseNode(R"json({"id":"window","type":"Window","children":[
      {"id":"button_1","type":"Button"},
      {"id":"button_3","type":"Button"},
      {"id":"cmp","type":"Component","component":"X","slots":{"content":[{"id":"text_1","type":"Text"}]}}
    ]})json");
    CHECK(GenerateId(root, "Button") == "button_2");
    CHECK(GenerateId(root, "Text") == "text_2");
    CHECK(GenerateId(root, "SliderFloat") == "sliderFloat_1");
    CHECK(GenerateId(root, "VSliderFloat") == "vSliderFloat_1");
    CHECK(GenerateId(root, "Window") == "window_1");
    CHECK(GenerateId(root, "imb::Toggle") == "imb__Toggle_1");
    CHECK(GenerateId(root, "") == "node_1");
    Node b;
    b.id = GenerateId(root, "Button");
    b.type = "Button";
    root.children.push_back(b);
    CHECK(GenerateId(root, "Button") == "button_4");
    std::vector<std::string> ids;
    CollectIds(root, ids);
    for (size_t i = 0; i < ids.size(); ++i)
        for (size_t j = i + 1; j < ids.size(); ++j) CHECK(ids[i] != ids[j]);
}

TEST_CASE("ValidatePanel accepts a well-formed panel") {
    Project project = MakeProject();
    Panel panel;
    Error e;
    REQUIRE(PanelFromJson(P(kPanelExample), panel, &e));
    std::vector<Diagnostic> diags;
    bool ok = ValidatePanel(project, panel, diags);
    for (const Diagnostic& d : diags) MESSAGE(d.node_id << "/" << d.prop << ": " << d.message);
    CHECK(ok);
    CHECK(CountLevel(diags, Diagnostic::Level::Error) == 0);
}

TEST_CASE("ValidatePanel: unknown type, unknown prop, duplicate id") {
    bool ok = true;
    std::vector<Diagnostic> d = Validate(R"json({"id":"w","type":"Window","props":{"title":"T"},"children":[
      {"id":"x","type":"Bogus"},
      {"id":"b","type":"Button","props":{"label":"ok","colour":"#ff0000ff"}},
      {"id":"t","type":"Text","props":{"text":"1"}},
      {"id":"t","type":"Text","props":{"text":"2"}},
      {"id":"9bad","type":"Text"}
    ]})json", &ok);
    CHECK_FALSE(ok);
    CHECK(HasError(d, "x", "unknown widget type"));
    CHECK(HasError(d, "b", "unknown prop \"colour\""));
    CHECK(HasError(d, "t", "duplicate node id"));
    CHECK(HasError(d, "9bad", "invalid node id"));
    for (const Diagnostic& diag : d) CHECK(diag.panel == "P");
    bool prop_named = false;
    for (const Diagnostic& diag : d) prop_named = prop_named || (diag.node_id == "b" && diag.prop == "colour");
    CHECK(prop_named);
}

TEST_CASE("ValidatePanel: Tab placement and TabBar children") {
    bool ok = true;
    std::vector<Diagnostic> d = Validate(R"json({"id":"w","type":"Window","props":{"title":"T"},"children":[
      {"id":"lonely","type":"Tab","props":{"label":"L"}},
      {"id":"tabs","type":"TabBar","children":[
         {"id":"good","type":"Tab","props":{"label":"G"}},
         {"id":"stray","type":"Text","props":{"text":"no"}}
      ]}
    ]})json", &ok);
    CHECK_FALSE(ok);
    CHECK(HasError(d, "lonely", "Tab must be a direct child of a TabBar"));
    CHECK(HasError(d, "stray", "accepts only"));
    CHECK_FALSE(HasError(d, "good", "Tab must"));
}

TEST_CASE("ValidatePanel: Slot outside component and MenuBar placement") {
    bool ok = true;
    std::vector<Diagnostic> d = Validate(R"json({"id":"w","type":"Window","props":{"title":"T"},"children":[
      {"id":"slot","type":"Slot","props":{"name":"content"}},
      {"id":"mb","type":"MenuBar"},
      {"id":"grp","type":"Group","children":[{"id":"mb2","type":"MenuBar"}]}
    ]})json", &ok);
    CHECK_FALSE(ok);
    CHECK(HasError(d, "slot", "Slot is only allowed inside a component"));
    CHECK(HasError(d, "mb", "MenuBar must be a direct child of a Window with menuBar"));
    CHECK(HasError(d, "mb2", "MenuBar must be"));

    d = Validate(R"json({"id":"w","type":"Window","props":{"title":"T","menuBar":true},"children":[
      {"id":"mb","type":"MenuBar","children":[{"id":"m","type":"Menu","props":{"label":"File"},"children":[
        {"id":"mi","type":"MenuItem","props":{"label":"Quit"}}]}]}
    ]})json", &ok);
    CHECK(ok);
    CHECK_FALSE(HasError(d, "mb", "MenuBar"));
}

TEST_CASE("ValidatePanel: Window only at root and children on leaf widgets") {
    bool ok = true;
    std::vector<Diagnostic> d = Validate(R"json({"id":"w","type":"Window","props":{"title":"T"},"children":[
      {"id":"inner","type":"Window","props":{"title":"X"}},
      {"id":"b","type":"Button","props":{"label":"b"},"children":[{"id":"t","type":"Text"}]}
    ]})json", &ok);
    CHECK_FALSE(ok);
    CHECK(HasError(d, "inner", "Window is only allowed as the panel root"));
    CHECK(HasError(d, "b", "cannot have children"));
}

TEST_CASE("ValidatePanel: component instances") {
    bool ok = true;
    std::vector<Diagnostic> d = Validate(R"json({"id":"w","type":"Window","props":{"title":"T"},"children":[
      {"id":"c1","type":"Component","component":"Nope"},
      {"id":"c2","type":"Component","component":"LabeledSlider","props":{"label":"Speed"}},
      {"id":"c3","type":"Component","component":"LabeledSlider","props":{"value":"player.speed","bogus":1},
       "slots":{"content":[{"id":"t","type":"Text","props":{"text":"x"}}],"other":[]}},
      {"id":"c4","type":"Component"}
    ]})json", &ok);
    CHECK_FALSE(ok);
    CHECK(HasError(d, "c1", "unknown component \"Nope\""));
    CHECK(HasError(d, "c2", "missing value for component param \"value\""));
    CHECK_FALSE(HasError(d, "c3", "missing value"));
    CHECK(HasDiag(d, Diagnostic::Level::Warning, "c3", "unknown param \"bogus\""));
    CHECK(HasError(d, "c3", "has no slot \"other\""));
    CHECK(HasError(d, "c4", "missing its \"component\" name"));

    d = Validate(R"json({"id":"w","type":"Window","props":{"title":"T"},"children":[
      {"id":"c","type":"Component","component":"LabeledSlider","props":{"value":"player.speed","label":"S","min":0,"max":5},
       "slots":{"content":[{"id":"t","type":"Text","props":{"text":"x"}}]}}
    ]})json", &ok);
    CHECK(ok);
}

TEST_CASE("ValidatePanel: colors, sizes, enums, flags and literal types") {
    bool ok = true;
    std::vector<Diagnostic> d = Validate(R"json({"id":"w","type":"Window","props":{"title":"T","sizeCond":"Sometimes","flags":["NoCollapse","NotAFlag"]},"children":[
      {"id":"t1","type":"Text","props":{"text":"a","color":"#12345"}},
      {"id":"t2","type":"Text","props":{"text":"a","color":"#123456"}},
      {"id":"b1","type":"Button","props":{"label":"b","size":{"w":"wide","h":"auto"}}},
      {"id":"b2","type":"Button","props":{"label":"b","size":{"w":"fill-10","h":"50%"}}},
      {"id":"b3","type":"Button","props":{"label":3}},
      {"id":"t3","type":"Text","props":{"text":"a","wrapped":"yes"}},
      {"id":"t4","type":"Text","props":{"text":{"param":"label"}}},
      {"id":"sp","type":"Spacing","props":{"count":"two"}}
    ]})json", &ok);
    CHECK_FALSE(ok);
    CHECK(HasError(d, "w", "expected one of"));
    CHECK(HasDiag(d, Diagnostic::Level::Warning, "w", "unknown flag \"NotAFlag\""));
    CHECK(HasError(d, "t1", "invalid color"));
    CHECK_FALSE(HasError(d, "t2", "invalid color"));
    CHECK(HasError(d, "b1", "invalid SizeSpec for \"w\""));
    CHECK_FALSE(HasError(d, "b2", "SizeSpec"));
    CHECK(HasError(d, "b3", "expected a string"));
    CHECK(HasError(d, "t3", "expected a boolean"));
    CHECK(HasError(d, "t4", "only allowed inside a component definition"));
    CHECK(HasError(d, "sp", "expected a number"));
}

TEST_CASE("ValidatePanel: bindings") {
    bool ok = true;
    std::vector<Diagnostic> d = Validate(R"json({"id":"w","type":"Window","props":{"title":"T"},"children":[
      {"id":"cb1","type":"Checkbox","props":{"label":"a"}},
      {"id":"cb2","type":"Checkbox","props":{"label":"a"},"bind":{"value":"nope.enabled"}},
      {"id":"cb3","type":"Checkbox","props":{"label":"a"},"bind":{"value":"state.missing"}},
      {"id":"cb4","type":"Checkbox","props":{"label":"a"},"bind":{"value":"state.open","other":"cfg.x"}},
      {"id":"cb5","type":"Checkbox","props":{"label":"a"},"bind":{"value":"cfg.esp.enabled"}},
      {"id":"cb6","type":"Checkbox","props":{"label":"a"},"bind":{"value":{"param":"value"}}},
      {"id":"cb7","type":"Checkbox","props":{"label":"a"},"bind":{"value":"1bad"}}
    ]})json", &ok);
    CHECK_FALSE(ok);
    CHECK(HasError(d, "cb1", "missing required binding \"value\""));
    CHECK(HasError(d, "cb2", "is not a panel param or \"state\""));
    CHECK(HasError(d, "cb3", "unknown state variable"));
    CHECK(HasError(d, "cb4", "unknown binding slot \"other\""));
    CHECK_FALSE(HasError(d, "cb4", "unknown state"));
    CHECK_FALSE(HasError(d, "cb5", ""));
    CHECK(HasError(d, "cb6", "only allowed inside a component definition"));
    CHECK(HasError(d, "cb7", "invalid binding path"));
}

TEST_CASE("ValidatePanel: events, actions, style, anim, layout, font") {
    bool ok = true;
    std::vector<Diagnostic> d = Validate(R"json({"id":"w","type":"Window","props":{"title":"T"},"children":[
      {"id":"b1","type":"Button","props":{"label":"b"},"events":{"onClick":{"actions":[
         {"type":"openPopup","target":"nothere"},{"type":"setTab","target":"b1","value":"x"},
         {"type":"explode"},{"type":"set","target":"state.open"},{"type":"toggle","target":"state.open"}]}}},
      {"id":"b2","type":"Button","props":{"label":"b"},"events":{"onHover":{"handler":true},"onClick":{}}},
      {"id":"pop","type":"Popup","children":[{"id":"close","type":"Button","props":{"label":"x"},
         "events":{"onClick":{"actions":[{"type":"closePopup"}]}}}]},
      {"id":"b3","type":"Button","props":{"label":"b"},"events":{"onClick":{"actions":[{"type":"openPopup","target":"pop"}]}},
       "style":{"colors":{"Button":"#ff0000ff","Nope":"#ff0000ff"},"vars":{"FrameRounding":{"x":1,"y":1},"ItemSpacing":4,"Bogus":1},"classes":["danger","ghost"]},
       "anim":{"transitions":[{"prop":"color.Button","to":"red"},{"prop":"color.Nope","to":"#00ff00ff"},{"prop":"var.Alpha","to":{"x":1,"y":1},"ease":"Wobble"}],
               "enter":{"preset":"Teleport"}},
       "fx":[{"type":"shadow","color":"#zz"}],
       "layout":{"anchor":"Middle","offset":{"x":1,"y":1}},
       "font":"Comic"},
      {"id":"cv","type":"Canvas","children":[{"id":"inside","type":"Text","props":{"text":"t"},"layout":{"anchor":"Center"},"font":"Bold"}]}
    ]})json", &ok);
    CHECK_FALSE(ok);
    CHECK(HasError(d, "b1", "openPopup target \"nothere\""));
    CHECK(HasError(d, "b1", "setTab target \"b1\""));
    CHECK(HasError(d, "b1", "unknown action type \"explode\""));
    CHECK(HasError(d, "b1", "set action requires \"value\""));
    CHECK(HasDiag(d, Diagnostic::Level::Warning, "b2", "unknown event \"onHover\""));
    CHECK(HasDiag(d, Diagnostic::Level::Info, "b2", "has no actions or handler"));
    CHECK_FALSE(HasError(d, "b3", "openPopup"));
    CHECK_FALSE(HasError(d, "close", ""));
    CHECK(HasDiag(d, Diagnostic::Level::Warning, "b3", "unknown ImGuiCol name \"Nope\""));
    CHECK(HasError(d, "b3", "style var \"FrameRounding\" expects a number"));
    CHECK(HasError(d, "b3", "style var \"ItemSpacing\" expects {\"x\", \"y\"}"));
    CHECK(HasDiag(d, Diagnostic::Level::Warning, "b3", "unknown ImGuiStyleVar name \"Bogus\""));
    CHECK(HasError(d, "b3", "unknown style class \"ghost\""));
    CHECK_FALSE(HasError(d, "b3", "unknown style class \"danger\""));
    CHECK(HasError(d, "b3", "needs a color string"));
    CHECK(HasDiag(d, Diagnostic::Level::Warning, "b3", "unknown ImGuiCol in transition"));
    CHECK(HasError(d, "b3", "wrong kind"));
    CHECK(HasDiag(d, Diagnostic::Level::Warning, "b3", "unknown ease \"Wobble\""));
    CHECK(HasDiag(d, Diagnostic::Level::Warning, "b3", "unknown enter preset \"Teleport\""));
    CHECK(HasError(d, "b3", "invalid color in \"color\""));
    CHECK(HasError(d, "b3", "unknown layout anchor \"Middle\""));
    CHECK(HasDiag(d, Diagnostic::Level::Warning, "b3", "layout is only used for direct children of a Canvas"));
    CHECK(HasDiag(d, Diagnostic::Level::Warning, "b3", "unknown font \"Comic\""));
    CHECK_FALSE(HasDiag(d, Diagnostic::Level::Warning, "inside", "layout"));
    CHECK_FALSE(HasDiag(d, Diagnostic::Level::Warning, "inside", "font"));
}

TEST_CASE("ValidatePanel: declarations") {
    Project project = MakeProject();
    Panel panel = MakePanel(R"({"id":"w","type":"Window","props":{"title":"T"}})");
    panel.params.push_back(Param{"player", "Player", Json()});
    panel.params.push_back(Param{"1x", "int", Json()});
    panel.state.push_back(StateVar{"open", "bool", Json()});
    panel.state.push_back(StateVar{"weird", "matrix", Json()});
    std::vector<Diagnostic> d;
    CHECK_FALSE(ValidatePanel(project, panel, d));
    CHECK(HasError(d, nullptr, "duplicate param name \"player\""));
    CHECK(HasError(d, nullptr, "invalid param name \"1x\""));
    CHECK(HasError(d, nullptr, "duplicate state name \"open\""));
    CHECK(HasError(d, nullptr, "invalid state type \"matrix\""));
}

TEST_CASE("ValidateComponent") {
    Project project = MakeProject();
    Component c;
    Error e;
    REQUIRE(ComponentFromJson(P(kComponentExample), c, &e));
    std::vector<Diagnostic> d;
    bool ok = ValidateComponent(project, c, d);
    for (const Diagnostic& diag : d) MESSAGE(diag.node_id << "/" << diag.prop << ": " << diag.message);
    CHECK(ok);

    Component bad = c;
    bad.root.children[0].SetProp("text", P(R"({"param":"nope"})"));
    bad.root.children[2].SetProp("name", Json("other"));
    bad.root.children[1].bind[0].second = PropValue(Json("player.speed"));
    Node self;
    self.id = "self";
    self.type = "Component";
    self.component = "LabeledSlider";
    bad.root.children.push_back(self);
    bad.params.push_back(Param{"weird", "matrix", Json()});
    d.clear();
    CHECK_FALSE(ValidateComponent(project, bad, d));
    CHECK(HasError(d, "lbl", "unknown component param \"nope\""));
    CHECK(HasError(d, "slot_content", "slot \"other\" is not declared"));
    CHECK(HasError(d, "sl", "component trees must reference bindings through"));
    CHECK(HasError(d, "self", "cannot instantiate itself"));
    CHECK(HasError(d, nullptr, "invalid component param type \"matrix\""));
    CHECK_FALSE(HasError(d, "slot_content", "Slot is only allowed"));
}

TEST_CASE("ValidatePanel: docking-only window flags") {
    Panel panel = MakePanel(R"json({"id":"w","type":"Window","props":{"title":"T","flags":["NoDocking","NoCollapse"]}})json");
    Project docking = MakeProject();
    docking.imgui_branch = "docking";
    std::vector<Diagnostic> d;
    CHECK(ValidatePanel(docking, panel, d));
    CHECK(d.empty());
    Project master = MakeProject();
    master.imgui_branch = "master";
    d.clear();
    CHECK(ValidatePanel(master, panel, d));
    CHECK(HasDiag(d, Diagnostic::Level::Warning, "w", "\"NoDocking\" requires the docking branch"));
    CHECK_FALSE(HasDiag(d, Diagnostic::Level::Warning, "w", "unknown flag"));
}

TEST_CASE("ColorToHex clamps NaN and out-of-range components") {
    CHECK(ColorToHex(Vec4{std::nanf(""), 2.0f, -1.0f, 0.5f}) == "#00ff0080");
    CHECK(ColorToHex(Vec4{HUGE_VALF, -HUGE_VALF, 1.0f, 0.0f}) == "#ff00ff00");
}

TEST_CASE("SizeSpec numbers are locale independent") {
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
        SizeSpec s;
        REQUIRE(SizeSpec::Parse(Json("50.5%"), s));
        CHECK(s.mode == SizeSpec::Mode::Percent);
        CHECK(s.value == 50.5f);
        REQUIRE(SizeSpec::Parse(Json("fill-2.5"), s));
        CHECK(s.value == 2.5f);
        SizeSpec px;
        px.mode = SizeSpec::Mode::Px;
        px.value = 0.1f;
        CHECK(DumpJson(px.ToJson()) == "0.1");
        CHECK(px.ToString() == "0.1");
    }
    std::setlocale(LC_NUMERIC, "C");
    SizeSpec px;
    px.mode = SizeSpec::Mode::Px;
    px.value = 0.1f;
    CHECK(DumpJson(px.ToJson()) == "0.1");
}
