#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <cmath>
#include <cstring>
#include <string>
#include <vector>

#include "imgui.h"
#include "imforge/imforge_interp.h"
#include "imgui_headless.h"

enum class TestMode { Off, Low, High };
IMFORGE_REFLECT_ENUM(TestMode, Off, Low, High);

struct TestState {
    bool flag = false;
    float f3[3] = {0, 0, 0};
    TestMode mode = TestMode::Low;
    int combo_idx = 1;
    std::string text = "ab";
    float frac = 0.5f;
    int radio = 0;
    float vals[8] = {0, 1, 2, 3, 4, 5, 6, 7};
    std::vector<float> series = {1, 2, 3};
    ImVec4 color = ImVec4(1, 0, 0, 1);
    float rgb[3] = {0, 1, 0};
    char buf[32] = "hi";
    bool sel = false;
    ImTextureID tex = ImTextureID();
};
IMFORGE_REFLECT(TestState, flag, f3, mode, combo_idx, text, frac, radio, vals, series, color, rgb, buf,
                sel, tex);

using namespace imforge;

namespace {

int g_fired = 0;
void OnChangeHandler(DrawCtx&) { ++g_fired; }

Node MakeNode(const char* id, const char* type) {
    Node n;
    n.id = id;
    n.type = type;
    return n;
}

Node Prop(Node n, const char* key, Json v) {
    n.SetProp(key, std::move(v));
    return n;
}

Node Bind(Node n, const char* slot, const char* path) {
    n.bind.emplace_back(std::string(slot), PropValue(Json(path)));
    return n;
}

Node Event(Node n, const char* name) {
    EventSpec ev;
    ev.handler = true;
    n.events.emplace_back(std::string(name), ev);
    return n;
}

Json Expr(const char* e) {
    Json o = Json::Object();
    o.Set("expr", Json(e));
    return o;
}

Json Size(Json w, Json h) {
    Json o = Json::Object();
    o.Set("w", std::move(w));
    o.Set("h", std::move(h));
    return o;
}

Json Items(std::initializer_list<const char*> items) {
    Json a = Json::Array();
    for (const char* s : items) a.Push(Json(s));
    return a;
}

std::vector<Node> Showcase() {
    std::vector<Node> v;
    v.push_back(Prop(Prop(MakeNode("t1", "Text"), "text", Json("HP: {st.frac:%.2f}")), "color", Json("#ff0000ff")));
    v.push_back(Prop(Prop(MakeNode("t2", "Text"), "text", Json("wrapped long text")), "wrapped", Json(true)));
    v.push_back(Prop(Prop(MakeNode("t3", "Text"), "text", Json("bullet")), "bullet", Json(true)));
    v.push_back(Prop(Prop(MakeNode("t4", "Text"), "text", Json("disabled")), "disabled", Json(true)));
    v.push_back(Prop(MakeNode("st", "SeparatorText"), "text", Json("Section")));
    v.push_back(MakeNode("sep", "Separator"));
    v.push_back(Prop(MakeNode("sp", "Spacing"), "count", Json(2)));
    v.push_back(MakeNode("nl", "NewLine"));
    v.push_back(Prop(MakeNode("dm", "Dummy"), "size", Size(Json(20), Json(20))));
    v.push_back(Prop(Prop(Prop(MakeNode("b1", "Button"), "label", Json("Go")), "repeat", Json(true)), "size",
                     Size(Json("fill"), Json("auto"))));
    v.push_back(Prop(MakeNode("b2", "SmallButton"), "label", Json("small")));
    v.push_back(Prop(MakeNode("b3", "ArrowButton"), "dir", Json("Down")));
    v.push_back(Bind(Prop(MakeNode("chk", "Checkbox"), "label", Json("Flag")), "value", "st.flag"));
    v.push_back(Bind(Prop(Prop(MakeNode("rb", "RadioButton"), "label", Json("One")), "value", Json(1)), "value",
                     "st.radio"));
    v.push_back(Bind(Prop(Prop(MakeNode("cb1", "Combo"), "label", Json("Combo")), "items", Items({"A", "B", "C"})),
                     "value", "st.combo_idx"));
    v.push_back(Bind(Prop(MakeNode("cb2", "Combo"), "label", Json("Mode")), "value", "st.mode"));
    v.push_back(Bind(Prop(Prop(Prop(MakeNode("lb", "ListBox"), "label", Json("List")), "items",
                               Items({"A", "B", "C"})),
                          "heightItems", Json(3)),
                     "value", "st.combo_idx"));
    v.push_back(Bind(Prop(Prop(MakeNode("sf3", "SliderFloat"), "label", Json("F3")), "components", Json(3)),
                     "value", "st.f3"));
    v.push_back(Prop(MakeNode("sf1", "SliderFloat"), "label", Json("F1")));
    v.push_back(Prop(Prop(MakeNode("si2", "SliderInt"), "label", Json("I2")), "components", Json(2)));
    v.push_back(Prop(Prop(MakeNode("vsf", "VSliderFloat"), "label", Json("VF")), "size", Size(Json(20), Json(80))));
    v.push_back(Prop(MakeNode("vsi", "VSliderInt"), "label", Json("VI")));
    v.push_back(Bind(Prop(Prop(MakeNode("df4", "DragFloat"), "label", Json("D4")), "components", Json(4)), "value",
                     "st.color"));
    v.push_back(Prop(Prop(MakeNode("di", "DragInt"), "label", Json("DI")), "format", Json("%d units")));
    v.push_back(Bind(Prop(Prop(MakeNode("it1", "InputText"), "label", Json("Text")), "hint", Json("type")),
                     "value", "st.text"));
    v.push_back(Bind(Event(Prop(MakeNode("it2", "InputText"), "label", Json("Buf")), "onEnter"), "value",
                     "st.buf"));
    v.push_back(Prop(Prop(MakeNode("itm", "InputTextMultiline"), "label", Json("Multi")), "size",
                     Size(Json("fill"), Json(60))));
    v.push_back(Prop(Prop(MakeNode("ii2", "InputInt"), "label", Json("II2")), "components", Json(2)));
    v.push_back(Prop(MakeNode("if1", "InputFloat"), "label", Json("IF")));
    v.push_back(Bind(Prop(MakeNode("ce4", "ColorEdit"), "label", Json("Col4")), "value", "st.color"));
    v.push_back(Bind(Prop(MakeNode("ce3", "ColorEdit"), "label", Json("Col3")), "value", "st.rgb"));
    v.push_back(Bind(Prop(Prop(MakeNode("cp", "ColorPicker"), "label", Json("Pick")), "alpha", Json(false)),
                     "value", "st.color"));
    v.push_back(Prop(Prop(MakeNode("cbt", "ColorButton"), "desc", Json("swatch")), "color", Json("#00ff00ff")));
    v.push_back(Prop(Prop(MakeNode("pb", "ProgressBar"), "fraction", Expr("st.frac * 0.5")), "overlay",
                     Json("{st.frac}")));
    v.push_back(Bind(Prop(MakeNode("img1", "Image"), "size", Size(Json(32), Json(32))), "texture", "st.tex"));
    v.push_back(Bind(MakeNode("img2", "Image"), "texture", "hooked"));
    v.push_back(MakeNode("img3", "Image"));
    v.push_back(Prop(MakeNode("ib", "ImageButton"), "size", Size(Json(24), Json(24))));
    v.push_back(Bind(Prop(MakeNode("sel", "Selectable"), "label", Json("Pick me")), "selected", "st.sel"));
    v.push_back(Prop(Prop(MakeNode("mi", "MenuItem"), "label", Json("Save")), "shortcut", Json("Ctrl+S")));
    v.push_back(Bind(Prop(MakeNode("pl1", "PlotLines"), "label", Json("Vals")), "values", "st.vals"));
    v.push_back(Bind(Prop(Prop(MakeNode("ph", "PlotHistogram"), "label", Json("Series")), "scaleMax", Json(4)),
                     "values", "st.series"));
    v.push_back(Prop(MakeNode("pl2", "PlotLines"), "label", Json("Demo")));
    return v;
}

struct Harness {
    test::HeadlessImGui gui;
    Interpreter interp;
    Panel panel;
    PanelInstance inst;
    std::vector<BindingRoot> roots;
    InterpOptions opts;
    TestState st;
    ImVec2 rmin, rmax;
    bool active = false;
    std::vector<float> xs;

    Harness() {
        panel.name = "Main";
        panel.root = MakeNode("win", "Window");
        panel.root.SetProp("title", Json("Test"));
        roots.push_back(BindingRoot{"st", &st, StructInfoOf<TestState>()});
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

    bool DrawOne(const Node& node, float item_width = 0) {
        bool r = false;
        gui.BeginFrame();
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(640, 480));
        ImGui::Begin("T", nullptr, ImGuiWindowFlags_NoSavedSettings);
        if (item_width > 0) ImGui::PushItemWidth(item_width);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        int before = dl->VtxBuffer.Size;
        DrawCtx ctx(interp, panel, inst, roots, opts, node);
        ImGui::PushID(node.id.c_str());
        r = DrawStockWidget(ctx);
        rmin = ImGui::GetItemRectMin();
        rmax = ImGui::GetItemRectMax();
        active = ImGui::IsItemActive();
        ImGui::PopID();
        xs.clear();
        for (int i = before; i < dl->VtxBuffer.Size; ++i) {
            float x = dl->VtxBuffer[i].pos.x;
            bool seen = false;
            for (float e : xs)
                if (std::fabs(e - x) < 0.01f) seen = true;
            if (!seen) xs.push_back(x);
        }
        for (size_t i = 0; i < xs.size(); ++i)
            for (size_t j = i + 1; j < xs.size(); ++j)
                if (xs[j] < xs[i]) std::swap(xs[i], xs[j]);
        if (item_width > 0) ImGui::PopItemWidth();
        ImGui::End();
        gui.EndFrame();
        return r;
    }

    bool Click(const Node& node, ImVec2 pos, float item_width = 0) {
        Mouse(pos, false);
        DrawOne(node, item_width);
        Mouse(pos, true);
        DrawOne(node, item_width);
        Mouse(pos, false);
        return DrawOne(node, item_width);
    }

    ImVec2 Center() const { return ImVec2((rmin.x + rmax.x) * 0.5f, (rmin.y + rmax.y) * 0.5f); }
};

}  // namespace

TEST_CASE("every leaf widget draws for three frames in design mode with mock storage") {
    Harness h;
    h.opts.design_mode = true;
    h.interp.hooks.texture = [](const std::string& name) {
        if (name != "hooked") return ImTextureID();
        return interp_widgets::TexFromInt<ImTextureID>(1, std::is_pointer<ImTextureID>());
    };
    h.panel.root.children = Showcase();
    for (int f = 0; f < 3; ++f) {
        h.gui.BeginFrame();
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(900, 700));
        ImGui::Begin("Host", nullptr, ImGuiWindowFlags_NoSavedSettings);
        ImGui::BeginChild("canvas", ImVec2(800, 600));
        h.interp.DrawPanel(h.panel, h.inst, h.roots, h.opts);
        ImGui::EndChild();
        ImGui::End();
        h.gui.EndFrame();
    }
    CHECK(h.gui.frames == 3);
    for (const Diagnostic& d : h.interp.last_diagnostics) {
        CAPTURE(d.node_id);
        CAPTURE(d.message);
        CHECK(d.level != Diagnostic::Level::Error);
    }
}

TEST_CASE("every leaf widget draws for three frames in runtime mode") {
    Harness h;
    h.panel.root.children = Showcase();
    for (int f = 0; f < 3; ++f) {
        h.gui.BeginFrame();
        h.interp.DrawPanel(h.panel, h.inst, h.roots, h.opts);
        h.gui.EndFrame();
    }
    CHECK(h.gui.frames == 3);
    for (const Diagnostic& d : h.interp.last_diagnostics) {
        CAPTURE(d.node_id);
        CAPTURE(d.message);
        CHECK(d.level != Diagnostic::Level::Error);
    }
}

TEST_CASE("checkbox click toggles the bound bool and fires onChange") {
    Harness h;
    Node chk = Bind(Event(Prop(MakeNode("chk", "Checkbox"), "label", Json("Flag")), "onChange"), "value", "st.flag");
    h.panel.root.children.push_back(chk);
    h.interp.RegisterHandler("Main.chk.onChange", &OnChangeHandler);
    g_fired = 0;
    CHECK_FALSE(h.DrawOne(chk));
    bool changed = h.Click(chk, h.Center());
    CHECK(changed);
    CHECK(h.st.flag);
    CHECK(g_fired == 1);
    h.Click(chk, h.Center());
    CHECK_FALSE(h.st.flag);
    CHECK(g_fired == 2);
}

TEST_CASE("SliderFloat with components=3 writes three floats") {
    Harness h;
    Node sl = Bind(Prop(MakeNode("sl", "SliderFloat"), "components", Json(3)), "value", "st.f3");
    h.panel.root.children.push_back(sl);
    const float width = 300.0f;
    h.DrawOne(sl, width);
    const ImGuiStyle& style = ImGui::GetStyle();
    float spacing = style.ItemInnerSpacing.x;
    float item_w = std::floor((width - spacing * 2) / 3);
    float y = h.Center().y;
    h.Click(sl, ImVec2(h.rmin.x + item_w * 0.5f, y), width);
    CHECK(h.st.f3[0] > 0.3f);
    CHECK(h.st.f3[0] < 0.7f);
    CHECK(h.st.f3[1] == 0.0f);
    CHECK(h.st.f3[2] == 0.0f);
    h.Click(sl, ImVec2(h.rmin.x + (item_w + spacing) * 2 + item_w * 0.5f, y), width);
    CHECK(h.st.f3[2] > 0.3f);
    CHECK(h.st.f3[2] < 0.7f);
    CHECK(h.st.f3[1] == 0.0f);
}

TEST_CASE("Combo maps enum value names when items are empty") {
    Harness h;
    Node cb = Bind(Prop(MakeNode("cb", "Combo"), "label", Json("Mode")), "value", "st.mode");
    h.panel.root.children.push_back(cb);
    CHECK_FALSE(h.DrawOne(cb));
    h.gui.BeginFrame();
    ImGui::Begin("T", nullptr, ImGuiWindowFlags_NoSavedSettings);
    {
        DrawCtx ctx(h.interp, h.panel, h.inst, h.roots, h.opts, cb);
        ImGui::PushID("cb");
        interp_widgets::ListState ls;
        ls.Load(ctx);
        REQUIRE(ls.items.size() == 3);
        CHECK(ls.items[0] == "Off");
        CHECK(ls.items[1] == "Low");
        CHECK(ls.items[2] == "High");
        CHECK(ls.map_enum);
        CHECK(ls.index == 1);
        CHECK(std::string(ls.Preview()) == "Low");
        ls.index = 2;
        ls.Store();
        CHECK(h.st.mode == TestMode::High);
        ls.Load(ctx);
        CHECK(ls.index == 2);
        ImGui::PopID();
    }
    ImGui::End();
    h.gui.EndFrame();
}

TEST_CASE("Combo with explicit items uses the int binding as index") {
    Harness h;
    Node cb = Bind(Prop(MakeNode("cb", "Combo"), "items", Items({"A", "B", "C"})), "value", "st.combo_idx");
    h.panel.root.children.push_back(cb);
    h.gui.BeginFrame();
    ImGui::Begin("T", nullptr, ImGuiWindowFlags_NoSavedSettings);
    {
        DrawCtx ctx(h.interp, h.panel, h.inst, h.roots, h.opts, cb);
        ImGui::PushID("cb");
        interp_widgets::ListState ls;
        ls.Load(ctx);
        CHECK_FALSE(ls.map_enum);
        CHECK(ls.index == 1);
        CHECK(std::string(ls.Preview()) == "B");
        ls.index = 2;
        ls.Store();
        CHECK(h.st.combo_idx == 2);
        ImGui::PopID();
    }
    ImGui::End();
    h.gui.EndFrame();
}

TEST_CASE("InputText bound to std::string grows past its capacity") {
    Harness h;
    Node it = Bind(MakeNode("it", "InputText"), "value", "st.text");
    h.panel.root.children.push_back(it);
    const float width = 300.0f;
    h.DrawOne(it, width);
    ImVec2 pos(h.rmin.x + width - 4.0f, h.Center().y);
    h.Click(it, pos, width);
    CHECK(h.active);
    const char* typed = "0123456789012345678901234567890123456789";
    ImGui::GetIO().AddInputCharactersUTF8(typed);
    h.DrawOne(it, width);
    CHECK(h.active);
    h.DrawOne(it, width);
    CHECK(h.st.text.size() == 42);
    CHECK(h.st.text.find(typed) != std::string::npos);
    CHECK(h.st.text.find("ab") != std::string::npos);
}

TEST_CASE("ProgressBar takes its fraction from an expression and value overrides it") {
    Harness h;
    Node pb = Prop(Prop(MakeNode("pb", "ProgressBar"), "fraction", Expr("st.frac * 0.5")), "overlay", Json(" "));
    h.panel.root.children.push_back(pb);
    h.DrawOne(pb, 400.0f);
    h.DrawOne(pb, 400.0f);
    REQUIRE(h.xs.size() == 3);
    float ratio = (h.xs[1] - h.xs[0]) / (h.xs[2] - h.xs[0]);
    CHECK(ratio == doctest::Approx(0.25f).epsilon(0.02));

    Node pb2 = Bind(pb, "value", "st.frac");
    pb2.id = "pb2";
    h.panel.root.children.push_back(pb2);
    h.DrawOne(pb2, 400.0f);
    h.DrawOne(pb2, 400.0f);
    REQUIRE(h.xs.size() == 3);
    ratio = (h.xs[1] - h.xs[0]) / (h.xs[2] - h.xs[0]);
    CHECK(ratio == doctest::Approx(0.5f).epsilon(0.02));
}

TEST_CASE("unknown types are rejected") {
    Harness h;
    Node bogus = MakeNode("x", "Bogus");
    CHECK_FALSE(IsStockLeafWidget("Bogus"));
    CHECK_FALSE(IsStockLeafWidget("VStack"));
    CHECK(IsStockLeafWidget("PlotHistogram"));
    CHECK_FALSE(h.DrawOne(bogus));
}

TEST_CASE("SafeFormat rejects formats that would read the wrong argument type") {
    using interp_widgets::SafeFormat;
    std::string a = "%.2f", b = "%d", c = "%s", d = "%f and %f", e = "plain", f = "%5.1f%%", g = "%*d",
                i = "%lld";
    CHECK(std::string(SafeFormat(a, true, "%.3f")) == "%.2f");
    CHECK(std::string(SafeFormat(b, true, "%.3f")) == "%.3f");
    CHECK(std::string(SafeFormat(b, false, "%d")) == "%d");
    CHECK(std::string(SafeFormat(a, false, "%d")) == "%d");
    CHECK(std::string(SafeFormat(c, true, "%.3f")) == "%.3f");
    CHECK(std::string(SafeFormat(d, true, "%.3f")) == "%.3f");
    CHECK(std::string(SafeFormat(e, true, "%.3f")) == "plain");
    CHECK(std::string(SafeFormat(f, true, "%.3f")) == "%5.1f%%");
    CHECK(std::string(SafeFormat(g, false, "%d")) == "%d");
    CHECK(std::string(SafeFormat(i, false, "%d")) == "%d");
}

TEST_CASE("flag names resolve to the enumerators of the running ImGui build") {
    Harness h;
    Node n = Prop(MakeNode("f", "InputText"), "flags", Items({"ReadOnly", "CallbackAlways", "CallbackResize", "Bogus"}));
    n = Prop(n, "wflags", Items({"NoNavInputs", "UnsavedDocument", "NoDecoration"}));
    n = Prop(n, "sflags", Items({"AlwaysClamp", "Logarithmic"}));
    n = Prop(n, "tflags", Items({"FittingPolicyResizeDown"}));
    n = Prop(n, "cflags", Items({"AlphaPreviewHalf", "AlphaBar"}));
    n = Prop(n, "one", Json("NoTitleBar"));
    DrawCtx ctx(h.interp, h.panel, h.inst, h.roots, h.opts, n);
    int it = ctx.GetFlags("flags", "InputTextFlags");
    CHECK((it & ImGuiInputTextFlags_ReadOnly) != 0);
    CHECK((it & ImGuiInputTextFlags_CallbackAlways) != 0);
    CHECK((it & ImGuiInputTextFlags_CallbackResize) != 0);
    CHECK(interp_detail::InputTextFlagsWithoutCallbacks(it) == ImGuiInputTextFlags_ReadOnly);
    CHECK(ctx.GetFlags("wflags", "WindowFlags") ==
          (ImGuiWindowFlags_NoNavInputs | ImGuiWindowFlags_UnsavedDocument | ImGuiWindowFlags_NoDecoration));
    CHECK(ctx.GetFlags("sflags", "SliderFlags") == (ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_Logarithmic));
#if IMGUI_VERSION_NUM >= 19220
    CHECK(ctx.GetFlags("tflags", "TabBarFlags") == ImGuiTabBarFlags_FittingPolicyShrink);
#else
    CHECK(ctx.GetFlags("tflags", "TabBarFlags") == ImGuiTabBarFlags_FittingPolicyResizeDown);
#endif
    CHECK(ctx.GetFlags("cflags", "ColorEditFlags") == (ImGuiColorEditFlags_AlphaPreviewHalf | ImGuiColorEditFlags_AlphaBar));
    CHECK(ctx.GetFlags("one", "WindowFlags") == ImGuiWindowFlags_NoTitleBar);
    bool bogus = false;
    for (const Diagnostic& d : h.interp.last_diagnostics)
        if (d.message.find("Bogus") != std::string::npos) bogus = true;
    CHECK(bogus);
}

TEST_CASE("InputText ignores callback flags and keeps unbound text across frames") {
    Harness h;
    Node it = Prop(Prop(MakeNode("it", "InputText"), "label", Json("T")), "flags",
                   Items({"CallbackAlways", "CallbackCompletion", "CallbackResize", "AllowTabInput"}));
    h.panel.root.children.push_back(it);
    const float width = 300.0f;
    h.DrawOne(it, width);
    ImVec2 pos(h.rmin.x + width - 4.0f, h.Center().y);
    h.Click(it, pos, width);
    CHECK(h.active);
    ImGui::GetIO().AddInputCharactersUTF8("hello");
    h.DrawOne(it, width);
    h.DrawOne(it, width);
    Value* m = h.inst.Mock("it.value");
    REQUIRE(m != nullptr);
    CHECK(m->s == "hello");
    for (const Diagnostic& d : h.interp.last_diagnostics) CHECK(d.level != Diagnostic::Level::Error);
}

TEST_CASE("Selectable with a fill width spans the available width") {
    Harness h;
    Node sel = Prop(Prop(MakeNode("sel", "Selectable"), "label", Json("S")), "size", Size(Json("fill"), Json("auto")));
    h.DrawOne(sel);
    CHECK(h.rmax.x - h.rmin.x > 100.0f);
    Node dm = Prop(MakeNode("dm", "Dummy"), "size", Size(Json("fill"), Json("fill-10")));
    h.DrawOne(dm);
    CHECK(h.rmax.x - h.rmin.x > 100.0f);
    CHECK(h.rmax.y - h.rmin.y > 100.0f);
}

TEST_CASE("design-mode mock storage for components 2..4 holds N floats or ints") {
    Harness h;
    h.opts.design_mode = true;
    h.roots.clear();
    const char* types[] = {"DragFloat", "SliderFloat", "InputFloat", "DragInt", "SliderInt", "InputInt"};
    std::vector<std::pair<std::string, int>> ids;
    for (int n = 2; n <= 4; ++n) {
        for (const char* t : types) {
            std::string id = std::string(t) + std::to_string(n);
            std::string path = "obj." + id;
            Node bound = Prop(Prop(MakeNode(id.c_str(), t), "label", Json(id.c_str())), "components", Json(n));
            h.panel.root.children.push_back(Bind(bound, "value", path.c_str()));
            std::string uid = id + "_u";
            h.panel.root.children.push_back(
                Prop(Prop(MakeNode(uid.c_str(), t), "label", Json(uid.c_str())), "components", Json(n)));
            ids.emplace_back(id, n);
        }
    }
    for (int f = 0; f < 3; ++f) {
        h.gui.BeginFrame();
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(900, 700));
        ImGui::Begin("Host", nullptr, ImGuiWindowFlags_NoSavedSettings);
        ImGui::BeginChild("canvas", ImVec2(800, 600));
        h.interp.DrawPanel(h.panel, h.inst, h.roots, h.opts);
        ImGui::EndChild();
        ImGui::End();
        h.gui.EndFrame();
    }
    for (const Diagnostic& d : h.interp.last_diagnostics) {
        CAPTURE(d.node_id);
        CAPTURE(d.message);
        CHECK(d.level != Diagnostic::Level::Error);
    }
    for (const auto& kv : ids) {
        CAPTURE(kv.first);
        const Value* m = h.inst.Mock("obj." + kv.first);
        REQUIRE(m != nullptr);
        CHECK(m->IsString());
        CHECK(m->s.size() == static_cast<size_t>(kv.second) * 4);
        const Value* u = nullptr;
        std::string suffix = kv.first + "_u.value";
        for (const auto& e : h.inst.mock)
            if (e.first.size() >= suffix.size() && e.first.compare(e.first.size() - suffix.size(), suffix.size(), suffix) == 0)
                u = &e.second;
        REQUIRE(u != nullptr);
        CHECK(u->s.size() == static_cast<size_t>(kv.second) * 4);
    }
}
