#pragma once
// interpreter + adapter checks for tests/controls_tests.cpp, included at its end

struct CtlA {
    int seg = 0, tab = 0, mask = 0, chips = 0, step = 3, tri = 0, chord = 0, unit = 0, pal = 0;
    int split = 0, weapon = 0, command = 0, changes = 0, clicks = 0;
    float range[2] = {20, 80};
    int irange[2] = {10, 90};
    float gain = 0.5f, quality = 5, rating = 3, dist = 1.5f;
    bool on = false, bold = false, palette_open = false;
    std::string search = "abc", tags = "red,green";
};
IMFORGE_REFLECT(CtlA, seg, tab, mask, chips, step, tri, chord, unit, pal, split, weapon, command, changes, clicks, range,
                irange, gain, quality, rating, dist, on, bold, palette_open, search, tags);

struct CtlB {
    ImVec2 pos = ImVec2(1, 2);
    float rot[3] = {0, 90, 0};
    ImVec4 col = ImVec4(1, 0, 0, 1);
    ImVec4 pick = ImVec4(0.2f, 0.5f, 0.8f, 1);
    float hue = 0.3f, sat = 1, val = 1;
    float grad[20] = {0, 1, 0, 0, 1, 1, 0, 0, 1, 1, -1, 0, 0, 0, 0, -1, 0, 0, 0, 0};
};
IMFORGE_REFLECT(CtlB, pos, rot, col, pick, hue, sat, val, grad);

namespace {

std::string ReadText(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

imforge::Json ParseFile(const std::string& path) {
    imforge::Json j;
    imforge::JsonError e;
    std::string text = ReadText(path);
    REQUIRE_MESSAGE(!text.empty(), "cannot read " << path);
    REQUIRE_MESSAGE(imforge::ParseJson(text, j, &e), path << ": " << e.message);
    return j;
}

std::vector<std::string> ManifestTypes(imforge::WidgetCatalog& catalog) {
    std::string dir = std::string(IMFORGE_SOURCE_DIR) + "/packs/controls/";
    imforge::Json m = ParseFile(dir + "manifest.json");
    std::vector<std::string> types;
    const imforge::Json& w = m["widgets"];
    for (size_t i = 0; i < w.Size(); ++i) {
        imforge::Json wj = ParseFile(dir + w[i].AsString());
        imforge::Error err;
        REQUIRE_MESSAGE(catalog.AddManifest(wj, &err), w[i].AsString() << ": " << err.message);
        types.push_back(wj["name"].AsString());
    }
    return types;
}

const char* kPanel = R"json({
  "schemaVersion": 1, "kind": "panel", "name": "Ctl",
  "params": [ { "name": "a", "type": "CtlA" }, { "name": "b", "type": "CtlB" } ],
  "root": { "id": "window", "type": "Window",
    "props": { "title": "Controls", "initialSize": {"x": 900, "y": 1500}, "initialPos": {"x": 0, "y": 0}, "sizeCond": "Always" },
    "children": [
      { "id": "sw", "type": "SwitchToggle", "props": { "label": "On", "kind": "Square" }, "bind": { "value": "a.on" },
        "events": { "onChange": { "actions": [ { "type": "set", "target": "a.changes", "value": { "expr": "a.changes + 1" } } ] } } },
      { "id": "seg", "type": "SegmentedControl", "bind": { "index": "a.seg" } },
      { "id": "tabs", "type": "PillTabs", "bind": { "index": "a.tab" } },
      { "id": "tbg", "type": "ToggleButtonGroup", "bind": { "mask": "a.mask" } },
      { "id": "chips", "type": "Chips", "bind": { "mask": "a.chips" } },
      { "id": "rs", "type": "RangeSlider", "bind": { "value": "a.range" } },
      { "id": "rsi", "type": "RangeSliderInt", "bind": { "value": "a.irange" } },
      { "id": "knob", "type": "Knob", "bind": { "value": "a.gain" } },
      { "id": "tick", "type": "TickSlider", "bind": { "value": "a.quality" } },
      { "id": "step", "type": "Stepper", "bind": { "value": "a.step" } },
      { "id": "it", "type": "IconToggle", "bind": { "value": "a.bold" } },
      { "id": "tri", "type": "TriCheckbox", "bind": { "state": "a.tri" } },
      { "id": "stars", "type": "RatingStars", "bind": { "value": "a.rating" } },
      { "id": "search", "type": "SearchField", "bind": { "text": "a.search" } },
      { "id": "tags", "type": "TagInput", "bind": { "tags": "a.tags" } },
      { "id": "chord", "type": "KeyChordPicker", "bind": { "chord": "a.chord" } },
      { "id": "num", "type": "DragNumber", "bind": { "value": "a.dist", "unit": "a.unit" } },
      { "id": "v2", "type": "Vec2Edit", "bind": { "value": "b.pos" } },
      { "id": "v3", "type": "Vec3Edit", "bind": { "value": "b.rot" } },
      { "id": "pal", "type": "ColorPalette", "bind": { "selected": "a.pal", "color": "b.col" } },
      { "id": "hue", "type": "HueStrip", "bind": { "hue": "b.hue" } },
      { "id": "sv", "type": "SatValBox", "bind": { "hue": "b.hue", "sat": "b.sat", "val": "b.val" } },
      { "id": "pick", "type": "ColorPickerMini", "bind": { "color": "b.pick" } },
      { "id": "grad", "type": "GradientEditor", "bind": { "stops": "b.grad" } },
      { "id": "split", "type": "SplitButton", "bind": { "selected": "a.split" },
        "events": { "onClick": { "actions": [ { "type": "set", "target": "a.clicks", "value": { "expr": "a.clicks + 1" } } ] } } },
      { "id": "dd", "type": "IconDropdown", "bind": { "current": "a.weapon" } },
      { "id": "cmd", "type": "CommandPalette", "bind": { "open": "a.palette_open", "selected": "a.command" } }
    ] } })json";

struct Item {
    std::string id;
    ImVec2 min, max;
};

struct InterpHarness {
    H h;
    imforge::Interpreter interp;
    imforge::Panel panel;
    imforge::PanelInstance inst;
    std::vector<imforge::BindingRoot> roots;
    imforge::InterpOptions opts;
    CtlA a;
    CtlB b;
    std::vector<Item> items;
    std::vector<std::string> types;

    InterpHarness() {
        types = ManifestTypes(interp.catalog);
        imforge::RegisterPackWidgets_controls(interp);
        imforge::Json pj;
        imforge::JsonError je;
        REQUIRE(imforge::ParseJson(kPanel, pj, &je));
        imforge::Error e;
        REQUIRE_MESSAGE(imforge::PanelFromJson(pj, panel, &e), e.message);
        roots.push_back(imforge::BindingRoot{"a", &a, imforge::StructInfoOf<CtlA>()});
        roots.push_back(imforge::BindingRoot{"b", &b, imforge::StructInfoOf<CtlB>()});
        interp.hooks.on_item = [this](const imforge::Node& n, const ImVec2& mn, const ImVec2& mx, bool) {
            items.push_back(Item{n.id, mn, mx});
        };
    }
    void Frame() {
        items.clear();
        h.gui.BeginFrame();
        interp.DrawPanel(panel, inst, roots, opts);
        h.gui.EndFrame();
    }
    const Item* Find(const char* id) const {
        for (const Item& it : items)
            if (it.id == id) return &it;
        return nullptr;
    }
    int Errors() const {
        int n = 0;
        for (const imforge::Diagnostic& d : interp.last_diagnostics)
            if (d.level == imforge::Diagnostic::Level::Error) {
                MESSAGE(d.node_id << ": " << d.message);
                ++n;
            }
        return n;
    }
};

}  // namespace

TEST_CASE("controls manifests load into the catalog and every type has an adapter") {
    imforge::WidgetCatalog catalog;
    std::vector<std::string> types = ManifestTypes(catalog);
    CHECK(types.size() >= 18);
    imforge::Interpreter interp;
    imforge::RegisterPackWidgets_controls(interp);
    for (const std::string& t : types) {
        const imforge::CustomWidgetDef* d = catalog.FindCustom(t.c_str());
        REQUIRE_MESSAGE(d != nullptr, t);
        CHECK_MESSAGE(interp.FindWidgetFn(t) != nullptr, t);
        CHECK_MESSAGE(d->call.rfind("imb::ctl::", 0) == 0, t);
        CHECK_MESSAGE(d->include == "imb_controls.h", t);
    }
    CHECK(interp.widgets.size() == types.size());
}

TEST_CASE("every controls adapter draws through the interpreter and drives its binding") {
    InterpHarness x;
    for (int i = 0; i < 3; ++i) x.Frame();
    CHECK(x.Errors() == 0);
    for (const std::string& t : x.types) {
        bool found = false;
        for (const imforge::Node& n : x.panel.root.children)
            if (n.type == t) found = x.Find(n.id.c_str()) != nullptr;
        CHECK_MESSAGE(found, t);
    }
    const Item* sw = x.Find("sw");
    REQUIRE(sw != nullptr);
    ImVec2 c(sw->min.x + 10.0f, (sw->min.y + sw->max.y) * 0.5f);
    x.h.Mouse(c, false);
    x.Frame();
    x.h.Mouse(c, true);
    x.Frame();
    x.h.Mouse(c, false);
    x.Frame();
    x.Frame();
    CHECK(x.a.on);
    CHECK(x.a.changes == 1);
    x.a.palette_open = true;
    for (int i = 0; i < 4; ++i) x.Frame();
    CHECK(x.Errors() == 0);
}

TEST_CASE("controls adapters draw in design mode without bindings") {
    InterpHarness x;
    x.opts.design_mode = true;
    for (imforge::Node& n : x.panel.root.children) n.bind.clear();
    for (int i = 0; i < 3; ++i) {
        x.items.clear();
        x.h.gui.BeginFrame();
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(1000, 1550));
        ImGui::Begin("Builder", nullptr, ImGuiWindowFlags_NoSavedSettings);
        ImGui::BeginChild("canvas", ImVec2(980, 1500));
        x.interp.DrawPanel(x.panel, x.inst, x.roots, x.opts);
        ImGui::EndChild();
        ImGui::End();
        x.h.gui.EndFrame();
    }
    CHECK(x.Errors() == 0);
    CHECK(x.Find("grad") != nullptr);
    CHECK(x.Find("cmd") != nullptr);
}
