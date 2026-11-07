#pragma once

#include <string>
#include <vector>

#include "imforge/imforge_json.h"

namespace imforge::builder {

struct Prefs {
    std::vector<std::string> recent_projects;
    int window_w = 1600, window_h = 900;
    bool window_maximized = false;
    std::string theme = "dark";
    std::string accent = "#4f8cffff";
    std::string last_transport = "tcp";
    std::string live_bind_host = "127.0.0.1";
    std::string fonts_dir;
    std::string templates_dir;
    float ui_scale = 1.0f;
    bool show_wizard_on_start = true;
    bool open_last_project = true;
    Json extra;

    static constexpr size_t kMaxRecent = 10;
};
using Settings = Prefs;

std::string ConfigDir();
std::string PrefsPath();
std::string ImGuiIniPath();
bool LoadPrefs(Prefs& out, std::string* err = nullptr);
bool SavePrefs(const Prefs& prefs, std::string* err = nullptr);
void AddRecentProject(Prefs& prefs, const std::string& dir);
void RemoveRecentProject(Prefs& prefs, const std::string& dir);
Json PrefsToJson(const Prefs& prefs);
void PrefsFromJson(const Json& j, Prefs& out);

}  // namespace imforge::builder
