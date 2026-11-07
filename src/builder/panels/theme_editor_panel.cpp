#include <cfloat>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "imgui.h"
#include "imgui_internal.h"

#include "../builder_fonts.h"
#include "../editor_context.h"
#include "../ui/color_utils.h"
#include "../ui/theme_apply.h"
#include "../ui/theme_preview.h"
#include "../ui/ui_helpers.h"
#include "editor_panel.h"
#include "theme_import.h"
#include "theme_state.h"

namespace imforge::builder {

namespace {

const ImGuiStyle& DefaultStyle() {
    static const ImGuiStyle s = [] {
        ImGuiStyle st;
        ImGui::StyleColorsDark(&st);
        return st;
    }();
    return s;
}

std::string UniqueThemeName(const Project& p, const std::string& base) {
    if (!FindThemeByName(p, base)) return base;
    for (int n = 2;; ++n) {
        std::string cand = base + " " + std::to_string(n);
        if (!FindThemeByName(p, cand)) return cand;
    }
}

std::string ThemeFileFor(const std::string& name) { return "themes/" + name + ".theme.json"; }

bool MatchesFilter(const std::string& filter, const char* name) {
    return filter.empty() || ImStristr(name, nullptr, filter.c_str(), nullptr) != nullptr;
}

std::vector<std::string> AllColorNames(const Theme& theme) {
    std::vector<std::string> names;
    size_t n = 0;
    const char* const* a = ImGuiColNames(n);
    for (size_t i = 0; i < n; ++i) names.push_back(a[i]);
    a = ImGuiColNamesDocking(n);
    for (size_t i = 0; i < n; ++i) names.push_back(a[i]);
    for (const auto& c : theme.colors) {
        bool known = false;
        for (const std::string& s : names)
            if (s == c.first) known = true;
        if (!known) names.push_back(c.first);
    }
    return names;
}

std::vector<std::string> AllVarNames(const Theme& theme) {
    std::vector<std::string> names;
    size_t n = 0;
    const char* const* a = ImGuiStyleVarNames(n);
    for (size_t i = 0; i < n; ++i) names.push_back(a[i]);
    a = ImGuiStyleVarNamesDocking(n);
    for (size_t i = 0; i < n; ++i) names.push_back(a[i]);
    for (const auto& v : theme.vars) {
        bool known = false;
        for (const std::string& s : names)
            if (s == v.first) known = true;
        if (!known) names.push_back(v.first);
    }
    return names;
}

void VarDragParams(const char* name, float& speed, float& min, float& max) {
    speed = 0.1f;
    min = 0;
    max = 64;
    if (std::strstr(name, "Alpha") || std::strstr(name, "Align")) {
        speed = 0.005f;
        max = 1;
    } else if (std::strstr(name, "MinSize")) {
        speed = 1;
        max = 1024;
    } else if (std::strstr(name, "Angle")) {
        speed = 0.01f;
        min = -1.57f;
        max = 1.57f;
    }
}

class ThemeEditorPanel : public EditorPanel {
public:
    const char* Name() const override { return "Theme Editor"; }
    const char* DefaultDock() const override { return "right"; }
    const char* Icon() const override {
#ifdef ICON_FA_PALETTE
        return ICON_FA_PALETTE;
#else
        return nullptr;
#endif
    }
    void Draw(EditorContext& ctx) override;

private:
    std::string filter_, name_buf_, import_text_, export_text_;
    ThemeImportResult import_result_;
    bool has_import_result_ = false;
    bool import_replace_ = false;
    int base_choice_ = 0;
    float preview_height_ = 300;

    void DrawToolbar(EditorContext& ctx, Theme* theme);
    void DrawPopups(EditorContext& ctx, Theme* theme);
    void DrawColors(EditorContext& ctx, Theme& theme);
    void DrawVars(EditorContext& ctx, Theme& theme);
    void DrawColorRow(EditorContext& ctx, Theme& theme, const char* name);
    void DrawVarRow(EditorContext& ctx, Theme& theme, const char* name);
    void DrawPreview(Theme& theme);
};

void ThemeEditorPanel::Draw(EditorContext& ctx) {
    if (!ctx.has_project) {
        ImGui::TextDisabled("Open a project to edit themes.");
        return;
    }
    Theme* theme = ctx.ActiveTheme();
    DrawToolbar(ctx, theme);
    DrawPopups(ctx, theme);
    theme = ctx.ActiveTheme();
    if (!theme) {
        ImGui::TextDisabled("This project has no themes yet. Click New.");
        return;
    }
    ImGui::Checkbox("Apply to editor preview", &GetThemePreviewState().apply_to_canvas);
    ui::Tooltip("When on, the Canvas renders panels with the active theme.");
    ImGui::SameLine();
    ui::TextDim("%d colors, %d variables", (int)theme->colors.size(), (int)theme->vars.size());
    ImGui::SetNextItemWidth(-FLT_MIN);
    ui::InputTextWithHintStd("##theme_filter", "Filter colors and variables", filter_);

    float editors_h = ImGui::GetContentRegionAvail().y - preview_height_ - 8.0f;
    if (editors_h < 120) editors_h = 120;
    if (ImGui::BeginChild("##theme_editors", ImVec2(0, editors_h))) {
        DrawColors(ctx, *theme);
        DrawVars(ctx, *theme);
    }
    ImGui::EndChild();
    ImGui::InvisibleButton("##theme_splitter", ImVec2(-FLT_MIN, 6.0f));
    if (ImGui::IsItemHovered() || ImGui::IsItemActive()) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
    if (ImGui::IsItemActive()) preview_height_ = ImClamp(preview_height_ - ImGui::GetIO().MouseDelta.y, 120.0f, 700.0f);
    DrawPreview(*theme);
}

void ThemeEditorPanel::DrawToolbar(EditorContext& ctx, Theme* theme) {
    Project& p = ctx.project;
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10);
    if (ImGui::BeginCombo("##active_theme", theme ? theme->name.c_str() : "(no theme)")) {
        for (Theme& t : p.themes) {
            if (ImGui::Selectable(t.name.c_str(), theme == &t)) {
                std::string nm = t.name;
                ctx.EditProject("Select theme", [&] { p.active_theme = nm; });
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::Button("New")) {
        name_buf_ = UniqueThemeName(p, "Theme");
        base_choice_ = 0;
        ImGui::OpenPopup("New theme");
    }
    ImGui::BeginDisabled(theme == nullptr);
    ImGui::SameLine();
    if (ImGui::Button("Duplicate") && theme) {
        Theme copy = *theme;
        copy.name = UniqueThemeName(p, theme->name + " Copy");
        copy.file = ThemeFileFor(copy.name);
        ctx.EditProject("Duplicate theme", [&] {
            p.themes.push_back(copy);
            p.active_theme = copy.name;
        });
    }
    ImGui::SameLine();
    if (ImGui::Button("Rename") && theme) {
        name_buf_ = theme->name;
        ImGui::OpenPopup("Rename theme");
    }
    ImGui::SameLine();
    if (ImGui::Button("Delete")) ImGui::OpenPopup("Delete theme");
    ImGui::SameLine();
    if (ImGui::Button("Import...")) {
        has_import_result_ = false;
        ImGui::OpenPopup("Import theme");
    }
    ImGui::SameLine();
    if (ImGui::Button("Export") && theme) {
        export_text_ = ExportThemeText(p, *theme);
        ImGui::SetClipboardText(export_text_.c_str());
        ctx.SetStatus("Theme code copied to clipboard");
        ImGui::OpenPopup("Export theme");
    }
    ImGui::EndDisabled();
}

void ThemeEditorPanel::DrawPopups(EditorContext& ctx, Theme* theme) {
    Project& p = ctx.project;
    if (ImGui::BeginPopupModal("New theme", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ui::InputTextStd("Name", name_buf_);
        ImGui::Combo("Start from", &base_choice_, "ImGui Dark\0ImGui Light\0ImGui Classic\0Empty (defaults)\0");
        if (ImGui::Button("Create") && !name_buf_.empty()) {
            Theme t;
            if (base_choice_ < 3) {
                ImGuiStyle st;
                if (base_choice_ == 1) ImGui::StyleColorsLight(&st);
                else if (base_choice_ == 2) ImGui::StyleColorsClassic(&st);
                else ImGui::StyleColorsDark(&st);
                t = ThemeFromStyle(st, name_buf_);
            }
            t.name = UniqueThemeName(p, name_buf_);
            t.file = ThemeFileFor(t.name);
            ctx.EditProject("New theme", [&] {
                p.themes.push_back(t);
                p.active_theme = t.name;
            });
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    if (ImGui::BeginPopupModal("Rename theme", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ui::InputTextStd("Name", name_buf_);
        if (ImGui::Button("Rename") && theme && !name_buf_.empty()) {
            std::string nm = name_buf_ == theme->name ? theme->name : UniqueThemeName(p, name_buf_);
            ctx.EditProject("Rename theme", [&] {
                bool was_active = p.active_theme == theme->name;
                theme->name = nm;
                theme->file = ThemeFileFor(nm);
                if (was_active) p.active_theme = nm;
            });
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    if (ImGui::BeginPopupModal("Delete theme", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (!theme) ImGui::CloseCurrentPopup();
        else ImGui::Text("Delete theme \"%s\"?", theme->name.c_str());
        if (ImGui::Button("Delete") && theme) {
            std::string nm = theme->name;
            ctx.EditProject("Delete theme", [&] {
                for (size_t i = 0; i < p.themes.size(); ++i) {
                    if (p.themes[i].name == nm) {
                        p.themes.erase(p.themes.begin() + (long)i);
                        break;
                    }
                }
                if (p.active_theme == nm) p.active_theme = p.themes.empty() ? std::string() : p.themes.front().name;
            });
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    ImGui::SetNextWindowSize(ImVec2(680, 520), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Import theme")) {
        ImGui::TextWrapped("Paste an ImGuiStyle setup function. Recognized: style.Colors[ImGuiCol_X] = ImVec4(...), "
                           "colors[ImGuiCol_X] = ImColor(r, g, b, a), IM_COL32(...), style.FrameRounding = 4.f, "
                           "style.WindowPadding = ImVec2(8, 8), PushStyleColor/PushStyleVar calls and StyleColorsDark/Light/Classic().");
        ImGui::Checkbox("Replace existing entries", &import_replace_);
        float reserve = ImGui::GetFrameHeightWithSpacing() * (has_import_result_ ? 4.0f : 2.0f);
        ui::InputTextMultilineStd("##import_code", import_text_,
                                  ImVec2(-FLT_MIN, ImGui::GetContentRegionAvail().y - reserve));
        if (has_import_result_) {
            ImGui::Text("Imported %d colors and %d variables%s%s", import_result_.colors, import_result_.vars,
                        import_result_.base.empty() ? "" : " on top of StyleColors", import_result_.base.c_str());
            if (!import_result_.warnings.empty()) {
                std::string w = std::to_string(import_result_.warnings.size()) + " warning(s): ";
                for (size_t i = 0; i < import_result_.warnings.size() && i < 4; ++i)
                    w += (i ? "; " : "") + import_result_.warnings[i];
                ImGui::PushTextWrapPos(0);
                ui::TextDim("%s", w.c_str());
                ImGui::PopTextWrapPos();
            }
        }
        if (ImGui::Button("Import") && theme) {
            ctx.EditProject("Import theme", [&] { import_result_ = ImportThemeFromCode(import_text_, *theme, import_replace_); });
            has_import_result_ = true;
            ctx.LogSource(LogLevel::Info, "theme", "imported %d colors, %d vars into %s (%d warnings)",
                          import_result_.colors, import_result_.vars, theme->name.c_str(),
                          (int)import_result_.warnings.size());
        }
        ImGui::SameLine();
        if (ImGui::Button("Close")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    ImGui::SetNextWindowSize(ImVec2(680, 520), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Export theme")) {
        ui::TextDim("The code below was copied to the clipboard.");
        ImGui::InputTextMultiline("##export_code", const_cast<char*>(export_text_.c_str()), export_text_.size() + 1,
                                  ImVec2(-FLT_MIN, ImGui::GetContentRegionAvail().y - ImGui::GetFrameHeightWithSpacing()),
                                  ImGuiInputTextFlags_ReadOnly);
        if (ImGui::Button("Copy again")) ImGui::SetClipboardText(export_text_.c_str());
        ImGui::SameLine();
        if (ImGui::Button("Close")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

void ThemeEditorPanel::DrawColors(EditorContext& ctx, Theme& theme) {
    if (!ui::Section("Colors")) return;
    std::vector<std::string> names = AllColorNames(theme);
    size_t ng = 0;
    const char* const* groups = ColorGroupNames(ng);
    for (size_t g = 0; g < ng; ++g) {
        std::vector<const char*> in_group;
        for (const std::string& n : names)
            if (std::strcmp(ColorGroupOf(n.c_str()), groups[g]) == 0 && MatchesFilter(filter_, n.c_str()))
                in_group.push_back(n.c_str());
        if (in_group.empty()) continue;
        ImGui::PushID(groups[g]);
        ImGuiTreeNodeFlags tf = ImGuiTreeNodeFlags_SpanAvailWidth | (filter_.empty() ? 0 : ImGuiTreeNodeFlags_DefaultOpen);
        if (g == 0) tf |= ImGuiTreeNodeFlags_DefaultOpen;
        if (ImGui::TreeNodeEx(groups[g], tf)) {
            for (const char* n : in_group) DrawColorRow(ctx, theme, n);
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
}

void ThemeEditorPanel::DrawColorRow(EditorContext& ctx, Theme& theme, const char* name) {
    ImGui::PushID(name);
    Vec4* cur = nullptr;
    for (auto& c : theme.colors)
        if (c.first == name) cur = &c.second;
    int idx = ImGuiColIndexByName(name);
    ImVec4 value = cur ? ToImVec4(*cur) : (idx >= 0 ? DefaultStyle().Colors[idx] : ImVec4(1, 1, 1, 1));
    ImGuiColorEditFlags edit_flags =
        ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf;
    if (ImGui::ColorEdit4("##color", &value.x, edit_flags)) {
        Vec4 nv = ToVec4(value);
        ctx.EditProject("Theme color", [&] {
            if (cur) *cur = nv;
            else theme.colors.emplace_back(name, nv);
        });
    }
    ImGui::SameLine();
    if (cur) ImGui::TextUnformatted(name);
    else ImGui::TextDisabled("%s", name);
    if (idx < 0) {
        ImGui::SameLine();
        ui::TextDim("(not in this ImGui)");
    }
    std::string hex = ColorToHex(value);
    const ImGuiStyle& st = ImGui::GetStyle();
    float w = ImGui::CalcTextSize(hex.c_str()).x + (cur ? ImGui::GetFrameHeight() + st.ItemSpacing.x : 0);
    float right = ImGui::GetWindowContentRegionMax().x - w;
    if (right > ImGui::GetCursorPosX()) ImGui::SameLine(right);
    else ImGui::SameLine();
    ui::TextDim("%s", hex.c_str());
    if (cur) {
        ImGui::SameLine();
        if (ImGui::SmallButton("x")) {
            ctx.EditProject("Remove theme color", [&] {
                for (size_t i = 0; i < theme.colors.size(); ++i) {
                    if (theme.colors[i].first == name) {
                        theme.colors.erase(theme.colors.begin() + (long)i);
                        break;
                    }
                }
            });
        }
        ui::Tooltip("Remove from theme (falls back to the ImGui default)");
    }
    ImGui::PopID();
}

void ThemeEditorPanel::DrawVars(EditorContext& ctx, Theme& theme) {
    if (!ui::Section("Style variables")) return;
    std::vector<std::string> names = AllVarNames(theme);
    for (const std::string& n : names)
        if (MatchesFilter(filter_, n.c_str())) DrawVarRow(ctx, theme, n.c_str());
}

void ThemeEditorPanel::DrawVarRow(EditorContext& ctx, Theme& theme, const char* name) {
    ImGui::PushID(name);
    Json* cur = nullptr;
    for (auto& v : theme.vars)
        if (v.first == name) cur = &v.second;
    bool vec2 = StyleVarIsVec2(name);
    bool vec2_build = false;
    int idx = ImGuiStyleVarIndexByName(name, &vec2_build);
    if (idx >= 0) vec2 = vec2_build;
    float f = 0;
    ImVec2 v;
    StyleVarValue(cur ? *cur : DefaultStyleVarJson(name), vec2, f, v);
    float speed, mn, mx;
    VarDragParams(name, speed, mn, mx);
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 9);
    bool changed = vec2 ? ImGui::DragFloat2("##var", &v.x, speed, mn, mx, "%.2f")
                        : ImGui::DragFloat("##var", &f, speed, mn, mx, "%.2f");
    if (changed) {
        Json j = StyleVarJson(vec2, f, v);
        ctx.EditProject("Theme variable", [&] {
            if (cur) *cur = j;
            else theme.vars.emplace_back(name, j);
        });
    }
    ImGui::SameLine();
    if (cur) ImGui::TextUnformatted(name);
    else ImGui::TextDisabled("%s", name);
    if (idx < 0) {
        ImGui::SameLine();
        ui::TextDim("(not in this ImGui)");
    }
    if (cur) {
        float right = ImGui::GetWindowContentRegionMax().x - ImGui::GetFrameHeight();
        if (right > ImGui::GetCursorPosX()) ImGui::SameLine(right);
        else ImGui::SameLine();
        if (ImGui::SmallButton("x")) {
            ctx.EditProject("Remove theme variable", [&] {
                for (size_t i = 0; i < theme.vars.size(); ++i) {
                    if (theme.vars[i].first == name) {
                        theme.vars.erase(theme.vars.begin() + (long)i);
                        break;
                    }
                }
            });
        }
        ui::Tooltip("Remove from theme (falls back to the ImGui default)");
    }
    ImGui::PopID();
}

void ThemeEditorPanel::DrawPreview(Theme& theme) {
    if (!ui::Section("Preview")) return;
    StyleScope scope = PushThemeStyle(theme);
    DrawThemePreviewWidgets("##theme_preview", ImVec2(0, preview_height_ - ImGui::GetFrameHeightWithSpacing()));
    PopStyleScope(scope);
}

}  // namespace

IMFORGE_REGISTER_PANEL(ThemeEditorPanel, 60);

}  // namespace imforge::builder
