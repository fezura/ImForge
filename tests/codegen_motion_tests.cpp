#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "codegen.h"
#include "codegen_test_util.h"
#include "imforge/imforge_core.h"
#include "imforge/imforge_motion_json.h"

using namespace imforge;
using namespace imforge::codegen;
using namespace codegen_test;

namespace {

const char* kMotionPanel = R"imbjson({ "schemaVersion": 1, "kind": "panel", "name": "Motion", "params": [],
  "state": [ { "name": "show", "type": "bool", "default": true }, { "name": "count", "type": "int", "default": 3 } ],
  "root": { "id": "win", "type": "Window", "props": { "title": "Motion", "initialSize": { "x": 500, "y": 400 }, "initialPos": { "x": 10, "y": 10 } },
    "anim": { "motions": [ { "preset": "FadeInUp", "durationScale": 0 } ] },
    "children": [
      { "id": "pop", "type": "Button", "props": { "label": "Pop" },
        "anim": { "motions": [ { "preset": "ZoomIn" }, { "preset": "Lift", "trigger": "hover", "durationScale": 0.5 } ] } },
      { "id": "fade", "type": "Text", "props": { "text": "sometimes" }, "visibleIf": "state.show",
        "anim": { "motions": [ { "preset": "FadeIn" }, { "preset": "FadeOut", "trigger": "exit" } ] } },
      { "id": "list", "type": "VStack", "anim": { "motions": [ { "preset": "StaggerFadeUp", "stagger": 0.05 } ] }, "children": [
          { "id": "row0", "type": "Text", "props": { "text": "zero" }, "anim": { "motions": [ { "preset": "FadeInUp", "stagger": 0.05 } ] } },
          { "id": "row1", "type": "Text", "props": { "text": "one" }, "anim": { "motions": [ { "preset": "FadeInUp", "stagger": 0.05 } ] } },
          { "id": "row2", "type": "Text", "props": { "text": "two" }, "anim": { "motions": [ { "preset": "FadeInUp", "stagger": 0.05 } ] } } ] },
      { "id": "chk", "type": "Checkbox", "props": { "label": "Show" }, "bind": { "value": "state.show" },
        "anim": { "motions": [ { "custom": "MyWiggle", "trigger": "attention", "on": "onChange" } ] } },
      { "id": "typed", "type": "Text", "props": { "text": "Count {state.count}" }, "anim": { "motions": [ { "preset": "Typewriter" } ] } },
      { "id": "typed_plain", "type": "Text", "props": { "text": "plain words", "color": "#ff8000ff" },
        "anim": { "motions": [ { "preset": "TypewriterSmooth", "delay": 0.1 } ] } },
      { "id": "spin", "type": "Button", "props": { "label": "Spin" }, "anim": { "motions": [ { "preset": "Spin", "trigger": "loop", "iterations": 0 } ] } },
      { "id": "sep", "type": "SeparatorText", "props": { "text": "Section" }, "anim": { "motions": [ { "preset": "Typewriter" } ] } },
      { "id": "both", "type": "Button", "props": { "label": "Both" }, "visibleIf": "state.show",
        "anim": { "enter": { "preset": "FadeSlideUp", "duration": 0.25 }, "motions": [ { "preset": "Pulse", "trigger": "attention", "on": "onClick" } ] } }
    ] } })imbjson";

const char* kWiggle = R"imbjson({ "schemaVersion": 1, "kind": "animation", "name": "MyWiggle", "category": "attention",
  "description": "small test wiggle", "duration": 0.5, "delay": 0.05, "iterations": 2, "direction": "alternate", "fill": "both",
  "origin": { "x": 0.5, "y": 1 }, "revealDir": "up", "trigger": "attention", "ease": "InOutSine",
  "tracks": {
    "rotate": [ { "t": 0, "v": 0 }, { "t": 0.5, "v": 8, "ease": "cubic-bezier(0.3,0,0.7,1)" }, { "t": 1, "v": 0 } ],
    "translateX": [ { "t": 0, "v": 0, "ease": "spring(170,26)" }, { "t": 1, "v": 0 } ],
    "scale": [ { "t": 0, "v": 1, "ease": "steps(4)" }, { "t": 0.5, "v": 1.1 }, { "t": 1, "v": 1 } ],
    "tint": [ { "t": 0, "v": "#ffffffff" }, { "t": 1, "v": "#ff8080ff" } ] } })imbjson";

const char* kPlainPanel = R"imbjson({ "schemaVersion": 1, "kind": "panel", "name": "Plain", "params": [], "state": [],
  "root": { "id": "win", "type": "Window", "props": { "title": "Plain" }, "children": [
      { "id": "b", "type": "Button", "props": { "label": "B" } } ] } })imbjson";

struct MotionFixture {
    Project project;
    Options opts;
    explicit MotionFixture(const std::vector<const char*>& panels, const std::vector<const char*>& anims = {}) {
        Json j;
        Error err;
        project.name = "MotionTest";
        project.live.enabled = false;
        for (const char* text : panels) {
            Panel p;
            REQUIRE(ParseJson(text, j));
            REQUIRE_MESSAGE(PanelFromJson(j, p, &err), err.message);
            project.panels.push_back(p);
        }
        for (const char* text : anims) {
            REQUIRE(ParseJson(text, j));
            project.animations.push_back(j);
        }
        opts = OptionsFromProject(project);
    }
    Result All() const { return GenerateAll(project, opts); }
};

void NoErrors(const Result& r) {
    for (const Diagnostic& d : r.diagnostics) CHECK_MESSAGE(d.level != Diagnostic::Level::Error, d.node_id << ": " << d.message);
}

std::vector<std::string> SplitLines(const std::string& s) {
    std::vector<std::string> out;
    size_t start = 0;
    while (start < s.size()) {
        size_t nl = s.find('\n', start);
        out.push_back(s.substr(start, nl == std::string::npos ? std::string::npos : nl - start));
        if (nl == std::string::npos) break;
        start = nl + 1;
    }
    return out;
}

}  // namespace

TEST_CASE("motions become a binding table and a BeginMotion scope") {
    MotionFixture fx({kMotionPanel}, {kWiggle});
    Result r = fx.All();
    NoErrors(r);
    const FileOut* cpp = r.Find("Motion.cpp");
    REQUIRE(cpp);
    const std::string& s = cpp->content;
    CHECK(Contains(s, "#include \"imb_motion.h\""));
    CHECK(Contains(s, "#include \"imb_motion_presets.h\""));
    CHECK(Contains(s, "#include \"imforge_animations.h\""));
    CHECK(Contains(s, "static const imb::MotionBinding imb_m_pop[] = {"));
    CHECK(Contains(s, "{imb::presets::ZoomIn(), imb::Trigger::Enter, 1.0f, 0.0f, -1, 0.0f},"));
    CHECK(Contains(s, "{imb::presets::Lift(), imb::Trigger::Hover, 0.5f, 0.0f, -1, 0.0f},"));
    CHECK(Contains(s, "const ImGuiID imb_mid_pop = ImGui::GetID(\"pop##m\");"));
    CHECK(Contains(s, "if (imb::BeginMotion(imb_mid_pop, imb_m_pop, 2, imb::MotionOptions())) {"));
    CHECK(Contains(s, "{imb::presets::Spin(), imb::Trigger::Loop, 1.0f, 0.0f, 0, 0.0f},"));
    CHECK(Contains(s, "{imforge_anim::MyWiggle(), imb::Trigger::Attention, 1.0f, 0.0f, -1, 0.0f},"));
    CHECK(Contains(s, "const ImGuiID imb_mid_win = ImGui::GetID(\"Motion/win##m\");\n    ImGui::SetNextWindowSize("));
    CHECK(Contains(s, "if (ImGui::Begin(\"Motion\", nullptr, 0)) {\n        if (imb::BeginMotion(imb_mid_win, imb_m_win, 1, imb::MotionOptions())) {"));
    CHECK(Contains(s, "{imb::presets::FadeInUp(), imb::Trigger::Enter, 1.0f, 0.0f, -1, 0.0f},"));
    size_t begins = 0, ends = 0;
    for (size_t p = 0; (p = s.find("imb::BeginMotion(", p)) != std::string::npos; ++p) ++begins;
    for (size_t p = 0; (p = s.find("imb::EndMotion();", p)) != std::string::npos; ++p) ++ends;
    CHECK(begins == 12);
    CHECK(ends == begins);
}

TEST_CASE("visibleIf drives MotionOptions.visible instead of an if") {
    MotionFixture fx({kMotionPanel}, {kWiggle});
    Result r = fx.All();
    const std::string& s = r.Find("Motion.cpp")->content;
    CHECK(Contains(s, "imb::MotionOptions imb_mo_fade;"));
    CHECK(Contains(s, "imb_mo_fade.visible = state.show;"));
    CHECK(Contains(s, "if (imb::BeginMotion(imb_mid_fade, imb_m_fade, 2, imb_mo_fade)) {"));
    CHECK(!Contains(s, "if (state.show) {"));
}

TEST_CASE("children of a staggered container get their stagger index") {
    MotionFixture fx({kMotionPanel}, {kWiggle});
    const std::string s = fx.All().Find("Motion.cpp")->content;
    CHECK(!Contains(s, "imb_mo_row0"));
    CHECK(Contains(s, "imb_mo_row1.stagger_index = 1;"));
    CHECK(Contains(s, "imb_mo_row2.stagger_index = 2;"));
    CHECK(Contains(s, "{imb::presets::FadeInUp(), imb::Trigger::Enter, 1.0f, 0.0f, -1, 0.05f},"));
}

TEST_CASE("an event named by `on` triggers the motion") {
    MotionFixture fx({kMotionPanel}, {kWiggle});
    const std::string s = fx.All().Find("Motion.cpp")->content;
    CHECK(Contains(s, "if (ImGui::Checkbox(\"Show\", &state.show)) {"));
    CHECK(Contains(s, "imb::TriggerMotion(imb_mid_chk, imb::Trigger::Attention);"));
}

TEST_CASE("text reveal motions truncate Text output") {
    MotionFixture fx({kMotionPanel}, {kWiggle});
    const std::string s = fx.All().Find("Motion.cpp")->content;
    CHECK(Contains(s, "std::snprintf(typed_txt, sizeof(typed_txt), \"Count %d\", (int)state.count);"));
    CHECK(Contains(s, "ImbTextReveal(typed_txt, imb::MotionTextReveal(imb_mid_typed));"));
    CHECK(Contains(s, "ImGui::TextUnformatted(typed_txt);"));
    CHECK(Contains(s, "ImGui::TextColored(ImVec4(1.0f, 0.502f, 0.0f, 1.0f), \"%s\", typed_plain_txt);"));
    CHECK(Contains(s, "inline void ImbTextReveal(char* text, float fraction) {"));
    CHECK(!Contains(s, "pop_txt"));
}

TEST_CASE("SeparatorText reveals too, legacy enter and motions open side by side") {
    MotionFixture fx({kMotionPanel}, {kWiggle});
    const std::string s = fx.All().Find("Motion.cpp")->content;
    CHECK(Contains(s, "std::snprintf(sep_txt, sizeof(sep_txt), \"%s\", \"Section\");"));
    CHECK(Contains(s, "ImbTextReveal(sep_txt, imb::MotionTextReveal(imb_mid_sep));"));
    CHECK(Contains(s, "ImGui::SeparatorText(sep_txt);"));
    CHECK(Contains(s, "imb_mo_both.visible = both_visible;"));
    CHECK(Contains(s, "const bool both_motion = imb::BeginMotion(imb_mid_both, imb_m_both, 1, imb_mo_both);"));
    CHECK(Contains(s, "const bool both_entered = imb::BeginEnter(ImGui::GetID(\"both\"), both_visible, both_visible ? 0.25f : 0.2f, imb::Preset::FadeSlideUp);"));
    CHECK(Contains(s, "if (both_motion || both_entered) {"));
    CHECK(Contains(s, "imb::TriggerMotion(imb_mid_both, imb::Trigger::Attention);"));
    size_t enter_end = s.find("if (both_entered) imb::EndEnter();"), motion_end = s.find("if (both_motion) imb::EndMotion();");
    REQUIRE(enter_end != std::string::npos);
    REQUIRE(motion_end != std::string::npos);
    CHECK(enter_end < motion_end);
}

TEST_CASE("motion headers are included only when used") {
    MotionFixture plain({kPlainPanel});
    Result pr = plain.All();
    for (const FileOut& f : pr.files) {
        CHECK_MESSAGE(!Contains(f.content, "imb_motion"), f.path);
        CHECK_MESSAGE(!Contains(f.content, "imforge_animations"), f.path);
    }
    CHECK(!pr.Find("imforge_animations.h"));
    CHECK(RuntimeHeadersFor(pr).empty());

    const char* presets_only = R"imbjson({ "schemaVersion": 1, "kind": "panel", "name": "P", "params": [], "state": [],
      "root": { "id": "win", "type": "Window", "props": { "title": "P" }, "children": [
        { "id": "b", "type": "Button", "props": { "label": "B" }, "anim": { "motions": [ { "preset": "FadeIn" } ] } } ] } })imbjson";
    MotionFixture pf({presets_only});
    Result r = pf.All();
    const std::string& s = r.Find("P.cpp")->content;
    CHECK(Contains(s, "#include \"imb_motion.h\""));
    CHECK(Contains(s, "#include \"imb_motion_presets.h\""));
    CHECK(!Contains(s, "imforge_animations"));
    std::vector<std::string> hs = RuntimeHeadersFor(r);
    CHECK(hs == std::vector<std::string>{"imb_anim.h", "imb_effects.h", "imb_motion.h", "imb_motion_presets.h", "imb_runtime.h"});

    pf.project.features.push_back({"animations", false});
    pf.opts = OptionsFromProject(pf.project);
    Result off = pf.All();
    CHECK(!Contains(off.Find("P.cpp")->content, "imb_motion"));
}

TEST_CASE("unknown presets and custom names are skipped with a warning") {
    const char* bad = R"imbjson({ "schemaVersion": 1, "kind": "panel", "name": "Bad", "params": [], "state": [],
      "root": { "id": "win", "type": "Window", "props": { "title": "Bad" }, "children": [
        { "id": "b", "type": "Button", "props": { "label": "B" }, "visibleIf": "true",
          "anim": { "motions": [ { "preset": "NoSuchThing" }, { "custom": "Missing" } ] } } ] } })imbjson";
    MotionFixture fx({bad});
    Result r = fx.All();
    int warns = 0;
    for (const Diagnostic& d : r.diagnostics) warns += d.level == Diagnostic::Level::Warning && d.node_id == "b";
    CHECK(warns == 2);
    const std::string& s = r.Find("Bad.cpp")->content;
    CHECK(!Contains(s, "imb::BeginMotion"));
    CHECK(!Contains(s, "imb_motion"));
    CHECK(Contains(s, "if (true) {"));
}

TEST_CASE("panel draw registers custom animations once") {
    MotionFixture fx({kMotionPanel}, {kWiggle});
    Result r = fx.All();
    const std::string& s = r.Find("Motion.cpp")->content;
    CHECK(Contains(s, "imforge_anim::RegisterAnimations();"));
    const FileOut* h = r.Find("imforge_animations.h");
    REQUIRE(h);
    CHECK(Contains(h->content, "#include \"imb_motion.h\""));
    CHECK(Contains(h->content, "inline const imb::AnimDef* MyWiggle()"));
    CHECK(Contains(h->content, "inline void RegisterAnimations()"));
    CHECK(Contains(h->content, "imb::RegisterAnim(MyWiggle());"));
    CHECK(h->content.find("\xe2\x80") == std::string::npos);
}

TEST_CASE("motion output is deterministic") {
    MotionFixture a({kMotionPanel}, {kWiggle});
    MotionFixture b({kMotionPanel}, {kWiggle});
    Result ra = a.All(), rb = b.All();
    REQUIRE(ra.files.size() == rb.files.size());
    for (size_t i = 0; i < ra.files.size(); ++i) {
        CHECK(ra.files[i].path == rb.files[i].path);
        CHECK_MESSAGE(ra.files[i].content == rb.files[i].content, ra.files[i].path);
        CHECK_MESSAGE(ra.files[i].content.find('\r') == std::string::npos, ra.files[i].path);
    }
    Result again = a.All();
    for (size_t i = 0; i < ra.files.size(); ++i) CHECK(ra.files[i].content == again.files[i].content);
}

#ifndef _WIN32

TEST_CASE("generated motion code compiles on 1.85, 1.91.9b and 1.93.0") {
    if (!HaveGxx()) {
        MESSAGE("g++ not available; skipping");
        return;
    }
    const char* const kVersions[] = {"imgui-v1.85", "imgui-v1.91.9b", "imgui-v1.93.0"};
    for (int split = 0; split < 2; ++split) {
        MotionFixture fx({kMotionPanel, kPlainPanel}, {kWiggle});
        fx.project.codegen.split_header = split == 1;
        fx.opts = OptionsFromProject(fx.project);
        Result r = fx.All();
        NoErrors(r);
        int ran = 0;
        for (const char* v : kVersions) {
            std::string dir = DepsDir() + "/" + v;
            if (!DirExists(dir)) {
                MESSAGE(v << " not found; skipped");
                continue;
            }
            CompileResult(r, {}, std::string("motion_") + v + (split ? "_split" : "_single"), dir, "ui");
            ++ran;
        }
        if (ran == 0) MESSAGE("no pinned ImGui checkout found");
    }
}

TEST_CASE("generated motion code and the presets header build as C++14") {
    if (!HaveGxx()) {
        MESSAGE("g++ not available; skipping");
        return;
    }
    MotionFixture fx({kMotionPanel}, {kWiggle});
    fx.project.codegen.cpp_standard = 14;
    fx.opts = OptionsFromProject(fx.project);
    Result r = fx.All();
    NoErrors(r);
    int ran = 0;
    for (const char* v : {"imgui-v1.85", "imgui-v1.91.9b"}) {
        std::string dir = DepsDir() + "/" + v;
        if (!DirExists(dir)) continue;
        CompileResult(r, {}, std::string("motion_cpp14_") + v, dir, "ui", "-std=c++14 -fno-exceptions -fno-rtti");
        ++ran;
    }
    if (ran == 0) MESSAGE("no pinned ImGui checkout found");
}

namespace {

bool RunProgram(const std::string& tag, const std::vector<FileOut>& files, const std::string& main_cpp, bool link_imgui,
                std::string& out) {
    std::string imgui = DepsDir() + "/imgui-v1.91.9b";
    if (!HaveGxx() || !DirExists(imgui)) {
        MESSAGE("g++ or imgui-v1.91.9b not available; skipping");
        return false;
    }
    std::string dir = TempRoot() + "/imforge_codegen_" + tag;
    REQUIRE(std::system(("rm -rf '" + dir + "' && mkdir -p '" + dir + "'").c_str()) == 0);
    for (const FileOut& f : files) REQUIRE(WriteFile(dir + "/" + f.path, f.content));
    REQUIRE(WriteFile(dir + "/main.cpp", main_cpp));
    std::string root = RepoRoot();
    std::string gxx = "g++ -std=c++17 -O0 -w -I'" + dir + "' -I'" + root + "/include' -I'" + root + "/include/imforge' -I'" + root +
                      "/tests' -I'" + imgui + "' ";
    std::vector<std::string> units = {dir + "/main.cpp"};
    for (const FileOut& f : files)
        if (f.path.size() > 4 && f.path.compare(f.path.size() - 4, 4, ".cpp") == 0) units.push_back(dir + "/" + f.path);
    if (link_imgui)
        for (const char* u : {"imgui.cpp", "imgui_draw.cpp", "imgui_tables.cpp", "imgui_widgets.cpp"}) units.push_back(imgui + "/" + u);
    std::string script = "cd '" + dir + "' || exit 1; ";
    std::string objs;
    for (size_t i = 0; i < units.size(); ++i) {
        std::string o = "u" + std::to_string(i) + ".o";
        script += "(" + gxx + "-c '" + units[i] + "' -o " + o + " 2>" + o + ".err) & ";
        objs += " " + o;
    }
    script += "wait; g++" + objs + " -o prog 2>link.err && ./prog >out.txt 2>run.err";
    int rc = std::system(script.c_str());
    std::string errs;
    for (size_t i = 0; i < units.size(); ++i) errs += ReadFile(dir + "/u" + std::to_string(i) + ".o.err");
    errs += ReadFile(dir + "/link.err") + ReadFile(dir + "/run.err");
    REQUIRE_MESSAGE(rc == 0, "build/run failed:\n" << errs);
    out = ReadFile(dir + "/out.txt");
    return true;
}

}  // namespace

TEST_CASE("custom animation header round trips through AnimToJson") {
    MotionFixture fx({kMotionPanel}, {kWiggle});
    Result r = fx.All();
    const FileOut* h = r.Find("imforge_animations.h");
    REQUIRE(h);
    AnimStorage st;
    Error err;
    REQUIRE_MESSAGE(AnimFromJson(fx.project.animations[0], st, &err), err.message);
    std::string expected = DumpJson(AnimToJson(*st.Def())) + "\n";
    std::string prog = R"cpp(#include "imforge_animations.h"
#include "imforge/imforge_motion_json.h"
#include <cstdio>
int main() {
    ui::imforge_anim::RegisterAnimations();
    ui::imforge_anim::RegisterAnimations();
    const imb::AnimDef* d = imb::FindAnim("MyWiggle");
    if (!d || d != ui::imforge_anim::MyWiggle()) return 2;
    std::printf("%s\n", imforge::DumpJson(imforge::AnimToJson(*d)).c_str());
    return 0;
}
)cpp";
    std::vector<FileOut> files = {*h};
    std::string out;
    if (!RunProgram("motion_anim_roundtrip", files, prog, true, out)) return;
    CHECK(out == expected);
}


namespace {

// draws the generated panel and the interpreter 10 frames each and compares, per frame, the bbox of
// the vertices whose rgb matches each color (alpha ignored, opacity motions scale it)
void CheckMotionParity(const std::string& tag, const char* panel, const std::string& name, const std::vector<std::string>& colors) {
    MotionFixture fx({panel});
    Result r = fx.All();
    NoErrors(r);
    std::string prog = "#include \"" + name + ".h\"\n#include \"imforge/imforge_interp.h\"\n#include \"imgui_headless.h\"\n#include <cstdio>\n";
    prog += std::string("static const char* kPanel = R\"imbjson(") + panel + ")imbjson\";\nstatic const ImU32 kRgb[] = {";
    for (size_t i = 0; i < colors.size(); ++i) prog += (i ? ", " : "") + colors[i];
    prog += "};\nstatic void DrawGenerated(ui::" + name + "State& st) { ui::Draw" + name + "(st); }\nusing GenState = ui::" + name + "State;\n";
    prog += R"cpp(
static void Dump(const char* side, int frame) {
    ImDrawData* dd = ImGui::GetDrawData();
    for (ImU32 c : kRgb) {
        float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
        int n = 0;
        for (int l = 0; l < dd->CmdListsCount; ++l) {
            const ImDrawList* dl = dd->CmdLists[l];
            for (int i = 0; i < dl->VtxBuffer.Size; ++i) {
                const ImDrawVert& v = dl->VtxBuffer[i];
                if ((v.col & 0x00ffffffu) != c || (v.col >> 24) == 0) continue;
                x0 = v.pos.x < x0 ? v.pos.x : x0;
                y0 = v.pos.y < y0 ? v.pos.y : y0;
                x1 = v.pos.x > x1 ? v.pos.x : x1;
                y1 = v.pos.y > y1 ? v.pos.y : y1;
                ++n;
            }
        }
        std::printf("%s %d %08x %d %.3f %.3f %.3f %.3f\n", side, frame, (unsigned)c, n, x0, y0, x1, y1);
    }
}
int main() {
    {
        imforge::test::HeadlessImGui g;
        GenState st;
        for (int f = 0; f < 10; ++f) {
            g.BeginFrame();
            DrawGenerated(st);
            g.EndFrame();
            Dump("gen", f);
        }
    }
    imb::ResetMotions();
    imb::Reset();
    {
        imforge::test::HeadlessImGui g;
        imforge::Project project;
        imforge::Json j;
        imforge::Error err;
        imforge::Panel panel;
        if (!imforge::ParseJson(kPanel, j) || !imforge::PanelFromJson(j, panel, &err)) return 3;
        imforge::Interpreter interp;
        interp.project = &project;
        imforge::PanelInstance inst;
        std::vector<imforge::BindingRoot> roots;
        imforge::InterpOptions opts;
        for (int f = 0; f < 10; ++f) {
            g.BeginFrame();
            interp.DrawPanel(panel, inst, roots, opts);
            g.EndFrame();
            Dump("interp", f);
        }
    }
    return 0;
}
)cpp";
    std::string out;
    if (!RunProgram(tag, r.files, prog, true, out)) return;
    std::vector<std::string> gen, interp;
    for (const std::string& l : SplitLines(out)) {
        if (l.compare(0, 4, "gen ") == 0) gen.push_back(l.substr(4));
        else if (l.compare(0, 7, "interp ") == 0) interp.push_back(l.substr(7));
    }
    REQUIRE(gen.size() == 10 * colors.size());
    REQUIRE(interp.size() == gen.size());
    std::vector<int> drawn(colors.size(), 0), moving(colors.size(), 0);
    for (size_t i = 0; i < gen.size(); ++i) {
        float a[4], b[4];
        unsigned ca = 0, cb = 0;
        int fa = 0, fb = 0, na = 0, nb = 0;
        REQUIRE(std::sscanf(gen[i].c_str(), "%d %x %d %f %f %f %f", &fa, &ca, &na, &a[0], &a[1], &a[2], &a[3]) == 7);
        REQUIRE(std::sscanf(interp[i].c_str(), "%d %x %d %f %f %f %f", &fb, &cb, &nb, &b[0], &b[1], &b[2], &b[3]) == 7);
        bool same = fa == fb && ca == cb && na == nb;
        for (int k = 0; same && na && k < 4; ++k) same = std::fabs(a[k] - b[k]) < 0.01f;
        CHECK_MESSAGE(same, "gen    " << gen[i] << "\ninterp " << interp[i]);
        size_t c = i % colors.size();
        drawn[c] += na > 0;
        if (i >= colors.size() && na) {
            float pa[4];
            int pf = 0, pn = 0;
            unsigned pc = 0;
            std::sscanf(gen[i - colors.size()].c_str(), "%d %x %d %f %f %f %f", &pf, &pc, &pn, &pa[0], &pa[1], &pa[2], &pa[3]);
            bool moved = pn != na;
            for (int k = 0; !moved && k < 4; ++k) moved = std::fabs(pa[k] - a[k]) > 0.05f;
            moving[c] += moved;
        }
    }
    for (size_t c = 0; c < colors.size(); ++c) {
        CHECK_MESSAGE(drawn[c] > 0, "never drawn: " << colors[c]);
        CHECK_MESSAGE(moving[c] > 0, "never animated: " << colors[c]);
    }
}

}  // namespace

TEST_CASE("scale and translate entrances match the interpreter for 10 frames") {
    const char* panel = R"imbjson({ "schemaVersion": 1, "kind": "panel", "name": "Par", "params": [], "state": [],
      "root": { "id": "win", "type": "Window", "props": { "title": "Par", "initialSize": { "x": 400, "y": 300 }, "initialPos": { "x": 20, "y": 20 } },
        "children": [
          { "id": "zoom", "type": "Button", "props": { "label": "Zoom", "size": { "w": 120, "h": 40 } }, "style": { "colors": { "Button": "#ff0000ff" } },
            "anim": { "motions": [ { "preset": "ZoomInUp" } ] } },
          { "id": "slide", "type": "Button", "props": { "label": "Slide", "size": { "w": 120, "h": 40 } }, "style": { "colors": { "Button": "#00ff00ff" } },
            "anim": { "motions": [ { "preset": "BackInLeft", "durationScale": 0.8 } ] } },
          { "id": "late", "type": "Button", "props": { "label": "Late" }, "style": { "colors": { "Button": "#0000ffff" } },
            "anim": { "motions": [ { "preset": "ScaleUpCenter", "delay": 0.05 } ] } }
        ] } })imbjson";
    CheckMotionParity("motion_parity", panel, "Par", {"IM_COL32(255, 0, 0, 0)", "IM_COL32(0, 255, 0, 0)", "IM_COL32(0, 0, 255, 0)"});
}

TEST_CASE("window content, stagger and typewriter motions match the interpreter") {
    const char* panel = R"imbjson({ "schemaVersion": 1, "kind": "panel", "name": "Par2", "params": [], "state": [],
      "root": { "id": "win", "type": "Window", "props": { "title": "Par2", "initialSize": { "x": 420, "y": 320 }, "initialPos": { "x": 30, "y": 10 } },
        "anim": { "motions": [ { "preset": "FadeInLeft", "durationScale": 0.5 } ] },
        "children": [
          { "id": "list", "type": "VStack", "anim": { "motions": [ { "preset": "StaggerScaleIn", "stagger": 0.04 } ] }, "children": [
              { "id": "a", "type": "Button", "props": { "label": "A", "size": { "w": 90, "h": 30 } }, "style": { "colors": { "Button": "#ffff00ff" } } },
              { "id": "b", "type": "Button", "props": { "label": "B", "size": { "w": 90, "h": 30 } }, "style": { "colors": { "Button": "#ff00ffff" } },
                "anim": { "motions": [ { "preset": "Spin", "trigger": "loop", "durationScale": 2 } ] } },
              { "id": "c", "type": "Button", "props": { "label": "C", "size": { "w": 90, "h": 30 } }, "style": { "colors": { "Button": "#ff8000ff" } } } ] },
          { "id": "typed", "type": "Text", "props": { "text": "typed out one letter at a time", "color": "#00ffffff" },
            "anim": { "motions": [ { "preset": "Typewriter", "durationScale": 0.15 } ] } }
        ] } })imbjson";
    CheckMotionParity("motion_parity2", panel, "Par2",
                      {"IM_COL32(255, 255, 0, 0)", "IM_COL32(255, 0, 255, 0)", "IM_COL32(255, 128, 0, 0)", "IM_COL32(0, 255, 255, 0)"});
}

TEST_CASE("legacy enter beside a motion and utf-8 text reveal match the interpreter") {
    const char* panel = R"imbjson({ "schemaVersion": 1, "kind": "panel", "name": "Par3", "params": [], "state": [],
      "root": { "id": "win", "type": "Window", "props": { "title": "Par3", "initialSize": { "x": 420, "y": 320 }, "initialPos": { "x": 30, "y": 10 } },
        "children": [
          { "id": "both", "type": "Button", "props": { "label": "Both", "size": { "w": 100, "h": 30 } }, "style": { "colors": { "Button": "#ffff00ff" } },
            "anim": { "enter": { "preset": "FadeSlideUp", "duration": 0.25 }, "motions": [ { "preset": "ZoomIn", "durationScale": 0.7 } ] } },
          { "id": "accent", "type": "Text", "props": { "text": "héllo wörld çà va", "color": "#00ffffff" },
            "anim": { "motions": [ { "preset": "Typewriter", "durationScale": 0.15 } ] } },
          { "id": "late", "type": "Button", "props": { "label": "Late", "size": { "w": 80, "h": 24 } }, "style": { "colors": { "Button": "#ff00ffff" } },
            "anim": { "motions": [ { "preset": "ZoomIn", "delay": 0.1, "stagger": 0.0 } ] } }
        ] } })imbjson";
    CheckMotionParity("motion_parity3", panel, "Par3", {"IM_COL32(255, 255, 0, 0)", "IM_COL32(0, 255, 255, 0)", "IM_COL32(255, 0, 255, 0)"});
}

#endif
