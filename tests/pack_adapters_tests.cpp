#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include <cstdarg>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "imgui.h"
#include "imforge/imforge_interp.h"
#include "imb_essentials.h"
#include "imforge_adapters_essentials.h"
#include "imgui_headless.h"

#include "../src/builder/project_io.cpp"
#include "../src/builder/bundled_packs.cpp"

// the other packs' adapters, when they exist (found relative to this file)
#if __has_include("../packs/controls/imforge_adapters_controls.h")
#include "../packs/controls/imforge_adapters_controls.h"
#define TEST_PACK_CONTROLS 1
#endif
#if __has_include("../packs/display/imforge_adapters_display.h")
#include "../packs/display/imforge_adapters_display.h"
#define TEST_PACK_DISPLAY 1
#endif
#if __has_include("../packs/gaming/imforge_adapters_gaming.h")
#include "../packs/gaming/imforge_adapters_gaming.h"
#define TEST_PACK_GAMING 1
#endif

namespace imforge::builder {
std::string ExeDir() { return "."; }
void EditorContext::Log(LogLevel, const char*, ...) {}
}  // namespace imforge::builder

struct EssCfg {
    bool god = false;
    int key = 0;
    int tab = 0;
    int side = 0;
    int sel = 0;
    bool open = true;
    int changes = 0;
    int clicks = 0;
    ImVec4 colors[3] = {ImVec4(1, 0, 0, 1), ImVec4(0, 1, 0, 1), ImVec4(0, 0, 1, 1)};
};
IMFORGE_REFLECT(EssCfg, god, key, tab, side, sel, open, changes, clicks, colors);

using namespace imforge;

namespace {

const char* kPanel = R"json({
  "schemaVersion": 1, "kind": "panel", "name": "Ess",
  "params": [ { "name": "cfg", "type": "EssCfg" } ],
  "root": { "id": "window", "type": "Window",
    "props": { "title": "Essentials", "initialSize": {"x": 700, "y": 700}, "initialPos": {"x": 0, "y": 0}, "sizeCond": "Always" },
    "children": [
      { "id": "tg", "type": "Toggle", "props": { "label": "God mode" }, "bind": { "value": "cfg.god" },
        "events": { "onChange": { "actions": [ { "type": "set", "target": "cfg.changes", "value": { "expr": "cfg.changes + 1" } } ] } } },
      { "id": "kb", "type": "KeybindPicker", "props": { "label": "Aim key" }, "bind": { "key": "cfg.key" } },
      { "id": "sw", "type": "ColorSwatchRow", "props": { "label": "Team", "count": 3 },
        "bind": { "colors": "cfg.colors", "selected": "cfg.sel" } },
      { "id": "hk", "type": "HotkeyLabel", "props": { "text": "Menu", "key": "cfg.key" } },
      { "id": "sp", "type": "Spinner", "props": { "radius": 10 } },
      { "id": "gh", "type": "GradientHeader", "props": { "label": "Visuals", "height": 28 } },
      { "id": "tb", "type": "AnimatedTabBar", "props": { "labels": ["A", "B", "C"] }, "bind": { "index": "cfg.tab" } },
      { "id": "st", "type": "AnimatedSideTabs", "props": { "labels": ["One", "Two"] }, "bind": { "index": "cfg.side" } },
      { "id": "gb", "type": "GlowButton", "props": { "label": "Save", "size": { "w": 120, "h": 30 } },
        "events": { "onClick": { "actions": [ { "type": "set", "target": "cfg.clicks", "value": { "expr": "cfg.clicks + 1" } } ] } } },
      { "id": "card", "type": "SectionCard", "props": { "title": "Card", "size": { "w": "fill", "h": 80 } },
        "bind": { "open": "cfg.open" },
        "children": [ { "id": "inner", "type": "Text", "props": { "text": "inside" } } ] },
      { "id": "toast", "type": "ToastHost", "props": { "corner": "TopLeft" } }
    ] } })json";

const char* const kAllEssentials[] = {"tg", "kb", "sw", "hk", "sp", "gh", "tb", "st", "gb", "card", "toast", "inner"};

std::string PacksDir() { return std::string(IMFORGE_SOURCE_DIR) + "/packs"; }

struct Item {
    std::string id;
    ImVec2 min, max;
};

struct Harness {
    test::HeadlessImGui gui;
    Interpreter interp;
    Panel panel;
    PanelInstance inst;
    std::vector<BindingRoot> roots;
    InterpOptions opts;
    EssCfg cfg;
    std::vector<Item> items;
    std::vector<builder::BundledPack> packs;
    std::vector<std::string> warnings;

    explicit Harness(const char* json, bool with_roots = true) {
        Json j;
        JsonError je;
        REQUIRE_MESSAGE(ParseJson(json, j, &je), je.message);
        Error e;
        REQUIRE_MESSAGE(PanelFromJson(j, panel, &e), e.message);
        packs = builder::ScanBundledPacks(PacksDir(), &interp.catalog, &warnings);
        RegisterPackWidgets_essentials(interp);
        if (with_roots) roots.push_back(BindingRoot{"cfg", &cfg, StructInfoOf<EssCfg>()});
        interp.hooks.on_item = [this](const Node& n, const ImVec2& mn, const ImVec2& mx, bool) {
            items.push_back(Item{n.id, mn, mx});
        };
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
        if (opts.design_mode) {
            ImGui::SetNextWindowPos(ImVec2(0, 0));
            ImGui::SetNextWindowSize(ImVec2(900, 700));
            ImGui::Begin("Builder", nullptr, ImGuiWindowFlags_NoSavedSettings);
            interp.DrawPanel(panel, inst, roots, opts);
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

    const Item& Get(const char* id) const {
        const Item* it = Find(id);
        REQUIRE_MESSAGE(it != nullptr, "item not drawn: " << id);
        return *it;
    }

    void ClickAt(ImVec2 c) {
        Mouse(c, false);
        Frame();
        Mouse(c, true);
        Frame();
        Mouse(c, false);
        Frame();
    }

    void Click(const char* id) {
        const Item& it = Get(id);
        ClickAt(ImVec2((it.min.x + it.max.x) * 0.5f, (it.min.y + it.max.y) * 0.5f));
    }

    int Errors() const {
        int n = 0;
        for (const Diagnostic& d : interp.last_diagnostics) {
            if (d.level != Diagnostic::Level::Error) continue;
            MESSAGE(d.node_id << ": " << d.message);
            ++n;
        }
        return n;
    }
};

}  // namespace

TEST_CASE("bundled packs load into the catalog and essentials has an adapter for every widget") {
    WidgetCatalog catalog;
    std::vector<std::string> warnings;
    std::vector<builder::BundledPack> packs = builder::ScanBundledPacks(PacksDir(), &catalog, &warnings);
    for (const std::string& w : warnings) MESSAGE(w);
    CHECK(warnings.empty());
    const builder::BundledPack* ess = nullptr;
    for (const builder::BundledPack& p : packs)
        if (p.name == "essentials") ess = &p;
    REQUIRE(ess != nullptr);
    CHECK(ess->title == "Essentials");
    CHECK(ess->widgets.size() == 11);
    CHECK(ess->includes.size() == 1);
    CHECK(ess->has_adapters);
    CHECK(std::find(ess->headers.begin(), ess->headers.end(), "imb_essentials.h") != ess->headers.end());

    Interpreter interp;
    CHECK(builder::RegisterBundledAdapters(interp) >= 1);
    for (const std::string& t : ess->widgets) {
        CHECK_MESSAGE(catalog.FindCustom(t.c_str()) != nullptr, t);
        CHECK_MESSAGE(interp.FindWidgetFn(t) != nullptr, t);
    }
    CHECK(interp.FindWidgetEnd("SectionCard") != nullptr);
    CHECK(interp.FindWidgetEnd("Toggle") == nullptr);
}

TEST_CASE("every pack with an adapter header registers all of its manifest types") {
    WidgetCatalog catalog;
    std::vector<builder::BundledPack> packs = builder::ScanBundledPacks(PacksDir(), &catalog, nullptr);
    Interpreter interp;
    std::vector<std::string> covered = {"essentials"};
    RegisterPackWidgets_essentials(interp);
#ifdef TEST_PACK_CONTROLS
    RegisterPackWidgets_controls(interp);
    covered.push_back("controls");
#endif
#ifdef TEST_PACK_DISPLAY
    RegisterPackWidgets_display(interp);
    covered.push_back("display");
#endif
#ifdef TEST_PACK_GAMING
    RegisterPackWidgets_gaming(interp);
    covered.push_back("gaming");
#endif
    for (const builder::BundledPack& p : packs) {
        if (std::find(covered.begin(), covered.end(), p.name) == covered.end()) continue;
        for (const std::string& t : p.widgets) {
            CHECK_MESSAGE(interp.FindWidgetFn(t) != nullptr, p.name << ": no adapter for " << t);
            const CustomWidgetDef* d = catalog.FindCustom(t.c_str());
            REQUIRE(d != nullptr);
            if (d->container && d->manifest["callEnd"].IsString())
                CHECK_MESSAGE(interp.FindWidgetEnd(t) != nullptr, p.name << ": container " << t << " needs an end function");
        }
    }
}

TEST_CASE("every essentials widget draws through the interpreter and drives its bindings") {
    Harness h(kPanel);
    h.Frames(3);
    for (const char* id : kAllEssentials) CHECK_MESSAGE(h.Find(id) != nullptr, id);
    CHECK(h.Errors() == 0);

    SUBCASE("toggle flips the bound bool and runs onChange") {
        h.Click("tg");
        CHECK(h.cfg.god);
        CHECK(h.cfg.changes == 1);
        h.Click("tg");
        CHECK_FALSE(h.cfg.god);
        CHECK(h.cfg.changes == 2);
    }
    SUBCASE("glow button fires onClick") {
        h.Click("gb");
        CHECK(h.cfg.clicks == 1);
        const Item& gb = h.Get("gb");
        CHECK(gb.max.x - gb.min.x == doctest::Approx(120));
    }
    SUBCASE("tab bar and side tabs write the index") {
        const Item& tb = h.Get("tb");
        h.ClickAt(ImVec2(tb.min.x + (tb.max.x - tb.min.x) * 0.85f, (tb.min.y + tb.max.y) * 0.5f));
        CHECK(h.cfg.tab == 2);
        const Item& st = h.Get("st");
        h.ClickAt(ImVec2(st.min.x + 10, st.min.y + (st.max.y - st.min.y) * 0.75f));
        CHECK(h.cfg.side == 1);
    }
    SUBCASE("swatch row selects into the bound int and reads the bound colors") {
        const Item& sw = h.Get("sw");
        float sz = sw.max.y - sw.min.y, gap = ImGui::GetStyle().ItemInnerSpacing.x;
        h.ClickAt(ImVec2(sw.min.x + 2 * (sz + gap) + sz * 0.5f, (sw.min.y + sw.max.y) * 0.5f));
        CHECK(h.cfg.sel == 2);
    }
    SUBCASE("section card header toggles the bound open flag") {
        const Item& card = h.Get("card");
        h.ClickAt(ImVec2(card.min.x + 60, card.min.y + 5));
        CHECK_FALSE(h.cfg.open);
        h.Frames(2);
        const Item& closed = h.Get("card");
        CHECK(closed.max.y - closed.min.y < 60);
    }
#if IMGUI_VERSION_NUM >= 18700
    SUBCASE("keybind picker captures the next key") {
        h.Click("kb");
        ImGuiIO& io = ImGui::GetIO();
        io.AddKeyEvent(ImGuiKey_F, true);
        h.Frame();
        io.AddKeyEvent(ImGuiKey_F, false);
        h.Frames(2);
        CHECK(h.cfg.key == (int)ImGuiKey_F);
    }
#endif
    CHECK(h.Errors() == 0);
}

TEST_CASE("essentials widgets draw in design mode without data and keep mock state") {
    Harness h(kPanel, false);
    h.opts.design_mode = true;
    h.Frames(3);
    for (const char* id : kAllEssentials) CHECK_MESSAGE(h.Find(id) != nullptr, id);
    CHECK(h.Errors() == 0);
    const Item& card = h.Get("card");
    CHECK(card.max.y - card.min.y > 60);
    const Item& toast = h.Get("toast");
    CHECK(toast.max.x - toast.min.x > 10);
    h.Click("tg");
    h.Frames(1);
    bool found = false;
    for (const auto& kv : h.inst.mock)
        if (kv.first == "cfg.god") found = kv.second.ToBool();
    CHECK(found);
}

TEST_CASE("unbound live widgets fall back to local storage") {
    Harness h(R"json({
      "schemaVersion": 1, "kind": "panel", "name": "Loose",
      "params": [ { "name": "cfg", "type": "EssCfg" } ],
      "root": { "id": "window", "type": "Window",
        "props": { "title": "Loose", "initialSize": {"x": 400, "y": 300}, "initialPos": {"x": 0, "y": 0}, "sizeCond": "Always" },
        "children": [
          { "id": "tg", "type": "Toggle", "props": { "label": "Loose" },
            "events": { "onChange": { "actions": [ { "type": "set", "target": "cfg.changes", "value": { "expr": "cfg.changes + 1" } } ] } } },
          { "id": "tb", "type": "AnimatedTabBar", "props": { "labels": ["A", "B"] } },
          { "id": "card", "type": "SectionCard", "props": { "title": "Fixed" },
            "children": [ { "id": "inner", "type": "Text", "props": { "text": "x" } } ] }
        ] } })json");
    h.Frames(2);
    h.Click("tg");
    CHECK(h.cfg.changes == 1);
    h.Click("tg");
    CHECK(h.cfg.changes == 2);
    const Item& card = h.Get("card");
    h.ClickAt(ImVec2(card.min.x + 30, card.min.y + 5));
    CHECK(h.Find("inner") != nullptr);
    CHECK(h.Errors() == 0);
}

TEST_CASE("pack helper prop readers use manifest defaults") {
    Harness h(R"json({
      "schemaVersion": 1, "kind": "panel", "name": "Props",
      "root": { "id": "window", "type": "Window",
        "props": { "title": "Props", "initialSize": {"x": 400, "y": 300}, "initialPos": {"x": 0, "y": 0}, "sizeCond": "Always" },
        "children": [
          { "id": "a", "type": "Toggle" },
          { "id": "b", "type": "ToastHost", "props": { "corner": "TopRight" } },
          { "id": "c", "type": "AnimatedTabBar", "props": { "labels": ["x", "", "z"] } }
        ] } })json", false);
    static std::vector<std::string> seen;
    seen.clear();
    h.interp.RegisterWidget("Toggle", [](DrawCtx& ctx) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%08x", pack::Color(ctx, "onColor"));
        seen.push_back(pack::Label(ctx) + "|" + buf);
        ImGui::TextUnformatted("t");
        return false;
    });
    h.interp.RegisterWidget("ToastHost", [](DrawCtx& ctx) {
        seen.push_back(std::to_string(pack::Enum(ctx, "corner")));
        ImGui::TextUnformatted("t");
        return false;
    });
    h.interp.RegisterWidget("AnimatedTabBar", [](DrawCtx& ctx) {
        std::string z = pack::ZeroList(ctx, "labels");
        seen.push_back(std::to_string(z.size()));
        ImGui::TextUnformatted("t");
        return false;
    });
    h.Frames(1);
    REQUIRE(seen.size() == 3);
    char want[64];
    std::snprintf(want, sizeof(want), "%08x", (unsigned)IM_COL32(0x2e, 0xcc, 0x71, 0xff));
    CHECK(seen[0] == std::string("Toggle###a|") + want);
    CHECK(seen[1] == "1");
    CHECK(seen[2] == "7");
}

TEST_CASE("used widget types cover panels and components") {
    Project p;
    Panel panel;
    panel.root.type = "Window";
    Node t;
    t.type = "Toggle";
    panel.root.children.push_back(t);
    p.panels.push_back(panel);
    Component c;
    c.root.type = "SectionCard";
    p.components.push_back(c);
    std::set<std::string> used = builder::UsedWidgetTypes(p);
    CHECK(used.count("Toggle") == 1);
    CHECK(used.count("SectionCard") == 1);
    CHECK(used.count("Window") == 1);
    CHECK(used.count("GlowButton") == 0);
}

TEST_CASE("pack discovery survives a missing folder and broken manifests") {
    namespace fs = std::filesystem;
    CHECK(builder::ScanBundledPacks(std::string(), nullptr, nullptr).empty());
    CHECK(builder::ScanBundledPacks("definitely/not/a/folder", nullptr, nullptr).empty());

    const fs::path root = fs::temp_directory_path() / "imforge_pack_scan_test";
    std::error_code ec;
    fs::remove_all(root, ec);
    auto write = [&](const fs::path& p, const char* text) {
        fs::create_directories(p.parent_path(), ec);
        std::ofstream(p, std::ios::binary) << text;
    };
    write(root / "broken" / "manifest.json", "{ not json");
    write(root / "notobject" / "manifest.json", "[1, 2]");
    write(root / "nameless" / "manifest.json", "{\"name\": \"\", \"widgets\": [\"widgets/a.imw.json\", \"../escape.json\", \"widgets/missing.imw.json\", 7]}");
    write(root / "nameless" / "widgets" / "a.imw.json", "{ broken");
    write(root / "empty_dir" / "readme.txt", "no manifest here");
    write(root / "stray_file.txt", "x");

    WidgetCatalog catalog;
    std::vector<std::string> warnings;
    std::vector<builder::BundledPack> packs = builder::ScanBundledPacks(root.generic_string(), &catalog, &warnings);
    REQUIRE(packs.size() == 1);
    CHECK(packs[0].name == "nameless");
    CHECK(packs[0].widgets.empty());
    CHECK(warnings.size() >= 4);
    fs::remove_all(root, ec);
}
