#include "file_dialog.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <system_error>

#include "imgui.h"
#include "imgui_internal.h"

namespace fs = std::filesystem;

namespace imforge {
namespace builder {

namespace {

struct Entry {
    std::string name;
    bool is_dir = false;
    unsigned long long size = 0;
};

struct DialogState {
    std::string key;
    std::string popup_id;
    FileDialogOptions options;
    std::string dir;
    std::vector<Entry> entries;
    std::vector<std::string> recent;
    std::string last_dir;
    char path_buf[1024] = "";
    char name_buf[512] = "";
    char filter_buf[64] = "";
    int selected = -1;
    int ext_filter = 0;
    bool pending_open = false;
    bool open = false;
    bool needs_refresh = true;
    bool show_hidden = false;
    bool drive_list = false;
};

DialogState& State() {
    static DialogState s;
    return s;
}

std::string ToUtf8(const fs::path& p) { return p.u8string(); }
fs::path FromUtf8(const std::string& s) { return fs::u8path(s); }

std::string LowerExt(const std::string& name) {
    size_t dot = name.find_last_of('.');
    if (dot == std::string::npos) return std::string();
    std::string ext = name.substr(dot);
    for (char& c : ext)
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    return ext;
}

bool NameLess(const Entry& a, const Entry& b) {
    if (a.is_dir != b.is_dir) return a.is_dir;
    return ImStricmp(a.name.c_str(), b.name.c_str()) < 0;
}

void RefreshDrives(DialogState& s) {
    s.entries.clear();
#ifdef _WIN32
    for (char letter = 'A'; letter <= 'Z'; ++letter) {
        std::string root;
        root.push_back(letter);
        root += ":\\";
        std::error_code ec;
        if (fs::exists(FromUtf8(root), ec) && !ec) {
            Entry e;
            e.name = root;
            e.is_dir = true;
            s.entries.push_back(e);
        }
    }
#else
    Entry e;
    e.name = "/";
    e.is_dir = true;
    s.entries.push_back(e);
#endif
    s.drive_list = true;
    s.needs_refresh = false;
    s.selected = -1;
    std::snprintf(s.path_buf, sizeof(s.path_buf), "%s", "");
}

void Refresh(DialogState& s) {
    s.drive_list = false;
    s.entries.clear();
    s.selected = -1;
    std::error_code ec;
    fs::path dir = FromUtf8(s.dir);
    if (s.dir.empty() || !fs::is_directory(dir, ec)) {
        RefreshDrives(s);
        return;
    }
    fs::directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec);
    if (ec) {
        RefreshDrives(s);
        return;
    }
    for (const fs::directory_entry& de : it) {
        std::error_code e2;
        Entry e;
        e.name = ToUtf8(de.path().filename());
        if (e.name.empty()) continue;
        if (!s.show_hidden && e.name[0] == '.') continue;
        e.is_dir = de.is_directory(e2);
        if (!e.is_dir) e.size = (unsigned long long)de.file_size(e2);
        s.entries.push_back(std::move(e));
    }
    std::sort(s.entries.begin(), s.entries.end(), NameLess);
    std::snprintf(s.path_buf, sizeof(s.path_buf), "%s", s.dir.c_str());
    s.needs_refresh = false;
}

void Navigate(DialogState& s, const std::string& dir) {
    std::error_code ec;
    fs::path p = FromUtf8(dir);
    fs::path canon = fs::weakly_canonical(p, ec);
    s.dir = ec ? ToUtf8(p) : ToUtf8(canon);
    s.needs_refresh = true;
    s.filter_buf[0] = 0;
}

void NavigateUp(DialogState& s) {
    if (s.drive_list) return;
    fs::path p = FromUtf8(s.dir);
    fs::path parent = p.parent_path();
    if (parent.empty() || parent == p) {
        s.dir.clear();
        s.needs_refresh = true;
        return;
    }
    Navigate(s, ToUtf8(parent));
}

bool PassesFilter(const DialogState& s, const Entry& e) {
    if (e.is_dir) return true;
    if (s.options.pick_directory) return false;
    if (s.filter_buf[0] && !ImStristr(e.name.c_str(), nullptr, s.filter_buf, nullptr)) return false;
    if (s.options.extensions.empty()) return true;
    if (s.ext_filter == (int)s.options.extensions.size() + 1) return true;
    std::string ext = LowerExt(e.name);
    if (s.ext_filter == 0) {
        for (const std::string& want : s.options.extensions)
            if (ext == want) return true;
        return false;
    }
    return ext == s.options.extensions[(size_t)s.ext_filter - 1];
}

std::string FormatSize(unsigned long long bytes) {
    char buf[32];
    if (bytes < 1024) std::snprintf(buf, sizeof(buf), "%llu B", bytes);
    else if (bytes < 1024 * 1024) std::snprintf(buf, sizeof(buf), "%.1f KB", bytes / 1024.0);
    else std::snprintf(buf, sizeof(buf), "%.1f MB", bytes / (1024.0 * 1024.0));
    return buf;
}

void AddRecent(DialogState& s, const std::string& dir) {
    if (dir.empty()) return;
    auto it = std::find(s.recent.begin(), s.recent.end(), dir);
    if (it != s.recent.end()) s.recent.erase(it);
    s.recent.insert(s.recent.begin(), dir);
    if (s.recent.size() > 10) s.recent.resize(10);
}

bool Accept(DialogState& s, std::string& out_path) {
    std::error_code ec;
    if (s.options.pick_directory) {
        std::string chosen = s.dir;
        if (s.selected >= 0 && s.selected < (int)s.entries.size() && s.entries[(size_t)s.selected].is_dir)
            chosen = s.drive_list ? s.entries[(size_t)s.selected].name
                                  : ToUtf8(FromUtf8(s.dir) / FromUtf8(s.entries[(size_t)s.selected].name));
        if (chosen.empty() || !fs::is_directory(FromUtf8(chosen), ec)) return false;
        out_path = chosen;
        AddRecent(s, chosen);
        return true;
    }
    std::string name = s.name_buf;
    if (name.empty()) return false;
    fs::path full = FromUtf8(name);
    if (full.is_relative()) full = FromUtf8(s.dir) / full;
    if (fs::is_directory(full, ec)) {
        Navigate(s, ToUtf8(full));
        s.name_buf[0] = 0;
        return false;
    }
    if (s.options.save) {
        if (!s.options.extensions.empty() && LowerExt(name).empty()) full += s.options.extensions[0];
    } else if (!fs::is_regular_file(full, ec)) {
        return false;
    }
    out_path = ToUtf8(full);
    AddRecent(s, ToUtf8(full.parent_path()));
    s.last_dir = ToUtf8(full.parent_path());
    return true;
}

void DrawToolbar(DialogState& s) {
    if (ImGui::Button("Up")) NavigateUp(s);
    ImGui::SameLine();
    if (ImGui::Button("Refresh")) s.needs_refresh = true;
    ImGui::SameLine();
    if (!s.recent.empty()) {
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 6);
        if (ImGui::BeginCombo("##recent", "Recent", ImGuiComboFlags_NoPreview | ImGuiComboFlags_HeightLarge)) {
            for (const std::string& r : s.recent)
                if (ImGui::Selectable(r.c_str())) Navigate(s, r);
            ImGui::EndCombo();
        }
        ImGui::SameLine();
    }
    ImGui::Checkbox("Hidden", &s.show_hidden);
    if (ImGui::IsItemDeactivatedAfterEdit()) s.needs_refresh = true;
    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputText("##path", s.path_buf, sizeof(s.path_buf), ImGuiInputTextFlags_EnterReturnsTrue)) {
        std::error_code ec;
        if (fs::is_directory(FromUtf8(s.path_buf), ec)) Navigate(s, s.path_buf);
        else if (fs::is_regular_file(FromUtf8(s.path_buf), ec)) {
            fs::path p = FromUtf8(s.path_buf);
            Navigate(s, ToUtf8(p.parent_path()));
            std::snprintf(s.name_buf, sizeof(s.name_buf), "%s", ToUtf8(p.filename()).c_str());
        }
    }
}

bool DrawList(DialogState& s, std::string& out_path, float height) {
    bool accepted = false;
    if (ImGui::BeginTable("##files", 2,
                          ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV,
                          ImVec2(0, height))) {
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 5.5f);
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();
        for (int i = 0; i < (int)s.entries.size(); ++i) {
            const Entry& e = s.entries[(size_t)i];
            if (!PassesFilter(s, e)) continue;
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::PushID(i);
            std::string label = e.is_dir ? "[" + e.name + "]" : e.name;
            bool selected = s.selected == i;
            if (ImGui::Selectable(label.c_str(), selected,
                                  ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick)) {
                s.selected = i;
                if (!e.is_dir) std::snprintf(s.name_buf, sizeof(s.name_buf), "%s", e.name.c_str());
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    if (e.is_dir) {
                        Navigate(s, s.drive_list ? e.name : ToUtf8(FromUtf8(s.dir) / FromUtf8(e.name)));
                    } else if (Accept(s, out_path)) {
                        accepted = true;
                    }
                }
            }
            ImGui::TableSetColumnIndex(1);
            if (!e.is_dir) ImGui::TextDisabled("%s", FormatSize(e.size).c_str());
            ImGui::PopID();
            if (s.needs_refresh) break;
        }
        ImGui::EndTable();
    }
    return accepted;
}

}  // namespace

void OpenFileDialog(const char* key, const std::string& start_dir, const FileDialogOptions& options) {
    DialogState& s = State();
    s.key = key ? key : "";
    s.popup_id = options.title + "###imforge_file_dialog";
    s.options = options;
    std::error_code ec;
    std::string dir = start_dir;
    if (dir.empty() || !fs::is_directory(FromUtf8(dir), ec)) dir = s.last_dir;
    if (dir.empty() || !fs::is_directory(FromUtf8(dir), ec)) dir = ToUtf8(fs::current_path(ec));
    s.dir = dir;
    s.needs_refresh = true;
    s.pending_open = true;
    s.open = true;
    s.selected = -1;
    s.ext_filter = 0;
    s.filter_buf[0] = 0;
    std::snprintf(s.name_buf, sizeof(s.name_buf), "%s", options.default_name.c_str());
}

bool FileDialogIsOpen(const char* key) {
    DialogState& s = State();
    return s.open && key && s.key == key;
}

const std::vector<std::string>& FileDialogRecentDirs() { return State().recent; }

void FileDialogAddRecentDir(const std::string& dir) { AddRecent(State(), dir); }

bool DrawFileDialog(const char* key, std::string& out_path) {
    DialogState& s = State();
    if (!key || s.key != key || !s.open) return false;
    if (s.pending_open) {
        ImGui::OpenPopup(s.popup_id.c_str());
        s.pending_open = false;
    }
    ImGui::SetNextWindowSize(ImVec2(640, 460), ImGuiCond_Appearing);
    bool keep_open = true;
    bool accepted = false;
    if (!ImGui::BeginPopupModal(s.popup_id.c_str(), &keep_open, ImGuiWindowFlags_NoSavedSettings)) {
        if (!ImGui::IsPopupOpen(s.popup_id.c_str())) s.open = false;
        return false;
    }
    if (s.needs_refresh) Refresh(s);
    DrawToolbar(s);
    float footer = ImGui::GetFrameHeightWithSpacing() * 2 + ImGui::GetStyle().ItemSpacing.y;
    float list_h = ImGui::GetContentRegionAvail().y - footer;
    if (list_h < ImGui::GetFrameHeight() * 4) list_h = ImGui::GetFrameHeight() * 4;
    if (DrawList(s, out_path, list_h)) accepted = true;

    if (s.options.pick_directory) {
        ImGui::TextUnformatted("Folder:");
        ImGui::SameLine();
        ImGui::TextUnformatted(s.dir.empty() ? "(select a drive)" : s.dir.c_str());
    } else {
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.55f);
        if (ImGui::InputText("##name", s.name_buf, sizeof(s.name_buf), ImGuiInputTextFlags_EnterReturnsTrue))
            if (Accept(s, out_path)) accepted = true;
        ImGui::SameLine();
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.5f);
        if (!s.options.extensions.empty()) {
            std::string all;
            for (const std::string& e : s.options.extensions) all += (all.empty() ? "*" : " *") + e;
            std::string preview = s.ext_filter == 0 ? all
                                  : s.ext_filter == (int)s.options.extensions.size() + 1
                                      ? "*.*"
                                      : "*" + s.options.extensions[(size_t)s.ext_filter - 1];
            if (ImGui::BeginCombo("##ext", preview.c_str())) {
                if (ImGui::Selectable(all.c_str(), s.ext_filter == 0)) s.ext_filter = 0;
                for (size_t i = 0; i < s.options.extensions.size(); ++i) {
                    std::string item = "*" + s.options.extensions[i];
                    if (ImGui::Selectable(item.c_str(), s.ext_filter == (int)i + 1)) s.ext_filter = (int)i + 1;
                }
                if (ImGui::Selectable("*.*", s.ext_filter == (int)s.options.extensions.size() + 1))
                    s.ext_filter = (int)s.options.extensions.size() + 1;
                ImGui::EndCombo();
            }
        } else {
            ImGui::InputTextWithHint("##filter", "filter", s.filter_buf, sizeof(s.filter_buf));
        }
    }
    const char* ok_label = s.options.pick_directory ? "Select folder" : (s.options.save ? "Save" : "Open");
    if (ImGui::Button(ok_label, ImVec2(ImGui::GetFontSize() * 8, 0)))
        if (Accept(s, out_path)) accepted = true;
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(ImGui::GetFontSize() * 6, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape))
        keep_open = false;
    if (accepted || !keep_open) {
        s.open = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
    return accepted;
}

}  // namespace builder
}  // namespace imforge
