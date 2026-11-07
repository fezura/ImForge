#include <string>

#include "imgui.h"

#include "../builder_style.h"
#include "../project_io.h"
#include "../ui/client_snippet.h"
#include "../ui/icon_table.h"
#include "../ui/theme_apply.h"
#include "../ui/ui_helpers.h"
#include "editor_panel.h"
#include "wizard.h"
#include "wizard_state.h"

namespace imforge::builder {

namespace {

class ProjectSettingsPanel final : public EditorPanel {
public:
    ProjectSettingsPanel() { open = false; }
    const char* Name() const override { return "Project Settings"; }
    const char* DefaultDock() const override { return "center"; }
    void OnProjectChanged(EditorContext&) override { attached_ = nullptr; }
    void Draw(EditorContext& ctx) override;

private:
    void DrawGeneral(EditorContext& ctx, bool& changed);
    void DrawThemes(EditorContext& ctx, bool& changed);
    void DrawSnippets(EditorContext& ctx);
    void ImportSelectedLibraries(EditorContext& ctx);

    wizard::SettingsForm form_;
    const Project* attached_ = nullptr;
    size_t undo_cursor_ = (size_t)-1;
    size_t undo_size_ = (size_t)-1;
    std::string snippet_, cmake_, snippet_key_;
    bool import_pending_ = false;
};

std::string UniqueThemeName(const Project& p, const std::string& base) {
    std::string name = base;
    for (int n = 2; FindThemeByName(p, name); ++n) name = base + " " + std::to_string(n);
    return name;
}

void ProjectSettingsPanel::DrawGeneral(EditorContext& ctx, bool& changed) {
    Project& p = ctx.project;
    if (ui::BeginPropertyTable("general_props")) {
        ui::PropertyLabel("Name");
        if (ui::InputTextStd("##name", p.name)) changed = true;
        if (!wizard::IsIdentifier(p.name)) {
            ui::PropertyLabel("");
            ImGui::PushStyleColor(ImGuiCol_Text, Colors().warning);
            ImGui::TextWrapped("The name should be a C++ identifier; generated code uses it in markers and defaults.");
            ImGui::PopStyleColor();
        }
        ui::PropertyLabel("Folder");
        ImGui::TextWrapped("%s", p.dir.c_str());
        ui::PropertyLabel("Active theme");
        if (ImGui::BeginCombo("##active_theme", p.active_theme.c_str())) {
            for (const Theme& t : p.themes)
                if (ImGui::Selectable(t.name.c_str(), t.name == p.active_theme)) {
                    p.active_theme = t.name;
                    changed = true;
                }
            ImGui::EndCombo();
        }
        ui::PropertyLabel("Fonts", "Project fonts and icon merging are edited in the Fonts panel");
        if (ImGui::Button("Open Fonts panel...")) ctx.OpenPanel("Fonts");
        ImGui::SameLine();
        ui::TextDim("%d font%s configured", (int)p.fonts.size(), p.fonts.size() == 1 ? "" : "s");
        ui::PropertyLabel("Documents");
        ui::TextDim("%d panels, %d components, %d themes, %d libraries", (int)p.panels.size(), (int)p.components.size(),
                    (int)p.themes.size(), (int)p.libraries.size());
        ui::PropertyLabel("State");
        if (ctx.dirty) {
            ImGui::PushStyleColor(ImGuiCol_Text, Colors().warning);
            ImGui::TextUnformatted("Unsaved changes (Ctrl+S)");
            ImGui::PopStyleColor();
        } else {
            ImGui::PushStyleColor(ImGuiCol_Text, Colors().success);
            ImGui::TextUnformatted("Saved");
            ImGui::PopStyleColor();
        }
        ui::EndPropertyTable();
    }
    ImGui::Spacing();
    if (ImGui::Button("New project...")) OpenWizard();
    ui::Tooltip("Run the New Project wizard");
}

void ProjectSettingsPanel::DrawThemes(EditorContext& ctx, bool& changed) {
    Project& p = ctx.project;
    ImGui::SeparatorText("Themes in this project");
    for (size_t i = 0; i < p.themes.size(); ++i) {
        Theme& t = p.themes[i];
        ImGui::PushID((int)i);
        if (ImGui::RadioButton(t.name.c_str(), t.name == p.active_theme)) {
            p.active_theme = t.name;
            changed = true;
        }
        ImGui::SameLine();
        ui::TextDim("%s  (%d colors, %d vars)", t.file.c_str(), (int)t.colors.size(), (int)t.vars.size());
        if (p.themes.size() > 1) {
            ImGui::SameLine();
            if (ImGui::SmallButton("Remove")) {
                std::string removed = t.name;
                p.themes.erase(p.themes.begin() + (long)i);
                if (p.active_theme == removed && !p.themes.empty()) p.active_theme = p.themes.front().name;
                changed = true;
                ImGui::PopID();
                break;
            }
        }
        ImGui::PopID();
    }
    if (ImGui::Button("Open Theme Editor...")) ctx.OpenPanel("Theme Editor");
    ImGui::Spacing();
    if (wizard::DrawThemeStep(form_, ctx, false)) changed = true;
    Theme chosen = form_.ChosenTheme();
    if (!chosen.name.empty()) {
        std::string label = "Add \"" + chosen.name + "\" to the project and make it active";
        if (ui::AccentButton(label.c_str())) {
            chosen.name = UniqueThemeName(p, chosen.name);
            chosen.file = DefaultThemeFile(chosen.name);
            p.themes.push_back(chosen);
            p.active_theme = chosen.name;
            form_.theme_choice = -1;
            form_.has_imported = false;
            changed = true;
        }
    }
}

void ProjectSettingsPanel::DrawSnippets(EditorContext& ctx) {
    Project& p = ctx.project;
    std::string key = LiveUrl(p) + "|" + p.codegen.output_dir + "|" + p.codegen.nmspace + "|" + p.name + "|" + p.renderer +
                      "|" + std::to_string(p.codegen.cpp_standard) + "|" + (p.Feature("shaderFx") ? "fx" : "") +
                      "|" + std::to_string(p.binding_types.size()) + "|" + std::to_string(p.libraries.size());
    if (key != snippet_key_ || snippet_.empty()) {
        snippet_key_ = key;
        snippet_ = MakeClientSnippet(p);
        cmake_ = MakeCMakeSnippet(p);
    }
    if (ImGui::Button("Re-run snippet generation")) {
        snippet_ = MakeClientSnippet(p);
        cmake_ = MakeCMakeSnippet(p);
        ctx.SetStatus("Client snippet regenerated from the current settings");
    }
    ImGui::SameLine();
    if (ImGui::Button("Copy client code")) {
        ImGui::SetClipboardText(snippet_.c_str());
        ctx.SetStatus("Client snippet copied to clipboard");
    }
    ImGui::SameLine();
    if (ImGui::Button("Copy CMake")) {
        ImGui::SetClipboardText(cmake_.c_str());
        ctx.SetStatus("CMake snippet copied to clipboard");
    }
    ImGui::SameLine();
    ui::TextDim("URL: %s", LiveUrl(p).c_str());
    float h = (ImGui::GetContentRegionAvail().y - ImGui::GetTextLineHeightWithSpacing() * 3) * 0.6f;
    if (h < ImGui::GetTextLineHeight() * 8) h = ImGui::GetTextLineHeight() * 8;
    ImGui::TextUnformatted("Application code");
    wizard::DrawCodeBox("##settings_client_snippet", snippet_, ctx, h);
    ImGui::TextUnformatted("CMake");
    float h2 = ImGui::GetContentRegionAvail().y;
    if (h2 < ImGui::GetTextLineHeight() * 6) h2 = ImGui::GetTextLineHeight() * 6;
    wizard::DrawCodeBox("##settings_cmake_snippet", cmake_, ctx, h2);
}

void ProjectSettingsPanel::ImportSelectedLibraries(EditorContext& ctx) {
    for (wizard::LibraryEntry& lib : form_.libraries) {
        if (!lib.selected || !lib.importable) continue;
        std::string err;
        if (ctx.ImportImblib(lib.path, &err)) lib.selected = false;
        else ctx.SetStatus("Import of " + lib.name + " failed: " + err, 8.0);
    }
}

void ProjectSettingsPanel::Draw(EditorContext& ctx) {
    if (!ctx.has_project) {
        ui::EmptyState(IconUtf8("fa-gear"), "No project open",
                       "Create a project to choose its ImGui target, platform, features and code style.",
                       ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.y * 2.0f);
        ImGui::Spacing();
        ui::CenteredRowStart(ui::ButtonWidth("New project..."));
        if (ui::AccentButton("New project...")) OpenWizard();
        return;
    }
    if (attached_ != &ctx.project || form_.project != &ctx.project) {
        form_ = wizard::SettingsForm();
        form_.Attach(ctx.project);
        attached_ = &ctx.project;
        undo_cursor_ = ctx.undo.Cursor();
        undo_size_ = ctx.undo.Size();
    } else if (ctx.undo.Cursor() != undo_cursor_ || ctx.undo.Size() != undo_size_) {
        form_.SyncTexts();
        undo_cursor_ = ctx.undo.Cursor();
        undo_size_ = ctx.undo.Size();
    }
    bool changed = false;
    ctx.BeginEdit("Project settings", DocKind::Project, std::string());
    if (ImGui::BeginTabBar("project_settings_tabs", ImGuiTabBarFlags_FittingPolicyScroll)) {
        struct Tab {
            const char* name;
            int id;
        };
        const Tab tabs[] = {{"General", 0}, {"ImGui target", 1}, {"Platform", 2}, {"Features", 3}, {"Bindings", 4},
                            {"Live", 5},    {"Code style", 6},   {"Themes", 7},   {"Client snippet", 8}};
        for (const Tab& t : tabs) {
            if (!ImGui::BeginTabItem(t.name)) continue;
            if (ImGui::BeginChild("tab_content", ImVec2(0, 0), ImGuiChildFlags_AlwaysUseWindowPadding)) {
                switch (t.id) {
                case 0: DrawGeneral(ctx, changed); break;
                case 1: changed |= wizard::DrawTargetStep(form_, ctx); break;
                case 2: changed |= wizard::DrawPlatformStep(form_, ctx); break;
                case 3:
                    changed |= wizard::DrawFeaturesStep(form_, ctx);
                    {
                        bool any = false;
                        for (const wizard::LibraryEntry& l : form_.libraries) any = any || (l.selected && l.importable);
                        if (!any) ImGui::BeginDisabled();
                        if (ImGui::Button("Import selected libraries")) import_pending_ = true;
                        if (!any) ImGui::EndDisabled();
                    }
                    break;
                case 4: changed |= wizard::DrawBindingsStep(form_, ctx); break;
                case 5: changed |= wizard::DrawLiveStep(form_, ctx); break;
                case 6: changed |= wizard::DrawCodegenStep(form_, ctx); break;
                case 7: DrawThemes(ctx, changed); break;
                default: DrawSnippets(ctx); break;
                }
            }
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ctx.EndEdit();
    undo_cursor_ = ctx.undo.Cursor();
    undo_size_ = ctx.undo.Size();
    if (import_pending_) {
        import_pending_ = false;
        ImportSelectedLibraries(ctx);
        undo_cursor_ = ctx.undo.Cursor();
        undo_size_ = ctx.undo.Size();
    }
    (void)changed;
}

}  // namespace

IMFORGE_REGISTER_PANEL(ProjectSettingsPanel, 80);

}  // namespace imforge::builder
