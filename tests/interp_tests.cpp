#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include <cmath>
#include <string>
#include <vector>

#include "imgui.h"
#include "imforge/imforge_interp.h"
#include "imgui_headless.h"

struct Esp {
    bool enabled = false;
    float range = 100.0f;
};
IMFORGE_REFLECT(Esp, enabled, range);

struct Settings {
    Esp esp;
    int mode = 1;
    std::vector<float> items = {1, 2, 3};
    std::string name = "cfg";
    long long big = 7;
};
IMFORGE_REFLECT(Settings, esp, mode, items, name, big);

struct Player {
    float health = 42.0f;
    int level = 1;
    ImVec2 pos = ImVec2(1, 2);
    bool alive = true;
};
IMFORGE_REFLECT(Player, health, level, pos, alive);

enum class Mode { Off = 0, Legit = 1, Rage = 2 };
IMFORGE_REFLECT_ENUM(Mode, Off, Legit, Rage);
namespace game {
enum class Tier { Low = 1, High = 5 };
}
IMFORGE_REFLECT_ENUM(game::Tier, Low, High);
struct Loadout {
    Mode mode = Mode::Legit;
    game::Tier tier = game::Tier::High;
};
IMFORGE_REFLECT(Loadout, mode, tier);

using namespace imforge;

namespace {

int g_handler_calls = 0;
void CountHandler(DrawCtx&) { ++g_handler_calls; }
std::string g_probe_text;
bool ProbeWidget(DrawCtx& ctx) {
    g_probe_text = ctx.GetString("text");
    ImGui::TextUnformatted(g_probe_text.c_str());
    return false;
}
int g_card_begin = 0, g_card_end = 0;
bool CardBegin(DrawCtx& ctx) {
    ++g_card_begin;
    ImGui::BeginChild(ctx.node.id.c_str(), ImVec2(0, 60));
    return true;
}
void CardEnd(DrawCtx&) {
    ++g_card_end;
    ImGui::EndChild();
}
int g_block_calls = 0;
void CodeBlock(DrawCtx&) {
    ++g_block_calls;
    ImGui::Dummy(ImVec2(10, 10));
}

struct Item {
    std::string id, type;
    ImVec2 min, max;
    bool hovered;
};

Json ParseJ(const char* text) {
    Json j;
    JsonError e;
    REQUIRE_MESSAGE(ParseJson(text, j, &e), e.message << " at " << e.line << ":" << e.col);
    return j;
}

Panel MakePanel(const char* text) {
    Panel p;
    Error e;
    REQUIRE_MESSAGE(PanelFromJson(ParseJ(text), p, &e), e.message);
    return p;
}

Component MakeComponent(const char* text) {
    Component c;
    Error e;
    REQUIRE_MESSAGE(ComponentFromJson(ParseJ(text), c, &e), e.message);
    return c;
}

struct Harness {
    test::HeadlessImGui gui;
    Interpreter interp;
    Panel panel;
    PanelInstance inst;
    std::vector<BindingRoot> roots;
    InterpOptions opts;
    Settings cfg;
    Player player;
    std::vector<Item> items;
    std::vector<std::string> clicks;
    bool design = false;

    explicit Harness(const char* panel_json, bool with_roots = true) : panel(MakePanel(panel_json)) {
        if (with_roots) {
            roots.push_back(BindingRoot{"cfg", &cfg, StructInfoOf<Settings>()});
            roots.push_back(BindingRoot{"player", &player, StructInfoOf<Player>()});
        }
        interp.hooks.on_item = [this](const Node& n, const ImVec2& mn, const ImVec2& mx, bool hovered) {
            items.push_back(Item{n.id, n.type, mn, mx, hovered});
        };
        interp.hooks.on_click = [this](const Node& n) { clicks.push_back(n.id); };
#if IMGUI_VERSION_NUM >= 18700
        ImGui::GetIO().ConfigInputTrickleEventQueue = false;
#endif
    }

    void Mouse(ImVec2 pos, bool down) {
        ImGuiIO& io = ImGui::GetIO();
#if IMGUI_VERSION_NUM >= 18700
        io.AddMousePosEvent(pos.x, pos.y);
        io.AddMouseButtonEvent(0, down);
#else
        io.MousePos = pos;
        io.MouseDown[0] = down;
#endif
    }

    void Frame() {
        items.clear();
        gui.BeginFrame();
        if (design) {
            opts.design_mode = true;
            ImGui::SetNextWindowPos(ImVec2(0, 0));
            ImGui::SetNextWindowSize(ImVec2(800, 600));
            ImGui::Begin("Builder", nullptr, ImGuiWindowFlags_NoSavedSettings);
            ImGui::BeginChild("canvas", ImVec2(600, 400));
            interp.DrawPanel(panel, inst, roots, opts);
            ImGui::EndChild();
            ImGui::End();
        } else {
            interp.DrawPanel(panel, inst, roots, opts);
        }
        gui.EndFrame();
    }

    void Frames(int n) {
        for (int i = 0; i < n; ++i) Frame();
    }

    const Item* Find(const char* id) const {
        for (const Item& it : items)
            if (it.id == id) return &it;
        return nullptr;
    }

    int Count(const char* id) const {
        int n = 0;
        for (const Item& it : items) n += it.id == id ? 1 : 0;
        return n;
    }

    ImVec2 Center(const char* id) const {
        const Item* it = Find(id);
        REQUIRE_MESSAGE(it != nullptr, "item not drawn: " << id);
        return ImVec2((it->min.x + it->max.x) * 0.5f, (it->min.y + it->max.y) * 0.5f);
    }

    void Click(const char* id) {
        ImVec2 c = Center(id);
        Mouse(c, false);
        Frame();
        Mouse(c, true);
        Frame();
        Mouse(c, false);
        Frame();
    }

    void ClickAt(ImVec2 c) {
        Mouse(c, false);
        Frame();
        Mouse(c, true);
        Frame();
        Mouse(c, false);
        Frame();
    }

    bool HasDiag(Diagnostic::Level lvl, const char* node_id, const char* needle) const {
        for (const Diagnostic& d : interp.last_diagnostics)
            if (d.level == lvl && (!node_id || d.node_id == node_id) && d.message.find(needle) != std::string::npos)
                return true;
        return false;
    }
};

const char* kWindowHead = R"json({
  "schemaVersion": 1, "kind": "panel", "name": "Main",
  "params": [ { "name": "player", "type": "Player" }, { "name": "cfg", "type": "Settings" } ],
  "state": [ { "name": "counter", "type": "int", "default": 3 },
             { "name": "open", "type": "bool", "default": true },
             { "name": "vol", "type": "float", "default": 0.25 },
             { "name": "msg", "type": "string", "default": "hi" } ],
  "root": { "id": "window", "type": "Window",
            "props": { "title": "Main Menu", "initialSize": {"x": 600, "y": 400}, "initialPos": {"x": 0, "y": 0}, "sizeCond": "Always" },
            "children": )json";

std::string WindowPanel(const char* children) { return std::string(kWindowHead) + children + " } }"; }

}  // namespace

TEST_CASE("checkbox bound through a reflected struct toggles the real bool on click") {
    std::string json = WindowPanel(R"json([
        { "id": "chk", "type": "Checkbox", "props": { "label": "ESP" }, "bind": { "value": "cfg.esp.enabled" } },
        { "id": "txt", "type": "Text", "props": { "text": "HP: {player.health:%.0f} lvl {player.level}" } }
    ])json");
    Harness h(json.c_str());
    h.Frames(2);
    REQUIRE(h.Find("chk") != nullptr);
    CHECK(h.Find("window") != nullptr);
    CHECK(h.cfg.esp.enabled == false);
    h.Click("chk");
    CHECK(h.cfg.esp.enabled == true);
    h.Click("chk");
    CHECK(h.cfg.esp.enabled == false);
    CHECK(h.interp.last_diagnostics.empty());
}

TEST_CASE("visibleIf and enabledIf follow bound values") {
    std::string json = WindowPanel(R"json([
        { "id": "chk", "type": "Checkbox", "props": { "label": "ESP" }, "bind": { "value": "cfg.esp.enabled" } },
        { "id": "btn", "type": "Button", "props": { "label": "Only when esp" }, "visibleIf": "cfg.esp.enabled",
          "events": { "onClick": { "handler": true } } },
        { "id": "dis", "type": "Button", "props": { "label": "Disabled" }, "enabledIf": "player.health > 100",
          "events": { "onClick": { "handler": true } } }
    ])json");
    Harness h(json.c_str());
    g_handler_calls = 0;
    h.interp.RegisterHandler("Main.btn.onClick", &CountHandler);
    h.interp.RegisterHandler("Main.dis.onClick", &CountHandler);
    h.Frames(2);
    CHECK(h.Count("btn") == 0);
    CHECK(h.Count("dis") == 1);
    h.cfg.esp.enabled = true;
    h.Frames(2);
    CHECK(h.Count("btn") == 1);
    h.Click("btn");
    CHECK(g_handler_calls == 1);
    h.Click("dis");
    CHECK(g_handler_calls == 1);
    h.player.health = 150;
    h.Frames(1);
    h.Click("dis");
    CHECK(g_handler_calls == 2);
}

TEST_CASE("state vars initialise from defaults and actions set/toggle them") {
    std::string json = WindowPanel(R"json([
        { "id": "probe", "type": "Probe", "props": { "text": "c={state.counter} m={state.msg} o={state.open}" } },
        { "id": "inc", "type": "Button", "props": { "label": "inc" },
          "events": { "onClick": { "actions": [
              { "type": "set", "target": "state.counter", "value": { "expr": "state.counter + 2" } },
              { "type": "toggle", "target": "state.open" },
              { "type": "set", "target": "state.msg", "value": "lvl {player.level}" },
              { "type": "set", "target": "player.level", "value": 9 },
              { "type": "toggle", "target": "cfg.esp.enabled" },
              { "type": "set", "target": "cfg.big", "value": 123 } ] } } }
    ])json");
    Harness h(json.c_str());
    h.interp.RegisterWidget("Probe", &ProbeWidget);
    h.Frames(2);
    CHECK(g_probe_text == "c=3 m=hi o=true");
    REQUIRE(h.inst.State("counter") != nullptr);
    CHECK(h.inst.State("counter")->ToInt() == 3);
    h.Click("inc");
    CHECK(h.inst.State("counter")->ToInt() == 5);
    CHECK(h.inst.State("open")->ToBool() == false);
    CHECK(h.player.level == 9);
    CHECK(h.cfg.esp.enabled == true);
    CHECK(h.cfg.big == 123);
    h.Frame();
    CHECK(g_probe_text == "c=5 m=lvl 1 o=false");
    CHECK(h.interp.last_diagnostics.empty());
}

TEST_CASE("HStack distributes fill widths and respects px and dpi scale") {
    std::string json = WindowPanel(R"json([
        { "id": "row", "type": "HStack", "props": { "spacing": 4 }, "children": [
            { "id": "a", "type": "Button", "props": { "label": "A", "size": { "w": "fill", "h": "auto" } } },
            { "id": "b", "type": "Button", "props": { "label": "B", "size": { "w": 100, "h": "auto" } } },
            { "id": "c", "type": "Button", "props": { "label": "C", "size": { "w": "fill", "h": "auto" } } } ] }
    ])json");
    Harness h(json.c_str());
    h.opts.dpi_scale = 2.0f;
    h.Frames(2);
    const Item* a = h.Find("a");
    const Item* b = h.Find("b");
    const Item* c = h.Find("c");
    const Item* row = h.Find("row");
    REQUIRE((a && b && c && row));
    float wa = a->max.x - a->min.x, wb = b->max.x - b->min.x, wc = c->max.x - c->min.x;
    CHECK(wb == doctest::Approx(200.0f));
    CHECK(wa == doctest::Approx(wc).epsilon(0.01));
    CHECK(wa > 50.0f);
    CHECK(a->min.y == doctest::Approx(b->min.y));
    CHECK(b->min.x > a->max.x);
    CHECK(c->min.x > b->max.x);
    float total = wa + wb + wc + 2 * 4.0f * 2.0f;
    CHECK(total == doctest::Approx(row->max.x - row->min.x).epsilon(0.02));
}

TEST_CASE("Grid places children in cells row-major") {
    std::string json = WindowPanel(R"json([
        { "id": "grid", "type": "Grid", "props": { "columns": 2, "headers": ["L", "R"], "columnWidths": [100, "fill"] },
          "children": [
            { "id": "c0", "type": "Text", "props": { "text": "0" } },
            { "id": "c1", "type": "Text", "props": { "text": "1" } },
            { "id": "c2", "type": "Text", "props": { "text": "2" } },
            { "id": "c3", "type": "Text", "props": { "text": "3" } } ] }
    ])json");
    Harness h(json.c_str());
    h.Frames(2);
    const Item *c0 = h.Find("c0"), *c1 = h.Find("c1"), *c2 = h.Find("c2"), *c3 = h.Find("c3");
    REQUIRE((c0 && c1 && c2 && c3));
    CHECK(c0->min.y == doctest::Approx(c1->min.y));
    CHECK(c1->min.x > c0->min.x);
    CHECK(c2->min.y > c0->min.y);
    CHECK(c2->min.x == doctest::Approx(c0->min.x));
    CHECK(c3->min.x == doctest::Approx(c1->min.x));
    CHECK(h.Find("grid") != nullptr);
}

TEST_CASE("component expansion substitutes params, binds through binding params and fills slots") {
    const char* comp_json = R"json({
      "schemaVersion": 1, "kind": "component", "name": "LabeledToggle",
      "params": [ { "name": "label", "type": "string", "default": "Value" },
                  { "name": "value", "type": "binding<bool>" },
                  { "name": "suffix", "type": "string", "default": "!" } ],
      "slots": [ "content" ],
      "root": { "id": "row", "type": "HStack", "children": [
         { "id": "lbl", "type": "Probe", "props": { "text": { "param": "label" } } },
         { "id": "chk", "type": "Checkbox", "props": { "label": "##v" }, "bind": { "value": { "param": "value" } } },
         { "id": "slot_content", "type": "Slot", "props": { "name": "content" } } ] }
    })json";
    std::string json = WindowPanel(R"json([
        { "id": "inst1", "type": "Component", "component": "LabeledToggle",
          "props": { "label": "Speed {player.level}", "value": "player.alive" },
          "slots": { "content": [ { "id": "hint", "type": "Text", "props": { "text": "lvl {player.level}" } } ] } },
        { "id": "inst2", "type": "Component", "component": "LabeledToggle",
          "props": { "value": "cfg.esp.enabled" } }
    ])json");
    Project project;
    project.components.push_back(MakeComponent(comp_json));
    Harness h(json.c_str());
    h.interp.project = &project;
    h.interp.RegisterWidget("Probe", &ProbeWidget);
    h.Frames(2);
    CHECK(h.Count("chk") == 2);
    CHECK(h.Count("hint") == 1);
    CHECK(h.Count("lbl") == 2);
    CHECK(g_probe_text == "Value");
    CHECK(h.player.alive == true);
    const Item* first_chk = h.Find("chk");
    REQUIRE(first_chk);
    ImVec2 c((first_chk->min.x + first_chk->max.x) * 0.5f, (first_chk->min.y + first_chk->max.y) * 0.5f);
    h.ClickAt(c);
    CHECK(h.player.alive == false);
    CHECK(h.cfg.esp.enabled == false);
    CHECK(h.interp.last_diagnostics.empty());
}

TEST_CASE("openPopup, closePopup and setTab actions") {
    std::string json = WindowPanel(R"json([
        { "id": "tabs", "type": "TabBar", "children": [
            { "id": "t1", "type": "Tab", "props": { "label": "One" }, "children": [
                { "id": "go2", "type": "Button", "props": { "label": "go" },
                  "events": { "onClick": { "actions": [ { "type": "setTab", "target": "tabs", "value": "t2" } ] } } } ] },
            { "id": "t2", "type": "Tab", "props": { "label": "Two" }, "children": [
                { "id": "in2", "type": "Text", "props": { "text": "two" } } ] } ] },
        { "id": "open", "type": "Button", "props": { "label": "open" },
          "events": { "onClick": { "actions": [ { "type": "openPopup", "target": "pop" } ] } } },
        { "id": "pop", "type": "Popup", "children": [
            { "id": "inpop", "type": "Text", "props": { "text": "popup" } },
            { "id": "close", "type": "Button", "props": { "label": "close" },
              "events": { "onClick": { "actions": [ { "type": "closePopup" } ] } } } ] }
    ])json");
    Harness h(json.c_str());
    h.Frames(2);
    CHECK(h.Count("go2") == 1);
    CHECK(h.Count("in2") == 0);
    h.Click("go2");
    h.Frames(2);
    CHECK(h.Count("in2") == 1);
    CHECK(h.Count("go2") == 0);
    CHECK(h.Count("inpop") == 0);
    h.Click("open");
    h.Frames(2);
    CHECK(h.Count("inpop") == 1);
    h.Click("close");
    h.Frames(2);
    CHECK(h.Count("inpop") == 0);
}

TEST_CASE("design mode uses mock storage shared between bindings and expressions") {
    std::string json = WindowPanel(R"json([
        { "id": "chk", "type": "Checkbox", "props": { "label": "ESP" }, "bind": { "value": "cfg.esp.enabled" } },
        { "id": "btn", "type": "Button", "props": { "label": "B" }, "visibleIf": "cfg.esp.enabled",
          "events": { "onClick": { "handler": true } } },
        { "id": "sl", "type": "SliderFloat", "props": { "label": "S" } },
        { "id": "plot", "type": "PlotLines", "bind": { "values": "cfg.items" } },
        { "id": "code", "type": "Code", "props": { "code": "ImGui::Text(\"x\");" } }
    ])json");
    Harness h(json.c_str(), false);
    h.design = true;
    g_handler_calls = 0;
    h.interp.RegisterHandler("Main.btn.onClick", &CountHandler);
    h.Frames(2);
    CHECK(h.Count("btn") == 0);
    CHECK(h.Count("sl") == 1);
    CHECK(h.Count("plot") == 1);
    CHECK(h.Count("code") == 1);
    CHECK(h.Find("window") != nullptr);
    REQUIRE(h.inst.Mock("cfg.esp.enabled") != nullptr);
    CHECK(h.inst.Mock("cfg.esp.enabled")->ToBool() == false);
    h.Click("chk");
    CHECK(h.inst.Mock("cfg.esp.enabled")->ToBool() == true);
    CHECK(h.clicks.size() >= 1);
    CHECK(h.clicks.front() == "chk");
    h.Frames(2);
    CHECK(h.Count("btn") == 1);
    h.Click("btn");
    CHECK(g_handler_calls == 0);
    CHECK(h.clicks.back() == "btn");
    CHECK(h.interp.last_diagnostics.empty());
}

TEST_CASE("float state bound to a slider round-trips through the shadow storage") {
    std::string json = WindowPanel(R"json([
        { "id": "sl", "type": "SliderFloat", "props": { "label": "##vol", "min": 0, "max": 1 }, "bind": { "value": "state.vol" } },
        { "id": "di", "type": "DragInt", "props": { "label": "Big" }, "bind": { "value": "cfg.big" } }
    ])json");
    Harness h(json.c_str());
    h.Frames(2);
    REQUIRE(h.Find("sl"));
    CHECK(h.inst.State("vol")->ToDouble() == doctest::Approx(0.25));
    ImVec2 c = h.Center("sl");
    h.Mouse(c, false);
    h.Frame();
    h.Mouse(c, true);
    h.Frame();
    h.Frame();
    h.Mouse(c, false);
    h.Frame();
    double v = h.inst.State("vol")->ToDouble();
    CHECK(v > 0.3);
    CHECK(v < 0.7);
    CHECK(h.cfg.big == 7);
    CHECK(h.interp.last_diagnostics.empty());
}

TEST_CASE("diagnostics are collected once per frame for unknown types, props and bindings") {
    std::string json = WindowPanel(R"json([
        { "id": "bogus", "type": "Bogus" },
        { "id": "bad", "type": "Checkbox", "props": { "label": "x" }, "bind": { "value": "player.nothing" } },
        { "id": "flag", "type": "Button", "props": { "label": "f", "size": { "w": 10, "h": 10 } }, "style": { "classes": ["nope"] } },
        { "id": "ex", "type": "Text", "props": { "text": "{player.health +}" } }
    ])json");
    Harness h(json.c_str());
    h.Frames(3);
    CHECK(h.HasDiag(Diagnostic::Level::Error, "bogus", "unknown widget type"));
    CHECK(h.HasDiag(Diagnostic::Level::Error, "bad", "cannot resolve binding"));
    CHECK(h.HasDiag(Diagnostic::Level::Warning, "flag", "unknown style class"));
    CHECK(h.HasDiag(Diagnostic::Level::Error, "ex", "expression"));
    int bogus = 0;
    for (const Diagnostic& d : h.interp.last_diagnostics) bogus += d.node_id == "bogus" ? 1 : 0;
    CHECK(bogus == 1);
    CHECK(h.Count("bogus") == 1);
    for (const Diagnostic& d : h.interp.last_diagnostics) CHECK(d.panel == "Main");
}

TEST_CASE("code blocks, icons, tooltips, style overrides and transitions draw without diagnostics") {
    std::string json = WindowPanel(R"json([
        { "id": "code", "type": "Code", "props": { "code": "x" } },
        { "id": "probe", "type": "Probe", "props": { "text": "{icon:fa-play} HP {player.health:%.0f}/{cfg.items.size}" } },
        { "id": "btn", "type": "Button", "props": { "label": "Styled" }, "tooltip": "tip {player.level}", "font": "Bold",
          "style": { "colors": { "Button": "#2ecc71ff", "TabActive": "#ff0000ff" }, "vars": { "FrameRounding": 8, "FramePadding": {"x": 6, "y": 3} }, "classes": ["danger"] },
          "anim": { "transitions": [ { "prop": "color.Button", "on": "hover", "to": "#27ae60ff", "duration": 0.15 },
                                     { "prop": "offset.y", "on": "active", "to": 1, "duration": 0.05 },
                                     { "prop": "alpha", "on": "hover", "to": 0.5 } ],
                    "enter": { "preset": "FadeSlideUp", "duration": 0.1 } },
          "fx": [ { "type": "shadow", "size": 12, "color": "#00000080" }, { "type": "border", "style": "Rainbow" }, { "type": "blur", "radius": 8 } ] },
        { "id": "col", "type": "VStack", "props": { "spacing": 2, "indent": 10 }, "anim": { "enter": { "preset": "Fade", "duration": 0.1, "stagger": 0.05 } }, "children": [
            { "id": "s1", "type": "Text", "props": { "text": "one" } },
            { "id": "s2", "type": "Text", "props": { "text": "two" } } ] },
        { "id": "hdr", "type": "CollapsingHeader", "props": { "label": "H", "defaultOpen": true }, "children": [
            { "id": "tree", "type": "TreeNode", "props": { "label": "T", "defaultOpen": true }, "children": [
                { "id": "leaf", "type": "Text", "props": { "text": "leaf" } } ] } ] },
        { "id": "child", "type": "Child", "props": { "size": { "w": "fill", "h": 80 }, "border": true }, "children": [
            { "id": "grp", "type": "Group", "children": [ { "id": "gt", "type": "Text", "props": { "text": "g" } } ] } ] }
    ])json");
    Project project;
    StyleOverride danger;
    danger.colors.emplace_back("Button", Vec4{0.75f, 0.22f, 0.17f, 1});
    danger.vars.emplace_back("FrameRounding", Json(6));
    project.style_classes.emplace_back("danger", danger);
    Harness h(json.c_str());
    h.interp.project = &project;
    h.interp.RegisterWidget("Probe", &ProbeWidget);
    h.interp.RegisterBlock("Main.code", &CodeBlock);
    h.interp.hooks.icon = [](const std::string& name) { return name == "fa-play" ? ">" : ""; };
    h.interp.hooks.font = [](const std::string&) { return ImGui::GetIO().Fonts->Fonts[0]; };
    g_block_calls = 0;
    h.Frames(3);
    CHECK(g_block_calls == 3);
    CHECK(g_probe_text == "> HP 42/3");
    CHECK(h.Count("btn") == 1);
    CHECK(h.Count("s1") == 1);
    CHECK(h.Count("s2") == 1);
    CHECK(h.Count("leaf") == 1);
    CHECK(h.Count("gt") == 1);
    CHECK(h.Count("code") == 1);
    const Item* btn = h.Find("btn");
    REQUIRE(btn);
    ImVec2 c((btn->min.x + btn->max.x) * 0.5f, (btn->min.y + btn->max.y) * 0.5f);
    h.Mouse(c, false);
    h.Frames(3);
    CHECK(h.Find("btn")->hovered);
    h.Mouse(c, true);
    h.Frames(2);
    h.Mouse(c, false);
    h.Frames(2);
    CHECK(h.Count("btn") == 1);
    for (const Diagnostic& d : h.interp.last_diagnostics) MESSAGE(d.node_id << ": " << d.message);
    CHECK(h.interp.last_diagnostics.empty());
}

TEST_CASE("Canvas anchors children and Window open binding closes the window") {
    std::string json = WindowPanel(R"json([
        { "id": "cv", "type": "Canvas", "props": { "size": { "w": 300, "h": 200 }, "border": true }, "children": [
            { "id": "tl", "type": "Button", "props": { "label": "TL", "size": { "w": 50, "h": 20 } } },
            { "id": "br", "type": "Button", "props": { "label": "BR", "size": { "w": 50, "h": 20 } },
              "layout": { "anchor": "BottomRight", "offset": { "x": -10, "y": -10 } } },
            { "id": "ce", "type": "Button", "props": { "label": "C", "size": { "w": 50, "h": 20 } },
              "layout": { "anchor": "Center" } } ] }
    ])json");
    Harness h(json.c_str());
    h.Frames(2);
    const Item *cv = h.Find("cv"), *tl = h.Find("tl"), *br = h.Find("br"), *ce = h.Find("ce");
    REQUIRE((cv && tl && br && ce));
    CHECK(br->max.x == doctest::Approx(cv->max.x - 10).epsilon(0.05));
    CHECK(br->max.y == doctest::Approx(cv->max.y - 10).epsilon(0.05));
    CHECK(tl->min.x < br->min.x);
    CHECK(ce->min.x > tl->min.x);
    CHECK(ce->min.x < br->min.x);
    CHECK(h.interp.last_diagnostics.empty());

    std::string json2 = std::string(kWindowHead) + "[]" + R"json( , "bind": { "open": "state.open" } } })json";
    Harness h2(json2.c_str());
    h2.Frames(2);
    CHECK(h2.Find("window") != nullptr);
    h2.inst.State("open")->b = false;
    h2.Frames(2);
    CHECK(h2.Find("window") == nullptr);
}

TEST_CASE("theme application writes style colors and vars") {
    test::HeadlessImGui gui;
    Interpreter interp;
    Theme t;
    t.colors.emplace_back("Button", Vec4{0.1f, 0.2f, 0.3f, 1});
    t.colors.emplace_back("TabActive", Vec4{0.5f, 0.5f, 0.5f, 1});
    t.vars.emplace_back("FrameRounding", Json(7));
    Json pad = Json::Object();
    pad.Set("x", Json(9));
    pad.Set("y", Json(4));
    t.vars.emplace_back("FramePadding", pad);
    interp.ApplyTheme(t);
    ImGuiStyle& st = ImGui::GetStyle();
    CHECK(st.Colors[ImGuiCol_Button].x == doctest::Approx(0.1f));
    CHECK(st.FrameRounding == doctest::Approx(7));
    CHECK(st.FramePadding.x == doctest::Approx(9));
#if IMGUI_VERSION_NUM >= 19090
    CHECK(st.Colors[ImGuiCol_TabSelected].x == doctest::Approx(0.5f));
#else
    CHECK(st.Colors[ImGuiCol_TabActive].x == doctest::Approx(0.5f));
#endif
}

TEST_CASE("fx inside table cells and closable modals with closePopup") {
    std::string json = WindowPanel(R"json([
        { "id": "grid", "type": "Grid", "props": { "columns": 2 }, "fx": [ { "type": "gradient" } ], "children": [
            { "id": "g0", "type": "Button", "props": { "label": "0" }, "fx": [ { "type": "shadow" }, { "type": "glow" }, { "type": "particles", "count": 4 } ] },
            { "id": "g1", "type": "Child", "props": { "size": { "w": "fill", "h": 40 } }, "fx": [ { "type": "noise" }, { "type": "cursorHighlight" } ], "children": [
                { "id": "g1t", "type": "Text", "props": { "text": "t" }, "fx": [ { "type": "radialGradient" }, { "type": "bogusFx" } ] } ] } ] },
        { "id": "show", "type": "Button", "props": { "label": "show" },
          "events": { "onClick": { "actions": [ { "type": "openPopup", "target": "modal" } ] } } },
        { "id": "modal", "type": "Modal", "props": { "title": "M", "closable": true }, "children": [
            { "id": "inm", "type": "Text", "props": { "text": "modal" } },
            { "id": "ok", "type": "Button", "props": { "label": "ok" },
              "events": { "onClick": { "actions": [ { "type": "closePopup" } ] } } } ] },
        { "id": "menubar", "type": "MenuBar", "children": [
            { "id": "menu", "type": "Menu", "props": { "label": "File" }, "children": [
                { "id": "mi", "type": "MenuItem", "props": { "label": "Quit" } } ] } ] }
    ])json");
    Harness h(json.c_str());
    h.Frames(3);
    CHECK(h.Count("g0") == 1);
    CHECK(h.Count("g1t") == 1);
    CHECK(h.Count("menu") == 1);
    CHECK(h.HasDiag(Diagnostic::Level::Warning, "g1t", "unknown fx type"));
    CHECK(h.Count("inm") == 0);
    h.Click("show");
    h.Frames(2);
    CHECK(h.Count("inm") == 1);
    h.Click("ok");
    h.Frames(2);
    CHECK(h.Count("inm") == 0);
}

namespace {
int g_dlg_open = 0, g_dlg_close = 0, g_tab_open = 0, g_tab_close = 0;
void DlgOpen(DrawCtx&) { ++g_dlg_open; }
void DlgClose(DrawCtx&) { ++g_dlg_close; }
void TabOpen(DrawCtx&) { ++g_tab_open; }
void TabClose(DrawCtx&) { ++g_tab_close; }
}  // namespace

TEST_CASE("bound Modal opens from its bool, syncs the bool on close and fires onOpen/onClose once per transition") {
    std::string json = WindowPanel(R"json([
        { "id": "show", "type": "Button", "props": { "label": "show" },
          "events": { "onClick": { "actions": [ { "type": "openPopup", "target": "dlg" } ] } } },
        { "id": "dlg", "type": "Modal", "props": { "title": "Dialog" }, "bind": { "open": "cfg.esp.enabled" },
          "events": { "onOpen": { "handler": true }, "onClose": { "handler": true } }, "children": [
            { "id": "inm", "type": "Text", "props": { "text": "inside" } },
            { "id": "ok", "type": "Button", "props": { "label": "ok" },
              "events": { "onClick": { "actions": [ { "type": "closePopup" } ] } } } ] },
        { "id": "tabs", "type": "TabBar", "children": [
            { "id": "t1", "type": "Tab", "props": { "label": "One" }, "bind": { "open": "player.alive" },
              "events": { "onOpen": { "handler": true }, "onClose": { "handler": true } } },
            { "id": "t2", "type": "Tab", "props": { "label": "Two" } } ] }
    ])json");
    Harness h(json.c_str());
    g_dlg_open = g_dlg_close = g_tab_open = g_tab_close = 0;
    h.interp.RegisterHandler("Main.dlg.onOpen", &DlgOpen);
    h.interp.RegisterHandler("Main.dlg.onClose", &DlgClose);
    h.interp.RegisterHandler("Main.t1.onOpen", &TabOpen);
    h.interp.RegisterHandler("Main.t1.onClose", &TabClose);
    h.Frames(3);
    CHECK(h.Count("inm") == 0);
    CHECK(g_dlg_open == 0);
    CHECK(g_dlg_close == 0);

    h.cfg.esp.enabled = true;
    h.Frame();
    CHECK(h.Count("inm") == 1);
    CHECK(g_dlg_open == 1);
    h.Frames(3);
    CHECK(h.Count("inm") == 1);
    CHECK(h.cfg.esp.enabled);
    CHECK(g_dlg_open == 1);
    CHECK(g_dlg_close == 0);

    h.Click("ok");
    CHECK_FALSE(h.cfg.esp.enabled);
    CHECK(g_dlg_close == 1);
    h.Frames(3);
    CHECK(h.Count("inm") == 0);
    CHECK_FALSE(h.cfg.esp.enabled);
    CHECK(g_dlg_open == 1);
    CHECK(g_dlg_close == 1);

    h.cfg.esp.enabled = true;
    h.Frame();
    CHECK(h.Count("inm") == 1);
    CHECK(g_dlg_open == 2);
    h.cfg.esp.enabled = false;
    h.Frame();
    CHECK(h.Count("inm") == 0);
    CHECK(g_dlg_close == 2);
    h.Frames(2);
    CHECK(h.Count("inm") == 0);
    CHECK(g_dlg_open == 2);
    CHECK(g_dlg_close == 2);

    h.Click("show");
    h.Frame();
    CHECK(h.cfg.esp.enabled);
    CHECK(h.Count("inm") == 1);
    CHECK(g_dlg_open == 3);
    CHECK(g_dlg_close == 2);

    CHECK(g_tab_open == 0);
    CHECK(g_tab_close == 0);
    h.player.alive = false;
    h.Frames(3);
    CHECK(h.Count("t1") == 0);
    CHECK(g_tab_close == 1);
    h.player.alive = true;
    h.Frames(3);
    CHECK(h.Count("t1") == 1);
    CHECK(g_tab_open == 0);
    CHECK(g_tab_close == 1);
}

TEST_CASE("custom container widgets with an end function draw their children between begin and end") {
    std::string json = WindowPanel(R"json([
        { "id": "card", "type": "Card", "children": [
            { "id": "inner", "type": "Text", "props": { "text": "inside" } } ] },
        { "id": "hdr", "type": "CollapsingHeader", "props": { "label": "H" }, "children": [
            { "id": "leaf", "type": "Text", "props": { "text": "leaf" } } ] },
        { "id": "ghost", "type": "Text", "props": { "text": "ghost" }, "visibleIf": "false" }
    ])json");
    Harness h(json.c_str());
    h.interp.RegisterWidget("Card", &CardBegin, &CardEnd);
    g_card_begin = g_card_end = 0;
    h.Frames(2);
    CHECK(g_card_begin == 2);
    CHECK(g_card_end == 2);
    CHECK(h.Count("inner") == 1);
    CHECK(h.Count("card") == 1);
    CHECK(h.Count("leaf") == 0);
    CHECK(h.Count("ghost") == 0);
    h.interp.hooks.force_trigger = [](const Node& n, const char* trigger, bool& forced) {
        if (n.id == "hdr" && std::string(trigger) == "open") return forced = true, true;
        if (n.id == "ghost" && std::string(trigger) == "visible") return forced = true, true;
        return false;
    };
    h.Frames(2);
    CHECK(h.Count("leaf") == 1);
    CHECK(h.Count("ghost") == 1);
    CHECK(h.interp.last_diagnostics.empty());
}

TEST_CASE("HStack keeps its row when the first child is hidden and Grid clamps the column count") {
    std::string json = WindowPanel(R"json([
        { "id": "before", "type": "Text", "props": { "text": "above" } },
        { "id": "row", "type": "HStack", "children": [
            { "id": "h0", "type": "Button", "props": { "label": "hidden" }, "visibleIf": "false" },
            { "id": "h1", "type": "Button", "props": { "label": "B" } },
            { "id": "h2", "type": "Button", "props": { "label": "C" } } ] },
        { "id": "grid", "type": "Grid", "props": { "columns": 1000 }, "children": [
            { "id": "g0", "type": "Text", "props": { "text": "0" } },
            { "id": "g1", "type": "Text", "props": { "text": "1" } } ] }
    ])json");
    Harness h(json.c_str());
    h.Frames(2);
    const Item *before = h.Find("before"), *row = h.Find("row"), *h1 = h.Find("h1"), *h2 = h.Find("h2");
    REQUIRE((before && row && h1 && h2));
    CHECK(h.Count("h0") == 0);
    CHECK(h1->min.y >= before->max.y);
    CHECK(h1->min.y == doctest::Approx(row->min.y));
    CHECK(h1->min.x == doctest::Approx(row->min.x));
    CHECK(h2->min.y == doctest::Approx(h1->min.y));
    CHECK(h2->min.x > h1->max.x);
    CHECK(h.Count("g0") == 1);
    CHECK(h.Count("g1") == 1);
    CHECK(h.HasDiag(Diagnostic::Level::Warning, "grid", "columns clamped"));
}

TEST_CASE("TreeNode with NoTreePushOnOpen and Canvas with a hidden last child stay balanced") {
    std::string json = WindowPanel(R"json([
        { "id": "tn", "type": "TreeNode", "props": { "label": "T", "defaultOpen": true, "flags": ["NoTreePushOnOpen"] }, "children": [
            { "id": "tnc", "type": "Text", "props": { "text": "c" } } ] },
        { "id": "after", "type": "Text", "props": { "text": "after" } },
        { "id": "cv", "type": "Canvas", "props": { "size": { "w": 200, "h": 100 } }, "children": [
            { "id": "vis", "type": "Button", "props": { "label": "V", "size": { "w": 20, "h": 20 } } },
            { "id": "hid", "type": "Button", "props": { "label": "H", "size": { "w": 20, "h": 20 } }, "visibleIf": "false",
              "layout": { "anchor": "BottomRight", "offset": { "x": 50, "y": 50 } } } ] },
        { "id": "plot", "type": "PlotLines", "props": { "label": "##p", "size": { "w": "fill", "h": 40 } } }
    ])json");
    Harness h(json.c_str());
    h.design = true;
    h.Frames(3);
    const Item *tnc = h.Find("tnc"), *after = h.Find("after"), *plot = h.Find("plot");
    REQUIRE((tnc && after && plot));
    CHECK(after->min.x == doctest::Approx(tnc->min.x));
    CHECK(h.Count("hid") == 0);
    CHECK(plot->max.x - plot->min.x > 100.0f);
}

TEST_CASE("Tab outside a TabBar draws a placeholder instead of asserting") {
    std::string json = WindowPanel(R"json([
        { "id": "stray", "type": "Tab", "props": { "label": "Stray" }, "children": [
            { "id": "inner", "type": "Text", "props": { "text": "x" } } ] },
        { "id": "tabs", "type": "TabBar", "children": [
            { "id": "ok", "type": "Tab", "props": { "label": "Ok" }, "children": [
                { "id": "okin", "type": "Text", "props": { "text": "y" } } ] } ] }
    ])json");
    Harness h(json.c_str());
    h.Frames(2);
    CHECK(h.Count("stray") == 1);
    CHECK(h.Count("inner") == 0);
    CHECK(h.Count("okin") == 1);
    CHECK(h.HasDiag(Diagnostic::Level::Error, "stray", "child of TabBar"));
    CHECK_FALSE(h.HasDiag(Diagnostic::Level::Error, "ok", "child of TabBar"));
}

TEST_CASE("set on an undeclared state var is readable back through interpolation") {
    std::string json = WindowPanel(R"json([
        { "id": "probe", "type": "Probe", "props": { "text": "x={state.extra}" } },
        { "id": "btn", "type": "Button", "props": { "label": "go" },
          "events": { "onClick": { "actions": [ { "type": "set", "target": "state.extra", "value": 5 } ] } } }
    ])json");
    Harness h(json.c_str());
    h.interp.RegisterWidget("Probe", &ProbeWidget);
    h.Frames(2);
    CHECK(g_probe_text == "x=?");
    h.Click("btn");
    h.Frame();
    CHECK(g_probe_text == "x=5");
    CHECK_FALSE(h.HasDiag(Diagnostic::Level::Error, "btn", "cannot write"));
}

TEST_CASE("Window open binding read after Begin survives mock storage growth in picker mode") {
    std::string json = std::string(kWindowHead) + R"json([
        { "id": "a", "type": "SliderFloat", "props": { "label": "A" } },
        { "id": "b", "type": "Checkbox", "props": { "label": "B" } },
        { "id": "c", "type": "InputText", "props": { "label": "C" } },
        { "id": "d", "type": "ColorEdit", "props": { "label": "D" } }
    ] , "bind": { "open": "nothing.here" } } })json";
    Harness h(json.c_str(), false);
    h.opts.picker_mode = "select";
    h.inst.MockOrCreate("nothing.here", Value(true));
    h.Frames(3);
    CHECK(h.Count("a") == 1);
    CHECK(h.Count("d") == 1);
    CHECK(h.Find("window") != nullptr);
    CHECK(h.inst.mock.size() >= 5);
}

TEST_CASE("enum literals in visibleIf resolve through reflected enums, including namespaced ones") {
    std::string json = WindowPanel(R"json([
        { "id": "rage", "type": "Text", "props": { "text": "r" }, "visibleIf": "lo.mode == Mode::Rage" },
        { "id": "legit", "type": "Text", "props": { "text": "l" }, "visibleIf": "lo.mode == Mode::Legit" },
        { "id": "high", "type": "Text", "props": { "text": "h" }, "visibleIf": "lo.tier == Tier::High" },
        { "id": "low", "type": "Text", "props": { "text": "lo" }, "visibleIf": "lo.tier == Tier::Low" },
        { "id": "bad", "type": "Text", "props": { "text": "b" }, "visibleIf": "lo.mode == Mode::Nope" }
    ])json");
    Harness h(json.c_str());
    Loadout lo;
    h.roots.push_back(BindingRoot{"lo", &lo, StructInfoOf<Loadout>()});
    h.Frames(2);
    CHECK(h.Count("rage") == 0);
    CHECK(h.Count("legit") == 1);
    CHECK(h.Count("high") == 1);
    CHECK(h.Count("low") == 0);
    CHECK_FALSE(h.HasDiag(Diagnostic::Level::Error, "rage", "unknown enum value"));
    CHECK_FALSE(h.HasDiag(Diagnostic::Level::Error, "high", "unknown enum value"));
    CHECK(h.HasDiag(Diagnostic::Level::Error, "bad", "unknown enum value 'Mode::Nope'"));
    lo.mode = Mode::Rage;
    lo.tier = game::Tier::Low;
    h.Frames(2);
    CHECK(h.Count("rage") == 1);
    CHECK(h.Count("legit") == 0);
    CHECK(h.Count("high") == 0);
    CHECK(h.Count("low") == 1);
    long long v = 0;
    CHECK(interp_detail::LookupEnumValue("::game::Tier", "High", v));
    CHECK(v == 5);
    CHECK(interp_detail::LookupEnumValue("Tier", "Low", v));
    CHECK(v == 1);
    CHECK_FALSE(interp_detail::LookupEnumValue("ier", "Low", v));
    CHECK_FALSE(interp_detail::LookupEnumValue("Mode", "Nope", v));
}

struct AimCfg {
    bool enabled = false;
    float fov = 90.0f;
    float smooth = 5.0f;
};
IMFORGE_REFLECT(AimCfg, enabled, fov, smooth);
struct CheatCfg {
    AimCfg aim;
};
IMFORGE_REFLECT(CheatCfg, aim);

namespace {

const char* kLabeledSliderJson = R"json({
  "schemaVersion": 1, "kind": "component", "name": "LabeledSlider",
  "params": [ { "name": "label", "type": "string", "default": "Value" },
              { "name": "value", "type": "binding<float>" },
              { "name": "min", "type": "float", "default": 0 },
              { "name": "max", "type": "float", "default": 1 },
              { "name": "format", "type": "string", "default": "%.2f" } ],
  "slots": [ "content" ],
  "root": { "id": "row", "type": "HStack", "props": { "align": "Center", "spacing": 8 }, "children": [
     { "id": "lbl", "type": "Text", "props": { "text": { "param": "label" } } },
     { "id": "sl", "type": "SliderFloat",
       "anim": { "transitions": [ { "on": "hover", "prop": "color.SliderGrab", "to": "#74adffff", "duration": 0.12, "ease": "OutQuad" } ] },
       "props": { "label": "##v", "flags": [ "AlwaysClamp" ], "format": { "param": "format" },
                  "min": { "param": "min" }, "max": { "param": "max" } },
       "bind": { "value": { "param": "value" } } },
     { "id": "slot_content", "type": "Slot", "props": { "name": "content" } } ] }
})json";

const char* kCardJson = R"json({
  "schemaVersion": 1, "kind": "component", "name": "Card",
  "params": [ { "name": "title", "type": "string", "default": "Card" },
              { "name": "accent", "type": "color", "default": "#3d8bfdff" } ],
  "slots": [ "content" ],
  "root": { "id": "card", "type": "Child",
    "anim": { "enter": { "duration": 0.25, "preset": "FadeSlideUp" } },
    "fx": [ { "type": "shadow", "color": "#00000066", "offset": { "x": 0, "y": 3 }, "rounding": 8, "size": 10 } ],
    "props": { "border": true, "childFlags": [ "AutoResizeY", "AlwaysUseWindowPadding" ], "size": { "w": "fill", "h": "auto" } },
    "children": [
      { "id": "card_header", "type": "HStack", "props": { "align": "Center", "spacing": 6 }, "children": [
          { "id": "card_dot", "type": "Text", "props": { "text": "*", "color": { "param": "accent" } } },
          { "id": "card_title", "type": "Text", "props": { "text": { "param": "title" } } } ] },
      { "id": "card_sep", "type": "Separator" },
      { "id": "card_body", "type": "VStack", "props": { "spacing": 6 }, "children": [
          { "id": "slot_content", "type": "Slot", "props": { "name": "content" } } ] } ] }
})json";

struct CheatHarness {
    Project project;
    Harness h;
    CheatCfg cfg;
    explicit CheatHarness(const std::string& json, bool with_roots) : h(json.c_str(), false) {
        project.components.push_back(MakeComponent(kLabeledSliderJson));
        project.components.push_back(MakeComponent(kCardJson));
        h.interp.project = &project;
        if (with_roots) h.roots.push_back(BindingRoot{"cfg", &cfg, StructInfoOf<CheatCfg>()});
    }
    bool NoErrors() const {
        for (const Diagnostic& d : h.interp.last_diagnostics)
            if (d.level == Diagnostic::Level::Error) {
                MESSAGE(d.node_id << ": " << d.message);
                return false;
            }
        return true;
    }
    bool Drawn(const char* id) const {
        const Item* it = h.Find(id);
        return it && it->max.x > it->min.x && it->max.y > it->min.y;
    }
};

const char* kSliderInstances = R"json([
    { "id": "aim_fov", "type": "Component", "component": "LabeledSlider",
      "props": { "label": "FOV", "value": "cfg.aim.fov", "min": 1, "max": 180, "format": "%.0f deg" } },
    { "id": "fov_probe", "type": "Probe", "props": { "text": "{cfg.aim.fov:%.0f}" } }
])json";

const char* kCardInstances = R"json([
    { "id": "aim_group", "type": "VStack", "anim": { "enter": { "duration": 0.25, "preset": "FadeSlideUp", "stagger": 0.04 } },
      "children": [
        { "id": "aim_card", "type": "Component", "component": "Card", "props": { "title": "Aimbot", "accent": "#e74c3cff" },
          "slots": { "content": [
            { "id": "aim_enabled", "type": "Checkbox", "props": { "label": "Enable aimbot" }, "bind": { "value": "cfg.aim.enabled" } },
            { "id": "aim_fov", "type": "Component", "component": "LabeledSlider", "enabledIf": "cfg.aim.enabled",
              "props": { "label": "FOV", "value": "cfg.aim.fov", "min": 1, "max": 180, "format": "%.0f deg" } },
            { "id": "aim_smooth", "type": "Component", "component": "LabeledSlider", "enabledIf": "cfg.aim.enabled",
              "props": { "label": "Smooth", "value": "cfg.aim.smooth", "min": 1, "max": 20, "format": "%.1f" } } ] } } ] }
])json";

}  // namespace

TEST_CASE("design mode expands a component whose binding param path has no root into mock storage") {
    CheatHarness ch(WindowPanel(kSliderInstances), false);
    ch.h.interp.RegisterWidget("Probe", &ProbeWidget);
    ch.h.design = true;
    ch.h.Frames(3);
    CHECK(ch.Drawn("aim_fov"));
    CHECK(ch.Drawn("lbl"));
    REQUIRE(ch.Drawn("sl"));
    CHECK(ch.NoErrors());
    const Item* fov = ch.h.Find("aim_fov");
    const Item* row = ch.h.Find("row");
    REQUIRE(row);
    CHECK(fov->min.x == doctest::Approx(row->min.x));
    CHECK(fov->max.x == doctest::Approx(row->max.x));
    const Item* sl = ch.h.Find("sl");
    ImVec2 grab(sl->max.x - 3.0f, (sl->min.y + sl->max.y) * 0.5f);
    ch.h.ClickAt(grab);
    ch.h.Frame();
    Value* mock = ch.h.inst.Mock("cfg.aim.fov");
    REQUIRE(mock != nullptr);
    CHECK(mock->ToDouble() > 150.0);
    CHECK(g_probe_text == std::to_string(static_cast<int>(std::lround(mock->ToDouble()))));
    CHECK(ch.NoErrors());
}

TEST_CASE("design mode expands components nested in a component slot (Card -> LabeledSlider)") {
    CheatHarness ch(WindowPanel(kCardInstances), false);
    ch.h.design = true;
    ch.h.Frames(3);
    CHECK(ch.Drawn("aim_card"));
    CHECK(ch.Drawn("card_title"));
    CHECK(ch.Drawn("aim_enabled"));
    CHECK(ch.Drawn("aim_fov"));
    CHECK(ch.Drawn("aim_smooth"));
    CHECK(ch.h.Count("sl") == 2);
    CHECK(ch.h.Count("lbl") == 2);
    CHECK(ch.NoErrors());
}

TEST_CASE("runtime component instances nested in a slot bind the real float") {
    CheatHarness ch(WindowPanel(kCardInstances), true);
    ch.cfg.aim.enabled = true;
    ch.h.Frames(3);
    CHECK(ch.h.Count("sl") == 2);
    const Item* sl = ch.h.Find("sl");
    REQUIRE(sl);
    ch.h.ClickAt(ImVec2(sl->max.x - 3.0f, (sl->min.y + sl->max.y) * 0.5f));
    CHECK(ch.cfg.aim.fov > 150.0f);
    CHECK(ch.cfg.aim.smooth == doctest::Approx(5.0f));
    CHECK(ch.h.inst.mock.empty());
    CHECK(ch.NoErrors());
}

TEST_CASE("renamed and newer flags, style vars and colors follow the running Dear ImGui") {
    int a = 0, b = 0;
    CHECK(interp_detail::ResolveFlag("TreeNodeFlags", "NavLeftJumpsBackHere", a) == interp_detail::FlagLookup::Found);
    CHECK(interp_detail::ResolveFlag("TreeNodeFlags", "NavLeftJumpsToParent", b) == interp_detail::FlagLookup::Found);
    CHECK(a == b);
#if IMGUI_VERSION_NUM >= 19200
    CHECK(a == static_cast<int>(ImGuiTreeNodeFlags_NavLeftJumpsToParent));
#else
    CHECK(a == static_cast<int>(ImGuiTreeNodeFlags_NavLeftJumpsBackHere));
#endif
    CHECK(interp_detail::ResolveFlag("TabBarFlags", "FittingPolicyResizeDown", a) == interp_detail::FlagLookup::Found);
    CHECK(interp_detail::ResolveFlag("TabBarFlags", "FittingPolicyShrink", b) == interp_detail::FlagLookup::Found);
    CHECK(a == b);
#if IMGUI_VERSION_NUM >= 19220
    CHECK(a == static_cast<int>(ImGuiTabBarFlags_FittingPolicyShrink));
#else
    CHECK(a == static_cast<int>(ImGuiTabBarFlags_FittingPolicyResizeDown));
#endif
#if IMGUI_VERSION_NUM >= 19200
    CHECK(interp_detail::ResolveFlag("TreeNodeFlags", "DrawLinesFull", a) == interp_detail::FlagLookup::Found);
    CHECK(a == static_cast<int>(ImGuiTreeNodeFlags_DrawLinesFull));
#else
    CHECK(interp_detail::ResolveFlag("TreeNodeFlags", "DrawLinesFull", a) == interp_detail::FlagLookup::NeedsVersion);
    CHECK(a == 19200);
#endif
    ImGuiStyle st;
    Json three(3.0);
    const bool has_sep = IMGUI_VERSION_NUM >= 19270;
    CHECK(interp_detail::SetStyleVarJson(st, "SeparatorSize", three) == has_sep);
    CHECK(interp_detail::StyleVar(st, "SeparatorSize").since == (has_sep ? 0 : 19270));
    CHECK(interp_detail::StyleVar(st, "Nope").since == 0);
    CHECK(interp_detail::StyleColorSince("TreeLines") == 19200);

    std::string json = WindowPanel(R"json([
        { "id": "t1", "type": "TreeNode", "props": { "label": "A", "flags": [ "NavLeftJumpsToParent", "DrawLinesFull" ] },
          "style": { "vars": { "TreeLinesSize": 2 }, "colors": { "TreeLines": "#ff0000ff" } } },
        { "id": "t2", "type": "TreeNode", "props": { "label": "B", "flags": [ "NavLeftJumpsBackHere", "DrawLinesFull" ] },
          "style": { "vars": { "TreeLinesSize": 2 } } },
        { "id": "tabs", "type": "TabBar", "props": { "flags": [ "FittingPolicyShrink" ] }, "children": [
            { "id": "tab1", "type": "Tab", "props": { "label": "One" } } ] },
        { "id": "s1", "type": "Separator", "style": { "vars": { "SeparatorSize": 2, "NotAVar": 1 } } }
    ])json");
    Harness h(json.c_str());
    h.Frames(2);
    CHECK(h.Count("t1") == 1);
    CHECK(h.Count("tab1") == 1);
    int infos = 0, warnings = 0, errors = 0;
    std::string all;
    for (const Diagnostic& d : h.interp.last_diagnostics) {
        all += d.node_id + ": " + d.message + "\n";
        infos += d.level == Diagnostic::Level::Info;
        warnings += d.level == Diagnostic::Level::Warning;
        errors += d.level == Diagnostic::Level::Error;
    }
    INFO(all);
    CHECK(errors == 0);
    CHECK(warnings == 1);
    CHECK(h.HasDiag(Diagnostic::Level::Warning, "s1", "unknown style var 'NotAVar'"));
    int expect_infos = (IMGUI_VERSION_NUM < 19200 ? 3 : 0) + (IMGUI_VERSION_NUM < 19270 ? 1 : 0);
    CHECK(infos == expect_infos);
#if IMGUI_VERSION_NUM < 19200
    CHECK(h.HasDiag(Diagnostic::Level::Info, "t1", "TreeNodeFlags_DrawLinesFull needs Dear ImGui 1.92.0"));
    CHECK(h.HasDiag(Diagnostic::Level::Info, "t1", "style var 'TreeLinesSize' needs Dear ImGui 1.92.0"));
    CHECK(h.HasDiag(Diagnostic::Level::Info, "t1", "style color 'TreeLines' needs Dear ImGui 1.92.0"));
#endif
}
