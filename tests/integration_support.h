#pragma once

// shared bits for integration_tests.cpp: example project types, loader, headless harness

#include "doctest.h"

#include "imgui.h"
#include "imforge/imforge_core.h"
#include "imforge/imforge_expr.h"
#include "imforge/imforge_interp.h"
#include "imgui_headless.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

// CheatMenu user types (examples/projects/CheatMenu/types.json)
enum class Mode { Off = 0, Legit = 1, Rage = 2 };
IMFORGE_REFLECT_ENUM(Mode, Off, Legit, Rage);

struct AimSettings {
    bool enabled = false;
    float fov = 90.0f;
    float smooth = 5.0f;
    int bone = 0;
    int key = 0;
};
IMFORGE_REFLECT(AimSettings, enabled, fov, smooth, bone, key);

struct EspSettings {
    bool enabled = false;
    ImVec4 color = ImVec4(0.0f, 1.0f, 0.0f, 1.0f);
    int box_type = 0;
    float max_distance = 500.0f;
};
IMFORGE_REFLECT(EspSettings, enabled, color, box_type, max_distance);

struct MiscSettings {
    bool bhop = false;
    float speed = 1.0f;
    bool fov_changer = false;
    float fov = 90.0f;
    bool watermark = false;
    bool crosshair = false;
    bool show_fov = false;
    int menu_key = 0;
};
IMFORGE_REFLECT(MiscSettings, bhop, speed, fov_changer, fov, watermark, crosshair, show_fov, menu_key);

struct Player {
    char name[32] = "Neo";
    float health = 100.0f;
    float max_hp = 100.0f;
    ImVec2 pos = ImVec2(12.5f, -3.0f);
    int ammo = 30;
    Mode mode = Mode::Legit;
};
IMFORGE_REFLECT(Player, name, health, max_hp, pos, ammo, mode);

struct Settings {
    EspSettings esp;
    AimSettings aim;
    MiscSettings misc;
};
IMFORGE_REFLECT(Settings, esp, aim, misc);

struct Item {
    char name[32] = "item";
    int count = 1;
};
IMFORGE_REFLECT(Item, name, count);

// ToolDock user types (examples/projects/ToolDock/types.json)
struct Transform {
    std::string name = "Cube";
    ImVec2 position = ImVec2(1.0f, 2.0f);
    float rotation = 45.0f;
    ImVec2 scale = ImVec2(1.0f, 1.0f);
    int layer = 0;
    bool visible = true;
};
IMFORGE_REFLECT(Transform, name, position, rotation, scale, layer, visible);

struct Material {
    std::string name = "Default";
    ImVec4 albedo = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
    float emissive[3] = {0.0f, 0.0f, 0.0f};
    float roughness = 0.5f;
    float metallic = 0.0f;
    int shader = 0;
    bool double_sided = false;
    std::string texture;
};
IMFORGE_REFLECT(Material, name, albedo, emissive, roughness, metallic, shader, double_sided, texture);

struct Stats {
    float frame_times[120] = {};
    float frame_time_ms = 16.0f;
    float fps = 60.0f;
    int draw_calls = 10;
    int triangles = 1000;
    float memory_mb = 128.0f;
    int log_count = 3;
    std::string last_log = "ready";
};
IMFORGE_REFLECT(Stats, frame_times, frame_time_ms, fps, draw_calls, triangles, memory_mb, log_count, last_log);

struct LogBuffer {
    int capacity = 1024;
    int count = 3;
    int dropped = 0;
};
IMFORGE_REFLECT(LogBuffer, capacity, count, dropped);

namespace imforge {
namespace itest {

namespace fs = std::filesystem;

// paths, files, processes
inline fs::path RepoRoot() {
#ifdef IMFORGE_SOURCE_DIR
    return fs::path(IMFORGE_SOURCE_DIR);
#else
    return fs::path(__FILE__).parent_path().parent_path();
#endif
}

inline fs::path ScratchRoot() {
#ifdef IMFORGE_BINARY_DIR
    return fs::path(IMFORGE_BINARY_DIR) / "integration_tmp";
#else
    return fs::temp_directory_path() / "imforge_integration";
#endif
}

inline fs::path FreshDir(const std::string& name) {
    fs::path p = ScratchRoot() / name;
    std::error_code ec;
    fs::remove_all(p, ec);
    fs::create_directories(p, ec);
    return p;
}

inline bool ReadFile(const fs::path& p, std::string& out) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

inline bool WriteFile(const fs::path& p, const std::string& text) {
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    std::ofstream f(p, std::ios::binary);
    if (!f) return false;
    f.write(text.data(), static_cast<std::streamsize>(text.size()));
    return static_cast<bool>(f);
}

inline std::string Env(const char* name) {
    const char* v = std::getenv(name);
    return v ? v : "";
}

// runs through the shell; stdout and stderr both land in `output`
inline int Run(const std::string& cmd, std::string& output, const fs::path& cwd = fs::path()) {
    static int counter = 0;
    fs::path log = ScratchRoot() / ("cmd_" + std::to_string(++counter) + ".log");
    std::error_code ec;
    fs::create_directories(log.parent_path(), ec);
    std::string full;
    if (!cwd.empty()) full += "cd \"" + cwd.string() + "\" && ";
    full += cmd + " > \"" + log.string() + "\" 2>&1";
    int rc = std::system(full.c_str());
#ifndef _WIN32
    if (rc != -1 && (rc & 0x7f) == 0) rc = (rc >> 8) & 0xff;
#endif
    ReadFile(log, output);
    return rc;
}

inline std::string Compiler() {
    std::string c = Env("IMFORGE_CXX");
    return c.empty() ? "g++" : c;
}

inline bool HaveCompiler() {
    static int cached = -1;
    if (cached < 0) {
        std::string out;
        cached = Run(Compiler() + " --version", out) == 0 ? 1 : 0;
    }
    return cached == 1;
}

inline std::string Python() {
    std::string p = Env("IMFORGE_PYTHON");
    if (!p.empty()) return p;
#ifdef _WIN32
    return "python";
#else
    return "python3";
#endif
}

inline bool HavePython() {
    static int cached = -1;
    if (cached < 0) {
        std::string out;
        cached = Run(Python() + " --version", out) == 0 ? 1 : 0;
    }
    return cached == 1;
}

// where the pinned ImGui checkouts live (imgui-v1.85, imgui-v1.91.9b, ...)
inline fs::path DepsRoot() {
    std::string d = Env("IMFORGE_IMGUI_DEPS");
    if (!d.empty()) return fs::path(d);
    std::string home = Env("HOME");
    if (home.empty()) home = Env("USERPROFILE");
    return fs::path(home) / "deps";
}

inline fs::path ImGuiCheckout(const char* tag) {
    fs::path p = DepsRoot() / (std::string("imgui-") + tag);
    std::error_code ec;
    return fs::is_regular_file(p / "imgui.h", ec) ? p : fs::path();
}

inline int CountOccurrences(const std::string& text, const char* needle) {
    int n = 0;
    for (size_t pos = text.find(needle); pos != std::string::npos; pos = text.find(needle, pos + 1)) ++n;
    return n;
}

inline std::string FirstLines(const std::string& text, int max_lines) {
    std::string out;
    int lines = 0;
    for (char c : text) {
        out += c;
        if (c == '\n' && ++lines >= max_lines) break;
    }
    return out;
}

// "line:col" of the first differing byte plus a bit of context from both sides
inline std::string DiffReport(const std::string& a, const std::string& b) {
    size_t n = std::min(a.size(), b.size()), i = 0;
    while (i < n && a[i] == b[i]) ++i;
    if (i == n && a.size() == b.size()) return "identical";
    int line = 1, col = 1;
    for (size_t k = 0; k < i; ++k) {
        if (a[k] == '\n') {
            ++line;
            col = 1;
        } else {
            ++col;
        }
    }
    size_t from = i > 60 ? i - 60 : 0;
    std::ostringstream ss;
    ss << "first difference at byte " << i << " (line " << line << ", col " << col << ")\n  expected: ..."
       << a.substr(from, 120) << "...\n  actual:   ..." << b.substr(from, 120) << "...";
    return ss.str();
}

// example project loader
struct LoadedFile {
    std::string rel_path;
    std::string text;
    Json json;
};

struct LoadedProject {
    std::string name;
    fs::path dir;
    Project project;
    LoadedFile project_file;
    Json types;
    std::vector<LoadedFile> panel_files, component_files, theme_files;
    std::vector<std::string> errors;
};

inline bool ParseFile(const fs::path& dir, const std::string& rel, LoadedFile& out, std::vector<std::string>& errors) {
    out.rel_path = rel;
    if (!ReadFile(dir / rel, out.text)) {
        errors.push_back("cannot read " + (dir / rel).string());
        return false;
    }
    JsonError je;
    if (!ParseJson(out.text, out.json, &je)) {
        errors.push_back(rel + ": json error " + je.message + " at " + std::to_string(je.line) + ":" +
                         std::to_string(je.col));
        return false;
    }
    return true;
}

inline bool LoadExampleProject(const std::string& name, LoadedProject& out) {
    out = LoadedProject();
    out.name = name;
    out.dir = RepoRoot() / "examples" / "projects" / name;
    if (!ParseFile(out.dir, "imforge.json", out.project_file, out.errors)) return false;
    Error err;
    out.project.dir = out.dir.string();
    if (!ProjectFromJson(out.project_file.json, out.project, &err)) {
        out.errors.push_back("imforge.json: " + err.message);
        return false;
    }
    for (const std::string& rel : out.project.panel_files) {
        LoadedFile f;
        if (!ParseFile(out.dir, rel, f, out.errors)) continue;
        Panel p;
        if (!PanelFromJson(f.json, p, &err)) {
            out.errors.push_back(rel + ": " + err.message);
            continue;
        }
        p.file = rel;
        out.project.panels.push_back(std::move(p));
        out.panel_files.push_back(std::move(f));
    }
    for (const std::string& rel : out.project.component_files) {
        LoadedFile f;
        if (!ParseFile(out.dir, rel, f, out.errors)) continue;
        Component c;
        if (!ComponentFromJson(f.json, c, &err)) {
            out.errors.push_back(rel + ": " + err.message);
            continue;
        }
        c.file = rel;
        out.project.components.push_back(std::move(c));
        out.component_files.push_back(std::move(f));
    }
    for (const std::string& rel : out.project.theme_files) {
        LoadedFile f;
        if (!ParseFile(out.dir, rel, f, out.errors)) continue;
        Theme t;
        if (!ThemeFromJson(f.json, t, &err)) {
            out.errors.push_back(rel + ": " + err.message);
            continue;
        }
        t.file = rel;
        out.project.themes.push_back(std::move(t));
        out.theme_files.push_back(std::move(f));
    }
    if (!out.project.types_json.empty()) {
        LoadedFile f;
        if (ParseFile(out.dir, out.project.types_json, f, out.errors)) out.types = f.json;
    }
    return out.errors.empty();
}

inline const char* const kExampleNames[] = {"CheatMenu", "ToolDock"};

inline LoadedProject& Example(const char* name) {
    static std::map<std::string, LoadedProject> cache;
    auto it = cache.find(name);
    if (it == cache.end()) {
        LoadedProject lp;
        LoadExampleProject(name, lp);
        it = cache.emplace(name, std::move(lp)).first;
    }
    return it->second;
}

inline const Panel* FindPanel(const Project& p, const std::string& name) {
    for (const Panel& pn : p.panels)
        if (pn.name == name) return &pn;
    return nullptr;
}

inline Panel& MutablePanel(LoadedProject& lp, const std::string& name) {
    for (Panel& pn : lp.project.panels)
        if (pn.name == name) return pn;
    static Panel none;
    FAIL("panel not found: " << name);
    return none;
}

// runtime binding roots for both projects
struct ExampleData {
    Player player;
    Settings cfg;
    Transform obj;
    Material mat;
    Stats stats;
    LogBuffer log;

    ExampleData() {
        for (int i = 0; i < 120; ++i) stats.frame_times[i] = 14.0f + static_cast<float>(i % 7);
    }

    std::vector<BindingRoot> Roots(const Panel& panel) {
        std::vector<BindingRoot> r;
        for (const Param& p : panel.params) {
            if (p.name == "player") r.push_back(BindingRoot{"player", &player, StructInfoOf<Player>()});
            else if (p.name == "cfg") r.push_back(BindingRoot{"cfg", &cfg, StructInfoOf<Settings>()});
            else if (p.name == "obj") r.push_back(BindingRoot{"obj", &obj, StructInfoOf<Transform>()});
            else if (p.name == "mat") r.push_back(BindingRoot{"mat", &mat, StructInfoOf<Material>()});
            else if (p.name == "stats") r.push_back(BindingRoot{"stats", &stats, StructInfoOf<Stats>()});
            else if (p.name == "log") r.push_back(BindingRoot{"log", &log, StructInfoOf<LogBuffer>()});
            else FAIL("no test data for panel param '" << p.name << "' of type " << p.type);
        }
        return r;
    }
};

// headless interpreter harness
struct PanelHarness {
    test::HeadlessImGui gui;
    Interpreter interp;
    PanelInstance inst;
    InterpOptions opts;
    std::vector<BindingRoot> roots;
    const Project* project = nullptr;
    const Panel* panel = nullptr;
    bool design = false;
    std::set<std::string> seen;
    std::map<std::string, std::pair<ImVec2, ImVec2>> rects;
    std::vector<Diagnostic> diags;
    ImVec2 mouse = ImVec2(5.0f, 700.0f);
    bool mouse_down = false;

    PanelHarness(const Project& p, const Panel& pn, bool design_mode, std::vector<BindingRoot> r = {})
        : roots(std::move(r)), project(&p), panel(&pn), design(design_mode) {
        interp.project = &p;
        opts.design_mode = design_mode;
        interp.hooks.on_item = [this](const Node& n, const ImVec2& mn, const ImVec2& mx, bool) {
            seen.insert(n.id);
            rects[n.id] = std::make_pair(mn, mx);
        };
#if IMGUI_VERSION_NUM >= 18700
        ImGui::GetIO().ConfigInputTrickleEventQueue = false;
#endif
    }
    PanelHarness(const PanelHarness&) = delete;
    PanelHarness& operator=(const PanelHarness&) = delete;

    void Frame() {
        seen.clear();
        rects.clear();
        ImGuiIO& io = ImGui::GetIO();
#if IMGUI_VERSION_NUM >= 18700
        io.AddMousePosEvent(mouse.x, mouse.y);
        io.AddMouseButtonEvent(0, mouse_down);
#else
        io.MousePos = mouse;
        io.MouseDown[0] = mouse_down;
#endif
        gui.BeginFrame();
        if (design) {
            ImGui::SetNextWindowPos(ImVec2(0, 0));
            ImGui::SetNextWindowSize(ImVec2(1280, 720));
            ImGui::Begin("Host", nullptr, ImGuiWindowFlags_NoSavedSettings);
            ImGui::BeginChild("canvas", ImVec2(1200, 680));
        }
        interp.DrawPanel(*panel, inst, roots, opts);
        if (design) {
            ImGui::EndChild();
            ImGui::End();
        }
        gui.EndFrame();
        for (const Diagnostic& d : interp.last_diagnostics) {
            bool dup = false;
            for (const Diagnostic& e : diags)
                if (e.message == d.message && e.node_id == d.node_id) dup = true;
            if (!dup) diags.push_back(d);
        }
    }
    void Frames(int n) {
        for (int i = 0; i < n; ++i) Frame();
    }

    int Errors() const {
        int n = 0;
        for (const Diagnostic& d : diags) n += d.level == Diagnostic::Level::Error ? 1 : 0;
        return n;
    }
    std::string Describe() const {
        std::ostringstream ss;
        for (const Diagnostic& d : diags)
            ss << "  [" << (d.level == Diagnostic::Level::Error ? "error" : d.level == Diagnostic::Level::Warning ? "warning" : "info")
               << "] " << d.panel << "." << d.node_id << (d.prop.empty() ? "" : "." + d.prop) << ": " << d.message << "\n";
        return ss.str();
    }
    void PrintWarnings(const std::string& context) const {
        for (const Diagnostic& d : diags)
            if (d.level != Diagnostic::Level::Error)
                MESSAGE(context << " " << d.panel << "." << d.node_id << ": " << d.message);
    }

    bool HasRect(const std::string& id) const { return rects.count(id) != 0; }
    ImVec2 Center(const std::string& id) const {
        auto it = rects.find(id);
        if (it == rects.end()) return ImVec2(-1, -1);
        return ImVec2((it->second.first.x + it->second.second.x) * 0.5f, (it->second.first.y + it->second.second.y) * 0.5f);
    }

    // hover, press, release, two frames each
    bool Click(const std::string& id) {
        if (!HasRect(id)) return false;
        ImVec2 c = Center(id);
        mouse = c;
        mouse_down = false;
        Frames(2);
        mouse_down = true;
        Frames(2);
        mouse_down = false;
        Frames(2);
        return true;
    }
};

// expected visibility, computed from the panel tree + test data (runtime mode, first frame)
struct DataValueEnv : expr::ValueEnv {
    const Panel& panel;
    const std::vector<BindingRoot>& roots;
    DataValueEnv(const Panel& p, const std::vector<BindingRoot>& r) : panel(p), roots(r) {}

    bool ReadPath(const std::string& path, Value& out) override {
        size_t dot = path.find('.');
        std::string root = path.substr(0, dot);
        std::string rest = dot == std::string::npos ? "" : path.substr(dot + 1);
        if (root == "state") {
            for (const StateVar& sv : panel.state) {
                if (sv.name != rest) continue;
                out = Value::FromJson(sv.def, interp_detail::KindForTypeName(sv.type));
                return true;
            }
            return false;
        }
        for (const BindingRoot& r : roots) {
            if (r.name != root) continue;
            void* p = nullptr;
            CType t;
            std::string err;
            if (!ResolvePath(r.ptr, r.type, rest.c_str(), &p, &t, &err)) return false;
            return ReadValue(p, t, out);
        }
        return false;
    }
    bool EnumValue(const std::string& enum_name, const std::string& member, long long& out) override {
        const EnumInfo* e = TypeRegistry::Global().FindEnum(enum_name.c_str());
        return e && e->ValueOf(member.c_str(), out);
    }
};

inline bool EvalVisible(const Node& n, expr::ValueEnv& env) {
    if (n.visible_if.empty()) return true;
    expr::Ast ast;
    Value v;
    Error err;
    if (!expr::Parse(n.visible_if, ast, &err) || !expr::Eval(ast, env, v, &err)) return true;
    return v.ToBool();
}

struct Expectation {
    std::set<std::string> visible_leaves;
    std::set<std::string> hidden;
};

inline void CollectExpected(const Project& project, const Node& n, expr::ValueEnv& env, const Node* instance,
                            Expectation& out, int depth = 0) {
    if (depth > 32) return;
    if (!EvalVisible(n, env)) {
        Walk(n, [&](const Node& c) { out.hidden.insert(c.id); });
        return;
    }
    const std::string& t = n.type;
    if (t == "Component") {
        if (const Component* c = project.FindComponent(n.component)) CollectExpected(project, c->root, env, &n, out, depth + 1);
        return;
    }
    if (t == "Slot") {
        const PropValue* name = n.Prop("name");
        if (!instance || !name) return;
        for (const auto& s : instance->slots)
            if (s.first == name->raw.AsString())
                for (const Node& c : s.second) CollectExpected(project, c, env, nullptr, out, depth + 1);
        return;
    }
    if (t == "Code") return;
    if (IsStockLeafWidget(t.c_str())) {
        out.visible_leaves.insert(n.id);
        return;
    }
    if (t == "Popup" || t == "Modal" || t == "Menu") {
        Walk(n, [&](const Node& c) {
            if (&c != &n) out.hidden.insert(c.id);
        });
        return;
    }
    if (t == "CollapsingHeader" || t == "TreeNode") {
        const PropValue* def_open = n.Prop("defaultOpen");
        if (!def_open || !def_open->raw.AsBool(false)) {
            Walk(n, [&](const Node& c) {
                if (&c != &n) out.hidden.insert(c.id);
            });
            return;
        }
    }
    if (t == "TabBar") {
        bool first = true;
        for (const Node& c : n.children) {
            if (first) CollectExpected(project, c, env, instance, out, depth + 1);
            else
                Walk(c, [&](const Node& d) {
                    if (&d != &c) out.hidden.insert(d.id);
                });
            first = false;
        }
        return;
    }
    for (const Node& c : n.children) CollectExpected(project, c, env, instance, out, depth + 1);
}

inline Expectation ExpectedVisibility(const Project& project, const Panel& panel, const std::vector<BindingRoot>& roots) {
    DataValueEnv env(panel, roots);
    Expectation e;
    CollectExpected(project, panel.root, env, nullptr, e);
    for (const std::string& id : e.visible_leaves) e.hidden.erase(id);
    return e;
}

inline const Node* FirstOfType(const Project& project, const Node& n, const char* type, int depth = 0) {
    if (depth > 32) return nullptr;
    if (n.type == type) return &n;
    if (n.type == "Component") {
        for (const auto& s : n.slots)
            for (const Node& c : s.second)
                if (const Node* r = FirstOfType(project, c, type, depth + 1)) return r;
        return nullptr;
    }
    for (const Node& c : n.children)
        if (const Node* r = FirstOfType(project, c, type, depth + 1)) return r;
    return nullptr;
}

inline std::string JoinSet(const std::set<std::string>& s) {
    std::string r;
    for (const std::string& x : s) r += (r.empty() ? "" : ", ") + x;
    return r;
}

}  // namespace itest
}  // namespace imforge
