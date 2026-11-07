#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "../src/builder/ui/panel_refactor.cpp"

using namespace imforge;
using namespace imforge::builder;

namespace {

RenameTarget Root(const char* from, const char* to) {
    RenameTarget t;
    t.from = from;
    t.to = to;
    return t;
}

RenameTarget StateVarTarget(const char* from, const char* to) {
    RenameTarget t = Root(from, to);
    t.state = true;
    return t;
}

Json ParseText(const char* text) {
    Json j;
    JsonError je;
    REQUIRE_MESSAGE(ParseJson(text, j, &je), je.message);
    return j;
}

Panel ParsePanel(const char* text) {
    Panel p;
    Error e;
    REQUIRE_MESSAGE(PanelFromJson(ParseText(text), p, &e), e.message);
    return p;
}

const Node& N(const Panel& p, const char* id) {
    const Node* n = FindNode(p.root, id);
    REQUIRE_MESSAGE(n != nullptr, id);
    return *n;
}

std::string PropStr(const Panel& p, const char* id, const char* prop) {
    const PropValue* pv = N(p, id).Prop(prop);
    REQUIRE_MESSAGE(pv != nullptr, prop);
    return pv->IsExpr() ? pv->Expr() : pv->raw.AsString("");
}

std::string BindStr(const Panel& p, const char* id, const char* slot) {
    const PropValue* pv = N(p, id).Bind(slot);
    REQUIRE_MESSAGE(pv != nullptr, slot);
    return pv->raw.AsString("");
}

const Action& Act(const Panel& p, const char* id, size_t i) {
    const Node& n = N(p, id);
    REQUIRE(!n.events.empty());
    REQUIRE(i < n.events[0].second.actions.size());
    return n.events[0].second.actions[i];
}

const char* kPanel = R"json({
  "schemaVersion": 1, "kind": "panel", "name": "MainMenu",
  "params": [ { "name": "player", "type": "Player" }, { "name": "cfg", "type": "Settings" },
              { "name": "myplayer", "type": "Player" }, { "name": "player2", "type": "Player" } ],
  "state": [ { "name": "open", "type": "bool", "default": true },
             { "name": "opened", "type": "bool", "default": false },
             { "name": "selectedTab", "type": "int", "default": 0 } ],
  "root": { "id": "window", "type": "Window", "props": { "title": "player {player.name}" },
    "bind": { "open": "state.open" },
    "children": [
      { "id": "btn", "type": "Button",
        "props": { "label": "HP {player.health:%.0f} / {player.max_hp} {{player.x}} {icon:fa-player}" },
        "visibleIf": "cfg.esp.enabled", "enabledIf": "player.health > 0 && cfg.esp.enabled",
        "tooltip": "{myplayer.x} {player2.y} {player.name}",
        "events": { "onClick": { "actions": [
          { "type": "toggle", "target": "cfg.esp.enabled" },
          { "type": "set", "target": "player.health", "value": { "expr": "player.max_hp - myplayer.x" } },
          { "type": "set", "target": "state.selectedTab", "value": 2 },
          { "type": "set", "target": "cfg.name", "value": "player {player.name}" },
          { "type": "copyClipboard", "value": "Pos {player.pos.x}, {state.open}" },
          { "type": "openPopup", "target": "player" },
          { "type": "setTab", "target": "tabs", "value": "player" } ] } },
        "fx": [ { "type": "blur", "radius": { "expr": "player.blur * 2" } } ] },
      { "id": "slider", "type": "SliderFloat", "props": { "label": "Health", "min": { "expr": "player.min_hp" } },
        "bind": { "value": "player.health" } },
      { "id": "slider2", "type": "SliderFloat", "props": { "label": "##v" }, "bind": { "value": "player2.health" } },
      { "id": "txt", "type": "Text", "props": { "text": { "expr": "player.name + \"player.x\"" } },
        "visibleIf": "state.open && !state.opened && myplayer.open" },
      { "id": "combo", "type": "Combo", "props": { "items": [ "{player.name}", "player" ], "label": "Mode" },
        "bind": { "value": "cfg.weapons[player.slot].mode" }, "enabledIf": "cfg.mode == Mode::Aim" },
      { "id": "code", "type": "Code", "props": { "code": "player.health = 0;" } },
      { "id": "inst", "type": "Component", "component": "LabeledSlider",
        "props": { "label": "HP {player.health}", "value": "player.health", "min": { "expr": "player.min_hp" },
                   "max": 100 },
        "slots": { "content": [ { "id": "nested", "type": "Checkbox", "props": { "label": "Alive" },
                                  "bind": { "value": "player.alive" } } ] } },
      { "id": "other", "type": "Component", "component": "Unknown",
        "props": { "value": "player.health", "caption": "player", "title": "HP {player.hp}" } }
    ] }
})json";

const char* kComponent = R"json({
  "schemaVersion": 1, "kind": "component", "name": "LabeledSlider",
  "params": [ { "name": "label", "type": "string", "default": "Value" },
              { "name": "value", "type": "binding<float>" },
              { "name": "min", "type": "float", "default": 0 }, { "name": "max", "type": "float", "default": 1 } ],
  "root": { "id": "row", "type": "HStack" }
})json";

Project MakeProject() {
    Project prj;
    Component c;
    Error e;
    REQUIRE_MESSAGE(ComponentFromJson(ParseText(kComponent), c, &e), e.message);
    prj.components.push_back(c);
    return prj;
}

}  // namespace

TEST_CASE("expression rename respects identifier boundaries") {
    int n = 0;
    CHECK(RenameInExpr("player.health > 0 && cfg.esp.enabled", Root("player", "hero"), &n) ==
          "hero.health > 0 && cfg.esp.enabled");
    CHECK(n == 1);
    CHECK(RenameInExpr("myplayer.x + player2.y + player.z", Root("player", "hero"), &n) == "myplayer.x + player2.y + hero.z");
    CHECK(n == 1);
    CHECK(RenameInExpr("cfg.player.x", Root("player", "hero"), &n) == "cfg.player.x");
    CHECK(n == 0);
    CHECK(RenameInExpr("player.name == \"player.x\"", Root("player", "hero")) == "hero.name == \"player.x\"");
    CHECK(RenameInExpr("inv.items[player.slot].name", Root("player", "hero")) == "inv.items[hero.slot].name");
    CHECK(RenameInExpr("player.items[player.idx]", Root("player", "p"), &n) == "p.items[p.idx]");
    CHECK(n == 2);
    CHECK(RenameInExpr("cfg.mode == Mode::Aim", Root("Mode", "M")) == "cfg.mode == Mode::Aim");
    CHECK(RenameInExpr("player", Root("player", "hero")) == "hero");
    CHECK(RenameInExpr("(player . health)>=1", Root("player", "hero")) == "(hero . health)>=1");
    CHECK(RenameInExpr("player.health >", Root("player", "hero")) == "hero.health >");
    CHECK(RenameInExpr("player.health $ 1", Root("player", "hero"), &n) == "player.health $ 1");
    CHECK(n == 0);
    CHECK(RenameInExpr("player.x", Root("player", "player")) == "player.x");
}

TEST_CASE("state variable rename only touches state.<name>") {
    int n = 0;
    CHECK(RenameInExpr("state.open && !state.opened", StateVarTarget("open", "visible"), &n) == "state.visible && !state.opened");
    CHECK(n == 1);
    CHECK(RenameInExpr("player.open || cfg.state.open || open", StateVarTarget("open", "visible"), &n) ==
          "player.open || cfg.state.open || open");
    CHECK(n == 0);
    CHECK(RenameInExpr("items[state.open]", StateVarTarget("open", "o")) == "items[state.o]");
}

TEST_CASE("interpolation rename keeps text, escapes, formats and icons") {
    int n = 0;
    CHECK(RenameInInterpolation("HP {player.health:%.0f} / {player.max_hp}", Root("player", "hero"), &n) ==
          "HP {hero.health:%.0f} / {hero.max_hp}");
    CHECK(n == 2);
    CHECK(RenameInInterpolation("player: {{player.x}} {icon:fa-player} { player.name }", Root("player", "hero"), &n) ==
          "player: {{player.x}} {icon:fa-player} { hero.name }");
    CHECK(n == 1);
    CHECK(RenameInInterpolation("{Mode::Aim} {myplayer.x}{player2.y}", Root("Mode", "M")) == "{Mode::Aim} {myplayer.x}{player2.y}");
    CHECK(RenameInInterpolation("{state.open ? 1}", StateVarTarget("open", "shown")) == "{state.open ? 1}");
    CHECK(RenameInInterpolation("on={state.open}", StateVarTarget("open", "shown")) == "on={state.shown}");
    CHECK(RenameInInterpolation("unterminated {player.x", Root("player", "hero")) == "unterminated {player.x");
    CHECK(RenameInInterpolation("plain player.x", Root("player", "hero")) == "plain player.x");
}

TEST_CASE("RenameRootInPanel rewrites every reference site") {
    Project prj = MakeProject();
    Panel p = ParsePanel(kPanel);
    int n = RenameRootInPanel(p, "player", "hero", &prj);

    CHECK(p.params[0].name == "hero");
    CHECK(p.params[2].name == "myplayer");
    CHECK(p.params[3].name == "player2");
    CHECK(PropStr(p, "window", "title") == "player {hero.name}");
    CHECK(PropStr(p, "btn", "label") == "HP {hero.health:%.0f} / {hero.max_hp} {{player.x}} {icon:fa-player}");
    CHECK(N(p, "btn").visible_if == "cfg.esp.enabled");
    CHECK(N(p, "btn").enabled_if == "hero.health > 0 && cfg.esp.enabled");
    CHECK(N(p, "btn").tooltip == "{myplayer.x} {player2.y} {hero.name}");
    CHECK(Act(p, "btn", 0).target == "cfg.esp.enabled");
    CHECK(Act(p, "btn", 1).target == "hero.health");
    CHECK(Act(p, "btn", 1).value["expr"].AsString() == "hero.max_hp - myplayer.x");
    CHECK(Act(p, "btn", 3).value.AsString() == "player {hero.name}");
    CHECK(Act(p, "btn", 4).value.AsString() == "Pos {hero.pos.x}, {state.open}");
    CHECK(Act(p, "btn", 5).target == "player");
    CHECK(Act(p, "btn", 6).value.AsString() == "player");
    CHECK(N(p, "btn").fx[0].params["radius"]["expr"].AsString() == "hero.blur * 2");
    CHECK(PropStr(p, "slider", "min") == "hero.min_hp");
    CHECK(PropStr(p, "slider", "label") == "Health");
    CHECK(BindStr(p, "slider", "value") == "hero.health");
    CHECK(BindStr(p, "slider2", "value") == "player2.health");
    CHECK(PropStr(p, "txt", "text") == "hero.name + \"player.x\"");
    CHECK(N(p, "txt").visible_if == "state.open && !state.opened && myplayer.open");
    const PropValue* items = N(p, "combo").Prop("items");
    REQUIRE(items);
    CHECK(items->raw[0].AsString() == "{hero.name}");
    CHECK(items->raw[1].AsString() == "player");
    CHECK(BindStr(p, "combo", "value") == "cfg.weapons[hero.slot].mode");
    CHECK(N(p, "combo").enabled_if == "cfg.mode == Mode::Aim");
    CHECK(PropStr(p, "code", "code") == "player.health = 0;");
    CHECK(PropStr(p, "inst", "label") == "HP {hero.health}");
    CHECK(PropStr(p, "inst", "value") == "hero.health");
    CHECK(PropStr(p, "inst", "min") == "hero.min_hp");
    CHECK(BindStr(p, "nested", "value") == "hero.alive");
    CHECK(PropStr(p, "other", "value") == "hero.health");
    CHECK(PropStr(p, "other", "caption") == "hero");
    CHECK(PropStr(p, "other", "title") == "HP {hero.hp}");
    CHECK(n == 22);
}

TEST_CASE("RenameRootInPanel without project uses catalog and path heuristics") {
    Panel p = ParsePanel(kPanel);
    RenameRootInPanel(p, "player", "hero");
    CHECK(PropStr(p, "inst", "value") == "hero.health");
    CHECK(PropStr(p, "inst", "label") == "HP {hero.health}");
    CHECK(BindStr(p, "slider", "value") == "hero.health");
    CHECK(PropStr(p, "code", "code") == "player.health = 0;");
}

TEST_CASE("component params typed as non-string are left alone") {
    Project prj = MakeProject();
    Panel p = ParsePanel(R"json({ "schemaVersion": 1, "kind": "panel", "name": "P",
      "params": [ { "name": "player", "type": "Player" } ],
      "root": { "id": "w", "type": "Window", "children": [
        { "id": "i", "type": "Component", "component": "LabeledSlider", "props": { "max": "player" } } ] } })json");
    CHECK(RenameRootInPanel(p, "player", "hero", &prj) == 0);
    CHECK(PropStr(p, "i", "max") == "player");
}

TEST_CASE("RenameStateVarInPanel renames declarations and state paths") {
    Project prj = MakeProject();
    Panel p = ParsePanel(kPanel);
    int n = RenameStateVarInPanel(p, "open", "visible", &prj);
    CHECK(p.state[0].name == "visible");
    CHECK(p.state[1].name == "opened");
    CHECK(BindStr(p, "window", "open") == "state.visible");
    CHECK(N(p, "txt").visible_if == "state.visible && !state.opened && myplayer.open");
    CHECK(Act(p, "btn", 4).value.AsString() == "Pos {player.pos.x}, {state.visible}");
    CHECK(n == 3);
    CHECK(RenameStateVarInPanel(p, "selectedTab", "tab", &prj) == 1);
    CHECK(Act(p, "btn", 2).target == "state.tab");
}

TEST_CASE("CollectPathRefs reports roots, state members and locations") {
    Project prj = MakeProject();
    Panel p = ParsePanel(kPanel);
    std::vector<PathRef> refs = CollectPathRefs(p, &prj);
    int player = 0, open_refs = 0, myplayer = 0;
    bool slider_bind = false;
    for (const PathRef& r : refs) {
        if (r.root == "player") ++player;
        if (r.root == "myplayer") ++myplayer;
        if (r.root == "state" && r.member == "open") ++open_refs;
        if (r.node_id == "slider" && r.where == "bind.value" && r.root == "player") slider_bind = true;
    }
    CHECK(player == 22);
    CHECK(myplayer == 3);
    CHECK(open_refs == 3);
    CHECK(slider_bind);
}

TEST_CASE("signature preview follows codegen naming settings") {
    Panel p = ParsePanel(kPanel);
    CodegenSettings s;
    std::string sig = PanelSignaturePreview(p, s);
    CHECK(sig.find("namespace ui {") != std::string::npos);
    CHECK(sig.find("struct MainMenuState {") != std::string::npos);
    CHECK(sig.find("    bool open = true;") != std::string::npos);
    CHECK(sig.find("int selected_tab = 0;") != std::string::npos);
    CHECK(sig.find("void DrawMainMenu(MainMenuState& state, Player& player, Settings& cfg, Player& myplayer, Player& player2);") !=
          std::string::npos);
    s.function_case = "snake";
    s.variable_case = "camel";
    s.nmspace.clear();
    s.dpi_mode = "variable";
    sig = PanelSignaturePreview(p, s);
    CHECK(sig.find("namespace") == std::string::npos);
    CHECK(sig.find("void draw_main_menu(MainMenuState& state") != std::string::npos);
    CHECK(sig.find("float imb_scale);") != std::string::npos);
    CHECK(sig.find("int selectedTab = 0;") != std::string::npos);
    CHECK(ApplyNameCase("selected_tab", "pascal") == "SelectedTab");
    CHECK(ApplyNameCase("HTTPServer", "snake") == "http_server");
}
