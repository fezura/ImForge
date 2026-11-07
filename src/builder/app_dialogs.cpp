#include <algorithm>
#include <filesystem>

#include "imgui.h"

#include "app.h"
#include "builder_style.h"
#include "os_util.h"
#include "panels/dialogs.h"
#include "project_io.h"
#include "ui/ui_helpers.h"

namespace imforge::builder {

namespace fs = std::filesystem;

void App::DrawModals(bool& quit) {
    DrawUnsavedPrompt(quit);
    DrawPathDialog();
    DrawAboutDialog(ctx_, show_about_, platform_.Name());
}

void App::DrawUnsavedPrompt(bool& quit) {
    (void)quit;
    const char* kTitle = "Unsaved changes###imforge_unsaved";
    if (show_unsaved_ && !ImGui::IsPopupOpen(kTitle)) ImGui::OpenPopup(kTitle);
    if (!show_unsaved_) return;
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal(kTitle, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
    ImGui::Text("Project \"%s\" has unsaved changes.", ctx_.ProjectDisplayName().c_str());
    ui::TextDim("%s", ctx_.project.dir.c_str());
    ImGui::Spacing();
    Pending action = pending_;
    std::string dir = pending_dir_;
    bool close = false;
    if (ui::AccentButton("Save", ImVec2(120 * ctx_.dpi_scale, 0))) {
        std::string err;
        if (ctx_.SaveProject(&err)) {
            close = true;
            Proceed(action, dir);
        } else {
            ctx_.SetStatus("Save failed: " + err, 8.0);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Don't Save", ImVec2(120 * ctx_.dpi_scale, 0))) {
        close = true;
        ctx_.dirty = false;
        Proceed(action, dir);
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120 * ctx_.dpi_scale, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        close = true;
        pending_ = Pending::None;
    }
    if (close) {
        show_unsaved_ = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void App::OpenPathDialog(PathDialog::Kind kind, const std::string& initial) {
    path_dialog_ = PathDialog();
    path_dialog_.kind = kind;
    path_dialog_.open = true;
    path_dialog_.focus_input = true;
    std::error_code ec;
    std::string start = initial;
    if (start.empty() || !fs::is_directory(start, ec)) start = HomeDirOrCwd();
    path_dialog_.path = NormalizePath(start);
    if (kind == PathDialog::Kind::NewProject) {
        path_dialog_.name = "MyMenu";
        std::string tdir = ctx_.prefs.templates_dir.empty() ? DefaultTemplatesDir() : ctx_.prefs.templates_dir;
        path_dialog_.templates = ListTemplates(tdir);
        path_dialog_.templates.insert(path_dialog_.templates.begin(), "(blank)");
    }
    RefreshPathDialogListing();
}

void App::RefreshPathDialogListing() {
    PathDialog& d = path_dialog_;
    d.dirs.clear();
    d.files.clear();
    std::error_code ec;
    fs::path p(d.path);
    if (!fs::is_directory(p, ec)) p = p.parent_path();
    if (!fs::is_directory(p, ec)) return;
    d.listed_dir = p.generic_string();
    for (const auto& e : fs::directory_iterator(p, ec)) {
        std::string n = e.path().filename().string();
        if (n.empty() || n[0] == '.') continue;
        if (e.is_directory(ec)) d.dirs.push_back(n);
        else if (d.kind == PathDialog::Kind::Import && e.path().extension() == ".imblib") d.files.push_back(n);
    }
    std::sort(d.dirs.begin(), d.dirs.end());
    std::sort(d.files.begin(), d.files.end());
}

bool App::AcceptPathDialog() {
    PathDialog& d = path_dialog_;
    d.error.clear();
    std::error_code ec;
    switch (d.kind) {
    case PathDialog::Kind::OpenProject: {
        std::string dir = d.path;
        if (fs::is_regular_file(dir, ec) && fs::path(dir).filename() == "imforge.json") dir = fs::path(dir).parent_path().string();
        if (!IsProjectDir(dir)) {
            d.error = "No imforge.json in " + dir;
            return false;
        }
        OpenProjectDir(dir);
        return true;
    }
    case PathDialog::Kind::SaveAs: {
        if (d.path.empty()) {
            d.error = "Choose a folder";
            return false;
        }
        std::string err;
        if (!ctx_.SaveProjectAs(d.path, &err)) {
            d.error = err;
            return false;
        }
        return true;
    }
    case PathDialog::Kind::Import: {
        if (!fs::is_regular_file(d.path, ec)) {
            d.error = "Select a .imblib file";
            return false;
        }
        std::string err;
        if (!ctx_.ImportImblib(d.path, &err)) {
            d.error = err;
            return false;
        }
        return true;
    }
    case PathDialog::Kind::NewProject: {
        if (d.name.empty() || !core_detail::IsIdentifier(d.name)) {
            d.error = "Project name must be an identifier (letters, digits, underscore)";
            return false;
        }
        std::string dest = JoinPath(d.path, d.name);
        std::string tdir;
        if (d.template_index > 0 && (size_t)d.template_index < d.templates.size()) {
            std::string base = ctx_.prefs.templates_dir.empty() ? DefaultTemplatesDir() : ctx_.prefs.templates_dir;
            tdir = JoinPath(base, d.templates[(size_t)d.template_index]);
        }
        Error terr;
        Project settings = SettingsFromTemplate(tdir, &terr);
        if (!terr.message.empty()) ctx_.Log(LogLevel::Warning, "template %s: %s (using defaults)", tdir.c_str(), terr.message.c_str());
        settings.live.transport = ctx_.prefs.last_transport;
        std::string err;
        if (!ctx_.NewProject(tdir, dest, d.name, settings, &err)) {
            d.error = err;
            return false;
        }
        if (EditorPanel* w = ctx_.FindPanel("Wizard")) w->open = false;
        return true;
    }
    default: return true;
    }
}

void App::DrawPathDialog() {
    PathDialog& d = path_dialog_;
    if (!d.open) return;
    const char* title = "Path###imforge_path_dialog";
    switch (d.kind) {
    case PathDialog::Kind::OpenProject: title = "Open Project###imforge_path_dialog"; break;
    case PathDialog::Kind::SaveAs: title = "Save Project As###imforge_path_dialog"; break;
    case PathDialog::Kind::Import: title = "Import .imblib###imforge_path_dialog"; break;
    case PathDialog::Kind::NewProject: title = "New Project###imforge_path_dialog"; break;
    default: break;
    }
    if (!ImGui::IsPopupOpen(title)) ImGui::OpenPopup(title);
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(620 * ctx_.dpi_scale, 460 * ctx_.dpi_scale), ImGuiCond_Appearing);
    bool keep_open = true;
    if (!ImGui::BeginPopupModal(title, &keep_open, ImGuiWindowFlags_NoCollapse)) {
        d.open = false;
        return;
    }
    bool accept = false;
    if (d.kind == PathDialog::Kind::NewProject) {
        if (ui::BeginPropertyTable("np", ImGui::GetFontSize() * 6.0f)) {
            ui::PropertyLabel("Name");
            if (d.focus_input) ImGui::SetKeyboardFocusHere();
            if (ui::InputTextStd("##name", d.name, ImGuiInputTextFlags_EnterReturnsTrue)) accept = true;
            ui::PropertyLabel("Template");
            const char* preview = d.templates.empty() ? "(blank)" : d.templates[(size_t)d.template_index].c_str();
            if (ImGui::BeginCombo("##tpl", preview)) {
                for (size_t i = 0; i < d.templates.size(); ++i)
                    if (ImGui::Selectable(d.templates[i].c_str(), (int)i == d.template_index)) d.template_index = (int)i;
                ImGui::EndCombo();
            }
            ui::PropertyLabel("Location");
            ImGui::TextUnformatted("");
            ui::EndPropertyTable();
        }
        d.focus_input = false;
    }
    const char* path_label = d.kind == PathDialog::Kind::Import ? "File" : "Folder";
    ImGui::AlignTextToFramePadding();
    ui::TextDim("%s", path_label);
    ImGui::SameLine();
    float browse_w = NativeDialogsAvailable() ? ImGui::CalcTextSize("Browse...").x + ImGui::GetStyle().FramePadding.x * 2 + 8 : 0;
    ImGui::SetNextItemWidth(-browse_w - (browse_w > 0 ? ImGui::GetStyle().ItemSpacing.x : 0));
    if (d.focus_input && d.kind != PathDialog::Kind::NewProject) {
        ImGui::SetKeyboardFocusHere();
        d.focus_input = false;
    }
    if (ui::InputTextStd("##path", d.path, ImGuiInputTextFlags_EnterReturnsTrue)) {
        std::error_code ec;
        if (fs::is_directory(d.path, ec) && d.kind != PathDialog::Kind::Import && !IsProjectDir(d.path)) {
            d.path = NormalizePath(d.path);
            RefreshPathDialogListing();
        } else {
            accept = true;
        }
    }
    if (browse_w > 0) {
        ImGui::SameLine();
        if (ImGui::Button("Browse...")) {
            std::string picked;
            bool ok = d.kind == PathDialog::Kind::Import ? NativePickFile(picked, "Import .imblib", "imblib", d.listed_dir)
                                                         : NativePickFolder(picked, title, d.listed_dir);
            if (ok) {
                d.path = picked;
                RefreshPathDialogListing();
                if (d.kind == PathDialog::Kind::OpenProject && IsProjectDir(picked)) accept = true;
            }
        }
    }
    if (d.kind == PathDialog::Kind::NewProject) ui::TextDim("Project will be created in %s", JoinPath(d.path, d.name).c_str());

    float footer = ImGui::GetFrameHeightWithSpacing() * 2.2f;
    if (ImGui::BeginChild("listing", ImVec2(0, -footer), IMFORGE_CHILD_BORDER)) {
        if (ImGui::Selectable("..", false, ImGuiSelectableFlags_AllowDoubleClick) && ImGui::IsMouseDoubleClicked(0)) {
            fs::path parent = fs::path(d.listed_dir).parent_path();
            if (!parent.empty()) {
                d.path = parent.generic_string();
                RefreshPathDialogListing();
            }
        }
        for (const std::string& dir : d.dirs) {
            std::string full = JoinPath(d.listed_dir, dir);
            bool is_project = IsProjectDir(full);
            if (is_project) ImGui::PushStyleColor(ImGuiCol_Text, Colors().accent);
            bool clicked = ImGui::Selectable((dir + "/").c_str(), d.path == full, ImGuiSelectableFlags_AllowDoubleClick);
            if (is_project) ImGui::PopStyleColor();
            if (clicked) {
                if (ImGui::IsMouseDoubleClicked(0)) {
                    if (d.kind == PathDialog::Kind::OpenProject && is_project) {
                        d.path = full;
                        accept = true;
                    } else {
                        d.path = full;
                        RefreshPathDialogListing();
                    }
                } else {
                    d.path = full;
                }
            }
        }
        for (const std::string& f : d.files) {
            std::string full = JoinPath(d.listed_dir, f);
            if (ImGui::Selectable(f.c_str(), d.path == full, ImGuiSelectableFlags_AllowDoubleClick)) {
                d.path = full;
                if (ImGui::IsMouseDoubleClicked(0)) accept = true;
            }
        }
    }
    ImGui::EndChild();
    if (!d.error.empty()) ImGui::TextColored(Colors().error, "%s", d.error.c_str());
    else ui::TextDim(" ");
    const char* ok_label = d.kind == PathDialog::Kind::OpenProject ? "Open"
                           : d.kind == PathDialog::Kind::SaveAs     ? "Save Here"
                           : d.kind == PathDialog::Kind::Import     ? "Import"
                                                                    : "Create";
    if (ui::AccentButton(ok_label, ImVec2(120 * ctx_.dpi_scale, 0))) accept = true;
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120 * ctx_.dpi_scale, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) keep_open = false;
    if (accept && AcceptPathDialog()) keep_open = false;
    if (!keep_open) {
        d.open = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

}  // namespace imforge::builder
