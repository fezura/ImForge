#if defined(_MSC_VER) && !defined(_CRT_SECURE_NO_WARNINGS)
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "prefs.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace imforge::builder {

namespace fs = std::filesystem;

std::string ConfigDir() {
    std::string base;
#ifdef _WIN32
    if (const char* appdata = std::getenv("APPDATA")) base = std::string(appdata) + "\\ImForge";
    else if (const char* home = std::getenv("USERPROFILE")) base = std::string(home) + "\\ImForge";
    else base = ".";
#else
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME")) base = std::string(xdg) + "/imforge";
    else if (const char* home = std::getenv("HOME")) base = std::string(home) + "/.config/imforge";
    else base = ".";
#endif
    std::error_code ec;
    fs::create_directories(base, ec);
    return base;
}

std::string PrefsPath() { return (fs::path(ConfigDir()) / "prefs.json").string(); }
std::string ImGuiIniPath() { return (fs::path(ConfigDir()) / "imgui_builder.ini").string(); }

Json PrefsToJson(const Prefs& p) {
    Json o = Json::Object();
    o.Set("schemaVersion", Json(1));
    o.Set("kind", Json("prefs"));
    Json recent = Json::Array();
    for (const std::string& r : p.recent_projects) recent.Push(Json(r));
    o.Set("recentProjects", std::move(recent));
    Json win = Json::Object();
    win.Set("width", Json(p.window_w));
    win.Set("height", Json(p.window_h));
    win.Set("maximized", Json(p.window_maximized));
    o.Set("window", std::move(win));
    o.Set("theme", Json(p.theme));
    o.Set("accent", Json(p.accent));
    o.Set("lastTransport", Json(p.last_transport));
    o.Set("liveBindHost", Json(p.live_bind_host));
    o.Set("fontsDir", Json(p.fonts_dir));
    o.Set("templatesDir", Json(p.templates_dir));
    o.Set("uiScale", Json(p.ui_scale));
    o.Set("showWizardOnStart", Json(p.show_wizard_on_start));
    o.Set("openLastProject", Json(p.open_last_project));
    if (!p.extra.IsNull()) o.Set("extra", p.extra);
    return o;
}

void PrefsFromJson(const Json& j, Prefs& out) {
    if (!j.IsObject()) return;
    const Json& recent = j["recentProjects"];
    if (recent.IsArray()) {
        out.recent_projects.clear();
        for (const Json& r : recent.arr)
            if (r.IsString() && !r.AsString().empty()) out.recent_projects.push_back(r.AsString());
    }
    const Json& win = j["window"];
    if (win["width"].IsNumber()) out.window_w = win["width"].AsInt();
    if (win["height"].IsNumber()) out.window_h = win["height"].AsInt();
    if (win["maximized"].IsBool()) out.window_maximized = win["maximized"].AsBool();
    if (out.window_w < 640) out.window_w = 640;
    if (out.window_h < 480) out.window_h = 480;
    if (j["theme"].IsString()) out.theme = j["theme"].AsString();
    if (j["accent"].IsString()) out.accent = j["accent"].AsString();
    if (j["lastTransport"].IsString()) out.last_transport = j["lastTransport"].AsString();
    if (j["liveBindHost"].IsString()) out.live_bind_host = j["liveBindHost"].AsString();
    if (j["fontsDir"].IsString()) out.fonts_dir = j["fontsDir"].AsString();
    if (j["templatesDir"].IsString()) out.templates_dir = j["templatesDir"].AsString();
    if (j["uiScale"].IsNumber()) out.ui_scale = (float)j["uiScale"].AsDouble();
    if (out.ui_scale < 0.5f || out.ui_scale > 4.0f) out.ui_scale = 1.0f;
    if (j["showWizardOnStart"].IsBool()) out.show_wizard_on_start = j["showWizardOnStart"].AsBool();
    if (j["openLastProject"].IsBool()) out.open_last_project = j["openLastProject"].AsBool();
    if (j.Has("extra")) out.extra = j["extra"];
}

bool LoadPrefs(Prefs& out, std::string* err) {
    std::ifstream in(PrefsPath(), std::ios::binary);
    if (!in) {
        if (err) *err = "prefs file not found";
        return false;
    }
    std::stringstream ss;
    ss << in.rdbuf();
    Json j;
    JsonError je;
    if (!ParseJson(ss.str(), j, &je)) {
        if (err) *err = "prefs.json: " + je.message;
        return false;
    }
    PrefsFromJson(j, out);
    return true;
}

bool SavePrefs(const Prefs& prefs, std::string* err) {
    std::string path = PrefsPath();
    std::string tmp = path + ".tmp";
    {
        std::ofstream outf(tmp, std::ios::binary | std::ios::trunc);
        if (!outf) {
            if (err) *err = "cannot write " + tmp;
            return false;
        }
        outf << DumpJson(PrefsToJson(prefs)) << "\n";
    }
    std::error_code ec;
    fs::rename(tmp, path, ec);
    if (ec) {
        ec.clear();
        fs::copy_file(tmp, path, fs::copy_options::overwrite_existing, ec);
        std::error_code cleanup;
        fs::remove(tmp, cleanup);
    }
    if (ec && err) *err = "cannot replace " + path;
    return !ec;
}

static std::string RecentKey(const std::string& dir) {
    std::string norm = fs::path(dir).lexically_normal().generic_string();
    while (norm.size() > 1 && norm.back() == '/') norm.pop_back();
#ifdef _WIN32
    for (char& c : norm) c = (char)std::tolower((unsigned char)c);
#endif
    return norm;
}

void AddRecentProject(Prefs& prefs, const std::string& dir) {
    if (dir.empty()) return;
    std::string norm = fs::path(dir).lexically_normal().generic_string();
    while (norm.size() > 1 && norm.back() == '/') norm.pop_back();
    RemoveRecentProject(prefs, norm);
    prefs.recent_projects.insert(prefs.recent_projects.begin(), norm);
    if (prefs.recent_projects.size() > Prefs::kMaxRecent) prefs.recent_projects.resize(Prefs::kMaxRecent);
}

void RemoveRecentProject(Prefs& prefs, const std::string& dir) {
    std::string key = RecentKey(dir);
    for (size_t i = 0; i < prefs.recent_projects.size();) {
        if (RecentKey(prefs.recent_projects[i]) == key) prefs.recent_projects.erase(prefs.recent_projects.begin() + (long)i);
        else ++i;
    }
}

}  // namespace imforge::builder
