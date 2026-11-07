#include <cfloat>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#include "imgui.h"

#include "../builder_fonts.h"
#include "../editor_context.h"
#include "../ui/file_dialog.h"
#include "../ui/fonts_state.h"
#include "../ui/icon_table.h"
#include "../ui/project_fonts.h"
#include "../ui/ui_helpers.h"
#include "editor_panel.h"

namespace imforge::builder {

namespace {

namespace fs = std::filesystem;

const char* kDialogKey = "fonts_panel_file";

std::string UniqueFontName(const Project& p, const std::string& base) {
    auto exists = [&](const std::string& n) {
        for (const FontSpec& f : p.fonts)
            if (f.name == n) return true;
        return false;
    };
    if (!exists(base)) return base;
    for (int i = 2;; ++i) {
        std::string cand = base + " " + std::to_string(i);
        if (!exists(cand)) return cand;
    }
}

std::string ProjectRelative(const std::string& path, const std::string& dir) {
    if (dir.empty()) return path;
    std::error_code ec;
    fs::path rel = fs::relative(fs::u8path(path), fs::u8path(dir), ec);
    if (ec || rel.empty()) return path;
    std::string s = rel.generic_u8string();
    if (s.compare(0, 2, "..") == 0) return fs::u8path(path).generic_u8string();
    return s;
}

int IndexOf(const char* const* names, size_t count, const std::string& value, int def) {
    for (size_t i = 0; i < count; ++i)
        if (value == names[i]) return (int)i;
    return def;
}

bool NamesCombo(const char* label, const char* const* names, size_t count, std::string& value, const char* def) {
    int cur = IndexOf(names, count, value.empty() ? def : value, 0);
    bool changed = false;
    if (ImGui::BeginCombo(label, names[cur])) {
        for (size_t i = 0; i < count; ++i) {
            if (ImGui::Selectable(names[i], (int)i == cur)) {
                value = names[i];
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    return changed;
}

class FontsPanel : public EditorPanel {
public:
    const char* Name() const override { return "Fonts"; }
    const char* DefaultDock() const override { return "right"; }
    const char* Icon() const override {
#ifdef ICON_FA_FONT
        return ICON_FA_FONT;
#else
        return nullptr;
#endif
    }
    void Draw(EditorContext& ctx) override;
    void OnProjectChanged(EditorContext&) override {
        FontsDirtyFlag() = true;
        selected_ = -1;
    }

private:
    int selected_ = -1;

    void DrawToolbar(EditorContext& ctx);
    void DrawFontList(EditorContext& ctx);
    void DrawEditor(EditorContext& ctx, int index);
    void DrawPreview(const FontSpec& spec);
    void HandleFileDialog(EditorContext& ctx);
};

void FontsPanel::Draw(EditorContext& ctx) {
    if (!ctx.has_project) {
        ImGui::TextDisabled("Open a project to manage fonts.");
        return;
    }
    DrawToolbar(ctx);
    DrawFontList(ctx);
    if (selected_ >= 0 && selected_ < (int)ctx.project.fonts.size()) DrawEditor(ctx, selected_);
    else ui::TextDim("Select a font above or add one.");
    HandleFileDialog(ctx);
}

void FontsPanel::DrawToolbar(EditorContext& ctx) {
    Project& p = ctx.project;
    if (ImGui::Button("Add font")) {
        FontSpec spec;
        spec.name = UniqueFontName(p, "Font");
        spec.size = 16;
        spec.ranges = "default";
        spec.merge_icons = "none";
        ctx.EditProject("Add font", [&] { p.fonts.push_back(spec); });
        selected_ = (int)p.fonts.size() - 1;
        FontsDirtyFlag() = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Reload fonts")) FontsDirtyFlag() = true;
    ui::Tooltip("Rebuild the font atlas with the project fonts at the start of the next frame");
    if (FontsDirtyFlag()) {
        ImGui::SameLine();
        ui::TextDim("rebuild pending");
    }
    size_t icon_count = 0;
    IconTable(icon_count);
    const std::string& src = IconTableSource();
    ui::TextDim("Icon table: %d icons (%s)", (int)icon_count, src == "builtin" ? "built-in list" : src.c_str());
}

void FontsPanel::DrawFontList(EditorContext& ctx) {
    Project& p = ctx.project;
    float h = ImGui::GetFrameHeightWithSpacing() * 5.5f;
    ImGuiTableFlags table_flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY;
    if (!ImGui::BeginTable("##font_list", 4, table_flags, ImVec2(0, h))) return;
    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 2.0f);
    ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 3.5f);
    ImGui::TableSetupColumn("File", ImGuiTableColumnFlags_WidthStretch, 3.0f);
    ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 5.0f);
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableHeadersRow();
    for (int i = 0; i < (int)p.fonts.size(); ++i) {
        const FontSpec& f = p.fonts[(size_t)i];
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::PushID(i);
        if (ImGui::Selectable(f.name.empty() ? "(unnamed)" : f.name.c_str(), selected_ == i,
                              ImGuiSelectableFlags_SpanAllColumns))
            selected_ = i;
        ImGui::TableSetColumnIndex(1);
        ImGui::Text("%.0f", (double)f.size);
        ImGui::TableSetColumnIndex(2);
        ImGui::TextUnformatted(f.file.c_str());
        ImGui::TableSetColumnIndex(3);
        const LoadedProjectFont* lf = FindLoadedProjectFont(f.name);
        if (!lf) ui::TextDim("pending");
        else if (lf->font && lf->error.empty()) ImGui::TextUnformatted("loaded");
        else if (lf->font) ImGui::TextColored(ui::LevelColor(2), "partial");
        else ImGui::TextColored(ui::LevelColor(3), "error");
        if (lf && !lf->error.empty()) ui::Tooltip(lf->error.c_str());
        ImGui::PopID();
    }
    ImGui::EndTable();
}

void FontsPanel::DrawEditor(EditorContext& ctx, int index) {
    Project& p = ctx.project;
    FontSpec& spec = p.fonts[(size_t)index];
    FontSpec edit = spec;
    const char* label = nullptr;

    if (ui::BeginPropertyTable("##font_props", ImGui::GetFontSize() * 6.5f)) {
        ui::PropertyLabel("Name");
        if (ui::InputTextStd("##font_name", edit.name)) label = "Font name";

        ui::PropertyLabel("File", "Path relative to the project folder, or absolute. Also searched in the builder fonts dir.");
        float browse_w = ImGui::CalcTextSize("Browse...").x + ImGui::GetStyle().FramePadding.x * 2;
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - browse_w - ImGui::GetStyle().ItemSpacing.x);
        if (ui::InputTextStd("##font_file", edit.file)) label = "Font file";
        ImGui::SameLine();
        if (ImGui::Button("Browse...")) {
            std::error_code ec;
            std::string start = (fs::u8path(p.dir) / "fonts").u8string();
            if (!fs::is_directory(fs::u8path(start), ec)) start = p.dir;
            FileDialogOptions opts;
            opts.title = "Choose a font file";
            opts.extensions = {".ttf", ".otf", ".ttc"};
            OpenFileDialog(kDialogKey, start, opts);
        }
        std::string resolved = ResolveFontFile(spec.file, p.dir);
        ui::PropertyLabel("Resolved");
        if (resolved.empty()) ImGui::TextColored(ui::LevelColor(2), "%s", spec.file.empty() ? "no file" : "not found");
        else ui::TextDim("%s", resolved.c_str());

        ui::PropertyLabel("Size");
        if (ImGui::DragFloat("##font_size", &edit.size, 0.25f, 6.0f, 128.0f, "%.1f px")) label = "Font size";

        size_t n = 0;
        ui::PropertyLabel("Glyph ranges");
        const char* const* ranges = GlyphRangeNames(n);
        if (NamesCombo("##font_ranges", ranges, n, edit.ranges, "default")) label = "Font glyph ranges";

        ui::PropertyLabel("Merge icons", "Merges an icon font into this font so {icon:fa-xxx} tokens render.");
        const char* const* merges = MergeIconNames(n);
        if (NamesCombo("##font_icons", merges, n, edit.merge_icons, "none")) label = "Font icons";
        ui::EndPropertyTable();
    }
    if (label) {
        ctx.EditProject(label, [&] { spec = edit; });
        FontsDirtyFlag() = true;
    }

    ImGui::BeginDisabled(index == 0);
    if (ImGui::Button("Up")) {
        ctx.EditProject("Reorder fonts", [&] { std::swap(p.fonts[(size_t)index], p.fonts[(size_t)index - 1]); });
        selected_ = index - 1;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(index + 1 >= (int)p.fonts.size());
    if (ImGui::Button("Down")) {
        ctx.EditProject("Reorder fonts", [&] { std::swap(p.fonts[(size_t)index], p.fonts[(size_t)index + 1]); });
        selected_ = index + 1;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Duplicate")) {
        FontSpec copy = spec;
        copy.name = UniqueFontName(p, spec.name + " Copy");
        ctx.EditProject("Duplicate font", [&] { p.fonts.insert(p.fonts.begin() + index + 1, copy); });
        selected_ = index + 1;
        FontsDirtyFlag() = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Remove")) {
        ctx.EditProject("Remove font", [&] { p.fonts.erase(p.fonts.begin() + index); });
        selected_ = -1;
        FontsDirtyFlag() = true;
        return;
    }
    DrawPreview(spec);
}

void FontsPanel::DrawPreview(const FontSpec& spec) {
    if (!ui::Section("Preview")) return;
    const LoadedProjectFont* lf = FindLoadedProjectFont(spec.name);
    if (!lf) {
        ui::TextDim("Not loaded yet. The atlas is rebuilt before the next frame when fonts change.");
        return;
    }
    if (!lf->font) {
        ImGui::TextColored(ui::LevelColor(3), "%s", lf->error.c_str());
        return;
    }
    if (!lf->error.empty()) ImGui::TextColored(ui::LevelColor(2), "%s", lf->error.c_str());
    ImGui::PushFont(lf->font);
    ImGui::TextUnformatted("The quick brown fox jumps over the lazy dog");
    ImGui::TextUnformatted("0123456789 !@#$%^&*() \xC3\x80\xC3\x89\xC3\x8E\xC3\x95\xC3\x9C \xC3\x9F\xC3\xB1\xC3\xA7");
    if (lf->icons_merged) {
        const char* sample[] = {"fa-play", "fa-pause", "fa-gear", "fa-house", "fa-user",
                                "fa-check", "fa-xmark", "fa-magnifying-glass", "fa-palette"};
        std::string icons;
        for (const char* s : sample) {
            const char* g = IconUtf8(s);
            if (g) icons += std::string(g) + "  ";
        }
        ImGui::TextUnformatted(icons.c_str());
    }
    ImGui::PopFont();
    ui::TextDim("%s at %.1f px%s", lf->resolved.c_str(), (double)lf->size, lf->icons_merged ? " + icons" : "");
}

void FontsPanel::HandleFileDialog(EditorContext& ctx) {
    std::string path;
    if (!DrawFileDialog(kDialogKey, path)) return;
    Project& p = ctx.project;
    if (selected_ < 0 || selected_ >= (int)p.fonts.size()) return;
    std::string rel = ProjectRelative(path, p.dir);
    ctx.EditProject("Font file", [&] { p.fonts[(size_t)selected_].file = rel; });
    FontsDirtyFlag() = true;
}

}  // namespace

IMFORGE_REGISTER_PANEL(FontsPanel, 61);

}  // namespace imforge::builder
