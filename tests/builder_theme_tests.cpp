#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include <cmath>
#include <cstring>
#include <string>
#include <vector>

#include "imgui.h"
#include "imgui_internal.h"

#include "imforge/imforge_core.h"

#include "imgui_headless.h"

#include "../src/builder/ui/theme_apply.cpp"
#include "../src/builder/panels/theme_import.cpp"

#define VarDef PresetVarDef
#define kVars kPresetVars
#define Num PresetNum
#define Trim PresetTrim
#define ParseColorExpr PresetParseColorExpr
#define StripComments PresetStripComments
#include "../src/builder/ui/theme_presets.cpp"
#undef VarDef
#undef kVars
#undef Num
#undef Trim
#undef ParseColorExpr
#undef StripComments

using namespace imforge;
using namespace imforge::builder;

namespace {

const Vec4* Col(const Theme& t, const char* name) {
    for (const auto& c : t.colors)
        if (c.first == name) return &c.second;
    return nullptr;
}

const Json* Var(const Theme& t, const char* name) {
    for (const auto& v : t.vars)
        if (v.first == name) return &v.second;
    return nullptr;
}

bool Near(float a, float b, float eps = 1e-5f) { return std::fabs(a - b) <= eps; }

bool NearColor(const Vec4* c, float x, float y, float z, float w, float eps = 1e-5f) {
    return c && Near(c->x, x, eps) && Near(c->y, y, eps) && Near(c->z, z, eps) && Near(c->w, w, eps);
}

float Scalar(const Theme& t, const char* name) {
    const Json* j = Var(t, name);
    REQUIRE_MESSAGE(j, "missing var " << name);
    float f = 0;
    ImVec2 v2;
    REQUIRE(StyleVarValue(*j, false, f, v2));
    return f;
}

ImVec2 Vec(const Theme& t, const char* name) {
    const Json* j = Var(t, name);
    REQUIRE_MESSAGE(j, "missing var " << name);
    float f = 0;
    ImVec2 v2;
    REQUIRE(StyleVarValue(*j, true, f, v2));
    return v2;
}

bool HasWarning(const ThemeImportResult& r, const char* needle) {
    for (const std::string& w : r.warnings)
        if (w.find(needle) != std::string::npos) return true;
    return false;
}

bool InTable(const char* const* names, size_t n, const std::string& name) {
    for (size_t i = 0; i < n; ++i)
        if (name == names[i]) return true;
    return false;
}

bool KnownColorName(const std::string& name) {
    size_t n = 0;
    const char* const* names = ImGuiColNames(n);
    if (InTable(names, n, name)) return true;
    names = ImGuiColNamesDocking(n);
    return InTable(names, n, name);
}

bool KnownVarName(const std::string& name) {
    size_t n = 0;
    const char* const* names = ImGuiStyleVarNames(n);
    if (InTable(names, n, name)) return true;
    names = ImGuiStyleVarNamesDocking(n);
    return InTable(names, n, name);
}

float Luminance(const Vec4& c) { return 0.2126f * c.x + 0.7152f * c.y + 0.0722f * c.z; }

const char* const kForwardCompatColors[] = {"InputTextCursor"};

bool ForwardCompat(const std::string& name) {
    for (const char* n : kForwardCompatColors)
        if (name == n) return true;
    return false;
}

bool NotInThisImGui(const std::string& name) {
    if (ForwardCompat(name)) return true;
#ifndef IMGUI_HAS_DOCK
    size_t n = 0;
    const char* const* dock = ImGuiColNamesDocking(n);
    if (InTable(dock, n, name)) return true;
#endif
    return ImGuiColMinVersion(name.c_str()) > IMGUI_VERSION_NUM;
}

const float kB = 1.0f / 255.0f;

}  // namespace

TEST_CASE("import: ImVec4 float style dump") {
    const char* code = R"(
ImVec4* colors = ImGui::GetStyle().Colors;
colors[ImGuiCol_Text]                   = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
colors[ImGuiCol_TextDisabled]           = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);
colors[ImGuiCol_WindowBg]               = ImVec4(0.06f, 0.06f, 0.06f, 0.94f);
colors[ImGuiCol_FrameBg]                = ImVec4(0.16f, 0.29f, 0.48f, 0.54f);
colors[ImGuiCol_Border]                 = ImVec4(1, 1, 1, 0.5);
)";
    Theme t;
    ThemeImportResult r = ImportThemeFromCode(code, t, true);
    CHECK(r.colors == 5);
    CHECK(r.vars == 0);
    CHECK(r.base.empty());
    CHECK_MESSAGE(r.warnings.empty(), (r.warnings.empty() ? std::string() : r.warnings[0]));
    CHECK(t.colors.size() == 5);
    CHECK(NearColor(Col(t, "Text"), 1, 1, 1, 1));
    CHECK(NearColor(Col(t, "TextDisabled"), 0.5f, 0.5f, 0.5f, 1));
    CHECK(NearColor(Col(t, "WindowBg"), 0.06f, 0.06f, 0.06f, 0.94f));
    CHECK(NearColor(Col(t, "FrameBg"), 0.16f, 0.29f, 0.48f, 0.54f));
    CHECK(NearColor(Col(t, "Border"), 1, 1, 1, 0.5f));
}

TEST_CASE("import: ImColor ints, IM_COL32, packed u32 and ImColor floats") {
    const char* code = R"(
ImGuiStyle& style = ImGui::GetStyle();
style.Colors[ImGuiCol_Button] = ImColor(255, 128, 0, 255);
style.Colors[ImGuiCol_ButtonHovered] = ImColor(255, 128, 0);
style.Colors[ImGuiCol_ButtonActive] = ImGui::ColorConvertU32ToFloat4(IM_COL32(10, 20, 30, 40));
style.Colors[ImGuiCol_Header] = ImColor(IM_COL32(0, 0, 255, 255));
style.Colors[ImGuiCol_HeaderHovered] = ImGui::ColorConvertU32ToFloat4(0xFF0000FF);
style.Colors[ImGuiCol_HeaderActive] = ImColor(0.5f, 0.25f, 0.125f, 1.0f);
style.Colors[ImGuiCol_CheckMark] = ImColor(0xFF00FF00);
)";
    Theme t;
    ThemeImportResult r = ImportThemeFromCode(code, t, true);
    CHECK(r.colors == 7);
    CHECK_MESSAGE(r.warnings.empty(), (r.warnings.empty() ? std::string() : r.warnings[0]));
    CHECK(NearColor(Col(t, "Button"), 1, 128 * kB, 0, 1));
    CHECK(NearColor(Col(t, "ButtonHovered"), 1, 128 * kB, 0, 1));
    CHECK(NearColor(Col(t, "ButtonActive"), 10 * kB, 20 * kB, 30 * kB, 40 * kB));
    CHECK(NearColor(Col(t, "Header"), 0, 0, 1, 1));
    CHECK(NearColor(Col(t, "HeaderHovered"), 1, 0, 0, 1));
    CHECK(NearColor(Col(t, "HeaderActive"), 0.5f, 0.25f, 0.125f, 1));
    CHECK(NearColor(Col(t, "CheckMark"), 0, 1, 0, 1));
}

TEST_CASE("import: component assignment, color copies and integer components") {
    const char* code = R"(
colors[ImGuiCol_Border] = ImVec4(1, 1, 1, 0.5);
colors[ImGuiCol_Border].w = 0.25f;
colors[ImGuiCol_Separator] = colors[ImGuiCol_Border];
colors[ImGuiCol_PopupBg] = ImVec4(255, 255, 255, 128);
ImGui::PushStyleColor(ImGuiCol_Tab, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
colors[ImGuiCol_TitleBg].x = 128;
)";
    Theme t;
    ThemeImportResult r = ImportThemeFromCode(code, t, true);
    CHECK(r.colors == 6);
    CHECK(NearColor(Col(t, "Border"), 1, 1, 1, 0.25f));
    CHECK(NearColor(Col(t, "Separator"), 1, 1, 1, 0.25f));
    CHECK(NearColor(Col(t, "PopupBg"), 1, 1, 1, 128 * kB));
    CHECK(HasWarning(r, "PopupBg: integer components treated as 0-255"));
    ImGuiStyle dark;
    ImGui::StyleColorsDark(&dark);
    ImVec4 td = dark.Colors[ImGuiCol_TextDisabled];
    CHECK(NearColor(Col(t, "Tab"), td.x, td.y, td.z, td.w));
    ImVec4 tb = dark.Colors[ImGuiCol_TitleBg];
    CHECK(NearColor(Col(t, "TitleBg"), 128 * kB, tb.y, tb.z, tb.w));
    CHECK(r.warnings.size() == 1);
}

TEST_CASE("import: style variables through members and PushStyleVar") {
    const char* code = R"(
ImGuiStyle& style = ImGui::GetStyle();
style.FrameRounding = 4.f;
style.WindowRounding = 0.0f;
style.WindowPadding = ImVec2(8, 8);
style.ItemSpacing = ImVec2(6.0f, 4.0f);
style.FramePadding.x = 10;
style.Alpha = 1.0f;
style.WindowMenuButtonPosition = ImGuiDir_None;
style.AntiAliasedLines = true;
ImGui::PushStyleVar(ImGuiStyleVar_GrabRounding, 3.0f);
ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(5, 2));
ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, {3, 3});
style->ScrollbarSize = 14;
style.ScrollbarRounding = -2;
)";
    Theme t;
    ThemeImportResult r = ImportThemeFromCode(code, t, true);
    CHECK(r.colors == 0);
    CHECK(r.vars == 11);
    CHECK(Scalar(t, "FrameRounding") == 4.0f);
    CHECK(Scalar(t, "WindowRounding") == 0.0f);
    CHECK(Vec(t, "WindowPadding").x == 8.0f);
    CHECK(Vec(t, "WindowPadding").y == 8.0f);
    CHECK(Vec(t, "ItemSpacing").x == 6.0f);
    CHECK(Vec(t, "ItemSpacing").y == 4.0f);
    CHECK(Vec(t, "FramePadding").x == 10.0f);
    CHECK(Vec(t, "FramePadding").y == ImGuiStyle().FramePadding.y);
    CHECK(Scalar(t, "Alpha") == 1.0f);
    CHECK(Scalar(t, "GrabRounding") == 3.0f);
    CHECK(Vec(t, "CellPadding").x == 5.0f);
    CHECK(Vec(t, "CellPadding").y == 2.0f);
    CHECK(Vec(t, "ItemInnerSpacing").x == 3.0f);
    CHECK(Scalar(t, "ScrollbarSize") == 14.0f);
    CHECK(Scalar(t, "ScrollbarRounding") == -2.0f);
    CHECK(HasWarning(r, "ignored style member WindowMenuButtonPosition"));
    CHECK(HasWarning(r, "ignored style member AntiAliasedLines"));
    CHECK(r.warnings.size() == 2);
    CHECK(Var(t, "style") == nullptr);
    CHECK(Var(t, "Colors") == nullptr);
}

TEST_CASE("import: legacy and docking color names resolve to the current names") {
    const char* code = R"(
colors[ImGuiCol_TabActive] = ImVec4(0.2f, 0.4f, 0.8f, 1.0f);
colors[ImGuiCol_TabUnfocused] = ImVec4(0.1f, 0.1f, 0.1f, 1.0f);
colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.15f, 0.15f, 0.15f, 1.0f);
colors[ImGuiCol_NavHighlight] = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
colors[ImGuiCol_ModalWindowDarkening] = ImVec4(0.8f, 0.8f, 0.8f, 0.35f);
colors[ImGuiCol_ChildWindowBg] = ImVec4(0, 0, 0, 0);
colors[ImGuiCol_Column] = ImVec4(0.5f, 0.5f, 0.5f, 0.6f);
colors[ImGuiCol_DockingPreview] = ImVec4(0.26f, 0.59f, 0.98f, 0.70f);
colors[ImGuiCol_DockingEmptyBg] = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
style.DockingSeparatorSize = 2.0f;
style.TabBarOverlineSize = 1.0f;
)";
    Theme t;
    ThemeImportResult r = ImportThemeFromCode(code, t, true);
    CHECK(r.colors == 9);
    CHECK(r.vars == 2);
    CHECK(r.warnings.empty());
    CHECK(NearColor(Col(t, "TabSelected"), 0.2f, 0.4f, 0.8f, 1));
    CHECK(NearColor(Col(t, "TabDimmed"), 0.1f, 0.1f, 0.1f, 1));
    CHECK(NearColor(Col(t, "TabDimmedSelected"), 0.15f, 0.15f, 0.15f, 1));
    CHECK(NearColor(Col(t, "NavCursor"), 0.26f, 0.59f, 0.98f, 1));
    CHECK(NearColor(Col(t, "ModalWindowDimBg"), 0.8f, 0.8f, 0.8f, 0.35f));
    CHECK(NearColor(Col(t, "ChildBg"), 0, 0, 0, 0));
    CHECK(NearColor(Col(t, "Separator"), 0.5f, 0.5f, 0.5f, 0.6f));
    CHECK(NearColor(Col(t, "DockingPreview"), 0.26f, 0.59f, 0.98f, 0.7f));
    CHECK(NearColor(Col(t, "DockingEmptyBg"), 0.2f, 0.2f, 0.2f, 1));
    CHECK(Col(t, "TabActive") == nullptr);
    CHECK(Col(t, "NavHighlight") == nullptr);
    CHECK(Scalar(t, "DockingSeparatorSize") == 2.0f);
    CHECK(Scalar(t, "TabBarOverlineSize") == 1.0f);
    for (const auto& c : t.colors) CHECK_MESSAGE(KnownColorName(c.first), c.first);
}

TEST_CASE("import: StyleColorsDark base, comments, COUNT loops and ScaleAllSizes") {
    const char* code = R"(
// my theme  colors[ImGuiCol_Text] = ImVec4(0, 0, 0, 0);
ImGui::StyleColorsDark();
ImVec4* colors = ImGui::GetStyle().Colors;
/* accent colors[ImGuiCol_Button] = ImVec4(0, 0, 0, 0); */ colors[ImGuiCol_CheckMark] = ImVec4(0.9f, 0.5f, 0.1f, 1.0f);
for (int i = 0; i < ImGuiCol_COUNT; i++) { ImVec4& col = colors[i]; col.w *= 0.9f; }
colors[ImGuiCol_Text] = ImVec4(0.9f, 0.9f, 0.9f, 1.0f); // brighter
style.ScaleAllSizes(2.0f);
)";
    Theme t;
    ThemeImportResult r = ImportThemeFromCode(code, t, true);
    CHECK(r.base == "Dark");
    CHECK(r.colors == ImGuiCol_COUNT + 2);
    CHECK((int)t.colors.size() == ImGuiCol_COUNT);
    CHECK(NearColor(Col(t, "CheckMark"), 0.9f, 0.5f, 0.1f, 1));
    CHECK(NearColor(Col(t, "Text"), 0.9f, 0.9f, 0.9f, 1));
    ImGuiStyle dark;
    ImGui::StyleColorsDark(&dark);
    ImVec4 wb = dark.Colors[ImGuiCol_WindowBg];
    CHECK(NearColor(Col(t, "WindowBg"), wb.x, wb.y, wb.z, wb.w));
    ImVec4 btn = dark.Colors[ImGuiCol_Button];
    CHECK(NearColor(Col(t, "Button"), btn.x, btn.y, btn.z, btn.w));
    CHECK(HasWarning(r, "ScaleAllSizes() ignored"));
    CHECK(r.warnings.size() == 1);

    Theme light;
    ThemeImportResult rl = ImportThemeFromCode("ImGui::StyleColorsLight(&style); style.FrameRounding = 2;", light, true);
    CHECK(rl.base == "Light");
    ImGuiStyle ls;
    ImGui::StyleColorsLight(&ls);
    ImVec4 lwb = ls.Colors[ImGuiCol_WindowBg];
    CHECK(NearColor(Col(light, "WindowBg"), lwb.x, lwb.y, lwb.z, lwb.w));
    CHECK(Scalar(light, "FrameRounding") == 2.0f);
    Theme classic;
    CHECK(ImportThemeFromCode("ImGui::StyleColorsClassic();", classic, true).base == "Classic");
}

TEST_CASE("import: replace_existing and merge semantics") {
    Theme t;
    t.name = "Mine";
    t.colors.emplace_back("Text", Vec4{0, 0, 0, 1});
    t.colors.emplace_back("Button", Vec4{0.1f, 0.2f, 0.3f, 1});
    t.vars.emplace_back("FrameRounding", Json(9.0));
    SUBCASE("merge keeps unrelated entries and overrides matching ones") {
        ThemeImportResult r = ImportThemeFromCode("colors[ImGuiCol_Text] = ImVec4(1, 1, 1, 1); style.Alpha = 0.5f;", t, false);
        CHECK(r.colors == 1);
        CHECK(r.vars == 1);
        CHECK(t.colors.size() == 2);
        CHECK(NearColor(Col(t, "Text"), 1, 1, 1, 1));
        CHECK(NearColor(Col(t, "Button"), 0.1f, 0.2f, 0.3f, 1));
        CHECK(t.vars.size() == 2);
        CHECK(Scalar(t, "FrameRounding") == 9.0f);
        CHECK(t.name == "Mine");
    }
    SUBCASE("replace clears everything first") {
        ImportThemeFromCode("colors[ImGuiCol_Text] = ImVec4(1, 1, 1, 1);", t, true);
        CHECK(t.colors.size() == 1);
        CHECK(t.vars.empty());
        CHECK(t.name == "Mine");
    }
    SUBCASE("merge resolves ImGuiCol_ references against the existing theme") {
        ImportThemeFromCode("colors[ImGuiCol_Header] = colors[ImGuiCol_Button];", t, false);
        CHECK(NearColor(Col(t, "Header"), 0.1f, 0.2f, 0.3f, 1));
    }
}

TEST_CASE("import: unparseable input produces warnings, not entries") {
    Theme t;
    ThemeImportResult r = ImportThemeFromCode("int x = 3; return x;", t, true);
    CHECK(r.colors == 0);
    CHECK(r.vars == 0);
    CHECK(HasWarning(r, "no ImGuiCol_/style assignments recognized"));
    CHECK(t.colors.empty());
    r = ImportThemeFromCode("colors[ImGuiCol_Text] = accent; colors[ImGuiCol_Border].y = foo;", t, true);
    CHECK(r.colors == 0);
    CHECK(HasWarning(r, "cannot parse color for Text"));
    CHECK(HasWarning(r, "Border: cannot parse component value"));
    r = ImportThemeFromCode("style.FrameRounding = kRounding;", t, true);
    CHECK(HasWarning(r, "FrameRounding: cannot parse value"));
    r = ImportThemeFromCode("ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, pad);", t, true);
    CHECK(HasWarning(r, "FramePadding: cannot parse value"));
    r = ImportThemeFromCode("", t, true);
    CHECK(r.warnings.size() == 1);
}

TEST_CASE("import: ImGuiCol_COUNT and empty color names are ignored") {
    Theme t;
    ThemeImportResult r = ImportThemeFromCode("for (int i = 0; i < ImGuiCol_COUNT; ++i) colors[i] = ImVec4(1, 1, 1, 1);", t, true);
    CHECK(r.colors == 0);
    CHECK(t.colors.empty());
}

TEST_CASE("ExportThemeAsCode is re-importable and ThemeFunctionName sanitizes") {
    CHECK(ThemeFunctionName("Dark") == "ApplyTheme_Dark");
    CHECK(ThemeFunctionName("My Theme-2") == "ApplyTheme_My_Theme_2");
    Theme t = MakeThemePreset("Nord");
    std::string code = ExportThemeAsCode(t, "ApplyTheme_Nord");
    CHECK(code.find("void ApplyTheme_Nord(ImGuiStyle& style)\n{\n") == 0);
    CHECK(code.find("    ImVec4* colors = style.Colors;\n") != std::string::npos);
    CHECK(code.find("    colors[ImGuiCol_Text] = ImVec4(") != std::string::npos);
    CHECK(code.find("    style.WindowPadding = ImVec2(10.000f, 10.000f);\n") != std::string::npos);
    CHECK(code.find("    style.FrameRounding = 4.000f;\n") != std::string::npos);
    CHECK(code.back() == '\n');
    Theme back;
    ThemeImportResult r = ImportThemeFromCode(code, back, true);
    CHECK(r.warnings.empty());
    CHECK(r.colors == (int)t.colors.size());
    CHECK(r.vars == (int)t.vars.size());
    for (const auto& c : t.colors) {
        const Vec4* b = Col(back, c.first.c_str());
        CHECK_MESSAGE(NearColor(b, c.second.x, c.second.y, c.second.z, c.second.w, 0.0006f), c.first);
    }
    for (const auto& v : t.vars) {
        bool vec2 = StyleVarIsVec2(v.first.c_str());
        float f1 = 0, f2 = 0;
        ImVec2 a, b;
        REQUIRE(StyleVarValue(v.second, vec2, f1, a));
        const Json* bj = Var(back, v.first.c_str());
        REQUIRE_MESSAGE(bj, v.first);
        REQUIRE(StyleVarValue(*bj, vec2, f2, b));
        if (vec2) {
            CHECK_MESSAGE(Near(a.x, b.x, 0.0006f), v.first);
            CHECK_MESSAGE(Near(a.y, b.y, 0.0006f), v.first);
        } else {
            CHECK_MESSAGE(Near(f1, f2, 0.0006f), v.first);
        }
    }
    Theme empty;
    CHECK(ExportThemeAsCode(empty, "F") == "void F(ImGuiStyle& style)\n{\n}\n");
    Project proj;
    std::string text = ExportThemeText(proj, t);
    CHECK_FALSE(text.empty());
    CHECK(text.find("ImGuiCol_Text") != std::string::npos);
}

TEST_CASE("presets: catalogue") {
    const std::vector<ThemePresetInfo>& presets = ThemePresets();
    REQUIRE(presets.size() == 6);
    const char* expected[] = {"Dark", "Light", "Neon", "Midnight", "Nord", "Dracula"};
    for (size_t i = 0; i < presets.size(); ++i) {
        CHECK(std::string(presets[i].name) == expected[i]);
        CHECK(std::strlen(presets[i].description) > 10);
        CHECK(presets[i].swatch[0] <= 0xffffff);
    }
    CHECK(FindThemePreset("Nord") == &presets[4]);
    CHECK(FindThemePreset("nord") == nullptr);
    CHECK(FindThemePreset("") == nullptr);
    Theme fallback = MakeThemePreset("Nope");
    CHECK(fallback.name == "Dark");
    CHECK(DumpJson(ThemeToJson(fallback)) == DumpJson(ThemeToJson(MakeThemePreset("Dark"))));
}

TEST_CASE("presets: every preset covers every ImGuiCol name and uses only known names") {
    size_t ncols = 0, ndock = 0;
    const char* const* cols = ImGuiColNames(ncols);
    const char* const* dock = ImGuiColNamesDocking(ndock);
    for (const ThemePresetInfo& info : ThemePresets()) {
        Theme t = MakeThemePreset(info.name);
        CAPTURE(info.name);
        CHECK(t.name == info.name);
        for (size_t i = 0; i < ncols; ++i) CHECK_MESSAGE(Col(t, cols[i]) != nullptr, "missing color " << cols[i]);
        for (size_t i = 0; i < ndock; ++i) CHECK_MESSAGE(Col(t, dock[i]) != nullptr, "missing color " << dock[i]);
        std::vector<std::string> seen;
        for (const auto& c : t.colors) {
            CHECK_MESSAGE((KnownColorName(c.first) || ForwardCompat(c.first)), "unknown color " << c.first);
            CHECK_MESSAGE(std::find(seen.begin(), seen.end(), c.first) == seen.end(), "duplicate color " << c.first);
            seen.push_back(c.first);
            CHECK(c.second.x >= 0.0f);
            CHECK(c.second.x <= 1.0f);
            CHECK(c.second.y >= 0.0f);
            CHECK(c.second.y <= 1.0f);
            CHECK(c.second.z >= 0.0f);
            CHECK(c.second.z <= 1.0f);
            CHECK(c.second.w >= 0.0f);
            CHECK(c.second.w <= 1.0f);
        }
        CHECK(std::fabs(Luminance(*Col(t, "Text")) - Luminance(*Col(t, "WindowBg"))) > 0.4f);
        CHECK(std::fabs(Luminance(*Col(t, "Text")) - Luminance(*Col(t, "FrameBg"))) > 0.3f);
        CHECK(Col(t, "Text")->w == 1.0f);
        CHECK(Col(t, "WindowBg")->w == 1.0f);
        CHECK(Col(t, "BorderShadow")->w == 0.0f);
        CHECK(Col(t, "TableRowBg")->w == 0.0f);
    }
}

TEST_CASE("presets: style vars are known, well-typed and sane") {
    for (const ThemePresetInfo& info : ThemePresets()) {
        Theme t = MakeThemePreset(info.name);
        CAPTURE(info.name);
        std::vector<std::string> seen;
        for (const auto& v : t.vars) {
            CHECK_MESSAGE(KnownVarName(v.first), "unknown var " << v.first);
            CHECK_MESSAGE(std::find(seen.begin(), seen.end(), v.first) == seen.end(), "duplicate var " << v.first);
            seen.push_back(v.first);
            bool vec2 = StyleVarIsVec2(v.first.c_str());
            CHECK_MESSAGE(v.second.IsObject() == vec2, v.first << " has the wrong JSON shape");
            if (vec2) {
                CHECK(v.second["x"].IsNumber());
                CHECK(v.second["y"].IsNumber());
                CHECK(v.second["x"].AsDouble() >= 0.0);
                CHECK(v.second["y"].AsDouble() >= 0.0);
            } else {
                CHECK(v.second.IsNumber());
                CHECK(v.second.AsDouble() >= 0.0);
            }
        }
        CHECK(Scalar(t, "Alpha") == 1.0f);
        CHECK(Scalar(t, "DisabledAlpha") > 0.0f);
        CHECK(Scalar(t, "DisabledAlpha") < 1.0f);
        CHECK(Scalar(t, "WindowRounding") <= 16.0f);
        CHECK(Scalar(t, "FrameRounding") <= Scalar(t, "WindowRounding") + 0.001f);
        CHECK(Scalar(t, "GrabRounding") <= Scalar(t, "FrameRounding"));
        CHECK(Scalar(t, "WindowBorderSize") <= 1.0f);
        CHECK((Scalar(t, "FrameBorderSize") == 0.0f || Scalar(t, "FrameBorderSize") == 1.0f));
        CHECK(Vec(t, "WindowMinSize").x >= 32.0f);
        CHECK(Vec(t, "WindowTitleAlign").x <= 1.0f);
        CHECK(Vec(t, "WindowTitleAlign").y <= 1.0f);
        CHECK(Vec(t, "ButtonTextAlign").x == 0.5f);
        CHECK(Vec(t, "FramePadding").y >= 2.0f);
        CHECK(Vec(t, "ItemSpacing").x >= 4.0f);
        CHECK(Scalar(t, "ScrollbarSize") >= 8.0f);
        CHECK(Scalar(t, "IndentSpacing") >= 8.0f);
        CHECK(t.vars.size() >= 30);
    }
}

TEST_CASE("presets: ThemeStyleVarNames agrees with the style tables and theme_apply") {
    size_t n = 0;
    const bool* vec2 = nullptr;
    const char* const* names = ThemeStyleVarNames(n, &vec2);
    REQUIRE(names);
    REQUIRE(vec2);
    CHECK(n >= 30);
    for (size_t i = 0; i < n; ++i) {
        CAPTURE(names[i]);
        CHECK(KnownVarName(names[i]));
        CHECK(vec2[i] == StyleVarIsVec2(names[i]));
        bool apply_vec2 = false;
        int idx = ImGuiStyleVarIndexByName(names[i], &apply_vec2);
        bool supported = ImGuiStyleVarMinVersion(names[i]) <= IMGUI_VERSION_NUM;
#ifndef IMGUI_HAS_DOCK
        if (std::strcmp(names[i], "DockingSeparatorSize") == 0) supported = false;
#endif
        if (supported) {
            CHECK(idx >= 0);
            CHECK(apply_vec2 == vec2[i]);
        }
    }
    size_t n2 = 0;
    CHECK(ThemeStyleVarNames(n2, nullptr) == names);
    CHECK(n2 == n);
}

TEST_CASE("theme_apply: ThemeFromStyle(ApplyThemeToStyle(t)) == t") {
    ImGuiStyle dark;
    ImGui::StyleColorsDark(&dark);
    dark.FrameRounding = 3.25f;
    dark.WindowPadding = ImVec2(7, 9);
    dark.Alpha = 0.875f;
    Theme t = ThemeFromStyle(dark, "Dark");
    CHECK(t.name == "Dark");
    CHECK((int)t.colors.size() == ImGuiCol_COUNT);
    CHECK(t.vars.size() == sizeof(kVars) / sizeof(kVars[0]));
    CHECK(Scalar(t, "FrameRounding") == 3.25f);
    CHECK(Vec(t, "WindowPadding").y == 9.0f);
    ImGuiStyle fresh;
    ImGui::StyleColorsLight(&fresh);
    ApplyThemeToStyle(t, fresh);
    for (int i = 0; i < ImGuiCol_COUNT; ++i) {
        CHECK(fresh.Colors[i].x == dark.Colors[i].x);
        CHECK(fresh.Colors[i].w == dark.Colors[i].w);
    }
    CHECK(fresh.FrameRounding == 3.25f);
    CHECK(fresh.WindowPadding.x == 7.0f);
    CHECK(fresh.Alpha == 0.875f);
    Theme back = ThemeFromStyle(fresh, "Dark");
    REQUIRE(back.colors.size() == t.colors.size());
    for (size_t i = 0; i < t.colors.size(); ++i) {
        CHECK(back.colors[i].first == t.colors[i].first);
        CHECK(back.colors[i].second == t.colors[i].second);
    }
    REQUIRE(back.vars.size() == t.vars.size());
    for (size_t i = 0; i < t.vars.size(); ++i) {
        CHECK(back.vars[i].first == t.vars[i].first);
        CHECK_MESSAGE(back.vars[i].second == t.vars[i].second, t.vars[i].first);
    }
    CHECK(DumpJson(ThemeToJson(back)) == DumpJson(ThemeToJson(t)));
}

TEST_CASE("theme_apply: presets survive ApplyThemeToStyle -> ThemeFromStyle") {
    for (const ThemePresetInfo& info : ThemePresets()) {
        Theme t = MakeThemePreset(info.name);
        CAPTURE(info.name);
        ImGuiStyle s;
        ApplyThemeToStyle(t, s);
        Theme back = ThemeFromStyle(s, t.name);
        int applied = 0;
        for (const auto& c : t.colors) {
            if (ImGuiColIndexByName(c.first.c_str()) < 0) {
                CHECK_MESSAGE(NotInThisImGui(c.first), c.first << " is not applicable to this ImGui and not forward-compat");
                continue;
            }
            ++applied;
            const Vec4* b = Col(back, c.first.c_str());
            REQUIRE_MESSAGE(b, c.first);
            CHECK_MESSAGE(*b == c.second, c.first);
        }
        CHECK(applied == ImGuiCol_COUNT);
        for (int i = 0; i < ImGuiCol_COUNT; ++i)
            CHECK_MESSAGE(Col(t, ImGui::GetStyleColorName(i)) != nullptr, "preset lacks " << ImGui::GetStyleColorName(i));
        for (const auto& v : t.vars) {
            bool vec2 = false;
            if (ImGuiStyleVarIndexByName(v.first.c_str(), &vec2) < 0) continue;
            const Json* b = Var(back, v.first.c_str());
            REQUIRE_MESSAGE(b, v.first);
            float f1 = 0, f2 = 0;
            ImVec2 a, bb;
            REQUIRE(StyleVarValue(v.second, vec2, f1, a));
            REQUIRE(StyleVarValue(*b, vec2, f2, bb));
            if (vec2) {
                CHECK_MESSAGE(Near(a.x, bb.x), v.first);
                CHECK_MESSAGE(Near(a.y, bb.y), v.first);
            } else {
                CHECK_MESSAGE(Near(f1, f2), v.first);
            }
        }
    }
}

TEST_CASE("theme_apply: name tables") {
    CHECK(std::string(ResolveColorAlias("TabActive")) == "TabSelected");
    CHECK(std::string(ResolveColorAlias("TabUnfocusedActive")) == "TabDimmedSelected");
    CHECK(std::string(ResolveColorAlias("ComboBg")) == "PopupBg");
    CHECK(std::string(ResolveColorAlias("Text")) == "Text");
    CHECK(std::string(ResolveColorAlias(nullptr)) == "");
    CHECK(ImGuiColIndexByName("Text") == ImGuiCol_Text);
    CHECK(ImGuiColIndexByName("TabActive") == ImGuiCol_TabSelected);
    CHECK(ImGuiColIndexByName("NavHighlight") == ImGuiCol_NavCursor);
    CHECK(ImGuiColIndexByName("ModalWindowDimBg") == ImGuiCol_ModalWindowDimBg);
    CHECK(ImGuiColIndexByName("") == -1);
    CHECK(ImGuiColIndexByName(nullptr) == -1);
    CHECK(ImGuiColIndexByName("Bogus") == -1);
    for (int i = 0; i < ImGuiCol_COUNT; ++i) CHECK(ImGuiColIndexByName(ImGui::GetStyleColorName(i)) == i);
    bool vec2 = false;
    CHECK(ImGuiStyleVarIndexByName("WindowPadding", &vec2) == ImGuiStyleVar_WindowPadding);
    CHECK(vec2);
    CHECK(ImGuiStyleVarIndexByName("FrameRounding", &vec2) == ImGuiStyleVar_FrameRounding);
    CHECK_FALSE(vec2);
    CHECK(ImGuiStyleVarIndexByName("Nope") == -1);
    CHECK(ImGuiStyleVarIndexByName(nullptr) == -1);
    CHECK(std::string(ColorGroupOf("ButtonHovered")) == "Button");
    CHECK(std::string(ColorGroupOf("TabActive")) == "Tab");
    CHECK(std::string(ColorGroupOf("DockingPreview")) == "Docking");
    CHECK(std::string(ColorGroupOf("TextSelectedBg")) == "Text");
    CHECK(std::string(ColorGroupOf("ResizeGrip")) == "Separator");
    CHECK(std::string(ColorGroupOf("Mystery")) == "Other");
    size_t n = 0;
    const char* const* groups = ColorGroupNames(n);
    CHECK(n == 14);
    CHECK(std::string(groups[0]) == "Text");
    CHECK(std::string(groups[n - 1]) == "Other");
    size_t ncols = 0;
    const char* const* cols = ImGuiColNames(ncols);
    for (size_t i = 0; i < ncols; ++i) CHECK_MESSAGE(std::string(ColorGroupOf(cols[i])) != "Other", cols[i] << " has no color group");
    cols = ImGuiColNamesDocking(ncols);
    for (size_t i = 0; i < ncols; ++i) CHECK_MESSAGE(std::string(ColorGroupOf(cols[i])) != "Other", cols[i] << " has no color group");
}

TEST_CASE("theme_apply: StyleVarValue / StyleVarJson / DefaultStyleVarJson") {
    float f = 0;
    ImVec2 v;
    Json obj = Json::Object();
    obj.Set("x", Json(1.5));
    obj.Set("y", Json(2));
    CHECK(StyleVarValue(obj, true, f, v));
    CHECK(v.x == 1.5f);
    CHECK(v.y == 2.0f);
    Json arr = Json::Array();
    arr.Push(Json(3));
    arr.Push(Json(4));
    CHECK(StyleVarValue(arr, true, f, v));
    CHECK(v.x == 3.0f);
    CHECK(v.y == 4.0f);
    CHECK(StyleVarValue(Json(5), true, f, v));
    CHECK(v.x == 5.0f);
    CHECK(v.y == 5.0f);
    CHECK_FALSE(StyleVarValue(Json("x"), true, f, v));
    Json one = Json::Array();
    one.Push(Json(1));
    CHECK_FALSE(StyleVarValue(one, true, f, v));
    CHECK(StyleVarValue(Json(2.5), false, f, v));
    CHECK(f == 2.5f);
    CHECK(StyleVarValue(obj, false, f, v));
    CHECK(f == 1.5f);
    CHECK_FALSE(StyleVarValue(Json(true), false, f, v));
    CHECK_FALSE(StyleVarValue(Json(), false, f, v));
    CHECK(StyleVarJson(false, 4.5f, ImVec2()) == Json(4.5));
    Json j2 = StyleVarJson(true, 0, ImVec2(1, 2));
    CHECK(j2["x"] == Json(1.0));
    CHECK(j2["y"] == Json(2.0));
    CHECK(StyleVarJson(false, 0.1f, ImVec2()) == Json(0.1));
    ImGuiStyle def;
    Json fp = DefaultStyleVarJson("FramePadding");
    CHECK(fp["x"].AsDouble() == (double)def.FramePadding.x);
    CHECK(fp["y"].AsDouble() == (double)def.FramePadding.y);
    CHECK(DefaultStyleVarJson("Alpha") == Json(1.0));
    CHECK(DefaultStyleVarJson("Nope") == Json(0.0));
}

TEST_CASE("theme_apply: push/pop scopes inside a frame") {
    test::HeadlessImGui gui;
    Project project;
    Theme dark = MakeThemePreset("Dark");
    project.themes.push_back(dark);
    StyleOverride danger;
    danger.colors.emplace_back("Button", Vec4{1, 0, 0, 1});
    danger.vars.emplace_back("FrameRounding", Json(6));
    danger.vars.emplace_back("Bogus", Json(1));
    danger.colors.emplace_back("NotAColor", Vec4{1, 1, 1, 1});
    project.style_classes.emplace_back("danger", danger);
    CHECK(FindThemeByName(project, "Dark") == &project.themes[0]);
    CHECK(FindThemeByName(project, "Light") == nullptr);
    CHECK(FindThemeByName(static_cast<const Project&>(project), "Dark") == &project.themes[0]);
    CHECK(FindStyleClass(project, "danger") == &project.style_classes[0].second);
    CHECK(FindStyleClass(project, "x") == nullptr);
    gui.Frame([&] {
        ImGuiStyle& style = ImGui::GetStyle();
        ImVec4 before = style.Colors[ImGuiCol_Button];
        float rounding = style.FrameRounding;
        StyleScope s = PushStyleOverride(danger);
        CHECK(s.colors == 1);
        CHECK(s.vars == 1);
        CHECK(style.Colors[ImGuiCol_Button].x == 1.0f);
        CHECK(style.FrameRounding == 6.0f);
        PopStyleScope(s);
        CHECK(style.Colors[ImGuiCol_Button].x == before.x);
        CHECK(style.FrameRounding == rounding);

        StyleScope cls = PushStyleClasses(project, {"danger", "missing", "danger"});
        CHECK(cls.colors == 2);
        CHECK(cls.vars == 2);
        PopStyleScope(cls);

        StyleScope th = PushThemeStyle(dark);
        int known_cols = 0;
        for (const auto& c : dark.colors) known_cols += ImGuiColIndexByName(c.first.c_str()) >= 0 ? 1 : 0;
        int known_vars = 0;
        for (const auto& v : dark.vars) known_vars += ImGuiStyleVarIndexByName(v.first.c_str()) >= 0 ? 1 : 0;
        CHECK(th.colors == known_cols);
        CHECK(th.vars == known_vars);
        const Vec4* wb = Col(dark, "WindowBg");
        CHECK(style.Colors[ImGuiCol_WindowBg].x == wb->x);
        CHECK(style.WindowRounding == Scalar(dark, "WindowRounding"));
        PopStyleScope(th);
        CHECK(style.FrameRounding == rounding);
        StyleScope none;
        PopStyleScope(none);
    });
}

TEST_CASE("ParseThemeFromCode (theme_presets) parses the common shapes") {
    const char* code = R"(
colors[ImGuiCol_Text] = ImVec4(1.0f, 0.5f, 0.25f, 1.0f);
colors[ImGuiCol_Button] = ImColor(255, 0, 0, 255);
colors[ImGuiCol_Header] = ImColor(0.5f, 0.5f, 0.5f);
colors[ImGuiCol_Border] = IM_COL32(0, 0, 255, 128);
ImGui::PushStyleColor(ImGuiCol_Tab, ImVec4(0, 1, 0, 1));
style.FrameRounding = 4.f;
style.WindowPadding = ImVec2(8, 6);
ImGui::PushStyleVar(ImGuiStyleVar_GrabRounding, 2.0f);
ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(1, 2));
style.Bogus = 3;
colors[ImGuiCol_COUNT] = ImVec4(1, 1, 1, 1);
)";
    Theme t;
    std::string err;
    int nc = 0, nv = 0;
    REQUIRE(ParseThemeFromCode(code, t, &err, &nc, &nv));
    CHECK(err.empty());
    CHECK(nc == 5);
    CHECK(nv == 4);
    CHECK(t.name == "Imported");
    CHECK(NearColor(Col(t, "Text"), 1, 0.5f, 0.25f, 1));
    CHECK(NearColor(Col(t, "Button"), 1, 0, 0, 1));
    CHECK(NearColor(Col(t, "Header"), 0.5f, 0.5f, 0.5f, 1));
    CHECK(NearColor(Col(t, "Border"), 0, 0, 1, 128 * kB));
    CHECK(NearColor(Col(t, "Tab"), 0, 1, 0, 1));
    CHECK(Scalar(t, "FrameRounding") == 4.0f);
    CHECK(Vec(t, "WindowPadding").y == 6.0f);
    CHECK(Scalar(t, "GrabRounding") == 2.0f);
    CHECK(Vec(t, "CellPadding").x == 1.0f);
    CHECK(Var(t, "Bogus") == nullptr);
    CHECK(Col(t, "COUNT") == nullptr);
    Theme none;
    CHECK_FALSE(ParseThemeFromCode("int x = 1;", none, &err, &nc, &nv));
    CHECK(nc == 0);
    CHECK(nv == 0);
    CHECK_FALSE(err.empty());
}

TEST_CASE("ParseThemeFromCode ignores commented-out code") {
    const char* code = R"(
// colors[ImGuiCol_Text] = ImVec4(1, 0, 0, 1);
colors[ImGuiCol_Button] = ImVec4(0, 1, 0, 1); // colors[ImGuiCol_Header] = ImVec4(1, 1, 1, 1);
/* colors[ImGuiCol_Border] = ImVec4(0, 0, 1, 1);
   style.FrameRounding = 9.0f; */
style.GrabRounding = 2.0f; /* style.Alpha = 0.5f; */
)";
    Theme t;
    std::string err;
    int nc = 0, nv = 0;
    REQUIRE(ParseThemeFromCode(code, t, &err, &nc, &nv));
    CHECK(nc == 1);
    CHECK(nv == 1);
    CHECK(NearColor(Col(t, "Button"), 0, 1, 0, 1));
    CHECK(Col(t, "Text") == nullptr);
    CHECK(Col(t, "Header") == nullptr);
    CHECK(Col(t, "Border") == nullptr);
    CHECK(Var(t, "FrameRounding") == nullptr);
    CHECK(Var(t, "Alpha") == nullptr);
    CHECK(Scalar(t, "GrabRounding") == 2.0f);
    Theme only_comments;
    CHECK_FALSE(ParseThemeFromCode("// colors[ImGuiCol_Text] = ImVec4(1, 0, 0, 1);\n", only_comments, &err));
}

TEST_CASE("ParseThemeFromCode stores current color names for legacy aliases") {
    const char* code = R"(
colors[ImGuiCol_TabActive] = ImVec4(0.2f, 0.4f, 0.8f, 1.0f);
colors[ImGuiCol_TabUnfocused] = ImVec4(0.1f, 0.1f, 0.1f, 1.0f);
colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.15f, 0.15f, 0.15f, 1.0f);
colors[ImGuiCol_NavHighlight] = ImVec4(0.26f, 0.59f, 0.98f, 1.0f);
colors[ImGuiCol_TabSelected] = ImVec4(0.3f, 0.3f, 0.3f, 1.0f);
)";
    Theme t;
    std::string err;
    int nc = 0;
    REQUIRE(ParseThemeFromCode(code, t, &err, &nc));
    CHECK(nc == 5);
    CHECK(t.colors.size() == 4);
    CHECK(NearColor(Col(t, "TabSelected"), 0.3f, 0.3f, 0.3f, 1));
    CHECK(NearColor(Col(t, "TabDimmed"), 0.1f, 0.1f, 0.1f, 1));
    CHECK(NearColor(Col(t, "TabDimmedSelected"), 0.15f, 0.15f, 0.15f, 1));
    CHECK(NearColor(Col(t, "NavCursor"), 0.26f, 0.59f, 0.98f, 1));
    CHECK(Col(t, "TabActive") == nullptr);
    CHECK(Col(t, "NavHighlight") == nullptr);
    for (const auto& c : t.colors) CHECK_MESSAGE(KnownColorName(c.first), c.first);
}
