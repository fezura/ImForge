#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include <cmath>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "imgui.h"
#include "imforge/imforge_interp.h"
#include "imgui_headless.h"

using namespace imforge;

namespace {

struct ProbeRec {
    ImDrawList* dl = nullptr;
    int start = 0, end = 0;
    std::string text;
};
std::map<std::string, ProbeRec> g_probes;
bool g_fire = false;

// a 100x40 button whose vertex range we can look at after the frame
bool ProbeDraw(DrawCtx& ctx) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ProbeRec r;
    r.dl = dl;
    r.start = dl->VtxBuffer.Size;
    r.text = ctx.GetString("text");
    ImGui::Button(("##" + ctx.node.id).c_str(), ImVec2(100, 40));
    r.end = dl->VtxBuffer.Size;
    g_probes[ctx.node.id] = r;
    if (g_fire) ctx.FireEvent("onClick");
    return false;
}

struct Box {
    ImVec2 mn = ImVec2(1e9f, 1e9f), mx = ImVec2(-1e9f, -1e9f);
    int alpha = 0;
    float W() const { return mx.x - mn.x; }
    float H() const { return mx.y - mn.y; }
};

Box VertBox(const char* id) {
    Box b;
    auto it = g_probes.find(id);
    REQUIRE_MESSAGE(it != g_probes.end(), "probe not drawn: " << id);
    const ProbeRec& r = it->second;
    REQUIRE(r.end > r.start);
    for (int i = r.start; i < r.end; ++i) {
        const ImDrawVert& v = r.dl->VtxBuffer[i];
        b.mn.x = v.pos.x < b.mn.x ? v.pos.x : b.mn.x;
        b.mn.y = v.pos.y < b.mn.y ? v.pos.y : b.mn.y;
        b.mx.x = v.pos.x > b.mx.x ? v.pos.x : b.mx.x;
        b.mx.y = v.pos.y > b.mx.y ? v.pos.y : b.mx.y;
        int a = static_cast<int>((v.col >> IM_COL32_A_SHIFT) & 0xFF);
        b.alpha = a > b.alpha ? a : b.alpha;
    }
    return b;
}

Json ParseJ(const char* text) {
    Json j;
    JsonError e;
    REQUIRE_MESSAGE(ParseJson(text, j, &e), e.message << " at " << e.line << ":" << e.col);
    return j;
}

const char* kAnims = R"json([
  {"name":"TestZoom","category":"entrance","duration":1,"tracks":{"scale":[{"t":0,"v":0.5},{"t":1,"v":1}]}},
  {"name":"TestFade","category":"entrance","duration":0.5,"tracks":{"opacity":[{"t":0,"v":0},{"t":1,"v":1}]}},
  {"name":"TestFadeOut","category":"exit","duration":0.5,"tracks":{"opacity":[{"t":0,"v":1},{"t":1,"v":0}]}},
  {"name":"TestType","category":"text","duration":1,"tracks":{"textReveal":[{"t":0,"v":0},{"t":1,"v":1}]}},
  {"name":"TestShake","category":"attention","duration":0.5,"tracks":{"translateX":[{"t":0,"v":0},{"t":0.5,"v":10},{"t":1,"v":0}]}},
  {"name":"TestGrow","category":"hover","duration":0.2,"tracks":{"scale":[{"t":0,"v":1},{"t":1,"v":1.5}]}}
])json";

struct Harness {
    test::HeadlessImGui gui;
    Project project;
    Interpreter interp;
    Panel panel;
    PanelInstance inst;
    std::vector<BindingRoot> roots;
    InterpOptions opts;
    std::map<std::string, std::pair<ImVec2, ImVec2>> rects;
    bool design = false;

    explicit Harness(const char* root_json) {
        imb::Reset();
        imb::ResetMotions();
        g_probes.clear();
        g_fire = false;
        project.name = "MotionTest";
        Json anims = ParseJ(kAnims);
        for (const Json& a : anims.arr) project.animations.push_back(a);
        interp.project = &project;
        interp.RegisterWidget("Probe", &ProbeDraw);
        interp.hooks.on_item = [this](const Node& n, const ImVec2& mn, const ImVec2& mx, bool) {
            rects[n.id] = std::make_pair(mn, mx);
        };
        panel.name = "P";
        panel.state.push_back(StateVar{"show", "bool", Json(true)});
        Error e;
        REQUIRE_MESSAGE(NodeFromJson(ParseJ(root_json), panel.root, &e), e.message);
    }

    void Frame() {
        rects.clear();
        g_probes.clear();
        gui.BeginFrame();
        if (design) {
            opts.design_mode = true;
            ImGui::SetNextWindowPos(ImVec2(0, 0));
            ImGui::SetNextWindowSize(ImVec2(900, 700));
            ImGui::Begin("Builder", nullptr, ImGuiWindowFlags_NoSavedSettings);
            ImGui::BeginChild("canvas", ImVec2(800, 600));
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
    bool Drawn(const char* id) const { return rects.count(id) != 0; }
    float RectW(const char* id) {
        REQUIRE_MESSAGE(Drawn(id), "not drawn: " << id);
        return rects[id].second.x - rects[id].first.x;
    }
    float RectX(const char* id) {
        REQUIRE_MESSAGE(Drawn(id), "not drawn: " << id);
        return rects[id].first.x;
    }
    bool HasDiag(Diagnostic::Level lvl, const char* needle) const {
        for (const Diagnostic& d : interp.last_diagnostics)
            if (d.level == lvl && d.message.find(needle) != std::string::npos) return true;
        return false;
    }
};

std::string Window(const char* children) {
    return std::string(R"json({"id":"win","type":"Window","props":{"title":"M","initialSize":{"x":800,"y":600},"initialPos":{"x":0,"y":0}},"children":[)json") +
           children + "]}";
}

}  // namespace

TEST_CASE("enter motion scales the vertices on the first frames") {
    Harness h(Window(R"json({"id":"p","type":"Probe","anim":{"motions":[{"custom":"TestZoom"}]}})json").c_str());
    h.Frame();
    Box b = VertBox("p");
    float w = h.RectW("p");
    CHECK(w == doctest::Approx(100.0f).epsilon(0.02));
    CHECK(b.W() < w * 0.6f);
    CHECK(b.W() > w * 0.4f);
    CHECK(b.H() < 40.0f * 0.6f);
    float cx = (b.mn.x + b.mx.x) * 0.5f, rcx = (h.rects["p"].first.x + h.rects["p"].second.x) * 0.5f;
    CHECK(cx == doctest::Approx(rcx).epsilon(0.01));
    h.Frames(30);
    b = VertBox("p");
    CHECK(b.W() > w * 0.7f);
    CHECK(b.W() < w * 0.95f);
    h.Frames(40);
    b = VertBox("p");
    CHECK(b.W() == doctest::Approx(w).epsilon(0.03));
}

TEST_CASE("exit motion keeps drawing, then hides") {
    Harness h(Window(R"json({"id":"p","type":"Probe","visibleIf":"state.show","anim":{"motions":[{"custom":"TestFadeOut","trigger":"exit"}]}},
                            {"id":"q","type":"Probe","visibleIf":"state.show"})json").c_str());
    h.Frames(3);
    REQUIRE(h.Drawn("p"));
    int full = VertBox("p").alpha;
    CHECK(full > 0);
    *h.inst.State("show") = Value(false);
    h.Frame();
    CHECK(h.Drawn("p"));
    CHECK_FALSE(h.Drawn("q"));
    h.Frames(14);
    REQUIRE(h.Drawn("p"));
    int half = VertBox("p").alpha;
    CHECK(half < full);
    CHECK(half > 0);
    h.Frames(25);
    CHECK_FALSE(h.Drawn("p"));
    CHECK(g_probes.count("p") == 0);

    *h.inst.State("show") = Value(true);
    h.Frame();
    CHECK(h.Drawn("p"));
    CHECK(VertBox("p").alpha == full);
}

TEST_CASE("stagger on a container delays each child") {
    Harness h(Window(R"json({"id":"list","type":"VStack","anim":{"motions":[{"custom":"TestFade","stagger":0.25}]},"children":[
        {"id":"c0","type":"Probe"},{"id":"c1","type":"Probe"},{"id":"c2","type":"Probe"}]})json").c_str());
    h.Frames(24);
    int a0 = VertBox("c0").alpha, a1 = VertBox("c1").alpha, a2 = VertBox("c2").alpha;
    CHECK(a0 > a1);
    CHECK(a1 > a2);
    CHECK(a2 == 0);
    h.Frames(60);
    CHECK(VertBox("c0").alpha == VertBox("c2").alpha);
    CHECK(VertBox("c2").alpha > 0);
}

TEST_CASE("text reveal cuts UTF-8 text by characters") {
    CHECK(interp_motion::RevealText("abcd", 1.0f) == "abcd");
    CHECK(interp_motion::RevealText("abcd", 0.0f).empty());
    CHECK(interp_motion::RevealText("abcd", 0.5f) == "ab");
    CHECK(interp_motion::RevealText("a\xC3\xA9\xE6\xBC\xA2" "b", 0.5f) == "a\xC3\xA9");
    CHECK(interp_motion::RevealText("a\xC3\xA9\xE6\xBC\xA2" "b", 0.75f) == "a\xC3\xA9\xE6\xBC\xA2");

    const std::string full = "Hello w\xC3\xB6rld";
    Harness h(Window(R"json({"id":"p","type":"Probe","props":{"text":"Hello wörld"},"anim":{"motions":[{"custom":"TestType"}]}},
                            {"id":"t","type":"Text","props":{"text":"Hello wörld"},"anim":{"motions":[{"custom":"TestType"}]}},
                            {"id":"plain","type":"Probe","props":{"text":"Hello wörld"}})json").c_str());
    h.Frame();
    CHECK(g_probes["p"].text.empty());
    CHECK(g_probes["plain"].text == full);
    float tw0 = h.RectW("t");
    h.Frames(29);
    std::string mid = g_probes["p"].text;
    CHECK(mid.size() > 2);
    CHECK(mid.size() < full.size());
    CHECK(full.compare(0, mid.size(), mid) == 0);
    float tw_mid = h.RectW("t");
    h.Frames(40);
    CHECK(g_probes["p"].text == full);
    float tw1 = h.RectW("t");
    CHECK(tw0 < tw_mid);
    CHECK(tw_mid < tw1);
}

TEST_CASE("an event named in `on` restarts the motion") {
    Harness h(Window(R"json({"id":"p","type":"Probe","anim":{"motions":[{"custom":"TestShake","trigger":"attention","on":"onClick"}]}})json").c_str());
    h.Frames(3);
    CHECK(VertBox("p").mn.x == doctest::Approx(h.RectX("p")).epsilon(0.001));
    g_fire = true;
    h.Frame();
    g_fire = false;
    h.Frames(15);
    float shifted = VertBox("p").mn.x - h.RectX("p");
    CHECK(shifted > 5.0f);
    h.Frames(30);
    CHECK(VertBox("p").mn.x == doctest::Approx(h.RectX("p")).epsilon(0.001));
    g_fire = true;
    h.Frame();
    g_fire = false;
    h.Frames(15);
    CHECK(VertBox("p").mn.x - h.RectX("p") > 5.0f);

    DrawCtx probe_ctx(h.interp, h.panel, h.inst, h.roots, h.opts, h.panel.root.children[0]);
    CHECK(probe_ctx.HasEvent("onClick"));
    CHECK_FALSE(probe_ctx.HasEvent("onChange"));
}

TEST_CASE("custom animations come from the project and follow edits") {
    Harness h(Window(R"json({"id":"p","type":"Probe","anim":{"motions":[{"custom":"TestZoom"}]}},
                            {"id":"m","type":"Probe","anim":{"motions":[{"custom":"Missing"},{"preset":"NoSuchPreset"}]}})json").c_str());
    h.Frame();
    const imb::AnimDef* def = imb::FindAnim("TestZoom");
    REQUIRE(def != nullptr);
    CHECK(def->duration == doctest::Approx(1.0f));
    CHECK(h.HasDiag(Diagnostic::Level::Error, "unknown custom animation 'Missing'"));
    CHECK(h.HasDiag(Diagnostic::Level::Warning, "unknown motion preset 'NoSuchPreset'"));
    CHECK(VertBox("m").W() == doctest::Approx(h.RectW("m")).epsilon(0.03));

    h.project.animations[0] = ParseJ(R"json({"name":"TestZoom","category":"entrance","duration":2,
        "tracks":{"scale":[{"t":0,"v":0.5},{"t":1,"v":1}]}})json");
    h.Frame();
    def = imb::FindAnim("TestZoom");
    REQUIRE(def != nullptr);
    CHECK(def->duration == doctest::Approx(2.0f));

    h.project.animations.push_back(ParseJ(R"json({"name":"Broken","category":"entrance","duration":1})json"));
    h.Frame();
    CHECK(h.HasDiag(Diagnostic::Level::Error, "Broken"));
    CHECK(imb::FindAnim("Broken") == nullptr);

    std::vector<std::string> errors;
    CHECK(RegisterCustomAnimations({ParseJ(R"json({"name":"Fresh","category":"loop","duration":1,
        "tracks":{"rotate":[{"t":0,"v":0},{"t":1,"v":360}]}})json")}, &errors) == 1);
    CHECK(errors.empty());
    CHECK(imb::FindAnim("Fresh") != nullptr);
}

TEST_CASE("registered presets resolve and core validation sees them") {
    static const imb::Keyframe keys[] = {{0.0f, {20, 0, 0, 0}, imb::EaseSpec()}, {1.0f, {0, 0, 0, 0}, imb::EaseSpec()}};
    static const imb::Track tracks[] = {{imb::Prop::TranslateY, keys, 2}};
    static imb::AnimDef def;
    def.name = "TestPresetSlide";
    def.category = "entrance";
    def.duration = 1.0f;
    def.tracks = tracks;
    def.track_count = 1;
    imb::RegisterAnim(&def);

    Harness h(Window(R"json({"id":"p","type":"Probe","anim":{"motions":[{"preset":"TestPresetSlide"}]}})json").c_str());
    h.Frame();
    Box b = VertBox("p");
    CHECK(b.mn.y > h.rects["p"].first.y + 15.0f);

    REQUIRE(MotionPresetResolver() != nullptr);
    CHECK(MotionPresetResolver()("TestPresetSlide"));
    CHECK_FALSE(MotionPresetResolver()("NoSuchPreset"));
    Panel vp;
    vp.name = "V";
    Error e;
    REQUIRE(NodeFromJson(ParseJ(Window(R"json({"id":"b","type":"Button","props":{"label":"x"},"anim":{"motions":[
        {"preset":"TestPresetSlide"},{"preset":"NoSuchPreset"}]}})json").c_str()), vp.root, &e));
    std::vector<Diagnostic> diags;
    ValidatePanel(h.project, vp, diags);
    bool warned_bad = false, warned_good = false;
    for (const Diagnostic& d : diags) {
        warned_bad = warned_bad || d.message.find("NoSuchPreset") != std::string::npos;
        warned_good = warned_good || d.message.find("TestPresetSlide") != std::string::npos;
    }
    CHECK(warned_bad);
    CHECK_FALSE(warned_good);
}

TEST_CASE("bundled presets play without any registration") {
    CHECK(imb::AnimCount() >= 120);
    Harness h(Window(R"json({"id":"p","type":"Probe","anim":{"motions":[{"preset":"FadeIn"}]}},
                            {"id":"q","type":"Probe","props":{"text":"Typewriter"},"anim":{"motions":[{"preset":"Typewriter"}]}},
                            {"id":"r","type":"Probe"})json").c_str());
    h.Frame();
    CHECK(VertBox("p").alpha < VertBox("r").alpha / 2);
    CHECK(g_probes["q"].text.size() < 3);
    h.Frames(90);
    CHECK(VertBox("p").alpha == VertBox("r").alpha);
    CHECK(g_probes["q"].text == "Typewriter");
    CHECK_FALSE(h.HasDiag(Diagnostic::Level::Warning, "unknown motion preset"));
}

TEST_CASE("animations off skips motions") {
    Harness h(Window(R"json({"id":"p","type":"Probe","anim":{"motions":[{"custom":"TestZoom"}]}},
                            {"id":"x","type":"Probe","visibleIf":"state.show","anim":{"motions":[{"custom":"TestFadeOut","trigger":"exit"}]}})json").c_str());
    h.opts.animations = false;
    h.Frame();
    CHECK(VertBox("p").W() == doctest::Approx(h.RectW("p")).epsilon(0.03));
    *h.inst.State("show") = Value(false);
    h.Frame();
    CHECK_FALSE(h.Drawn("x"));
}

TEST_CASE("design mode can force hover through hooks.motion_options") {
    Harness h(Window(R"json({"id":"forced","type":"Probe","anim":{"motions":[{"custom":"TestGrow","trigger":"hover"}]}},
                            {"id":"idle","type":"Probe","anim":{"motions":[{"custom":"TestGrow","trigger":"hover"}]}})json").c_str());
    h.design = true;
    int calls = 0;
    h.interp.hooks.motion_options = [&calls](const Node& n, imb::MotionOptions& mo) {
        ++calls;
        if (n.id == "forced") mo.force_hover = true;
    };
    h.Frames(20);
    CHECK(calls > 0);
    CHECK(VertBox("forced").W() > h.RectW("forced") * 1.4f);
    CHECK(VertBox("idle").W() == doctest::Approx(h.RectW("idle")).epsilon(0.03));
}

TEST_CASE("window motions animate the content, in the builder and in a real window") {
    const char* root = R"json({"id":"win","type":"Window","props":{"title":"M","initialSize":{"x":800,"y":600},"initialPos":{"x":0,"y":0}},
        "anim":{"motions":[{"custom":"TestZoom"}]},"children":[{"id":"p","type":"Probe"}]})json";
    {
        Harness h(root);
        h.Frame();
        CHECK(VertBox("p").W() < h.RectW("p") * 0.6f);
        h.Frames(70);
        CHECK(VertBox("p").W() == doctest::Approx(h.RectW("p")).epsilon(0.03));
    }
    {
        Harness h(root);
        h.design = true;
        h.Frame();
        CHECK(VertBox("p").W() < h.RectW("p") * 0.6f);
        CHECK(h.Drawn("win"));
    }
}

TEST_CASE("panels whose root nodes share an id get their own window and motion state") {
    Harness h(R"json({"id":"window","type":"Window","props":{"title":"A","initialSize":{"x":300,"y":200},"initialPos":{"x":0,"y":0}},
        "anim":{"motions":[{"custom":"TestZoom"}]},"children":[{"id":"p","type":"Probe"}]})json");
    Panel other;
    other.name = "Q";
    Error e;
    REQUIRE(NodeFromJson(ParseJ(R"json({"id":"window","type":"Window","props":{"title":"B","initialSize":{"x":300,"y":200},"initialPos":{"x":400,"y":0}},
        "anim":{"motions":[{"custom":"TestFade"}]},"children":[{"id":"q","type":"Probe"}]})json"), other.root, &e));
    PanelInstance other_inst;
    for (int i = 0; i < 90; ++i) {
        h.rects.clear();
        g_probes.clear();
        h.gui.BeginFrame();
        h.interp.DrawPanel(h.panel, h.inst, h.roots, h.opts);
        h.interp.DrawPanel(other, other_inst, h.roots, h.opts);
        h.gui.EndFrame();
    }
    CHECK(VertBox("p").W() == doctest::Approx(h.RectW("p")).epsilon(0.03));
    CHECK(VertBox("q").alpha >= static_cast<int>((ImGui::GetColorU32(ImGuiCol_Button) >> IM_COL32_A_SHIFT) & 0xFF) - 1);
    CHECK(g_probes["p"].dl != g_probes["q"].dl);
    CHECK(VertBox("p").mx.x < 300.0f);
    CHECK(VertBox("q").mn.x > 400.0f);
}

TEST_CASE("more than eight motions on one node are cut off with a warning") {
    std::string motions;
    for (int i = 0; i < 10; ++i) motions += std::string(i ? "," : "") + R"json({"custom":"TestFade"})json";
    Harness h(Window((std::string(R"json({"id":"p","type":"Probe","anim":{"motions":[)json") + motions + "]}}").c_str()).c_str());
    h.Frames(3);
    CHECK(h.Drawn("p"));
    CHECK(h.HasDiag(Diagnostic::Level::Warning, "more than 8 motions"));
    h.Frames(60);
    CHECK(VertBox("p").alpha > 0);
}

TEST_CASE("a custom animation dropped from the project stops resolving") {
    Harness h(Window(R"json({"id":"p","type":"Probe","anim":{"motions":[{"custom":"TestZoom"}]}})json").c_str());
    h.Frame();
    CHECK(VertBox("p").W() < 80.0f);
    h.project.animations.clear();
    h.Frame();
    CHECK(h.HasDiag(Diagnostic::Level::Error, "unknown custom animation 'TestZoom'"));
    CHECK(VertBox("p").W() == doctest::Approx(100.0f).epsilon(0.02));
}

TEST_CASE("a grid inside an animated container is transformed with it") {
    Harness h(Window(R"json({"id":"c","type":"VStack","anim":{"motions":[{"custom":"TestZoom"}]},"children":[
        {"id":"t","type":"Grid","props":{"columns":2},"children":[{"id":"a","type":"Probe"},{"id":"b","type":"Probe"}]}]})json").c_str());
    h.Frame();
    h.Frame();
    REQUIRE(g_probes.count("a") == 1);
    for (const char* id : {"a", "b"}) {
        auto it = g_probes.find(id);
        if (it == g_probes.end()) continue;
        Box b = VertBox(id);
        CHECK(b.W() < 100.0f);
        CHECK(std::isfinite(b.mn.x));
    }
}

TEST_CASE("reveal presets anchor the visible area to the edge their name says") {
    struct Row { const char* name; imb::RevealDir dir; };
    const Row rows[] = {{"ClipRevealLeft", imb::RevealDir::Left}, {"ClipRevealRight", imb::RevealDir::Right},
                        {"ClipRevealUp", imb::RevealDir::Up}, {"ClipRevealDown", imb::RevealDir::Down},
                        {"ClipRevealCenter", imb::RevealDir::Center}, {"ClipHideLeft", imb::RevealDir::Left},
                        {"ClipHideRight", imb::RevealDir::Right}, {"ClipHideUp", imb::RevealDir::Up},
                        {"ClipHideDown", imb::RevealDir::Down}, {"ClipHideCenter", imb::RevealDir::Center},
                        {"StaggerReveal", imb::RevealDir::Left}, {"RevealLeft", imb::RevealDir::Left}};
    for (const Row& r : rows) {
        const imb::AnimDef* d = imb::FindAnim(r.name);
        REQUIRE_MESSAGE(d, r.name);
        CHECK_MESSAGE(d->reveal_dir == r.dir, r.name);
        for (float r_ : {0.25f, 0.5f}) {
            ImVec2 mn(0, 0), mx(100, 100);
            imb::detail::RevealRect(r.dir, r_, mn, mx);
            if (r.dir == imb::RevealDir::Left) CHECK((mn.x == 0.0f && mx.x == doctest::Approx(100 * r_)));
            if (r.dir == imb::RevealDir::Right) CHECK((mx.x == 100.0f && mn.x == doctest::Approx(100 * (1 - r_))));
            if (r.dir == imb::RevealDir::Up) CHECK((mn.y == 0.0f && mx.y == doctest::Approx(100 * r_)));
            if (r.dir == imb::RevealDir::Down) CHECK((mx.y == 100.0f && mn.y == doctest::Approx(100 * (1 - r_))));
            if (r.dir == imb::RevealDir::Center) CHECK(((mn.x + mx.x) * 0.5f == doctest::Approx(50.0f) && mx.x - mn.x == doctest::Approx(100 * r_)));
        }
    }
}
