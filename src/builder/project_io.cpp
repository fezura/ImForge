#include "project_io.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

#include "builder_fonts.h"
#include "editor_context.h"

#if __has_include("codegen.h")
#include "codegen.h"
#define IMFORGE_HAS_CODEGEN 1
#else
#define IMFORGE_HAS_CODEGEN 0
#endif

#if __has_include("roundtrip.h")
#include "roundtrip.h"
#define IMFORGE_HAS_ROUNDTRIP 1
#else
#define IMFORGE_HAS_ROUNDTRIP 0
#endif

#if __has_include("miniz.h")
#include "miniz.h"
#define IMFORGE_HAS_MINIZ 1
#else
#define IMFORGE_HAS_MINIZ 0
#endif

namespace imforge::builder {

namespace fs = std::filesystem;

static bool Fail(Error* err, const std::string& msg) {
    if (err) {
        err->message = msg;
        err->line = err->col = 0;
    }
    return false;
}

bool ReadTextFile(const std::string& path, std::string& out, Error* err) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return Fail(err, "cannot open " + path);
    std::stringstream ss;
    ss << in.rdbuf();
    out = ss.str();
    return true;
}

bool IsSafeRelativePath(const std::string& rel) {
    if (rel.empty() || rel.size() > 260 || rel[0] == '/') return false;
    for (char ch : rel) {
        unsigned char c = (unsigned char)ch;
        if (c < 0x20 || c == 0x7F || c == '\\' || c == ':') return false;
    }
    size_t start = 0;
    while (start <= rel.size()) {
        size_t end = rel.find('/', start);
        if (end == std::string::npos) end = rel.size();
        std::string seg = rel.substr(start, end - start);
        bool last = end == rel.size();
        if (seg == ".." || (seg.empty() && !last)) return false;
        start = end + 1;
    }
    return rel.back() != '/';
}

std::string SanitizeFileStem(const std::string& name) {
    std::string out;
    for (char c : name) {
        bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-' ||
                  c == '.' || c == ' ' || (unsigned char)c >= 0x80;
        out.push_back(ok ? c : '_');
    }
    if (out.empty() || out == "." || out == "..") out.insert(0, "_");
    return out;
}

static std::string CanonicalString(const std::string& p) {
    std::error_code ec;
    fs::path abs = fs::absolute(fs::path(p.empty() ? "." : p), ec);
    fs::path canon = fs::weakly_canonical(abs, ec);
    return (ec ? abs.lexically_normal() : canon).generic_string();
}

bool PathIsInside(const std::string& base, const std::string& path) {
    fs::path b = CanonicalString(base), t = CanonicalString(path);
    fs::path rel = t.lexically_relative(b);
    if (rel.empty() || rel.is_absolute()) return false;
    return *rel.begin() != "..";
}

std::string SafeJoin(const std::string& base, const std::string& rel) {
    if (!IsSafeRelativePath(rel)) return std::string();
    std::string joined = (fs::path(base) / fs::path(rel)).generic_string();
    return PathIsInside(base, joined) ? joined : std::string();
}

static std::set<std::string>& ApprovedOutputDirs() {
    static std::set<std::string> dirs;
    return dirs;
}

static std::map<std::string, std::string>& LoadedOutputDirs() {
    static std::map<std::string, std::string> dirs;
    return dirs;
}

void ApproveExternalOutputDir(const std::string& dir) { ApprovedOutputDirs().insert(CanonicalString(dir)); }

bool OutputDirAllowed(const Project& project, const std::string& out_dir) {
    if (project.dir.empty()) return false;
    if (PathIsInside(project.dir, out_dir)) return true;
    const char* env = std::getenv("IMFORGE_ALLOW_EXTERNAL_OUTPUT");
    if (env && std::string(env) == "1") return true;
    if (ApprovedOutputDirs().count(CanonicalString(out_dir))) return true;
    auto it = LoadedOutputDirs().find(CanonicalString(project.dir));
    return it == LoadedOutputDirs().end() || it->second != project.codegen.output_dir;
}

bool WriteTextFile(const std::string& path, const std::string& content, Error* err, bool* changed) {
    std::string existing;
    if (ReadTextFile(path, existing) && existing == content) {
        if (changed) *changed = false;
        return true;
    }
    std::error_code ec;
    fs::path p(path);
    if (fs::is_symlink(fs::symlink_status(p, ec))) return Fail(err, "refusing to write through a symlink: " + path);
    if (p.has_parent_path()) fs::create_directories(p.parent_path(), ec);
    std::ofstream outf(path, std::ios::binary | std::ios::trunc);
    if (!outf) return Fail(err, "cannot write " + path);
    outf << content;
    if (changed) *changed = true;
    return true;
}

bool ReadJsonFile(const std::string& path, Json& out, Error* err) {
    std::string text;
    if (!ReadTextFile(path, text, err)) return false;
    JsonError je;
    if (!ParseJson(text, out, &je)) {
        if (err) {
            err->message = path + ": " + je.message;
            err->line = je.line;
            err->col = je.col;
        }
        return false;
    }
    return true;
}

bool WriteJsonFile(const std::string& path, const Json& j, Error* err, bool* changed) {
    return WriteTextFile(path, DumpJson(j) + "\n", err, changed);
}

std::string JoinPath(const std::string& a, const std::string& b) {
    if (a.empty()) return b;
    if (b.empty()) return a;
    fs::path pb(b);
    if (pb.is_absolute()) return pb.generic_string();
    return (fs::path(a) / pb).generic_string();
}

std::string NormalizePath(const std::string& p) {
    std::error_code ec;
    fs::path abs = fs::absolute(fs::path(p), ec);
    std::string s = (ec ? fs::path(p) : abs).lexically_normal().generic_string();
    while (s.size() > 1 && s.back() == '/') s.pop_back();
    return s;
}

std::string FileStem(const std::string& path) {
    std::string stem = fs::path(path).filename().string();
    size_t dot = stem.find('.');
    return dot == std::string::npos ? stem : stem.substr(0, dot);
}

bool IsProjectDir(const std::string& dir) {
    std::error_code ec;
    return fs::is_regular_file(fs::path(dir) / "imforge.json", ec);
}

std::string DefaultPanelFile(const std::string& name) { return "panels/" + SanitizeFileStem(name) + ".imb.json"; }
std::string DefaultComponentFile(const std::string& name) { return "components/" + SanitizeFileStem(name) + ".imc.json"; }
std::string DefaultThemeFile(const std::string& name) { return "themes/" + SanitizeFileStem(name) + ".theme.json"; }

bool LoadProject(const std::string& dir_in, Project& out, Error* err) {
    std::string dir = NormalizePath(dir_in);
    Json pj;
    if (!ReadJsonFile(JoinPath(dir, "imforge.json"), pj, err)) return false;
    Project p;
    p.dir = dir;
    p.widget_catalog = out.widget_catalog;
    if (!ProjectFromJson(pj, p, err)) return false;
    for (const std::vector<std::string>* files : {&p.theme_files, &p.component_files, &p.panel_files})
        for (const std::string& f : *files)
            if (!IsSafeRelativePath(f)) return Fail(err, "imforge.json: unsafe file path \"" + SanitizeFileStem(f.substr(0, 80)) + "\"");
    LoadedOutputDirs()[CanonicalString(dir)] = p.codegen.output_dir;
    for (const std::string& f : p.theme_files) {
        Json tj;
        if (!ReadJsonFile(JoinPath(dir, f), tj, err)) return false;
        Theme t;
        t.file = f;
        if (!ThemeFromJson(tj, t, err)) return false;
        p.themes.push_back(std::move(t));
    }
    for (const std::string& f : p.component_files) {
        Json cj;
        if (!ReadJsonFile(JoinPath(dir, f), cj, err)) return false;
        Component c;
        c.file = f;
        if (!ComponentFromJson(cj, c, err)) return false;
        p.components.push_back(std::move(c));
    }
    for (const std::string& f : p.panel_files) {
        Json pnj;
        if (!ReadJsonFile(JoinPath(dir, f), pnj, err)) return false;
        Panel pn;
        pn.file = f;
        if (!PanelFromJson(pnj, pn, err)) return false;
        p.panels.push_back(std::move(pn));
    }
    out = std::move(p);
    return true;
}

bool SaveProject(const Project& project_in, Error* err, std::vector<std::string>* written) {
    if (project_in.dir.empty()) return Fail(err, "project has no directory");
    Project project = project_in;
    for (Panel& p : project.panels)
        if (p.file.empty()) p.file = DefaultPanelFile(p.name);
    for (Component& c : project.components)
        if (c.file.empty()) c.file = DefaultComponentFile(c.name);
    for (Theme& t : project.themes)
        if (t.file.empty()) t.file = DefaultThemeFile(t.name);
    auto write = [&](const std::string& rel, const Json& j) {
        bool changed = false;
        std::string path = SafeJoin(project.dir, rel);
        if (path.empty()) return Fail(err, "refusing to write outside the project folder: " + SanitizeFileStem(rel.substr(0, 80)));
        if (!WriteJsonFile(path, j, err, &changed)) return false;
        if (changed && written) written->push_back(path);
        return true;
    };
    if (!write("imforge.json", ProjectToJson(project))) return false;
    for (const Theme& t : project.themes)
        if (!write(t.file, ThemeToJson(t))) return false;
    for (const Component& c : project.components)
        if (!write(c.file, ComponentToJson(c))) return false;
    for (const Panel& p : project.panels)
        if (!write(p.file, PanelToJson(p))) return false;
    return true;
}

void ApplyProjectSettings(Project& dst, const Project& s) {
    if (!s.imgui_version.empty()) dst.imgui_version = s.imgui_version;
    if (!s.imgui_branch.empty()) dst.imgui_branch = s.imgui_branch;
    if (!s.window.empty()) dst.window = s.window;
    if (!s.renderer.empty()) dst.renderer = s.renderer;
    if (!s.features.empty()) dst.features = s.features;
    dst.codegen = s.codegen;
    dst.live = s.live;
    if (!s.binding_types.empty()) dst.binding_types = s.binding_types;
    if (!s.include_paths.empty()) dst.include_paths = s.include_paths;
    if (!s.compile_commands.empty()) dst.compile_commands = s.compile_commands;
    if (!s.types_json.empty()) dst.types_json = s.types_json;
    if (!s.fonts.empty()) dst.fonts = s.fonts;
    if (!s.active_theme.empty()) dst.active_theme = s.active_theme;
    if (!s.style_classes.empty()) dst.style_classes = s.style_classes;
    if (!s.libraries.empty()) dst.libraries = s.libraries;
}

Project SettingsFromTemplate(const std::string& template_dir, Error* err) {
    Project settings;
    if (template_dir.empty()) return settings;
    Project tpl;
    Error e;
    if (LoadProject(template_dir, tpl, &e)) return tpl;
    if (err) *err = e;
    return settings;
}

Panel MakeDefaultPanel(const std::string& name) {
    Panel p;
    p.name = name;
    p.file = DefaultPanelFile(name);
    p.root.id = "window";
    p.root.type = "Window";
    p.root.SetProp("title", Json(name));
    Json size = Json::Object();
    size.Set("x", Json(520));
    size.Set("y", Json(380));
    p.root.SetProp("initialSize", size);
    p.root.SetProp("sizeCond", Json("FirstUseEver"));
    Node text;
    text.id = "text_1";
    text.type = "Text";
    text.SetProp("text", Json("Hello from " + name));
    p.root.children.push_back(std::move(text));
    return p;
}

Theme MakeDefaultTheme(const std::string& name) {
    Theme t;
    t.name = name;
    t.file = DefaultThemeFile(name);
    return t;
}

std::string DefaultTemplatesDir() {
    std::error_code ec;
    fs::path exe = ExeDir();
    const char* candidates[] = {"templates", "../templates", "../../templates", "../../../templates", "../share/imforge/templates"};
    for (const char* c : candidates) {
        fs::path p = exe / c;
        if (fs::is_directory(p, ec)) return p.lexically_normal().generic_string();
    }
    return (exe / "templates").generic_string();
}

std::vector<std::string> ListTemplates(const std::string& templates_dir) {
    std::vector<std::string> out;
    std::error_code ec;
    if (!fs::is_directory(templates_dir, ec)) return out;
    for (const auto& e : fs::directory_iterator(templates_dir, ec)) {
        if (!e.is_directory(ec)) continue;
        std::string n = e.path().filename().string();
        if (!n.empty() && n[0] != '.') out.push_back(n);
    }
    std::sort(out.begin(), out.end());
    return out;
}

static bool IsTextTemplateFile(const fs::path& p) {
    static const char* exts[] = {".json", ".h", ".hpp", ".cpp", ".c", ".txt", ".md", ".cmake", ".ini", ".cfg", ".yml", ".yaml", ".in"};
    std::string ext = p.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    for (const char* e : exts)
        if (ext == e) return true;
    return p.filename() == "CMakeLists.txt";
}

static std::string ReplaceAll(std::string s, const std::string& from, const std::string& to) {
    if (from.empty()) return s;
    size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::string::npos) {
        s.replace(pos, from.size(), to);
        pos += to.size();
    }
    return s;
}

bool NewProjectFromTemplate(const std::string& template_dir, const std::string& dest_in, const std::string& name,
                            const Project& settings, Error* err) {
    if (name.empty()) return Fail(err, "project name is empty");
    std::string dest = NormalizePath(dest_in);
    std::error_code ec;
    if (fs::exists(dest, ec) && IsProjectDir(dest)) return Fail(err, dest + " already contains an ImForge project");
    fs::create_directories(dest, ec);
    if (ec) return Fail(err, "cannot create " + dest);
    bool have_template = !template_dir.empty() && fs::is_directory(template_dir, ec);
    if (have_template) {
        for (const auto& e : fs::recursive_directory_iterator(template_dir, ec)) {
            if (e.is_symlink(ec)) continue;
            fs::path rel = fs::relative(e.path(), template_dir, ec);
            std::string rel_s = ReplaceAll(rel.generic_string(), "{{PROJECT_NAME}}", SanitizeFileStem(name));
            fs::path target = fs::path(dest) / rel_s;
            if (!IsSafeRelativePath(rel_s) || !PathIsInside(dest, target.generic_string())) return Fail(err, "template path escapes the project folder: " + rel_s);
            if (e.is_directory(ec)) {
                fs::create_directories(target, ec);
                continue;
            }
            if (!e.is_regular_file(ec)) continue;
            if (IsTextTemplateFile(e.path())) {
                std::string text;
                if (!ReadTextFile(e.path().string(), text, err)) return false;
                text = ReplaceAll(text, "{{PROJECT_NAME}}", name);
                if (!WriteTextFile(target.string(), text, err)) return false;
            } else {
                fs::create_directories(target.parent_path(), ec);
                fs::copy_file(e.path(), target, fs::copy_options::overwrite_existing, ec);
                if (ec) return Fail(err, "cannot copy " + e.path().string());
            }
        }
    }
    Project p;
    if (IsProjectDir(dest)) {
        if (!LoadProject(dest, p, err)) return false;
    } else {
        p.dir = dest;
        p.themes.push_back(MakeDefaultTheme("Dark"));
        p.active_theme = "Dark";
        p.panels.push_back(MakeDefaultPanel("Main"));
        p.features = {{"animations", true}, {"drawlistFx", true},  {"shaderFx", false},
                      {"iconFonts", true},  {"dpiScaling", true},  {"docking", false}};
    }
    p.name = name;
    ApplyProjectSettings(p, settings);
    if (p.themes.empty()) {
        p.themes.push_back(MakeDefaultTheme("Dark"));
        if (p.active_theme.empty()) p.active_theme = "Dark";
    }
    if (p.panels.empty()) p.panels.push_back(MakeDefaultPanel("Main"));
    return SaveProject(p, err);
}

bool ExportCode(Project& project, Error* err, ExportReport* report) {
    ExportReport local;
    ExportReport& rep = report ? *report : local;
    rep.codegen_available = IMFORGE_HAS_CODEGEN != 0;
    rep.roundtrip_available = IMFORGE_HAS_ROUNDTRIP != 0;
    if (project.dir.empty()) return Fail(err, "project has no directory");
#if IMFORGE_HAS_CODEGEN
    codegen::Options opts = codegen::OptionsFromProject(project);
    std::string out_dir = JoinPath(project.dir, project.codegen.output_dir);
    if (!OutputDirAllowed(project, out_dir))
        return Fail(err, "output directory " + out_dir + " is outside the project folder; confirm it in the export dialog first");
    auto emit = [&](const std::string& rel, const std::string& content) {
        bool changed = false;
        std::string path = SafeJoin(out_dir, rel);
        if (path.empty()) return Fail(err, "refusing to write outside the output folder: " + SanitizeFileStem(rel.substr(0, 80)));
        if (!WriteTextFile(path, content, err, &changed)) return false;
        (changed ? rep.written : rep.unchanged).push_back(path);
        return true;
    };
    auto append_diags = [&](const std::vector<Diagnostic>& d) { rep.diagnostics.insert(rep.diagnostics.end(), d.begin(), d.end()); };
    for (Panel& panel : project.panels) {
        codegen::Result res = codegen::GeneratePanel(project, panel, opts);
        append_diags(res.diagnostics);
        std::vector<std::pair<std::string, std::string>> outputs;
#if IMFORGE_HAS_ROUNDTRIP
        std::vector<std::pair<std::string, std::string>> existing;
        for (const codegen::FileOut& f : res.files) {
            std::string text;
            if (ReadTextFile(JoinPath(out_dir, f.path), text) && text != f.content) existing.emplace_back(f.path, text);
        }
        if (!existing.empty()) {
            roundtrip::MergeReport mr = roundtrip::MergeFiles(panel, existing, res, outputs);
            append_diags(mr.diagnostics);
            for (const roundtrip::Conflict& c : mr.conflicts)
                rep.conflicts.push_back({panel.name, c.node_id, c.reason, c.user_code, c.generated_code});
        }
#endif
        if (outputs.empty())
            for (const codegen::FileOut& f : res.files) outputs.emplace_back(f.path, f.content);
        for (const auto& o : outputs)
            if (!emit(o.first, o.second)) return false;
    }
    codegen::Result rest[] = {codegen::GenerateComponents(project, opts), codegen::GenerateStyles(project, opts),
                              codegen::GenerateFonts(project, opts), codegen::GenerateReflection(project, opts),
                              codegen::GenerateDevGlue(project, opts)};
    for (codegen::Result& r : rest) {
        append_diags(r.diagnostics);
        for (const codegen::FileOut& f : r.files)
            if (!emit(f.path, f.content)) return false;
    }
    for (const Theme& t : project.themes) {
        codegen::Result r = codegen::GenerateTheme(project, t, opts);
        append_diags(r.diagnostics);
        for (const codegen::FileOut& f : r.files)
            if (!emit(f.path, f.content)) return false;
    }
    return true;
#else
    return Fail(err, "codegen not available in this build (src/codegen not linked)");
#endif
}

bool ExportImblib(const Project& project, const std::string& imblib_path, const std::string& lib_name,
                  const std::vector<std::string>& component_names, Error* err) {
#if IMFORGE_HAS_MINIZ
    mz_zip_archive zip;
    std::memset(&zip, 0, sizeof(zip));
    std::error_code ec;
    fs::remove(imblib_path, ec);
    if (!mz_zip_writer_init_file(&zip, imblib_path.c_str(), 0)) return Fail(err, "cannot create " + imblib_path);
    Json manifest = Json::Object();
    manifest.Set("schemaVersion", Json(1));
    manifest.Set("kind", Json("imblib"));
    manifest.Set("name", Json(lib_name));
    manifest.Set("version", Json("1.0.0"));
    manifest.Set("minImGuiVersion", Json(18500));
    Json comps = Json::Array(), widgets = Json::Array(), headers = Json::Array(), deps = Json::Array();
    bool ok = true;
    for (const Component& c : project.components) {
        if (std::find(component_names.begin(), component_names.end(), c.name) == component_names.end()) continue;
        std::string rel = "components/" + SanitizeFileStem(c.name) + ".imc.json";
        std::string text = DumpJson(ComponentToJson(c)) + "\n";
        ok = ok && mz_zip_writer_add_mem(&zip, rel.c_str(), text.data(), text.size(), (mz_uint)MZ_DEFAULT_LEVEL);
        comps.Push(Json(rel));
    }
    manifest.Set("components", comps);
    manifest.Set("widgets", widgets);
    manifest.Set("headers", headers);
    manifest.Set("dependencies", deps);
    std::string mtext = DumpJson(manifest) + "\n";
    ok = ok && mz_zip_writer_add_mem(&zip, "manifest.json", mtext.data(), mtext.size(), (mz_uint)MZ_DEFAULT_LEVEL);
    ok = ok && mz_zip_writer_finalize_archive(&zip);
    mz_zip_writer_end(&zip);
    return ok ? true : Fail(err, "failed writing " + imblib_path);
#else
    (void)project;
    (void)lib_name;
    (void)component_names;
    return Fail(err, "imblib export needs miniz, which is not available in this build (" + imblib_path + ")");
#endif
}

}  // namespace imforge::builder
