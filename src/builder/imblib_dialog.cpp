#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "imgui.h"

#include "builder_style.h"
#include "editor_context.h"
#include "imblib.h"
#include "ui/ui_helpers.h"

#if __has_include("ui/file_dialog.h")
#include "ui/file_dialog.h"
#define IMFORGE_IMBLIB_FILE_DIALOG 1
#else
#define IMFORGE_IMBLIB_FILE_DIALOG 0
#endif

namespace imforge::builder {

namespace {

namespace fs = std::filesystem;

struct DialogState {
    bool import_pending = false, import_open = false;
    bool export_pending = false, export_open = false;
    std::string import_path, preview_path, error;
    LibraryInfo preview;
    std::string export_path, export_name, export_version = "1.0.0", auto_path;
    std::vector<std::string> comp_names, widget_types;
    std::vector<char> comp_sel, widget_sel;
};

DialogState& State() {
    static DialogState s;
    return s;
}

std::string JoinDir(const std::string& dir, const std::string& rel) {
    return dir.empty() ? rel : (fs::path(dir) / rel).generic_string();
}

std::string FileName(const std::string& path) { return fs::path(path).filename().generic_string(); }

bool DoExport(EditorContext& ctx, const ImblibExportOptions& opts, const std::string& out_path, std::string* err) {
    if (!ctx.has_project) {
        if (err) *err = "no project open";
        return false;
    }
    std::vector<std::string> warnings;
    std::string e;
    if (!imblib::Export(ctx.project, &ctx.catalog, opts, out_path, &e, &warnings)) {
        ctx.LogSource(LogLevel::Error, "imblib", "export failed: %s", e.c_str());
        if (err) *err = e;
        return false;
    }
    for (const std::string& w : warnings) ctx.LogSource(LogLevel::Warning, "imblib", "%s", w.c_str());
    ctx.LogSource(LogLevel::Info, "imblib", "exported library \"%s\" (%d components, %d widgets) to %s", opts.name.c_str(),
                  (int)opts.component_names.size(), (int)opts.widget_types.size(), out_path.c_str());
    ctx.SetStatus("Exported " + FileName(out_path));
    return true;
}

void DrawBrowse(const char* key, std::string& path, bool save, const std::string& start_dir, const char* title) {
#if IMFORGE_IMBLIB_FILE_DIALOG
    ImGui::SameLine();
    if (ImGui::Button("Browse...")) {
        FileDialogOptions o;
        o.title = title;
        o.extensions = {".imblib"};
        o.save = save;
        o.default_name = save ? FileName(path) : std::string();
        std::string dir = start_dir;
        std::error_code ec;
        if (!path.empty() && fs::path(path).has_parent_path() && fs::is_directory(fs::path(path).parent_path(), ec))
            dir = fs::path(path).parent_path().generic_string();
        OpenFileDialog(key, dir, o);
    }
    std::string picked;
    if (DrawFileDialog(key, picked)) path = picked;
#else
    (void)key;
    (void)path;
    (void)save;
    (void)start_dir;
    (void)title;
#endif
}

void DrawErrorText(const std::string& text) {
    if (text.empty()) return;
    ImGui::PushStyleColor(ImGuiCol_Text, Colors().error);
    ImGui::TextWrapped("%s", text.c_str());
    ImGui::PopStyleColor();
}

std::string JoinNames(const std::vector<std::string>& v) {
    std::string out;
    for (const std::string& s : v) out += (out.empty() ? "" : ", ") + s;
    return out.empty() ? "-" : out;
}

void DrawImportDialog(EditorContext& ctx) {
    DialogState& s = State();
    const char* title = "Import library###imblib_import";
    if (s.import_pending) {
        ImGui::OpenPopup(title);
        s.import_pending = false;
    }
    if (!s.import_open) return;
    ImGui::SetNextWindowSize(ImVec2(540, 0), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal(title, &s.import_open, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
        if (!ImGui::IsPopupOpen(title)) s.import_open = false;
        return;
    }
    ImGui::TextUnformatted("Library file (.imblib)");
    ImGui::SetNextItemWidth(-(ImGui::CalcTextSize("Browse...").x + ImGui::GetStyle().FramePadding.x * 2 + ImGui::GetStyle().ItemSpacing.x));
    ui::InputTextStd("##import_path", s.import_path);
    DrawBrowse("imblib_import", s.import_path, false, ctx.project.dir, "Import library");
    if (s.import_path != s.preview_path) {
        s.preview_path = s.import_path;
        s.preview = LibraryInfo();
        s.error.clear();
        std::error_code ec;
        if (!s.import_path.empty() && fs::is_regular_file(s.import_path, ec)) imblib::ReadLibraryInfo(s.import_path, s.preview);
    }
    if (s.preview.valid) {
        ImGui::Separator();
        ImGui::Text("%s %s", s.preview.name.c_str(), s.preview.version.c_str());
        ImGui::SameLine();
        ui::TextDim("(min ImGui %s)", Project::VersionString(s.preview.min_imgui_version).c_str());
        ImGui::TextWrapped("Components: %s", JoinNames(s.preview.components).c_str());
        ImGui::TextWrapped("Widgets: %s", JoinNames(s.preview.widgets).c_str());
        ImGui::TextWrapped("Headers: %s", JoinNames(s.preview.headers).c_str());
        if (!s.preview.dependencies.empty()) ImGui::TextWrapped("Depends on: %s", JoinNames(s.preview.dependencies).c_str());
        for (const std::string& c : s.preview.components) {
            if (ctx.FindComponentDoc(c)) {
                ImGui::PushStyleColor(ImGuiCol_Text, Colors().warning);
                ImGui::TextWrapped("%s already exists in the project; a differing copy is imported as %s.%s", c.c_str(),
                                   s.preview.name.c_str(), c.c_str());
                ImGui::PopStyleColor();
            }
        }
    } else if (!s.preview.error.empty()) {
        DrawErrorText(s.preview.error);
    }
    DrawErrorText(s.error);
    ImGui::Separator();
    ImGui::BeginDisabled(!s.preview.valid);
    if (ui::AccentButton("Import", ImVec2(110, 0))) {
        std::string err;
        if (ImportImblib(ctx, s.import_path, &err)) {
            s.import_open = false;
            ImGui::CloseCurrentPopup();
        } else {
            s.error = err;
        }
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(90, 0))) {
        s.import_open = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void DrawCheckList(const char* id, const std::vector<std::string>& names, std::vector<char>& sel, float height) {
    ImGui::PushID(id);
    if (ImGui::SmallButton("All"))
        std::fill(sel.begin(), sel.end(), (char)1);
    ImGui::SameLine();
    if (ImGui::SmallButton("None"))
        std::fill(sel.begin(), sel.end(), (char)0);
    if (ImGui::BeginChild("list", ImVec2(0, height), ImGuiChildFlags_Borders)) {
        if (names.empty()) ui::TextDim("(none)");
        for (size_t i = 0; i < names.size(); ++i) {
            bool v = sel[i] != 0;
            ImGui::PushID((int)i);
            if (ImGui::Checkbox(names[i].c_str(), &v)) sel[i] = v ? 1 : 0;
            ImGui::PopID();
        }
    }
    ImGui::EndChild();
    ImGui::PopID();
}

void DrawExportDialog(EditorContext& ctx) {
    DialogState& s = State();
    const char* title = "Export library###imblib_export";
    if (s.export_pending) {
        ImGui::OpenPopup(title);
        s.export_pending = false;
    }
    if (!s.export_open) return;
    ImGui::SetNextWindowSize(ImVec2(560, 0), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal(title, &s.export_open, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
        if (!ImGui::IsPopupOpen(title)) s.export_open = false;
        return;
    }
    if (ui::BeginPropertyTable("export_props")) {
        ui::PropertyLabel("Library name");
        if (ui::InputTextStd("##name", s.export_name)) {
            std::string auto_path = JoinDir(ctx.project.dir, "libs/" + imblib::SanitizeLibraryName(s.export_name) + ".imblib");
            if (s.export_path == s.auto_path) s.export_path = auto_path;
            s.auto_path = auto_path;
        }
        ui::PropertyLabel("Version");
        ui::InputTextStd("##version", s.export_version);
        ui::EndPropertyTable();
    }
    ImGui::TextUnformatted("Output file");
    ImGui::SetNextItemWidth(-(ImGui::CalcTextSize("Browse...").x + ImGui::GetStyle().FramePadding.x * 2 + ImGui::GetStyle().ItemSpacing.x));
    ui::InputTextStd("##export_path", s.export_path);
    DrawBrowse("imblib_export", s.export_path, true, ctx.project.dir, "Export library");
    float h = ImGui::GetTextLineHeightWithSpacing() * 6.5f;
    if (ImGui::BeginTable("export_lists", 2, ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted("Components");
        DrawCheckList("comps", s.comp_names, s.comp_sel, h);
        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted("Custom widgets");
        DrawCheckList("widgets", s.widget_types, s.widget_sel, h);
        ImGui::EndTable();
    }
    ui::TextDim("Headers referenced by the selected widgets are bundled when found in the project.");
    DrawErrorText(s.error);
    ImGui::Separator();
    if (ui::AccentButton("Export", ImVec2(110, 0))) {
        ImblibExportOptions opts;
        opts.name = imblib::SanitizeLibraryName(s.export_name);
        opts.version = s.export_version;
        for (size_t i = 0; i < s.comp_names.size(); ++i)
            if (s.comp_sel[i]) opts.component_names.push_back(s.comp_names[i]);
        for (size_t i = 0; i < s.widget_types.size(); ++i)
            if (s.widget_sel[i]) opts.widget_types.push_back(s.widget_types[i]);
        if (opts.component_names.empty() && opts.widget_types.empty()) {
            s.error = "Select at least one component or widget";
        } else if (s.export_path.empty()) {
            s.error = "Choose an output file";
        } else {
            std::string err;
            if (DoExport(ctx, opts, s.export_path, &err)) {
                s.export_open = false;
                ImGui::CloseCurrentPopup();
            } else {
                s.error = err;
            }
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(90, 0))) {
        s.export_open = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

}  // namespace

bool ImportImblib(EditorContext& ctx, const std::string& path, std::string* err) { return ctx.ImportImblib(path, err); }

bool ExportImblib(EditorContext& ctx, const std::vector<std::string>& component_names,
                  const std::vector<std::string>& widget_types, const std::string& out_path, std::string* err) {
    ImblibExportOptions opts;
    opts.name = imblib::LibraryNameFromPath(out_path);
    opts.component_names = component_names;
    opts.widget_types = widget_types;
    return DoExport(ctx, opts, out_path, err);
}

void OpenImblibImportDialog(EditorContext& ctx) {
    DialogState& s = State();
    s.import_pending = true;
    s.import_open = true;
    s.error.clear();
    s.preview_path.clear();
    if (s.import_path.empty()) s.import_path = JoinDir(ctx.project.dir, "libs");
}

void OpenImblibExportDialog(EditorContext& ctx, const std::vector<std::string>& preselect_components) {
    DialogState& s = State();
    s.export_pending = true;
    s.export_open = true;
    s.error.clear();
    s.comp_names.clear();
    s.widget_types.clear();
    for (const Component& c : ctx.project.components) s.comp_names.push_back(c.name);
    for (const CustomWidgetDef& w : ctx.catalog.custom) s.widget_types.push_back(w.type);
    s.comp_sel.assign(s.comp_names.size(), 0);
    s.widget_sel.assign(s.widget_types.size(), 0);
    for (size_t i = 0; i < s.comp_names.size(); ++i)
        if (std::find(preselect_components.begin(), preselect_components.end(), s.comp_names[i]) != preselect_components.end())
            s.comp_sel[i] = 1;
    if (s.export_name.empty()) s.export_name = imblib::SanitizeLibraryName(ctx.project.name);
    s.auto_path = JoinDir(ctx.project.dir, "libs/" + imblib::SanitizeLibraryName(s.export_name) + ".imblib");
    if (s.export_path.empty()) s.export_path = s.auto_path;
}

void DrawImblibDialogs(EditorContext& ctx) {
    DrawImportDialog(ctx);
    DrawExportDialog(ctx);
}

}  // namespace imforge::builder
