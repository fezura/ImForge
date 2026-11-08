#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "imgui.h"

#include "imforge/imforge_core.h"
#include "imforge/imforge_widgets_flags.h"

#if __has_include("imforge/imb_motion_presets.h")
#include "imforge/imb_motion_presets.h"
#define IMFORGE_TEST_HAVE_PRESETS 1
#endif

#include "imgui_headless.h"

#include "../src/builder/ui/theme_apply.cpp"
#include "../src/builder/design_apply.cpp"

#ifndef IMFORGE_SOURCE_DIR
#define IMFORGE_SOURCE_DIR "."
#endif

using namespace imforge;
using namespace imforge::builder;

namespace {

constexpr int kBaseImGui = 19191;

struct DesignFile {
    std::string stem;
    Json json;
};

const std::vector<DesignFile>& Designs() {
    static std::vector<DesignFile> out;
    static bool loaded = false;
    if (loaded) return out;
    loaded = true;
    namespace fs = std::filesystem;
    const fs::path dir = fs::path(IMFORGE_SOURCE_DIR) / "presets" / "designs";
    std::error_code ec;
    std::vector<fs::path> files;
    for (const auto& e : fs::directory_iterator(dir, ec)) {
        const std::string fn = e.path().filename().string();
        const std::string ext = ".imdesign.json";
        if (fn.size() > ext.size() && fn.compare(fn.size() - ext.size(), ext.size(), ext) == 0) files.push_back(e.path());
    }
    std::sort(files.begin(), files.end());
    for (const fs::path& p : files) {
        std::ifstream in(p, std::ios::binary);
        std::stringstream ss;
        ss << in.rdbuf();
        DesignFile d;
        const std::string fn = p.filename().string();
        d.stem = fn.substr(0, fn.size() - std::strlen(".imdesign.json"));
        JsonError je;
        if (!ParseJson(ss.str(), d.json, &je)) FAIL_CHECK(fn << ": bad json: " << je.message);
        out.push_back(std::move(d));
    }
    return out;
}

Vec4 Col(const Json& map, const char* name, const Vec4& fallback) {
    bool ok = false;
    Vec4 c = ParseColor(map[name].AsString(), &ok);
    return ok ? c : fallback;
}

Vec4 Over(const Vec4& fg, const Vec4& bg) {
    Vec4 r;
    r.x = fg.x * fg.w + bg.x * (1 - fg.w);
    r.y = fg.y * fg.w + bg.y * (1 - fg.w);
    r.z = fg.z * fg.w + bg.z * (1 - fg.w);
    r.w = 1;
    return r;
}

double Luminance(const Vec4& c) {
    auto ch = [](double v) { return v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4); };
    return 0.2126 * ch(c.x) + 0.7152 * ch(c.y) + 0.0722 * ch(c.z);
}

double Contrast(const Vec4& a, const Vec4& b) {
    double la = Luminance(a), lb = Luminance(b);
    if (la < lb) std::swap(la, lb);
    return (la + 0.05) / (lb + 0.05);
}

Vec4 Solid(float v) {
    Vec4 c;
    c.x = c.y = c.z = v;
    c.w = 1;
    return c;
}

bool KnownColor(const char* name) {
    static const char* const kNewer[] = {"InputTextCursor", "TreeLines", "UnsavedMarker", "DragDropTargetBg", "CheckboxSelectedBg"};
    if (std::strcmp(ImGuiColCanonicalName(name), name) != 0) return false;
    if (ImGuiColMinVersion(name) > 0) return true;
    for (const char* n : kNewer)
        if (std::strcmp(n, name) == 0) return true;
    return false;
}

void CheckVars(const std::string& where, const Json& vars) {
    for (const auto& kv : vars.obj) {
        const char* n = kv.first.c_str();
        INFO(where << " var " << kv.first);
        CHECK(ImGuiStyleVarMinVersion(n) > 0);
        if (StyleVarIsVec2(n)) {
            CHECK((kv.second.IsObject() && kv.second["x"].IsNumber() && kv.second["y"].IsNumber()));
            CHECK(kv.second["x"].AsDouble() >= 0);
            CHECK(kv.second["y"].AsDouble() >= 0);
        } else {
            REQUIRE(kv.second.IsNumber());
            CHECK(kv.second.AsDouble() >= 0);
        }
    }
}

bool HasProseDash(const std::string& s) {
    return s.find("\xE2\x80\x93") != std::string::npos || s.find("\xE2\x80\x94") != std::string::npos ||
           s.find("\xE2\x86\x92") != std::string::npos;
}

Node MakeNode(const char* id, const char* type) {
    Node n;
    n.id = id;
    n.type = type;
    return n;
}

}  // namespace

TEST_CASE("design files have the basic fields") {
    const auto& designs = Designs();
    REQUIRE(!designs.empty());
    for (const DesignFile& f : designs) {
        const Json& d = f.json;
        INFO(f.stem);
        CHECK(d["schemaVersion"].AsInt() == 1);
        CHECK(d["kind"].AsString() == "design");
        CHECK(d["name"].AsString() == f.stem);
        CHECK(!d["title"].AsString().empty());
        CHECK(!d["description"].AsString().empty());
        CHECK(!HasProseDash(d["title"].AsString()));
        CHECK(!HasProseDash(d["description"].AsString()));
        for (const char* cls : {"accent-button", "ghost-button", "danger", "card", "muted", "heading"})
            CHECK_MESSAGE(d["styleClasses"][cls].IsObject(), "missing class " << cls);
        for (const char* k : {"accent", "accent2", "success", "warning", "danger"}) {
            bool ok = false;
            ParseColor(d["widgetStyle"][k].AsString(), &ok);
            CHECK_MESSAGE(ok, "widgetStyle." << k);
        }
        for (const Json& font : d["fonts"].arr) {
            CHECK(!font["family"].AsString().empty());
            CHECK(!font["role"].AsString().empty());
        }
    }
}

TEST_CASE("designs define every 1.91.9 color and only known names") {
    std::vector<const char*> required;
    size_t n = 0;
    const char* const* names = ImGuiColNames(n);
    for (size_t i = 0; i < n; ++i)
        if (ImGuiColMinVersion(names[i]) <= kBaseImGui) required.push_back(names[i]);
    names = ImGuiColNamesDocking(n);
    for (size_t i = 0; i < n; ++i) required.push_back(names[i]);
    CHECK(required.size() >= 58);

    for (const DesignFile& f : Designs()) {
        const Json& colors = f.json["theme"]["colors"];
        INFO(f.stem);
        REQUIRE(colors.IsObject());
        for (const char* name : required) CHECK_MESSAGE(colors[name].IsString(), "missing color " << name);
        for (const auto& kv : colors.obj) {
            CHECK_MESSAGE(KnownColor(kv.first.c_str()), "unknown color " << kv.first);
            bool ok = false;
            ParseColor(kv.second.AsString(), &ok);
            CHECK_MESSAGE(ok, "bad hex for " << kv.first);
        }
        for (const auto& cls : f.json["styleClasses"].obj)
            for (const auto& kv : cls.second["colors"].obj)
                CHECK_MESSAGE(KnownColor(kv.first.c_str()), cls.first << ": unknown color " << kv.first);
    }
}

TEST_CASE("design style vars have the right shape") {
    for (const DesignFile& f : Designs()) {
        const Json& vars = f.json["theme"]["vars"];
        INFO(f.stem);
        REQUIRE(vars.IsObject());
        for (const char* v : {"WindowPadding", "WindowRounding", "WindowBorderSize", "FramePadding", "FrameRounding",
                              "FrameBorderSize", "ItemSpacing", "GrabRounding", "TabRounding", "ScrollbarSize"})
            CHECK_MESSAGE((vars[v].IsNumber() || vars[v].IsObject()), "missing var " << v);
        CheckVars(f.stem, vars);
        if (vars["Alpha"].IsNumber()) CHECK(vars["Alpha"].AsDouble() <= 1.0);
        if (vars["WindowMinSize"].IsObject()) {
            CHECK(vars["WindowMinSize"]["x"].AsDouble() >= 1.0);
            CHECK(vars["WindowMinSize"]["y"].AsDouble() >= 1.0);
        }
        for (const auto& cls : f.json["styleClasses"].obj) CheckVars(f.stem + "." + cls.first, cls.second["vars"]);
    }
}

TEST_CASE("design text is readable") {
    for (const DesignFile& f : Designs()) {
        const Json& c = f.json["theme"]["colors"];
        INFO(f.stem);
        const Vec4 text = Col(c, "Text", Solid(1));
        const Vec4 dim = Col(c, "TextDisabled", Solid(0.5f));
        // translucent windows have to hold up over both a dark and a bright scene
        for (float backdrop : {0.0f, 1.0f}) {
            INFO("backdrop " << backdrop);
            const Vec4 win = Over(Col(c, "WindowBg", Solid(0)), Solid(backdrop));
            const Vec4 child = Over(Col(c, "ChildBg", Vec4()), win);
            const Vec4 frame = Over(Col(c, "FrameBg", Vec4()), child);
            const Vec4 button = Over(Col(c, "Button", Vec4()), child);
            CHECK(Contrast(text, win) >= 4.5);
            CHECK(Contrast(text, child) >= 4.5);
            CHECK(Contrast(text, frame) >= 4.5);
            CHECK(Contrast(text, button) >= 4.5);
            CHECK(Contrast(dim, win) >= 3.0);
            CHECK(Contrast(dim, child) >= 3.0);
            CHECK(Contrast(dim, frame) >= 3.0);
            for (const char* cls : {"accent-button", "danger"}) {
                const Json& cc = f.json["styleClasses"][cls]["colors"];
                if (!cc["Button"].IsString()) continue;
                const Vec4 t = Col(cc, "Text", text);
                CHECK_MESSAGE(Contrast(t, Over(Col(cc, "Button", Vec4()), win)) >= 4.5, cls << " text on Button");
            }
        }
    }
}

TEST_CASE("designs apply to ImGuiStyle and draw a frame") {
    test::HeadlessImGui im;
    for (const DesignFile& f : Designs()) {
        INFO(f.stem);
        Theme theme;
        Error err;
        REQUIRE_MESSAGE(ThemeFromDesign(f.json, theme, &err), err.message);
        ImGui::GetStyle() = ImGuiStyle();
        ApplyThemeToStyle(theme, ImGui::GetStyle());
        const ImVec4 t = ImGui::GetStyle().Colors[ImGuiCol_Text];
        const Vec4 want = Col(f.json["theme"]["colors"], "Text", Vec4());
        CHECK(std::fabs(t.x - want.x) < 1e-3f);
        CHECK(std::fabs(t.w - want.w) < 1e-3f);

        std::vector<StyleOverride> classes;
        for (const auto& kv : f.json["styleClasses"].obj) {
            StyleOverride so;
            CHECK_MESSAGE(core_detail::StyleFromJson(kv.second, so, &err, kv.first), err.message);
            classes.push_back(so);
        }
        im.Frames(2, [&] {
            ImGui::SetNextWindowSize(ImVec2(420, 360));
            ImGui::Begin("design");
            ImGui::Text("label");
            ImGui::TextDisabled("disabled");
            ImGui::SeparatorText("section");
            static bool check = true;
            static float value = 0.4f;
            static char buf[32] = "text";
            ImGui::Checkbox("check", &check);
            ImGui::SliderFloat("slider", &value, 0, 1);
            ImGui::InputText("input", buf, sizeof(buf));
            ImGui::ProgressBar(value);
            if (ImGui::BeginTabBar("tabs")) {
                if (ImGui::BeginTabItem("one")) ImGui::EndTabItem();
                if (ImGui::BeginTabItem("two")) ImGui::EndTabItem();
                ImGui::EndTabBar();
            }
            if (ImGui::BeginTable("table", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
                ImGui::TableSetupColumn("a");
                ImGui::TableSetupColumn("b");
                ImGui::TableHeadersRow();
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::Selectable("row", true);
                ImGui::TableNextColumn();
                ImGui::Text("1");
                ImGui::EndTable();
            }
            ImGui::CollapsingHeader("header", ImGuiTreeNodeFlags_DefaultOpen);
            for (const StyleOverride& so : classes) {
                StyleScope s = PushStyleOverride(so);
                ImGui::PushID(&so);
                ImGui::BeginChild("card", ImVec2(0, 40), true);
                ImGui::Button("button");
                ImGui::EndChild();
                ImGui::PopID();
                PopStyleScope(s);
            }
            ImGui::End();
        });
    }
    ImGui::GetStyle() = ImGuiStyle();
}

TEST_CASE("designs apply to a project in one step") {
    for (const DesignFile& f : Designs()) {
        INFO(f.stem);
        Project p;
        p.name = "P";
        Panel panel;
        panel.name = "Main";
        panel.root = MakeNode("root", "Window");
        Node list = MakeNode("list", "VStack");
        list.children.push_back(MakeNode("a", "Text"));
        list.children.push_back(MakeNode("b", "Text"));
        Node ok = MakeNode("ok", "Button");
        ok.style.classes.push_back("accent-button");
        list.children.push_back(ok);
        panel.root.children.push_back(list);
        panel.root.children.push_back(MakeNode("c", "Checkbox"));
        p.panels.push_back(panel);

        DesignApplyResult r;
        Error err;
        REQUIRE_MESSAGE(ApplyDesign(p, f.json, kApplyEverything, &r, &err), err.message);
        CHECK(p.active_theme == f.stem);
        CHECK(r.classes == (int)f.json["styleClasses"].obj.size());
        CHECK(r.warnings.empty());
        if (f.json["motions"].IsObject() && !f.json["motions"].obj.empty()) CHECK(r.motions > 0);
    }
}

TEST_CASE("design motions use existing presets") {
#ifdef IMFORGE_TEST_HAVE_PRESETS
    std::set<std::string> known;
    int count = 0;
    const char* const* names = imb::presets::Names(count);
    for (int i = 0; i < count; ++i) known.insert(names[i]);
    REQUIRE(!known.empty());
    for (const DesignFile& f : Designs()) {
        INFO(f.stem);
        for (const auto& kv : f.json["motions"].obj) {
            const size_t dot = kv.first.find('.');
            if (dot != std::string::npos) CHECK_MESSAGE(IsMotionTriggerName(kv.first.substr(dot + 1)), kv.first);
            std::string name = kv.second.IsString() ? kv.second.AsString() : kv.second["preset"].AsString();
            if (name.empty() && kv.second["custom"].IsString()) continue;
            CHECK_MESSAGE(known.count(name) == 1, kv.first << ": unknown preset " << name);
        }
    }
#else
    MESSAGE("imb_motion_presets.h not generated yet, skipping");
#endif
}
