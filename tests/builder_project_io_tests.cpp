#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "../src/builder/project_io.cpp"

namespace imforge::builder {
std::string ExeDir() { return std::filesystem::current_path().generic_string(); }
}  // namespace imforge::builder

using namespace imforge;
using namespace imforge::builder;

namespace {

namespace fs = std::filesystem;

std::string SourceDir() {
#ifdef IMFORGE_SOURCE_DIR
    return IMFORGE_SOURCE_DIR;
#else
    return ".";
#endif
}

struct TempDir {
    std::string path;
    explicit TempDir(const char* tag) {
        static int counter = 0;
        fs::path p = fs::temp_directory_path() / ("imforge_builder_io_" + std::string(tag) + "_" + std::to_string(++counter));
        std::error_code ec;
        fs::remove_all(p, ec);
        fs::create_directories(p, ec);
        path = p.generic_string();
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
    std::string Sub(const char* rel) const { return JoinPath(path, rel); }
};

std::string Slurp(const std::string& path) {
    std::string text;
    REQUIRE_MESSAGE(ReadTextFile(path, text), "cannot read " << path);
    return text;
}

void Spit(const std::string& path, const std::string& text) {
    Error e;
    REQUIRE_MESSAGE(WriteTextFile(path, text, &e), e.message);
}

Json ParseText(const std::string& text) {
    Json j;
    JsonError je;
    REQUIRE_MESSAGE(ParseJson(text, j, &je), je.message);
    return j;
}

std::string Canonical(const std::string& text) { return DumpJson(ParseText(text)) + "\n"; }

bool IsDefaultish(const Json& j) {
    if (j.IsNull()) return true;
    if (j.IsBool()) return !j.AsBool();
    if (j.IsNumber()) return j.AsDouble() == 0;
    if (j.IsString()) return j.AsString().empty();
    if (j.IsArray()) return j.Size() == 0;
    for (const auto& kv : j.obj)
        if (!IsDefaultish(kv.second)) return false;
    return true;
}

bool SameModuloDefaults(const Json& src, const Json& saved, const std::string& path, std::vector<std::string>& diffs) {
    if (src.IsObject() && saved.IsObject()) {
        bool ok = true;
        for (const auto& kv : src.obj) {
            std::string p = path + "/" + kv.first;
            if (!saved.Has(kv.first.c_str())) {
                bool def = IsDefaultish(kv.second);
                diffs.push_back(p + (def ? " = " + DumpJson(kv.second, 0) + " omitted on save (default)"
                                         : " DROPPED non-default value " + DumpJson(kv.second, 0)));
                ok = ok && def;
                continue;
            }
            ok = SameModuloDefaults(kv.second, saved[kv.first], p, diffs) && ok;
        }
        for (const auto& kv : saved.obj) {
            if (src.Has(kv.first.c_str())) continue;
            bool def = IsDefaultish(kv.second);
            diffs.push_back(path + "/" + kv.first + " = " + DumpJson(kv.second, 0) + " added on save" +
                            (def ? " (default)" : " (NON-default)"));
            ok = ok && def;
        }
        return ok;
    }
    if (src.IsArray() && saved.IsArray()) {
        if (src.Size() != saved.Size()) {
            diffs.push_back(path + " array length " + std::to_string(src.Size()) + " -> " + std::to_string(saved.Size()));
            return false;
        }
        bool ok = true;
        for (size_t i = 0; i < src.Size(); ++i)
            ok = SameModuloDefaults(src[i], saved[i], path + "/" + std::to_string(i), diffs) && ok;
        return ok;
    }
    if (src == saved) return true;
    diffs.push_back(path + " changed " + DumpJson(src, 0) + " -> " + DumpJson(saved, 0));
    return false;
}

void CheckSameFile(const std::string& label, const std::string& source, const std::string& saved) {
    if (source == saved) return;
    std::vector<std::string> diffs;
    bool ok = SameModuloDefaults(ParseText(source), ParseText(saved), "", diffs);
    std::string list;
    for (const std::string& d : diffs) list += "    " + d + "\n";
    if (diffs.empty()) list = "    (formatting only)\n";
    CHECK_MESSAGE(ok, label << " lost or changed data on save:\n" << list);
    if (ok) WARN_MESSAGE(false, label << " is not byte-identical after save (source not in canonical form):\n" << list);
}

std::vector<std::string> DocFiles(const Project& p) {
    std::vector<std::string> out{"imforge.json"};
    for (const Theme& t : p.themes) out.push_back(t.file);
    for (const Component& c : p.components) out.push_back(c.file);
    for (const Panel& pn : p.panels) out.push_back(pn.file);
    return out;
}

void CheckFilesIdentical(const std::string& src_dir, const std::string& dst_dir, const std::vector<std::string>& files) {
    for (const std::string& rel : files) {
        std::string a = Slurp(JoinPath(src_dir, rel));
        std::string b = Slurp(JoinPath(dst_dir, rel));
        CHECK_MESSAGE(Canonical(a) == a, rel << " source is not sorted/indented like DumpJson");
        CheckSameFile(rel, a, b);
    }
}

int ErrorCount(const std::vector<Diagnostic>& d) {
    int n = 0;
    for (const Diagnostic& x : d) n += x.level == Diagnostic::Level::Error ? 1 : 0;
    return n;
}

std::string DiagText(const std::vector<Diagnostic>& d) {
    std::string s;
    for (const Diagnostic& x : d)
        if (x.level == Diagnostic::Level::Error) s += x.panel + "/" + x.node_id + ": " + x.message + "\n";
    return s;
}

// the bundled packs' widget manifests, so templates using pack widgets don't read as unknown types
WidgetCatalog& PackCatalog() {
    static WidgetCatalog cat;
    static bool loaded = false;
    if (loaded) return cat;
    loaded = true;
    std::error_code ec;
    for (fs::directory_iterator it(fs::path(SourceDir()) / "packs", ec), end; !ec && it != end; it.increment(ec)) {
        Json m;
        if (!ReadJsonFile((it->path() / "manifest.json").generic_string(), m)) continue;
        for (const Json& w : m["widgets"].arr) {
            Json wj;
            Error we;
            if (w.IsString() && ReadJsonFile((it->path() / w.AsString()).generic_string(), wj)) cat.AddManifest(wj, &we);
        }
    }
    return cat;
}

void ValidateLoadedProject(const Project& loaded) {
    Project p = loaded;
    p.widget_catalog = &PackCatalog();
    for (const Panel& pn : p.panels) {
        std::vector<Diagnostic> d;
        ValidatePanel(p, pn, d);
        CHECK_MESSAGE(ErrorCount(d) == 0, "panel " << pn.name << ":\n" << DiagText(d));
    }
    for (const Component& c : p.components) {
        std::vector<Diagnostic> d;
        ValidateComponent(p, c, d);
        CHECK_MESSAGE(ErrorCount(d) == 0, "component " << c.name << ":\n" << DiagText(d));
    }
}

void RoundTripProject(const char* rel_dir, const char* tag, size_t expected_files) {
    std::string src = JoinPath(SourceDir(), rel_dir);
    REQUIRE_MESSAGE(IsProjectDir(src), src << " is not a project dir");
    Project p;
    Error e;
    REQUIRE_MESSAGE(LoadProject(src, p, &e), e.message);
    CHECK(p.dir == NormalizePath(src));
    std::vector<std::string> files = DocFiles(p);
    CHECK(files.size() == expected_files);

    TempDir tmp(tag);
    Project copy = p;
    copy.dir = tmp.path;
    std::vector<std::string> written;
    REQUIRE_MESSAGE(SaveProject(copy, &e, &written), e.message);
    CHECK(written.size() == files.size());
    CheckFilesIdentical(src, tmp.path, files);

    written.clear();
    REQUIRE(SaveProject(copy, &e, &written));
    CHECK(written.empty());

    Project again;
    REQUIRE_MESSAGE(LoadProject(tmp.path, again, &e), e.message);
    CHECK(DumpJson(ProjectToJson(again)) == DumpJson(ProjectToJson(p)));
    REQUIRE(again.panels.size() == p.panels.size());
    for (size_t i = 0; i < p.panels.size(); ++i)
        CHECK(DumpJson(PanelToJson(again.panels[i])) == DumpJson(PanelToJson(p.panels[i])));
    REQUIRE(again.themes.size() == p.themes.size());
    for (size_t i = 0; i < p.themes.size(); ++i)
        CHECK(DumpJson(ThemeToJson(again.themes[i])) == DumpJson(ThemeToJson(p.themes[i])));
    REQUIRE(again.components.size() == p.components.size());
    for (size_t i = 0; i < p.components.size(); ++i)
        CHECK(DumpJson(ComponentToJson(again.components[i])) == DumpJson(ComponentToJson(p.components[i])));
    ValidateLoadedProject(p);
}

}  // namespace

TEST_CASE("LoadProject reads examples/projects/CheatMenu completely") {
    std::string src = JoinPath(SourceDir(), "examples/projects/CheatMenu");
    Project p;
    Error e;
    REQUIRE_MESSAGE(LoadProject(src, p, &e), e.message);
    CHECK(p.name == "CheatMenu");
    CHECK(p.window == "win32");
    CHECK(p.renderer == "dx11");
    CHECK(p.imgui_branch == "docking");
    CHECK(p.active_theme == "Dark");
    REQUIRE(p.themes.size() == 2);
    CHECK(p.themes[0].name == "Dark");
    CHECK(p.themes[0].file == "themes/Dark.theme.json");
    CHECK(p.themes[1].name == "Neon");
    CHECK(p.themes[1].colors.size() > 50);
    REQUIRE(p.components.size() == 2);
    CHECK(p.components[0].name == "LabeledSlider");
    CHECK(p.components[0].file == "components/LabeledSlider.imc.json");
    CHECK(p.components[1].name == "Card");
    REQUIRE(p.panels.size() == 2);
    CHECK(p.panels[0].name == "MainMenu");
    CHECK(p.panels[0].file == "panels/MainMenu.imb.json");
    CHECK(p.panels[1].name == "Overlay");
    CHECK(p.panels[1].params.size() == 2);
    CHECK(p.panels[1].root.type == "Window");
    CHECK(p.binding_types == std::vector<std::string>{"Player", "Settings"});
    CHECK(p.style_classes.size() == 3);
    CHECK(p.fonts.size() == 4);
    CHECK(p.codegen.nmspace == "menu");
    CHECK(p.live.tcp_port == 7777);
}

TEST_CASE("LoadProject keeps the caller's widget catalog") {
    WidgetCatalog cat;
    Project p;
    p.widget_catalog = &cat;
    Error e;
    REQUIRE(LoadProject(JoinPath(SourceDir(), "examples/projects/ToolDock"), p, &e));
    CHECK(p.widget_catalog == &cat);
}

TEST_CASE("CheatMenu: LoadProject -> SaveProject reproduces every file byte for byte") {
    RoundTripProject("examples/projects/CheatMenu", "cheatmenu", 7);
}

TEST_CASE("ToolDock: LoadProject -> SaveProject reproduces every file byte for byte") {
    RoundTripProject("examples/projects/ToolDock", "tooldock", 4);
    Project p;
    Error e;
    REQUIRE(LoadProject(JoinPath(SourceDir(), "examples/projects/ToolDock"), p, &e));
    CHECK(p.name == "ToolDock");
    REQUIRE(p.panels.size() == 2);
    CHECK(p.panels[0].name == "Inspector");
    CHECK(p.panels[1].name == "LogView");
    REQUIRE(p.themes.size() == 1);
    CHECK(p.themes[0].name == "Graphite");
    CHECK(p.components.empty());
    CHECK(p.Feature("docking"));
}

TEST_CASE("SaveProject fills in default file names and creates directories") {
    TempDir tmp("defaults");
    Project p;
    p.dir = tmp.path;
    p.name = "Fresh";
    p.panels.push_back(MakeDefaultPanel("Main"));
    p.panels.back().file.clear();
    p.themes.push_back(MakeDefaultTheme("Dark"));
    p.themes.back().file.clear();
    Component c;
    c.name = "Card";
    c.root.id = "root";
    c.root.type = "VStack";
    p.components.push_back(c);
    Error e;
    std::vector<std::string> written;
    REQUIRE_MESSAGE(SaveProject(p, &e, &written), e.message);
    CHECK(written.size() == 4);
    CHECK(fs::exists(tmp.Sub("imforge.json")));
    CHECK(fs::exists(tmp.Sub("panels/Main.imb.json")));
    CHECK(fs::exists(tmp.Sub("themes/Dark.theme.json")));
    CHECK(fs::exists(tmp.Sub("components/Card.imc.json")));
    Project back;
    REQUIRE_MESSAGE(LoadProject(tmp.path, back, &e), e.message);
    CHECK(back.name == "Fresh");
    REQUIRE(back.panels.size() == 1);
    CHECK(back.panels[0].file == "panels/Main.imb.json");
    CHECK(back.panels[0].root.children.size() == 1);
    CHECK(back.components[0].file == "components/Card.imc.json");
    CHECK(back.themes[0].file == "themes/Dark.theme.json");
    CHECK(Slurp(tmp.Sub("imforge.json")) == DumpJson(ProjectToJson(back)) + "\n");
}

TEST_CASE("SaveProject without a directory fails") {
    Project p;
    Error e;
    CHECK_FALSE(SaveProject(p, &e));
    CHECK(e.message == "project has no directory");
}

TEST_CASE("LoadProject error paths") {
    TempDir tmp("errors");
    Error e;
    Project p;
    SUBCASE("missing directory") {
        CHECK_FALSE(LoadProject(tmp.Sub("nope"), p, &e));
        CHECK(e.message.find("cannot open") != std::string::npos);
        CHECK(e.message.find("imforge.json") != std::string::npos);
    }
    SUBCASE("broken imforge.json reports path, line and column") {
        Spit(tmp.Sub("imforge.json"), "{\n  \"kind\": \"project\",\n  \"name\": \n}\n");
        CHECK_FALSE(LoadProject(tmp.path, p, &e));
        CHECK(e.message.find("imforge.json") != std::string::npos);
        CHECK(e.message.find(": ") != std::string::npos);
        CHECK(e.line >= 3);
        CHECK(e.col >= 1);
    }
    SUBCASE("imforge.json of the wrong kind") {
        Spit(tmp.Sub("imforge.json"), "{\"schemaVersion\":1,\"kind\":\"panel\",\"name\":\"x\"}\n");
        CHECK_FALSE(LoadProject(tmp.path, p, &e));
        CHECK_FALSE(e.message.empty());
    }
    SUBCASE("missing referenced panel file") {
        Project src;
        src.dir = tmp.path;
        src.name = "P";
        src.panels.push_back(MakeDefaultPanel("Main"));
        REQUIRE(SaveProject(src, &e));
        fs::remove(tmp.Sub("panels/Main.imb.json"));
        CHECK_FALSE(LoadProject(tmp.path, p, &e));
        CHECK(e.message.find("cannot open") != std::string::npos);
        CHECK(e.message.find("Main.imb.json") != std::string::npos);
    }
    SUBCASE("broken theme JSON names the theme file") {
        Project src;
        src.dir = tmp.path;
        src.name = "P";
        src.panels.push_back(MakeDefaultPanel("Main"));
        src.themes.push_back(MakeDefaultTheme("Dark"));
        REQUIRE(SaveProject(src, &e));
        Spit(tmp.Sub("themes/Dark.theme.json"), "{ \"kind\": \"theme\", ");
        CHECK_FALSE(LoadProject(tmp.path, p, &e));
        CHECK(e.message.find("Dark.theme.json") != std::string::npos);
    }
    SUBCASE("component file with wrong kind") {
        Project src;
        src.dir = tmp.path;
        src.name = "P";
        src.panels.push_back(MakeDefaultPanel("Main"));
        Component c;
        c.name = "Card";
        c.root.id = "root";
        c.root.type = "VStack";
        src.components.push_back(c);
        REQUIRE(SaveProject(src, &e));
        Spit(tmp.Sub("components/Card.imc.json"), "{\"schemaVersion\":1,\"kind\":\"theme\",\"name\":\"Card\"}\n");
        CHECK_FALSE(LoadProject(tmp.path, p, &e));
        CHECK_FALSE(e.message.empty());
    }
}

TEST_CASE("ReadJsonFile / WriteJsonFile / WriteTextFile") {
    TempDir tmp("files");
    Error e;
    bool changed = false;
    std::string path = tmp.Sub("deep/er/file.txt");
    REQUIRE(WriteTextFile(path, "hello", &e, &changed));
    CHECK(changed);
    changed = true;
    REQUIRE(WriteTextFile(path, "hello", &e, &changed));
    CHECK_FALSE(changed);
    REQUIRE(WriteTextFile(path, "hello2", &e, &changed));
    CHECK(changed);
    CHECK(Slurp(path) == "hello2");
    std::string text;
    CHECK_FALSE(ReadTextFile(tmp.Sub("missing"), text, &e));
    CHECK(e.message.find("cannot open") != std::string::npos);

    Json j = Json::Object();
    j.Set("b", Json(1));
    j.Set("a", Json("x"));
    REQUIRE(WriteJsonFile(tmp.Sub("j.json"), j, &e, &changed));
    CHECK(changed);
    CHECK(Slurp(tmp.Sub("j.json")) == "{\n  \"a\": \"x\",\n  \"b\": 1\n}\n");
    Json back;
    REQUIRE(ReadJsonFile(tmp.Sub("j.json"), back, &e));
    CHECK(back == j);
    Spit(tmp.Sub("bad.json"), "[1, 2,");
    CHECK_FALSE(ReadJsonFile(tmp.Sub("bad.json"), back, &e));
    CHECK(e.message.find("bad.json: ") != std::string::npos);
    CHECK(e.line >= 1);
}

TEST_CASE("path helpers") {
    CHECK(JoinPath("a", "b") == "a/b");
    CHECK(JoinPath("", "b") == "b");
    CHECK(JoinPath("a", "") == "a");
    CHECK(JoinPath("a/b", "c/d.json") == "a/b/c/d.json");
    std::string abs = fs::temp_directory_path().generic_string();
    CHECK(JoinPath("ignored", abs) == abs);
    CHECK(NormalizePath("/x/y/").find("/x/y") != std::string::npos);
    CHECK(NormalizePath("/x/./y/../z") == NormalizePath("/x/z"));
    CHECK(NormalizePath("/x/y/").back() != '/');
    CHECK(FileStem("panels/MainMenu.imb.json") == "MainMenu");
    CHECK(FileStem("C:/proj/themes/Dark.theme.json") == "Dark");
    CHECK(FileStem("plain") == "plain");
    CHECK(FileStem("") == "");
    CHECK(DefaultPanelFile("A") == "panels/A.imb.json");
    CHECK(DefaultComponentFile("B") == "components/B.imc.json");
    CHECK(DefaultThemeFile("C") == "themes/C.theme.json");
    CHECK(IsProjectDir(JoinPath(SourceDir(), "examples/projects/CheatMenu")));
    CHECK_FALSE(IsProjectDir(JoinPath(SourceDir(), "examples/projects")));
    CHECK_FALSE(IsProjectDir(JoinPath(SourceDir(), "examples/projects/does-not-exist")));
}

TEST_CASE("MakeDefaultPanel and MakeDefaultTheme") {
    Panel p = MakeDefaultPanel("Hud");
    CHECK(p.name == "Hud");
    CHECK(p.file == "panels/Hud.imb.json");
    CHECK(p.root.id == "window");
    CHECK(p.root.type == "Window");
    REQUIRE(p.root.Prop("title"));
    CHECK(p.root.Prop("title")->raw.AsString() == "Hud");
    CHECK(p.root.Prop("initialSize")->raw["x"].AsInt() == 520);
    CHECK(p.root.Prop("sizeCond")->raw.AsString() == "FirstUseEver");
    REQUIRE(p.root.children.size() == 1);
    CHECK(p.root.children[0].type == "Text");
    CHECK(p.root.children[0].Prop("text")->raw.AsString() == "Hello from Hud");
    Project proj;
    std::vector<Diagnostic> d;
    CHECK(ValidatePanel(proj, p, d));
    CHECK(ErrorCount(d) == 0);
    Theme t = MakeDefaultTheme("Dark");
    CHECK(t.name == "Dark");
    CHECK(t.file == "themes/Dark.theme.json");
    CHECK(t.colors.empty());
}

TEST_CASE("ApplyProjectSettings only overrides what the settings provide") {
    Project dst;
    dst.imgui_version = "1.89.9";
    dst.imgui_branch = "master";
    dst.window = "win32";
    dst.renderer = "dx11";
    dst.features = {{"docking", true}};
    dst.codegen.nmspace = "menu";
    dst.live.tcp_port = 1234;
    dst.binding_types = {"Player"};
    dst.fonts.push_back(FontSpec{"Default", "f.ttf", "default", "", 16});
    dst.active_theme = "Neon";
    dst.libraries = {"libs/a.imblib"};
    Project s;
    s.imgui_version.clear();
    s.imgui_branch.clear();
    s.window.clear();
    s.renderer.clear();
    s.codegen.nmspace = "ui2";
    s.live.tcp_port = 9999;
    ApplyProjectSettings(dst, s);
    CHECK(dst.imgui_version == "1.89.9");
    CHECK(dst.imgui_branch == "master");
    CHECK(dst.window == "win32");
    CHECK(dst.renderer == "dx11");
    CHECK(dst.Feature("docking"));
    CHECK(dst.codegen.nmspace == "ui2");
    CHECK(dst.live.tcp_port == 9999);
    CHECK(dst.binding_types == std::vector<std::string>{"Player"});
    CHECK(dst.fonts.size() == 1);
    CHECK(dst.active_theme == "Neon");
    CHECK(dst.libraries.size() == 1);
    s.renderer = "vulkan";
    s.features = {{"shaderFx", true}};
    s.binding_types = {"A", "B"};
    s.active_theme = "Light";
    ApplyProjectSettings(dst, s);
    CHECK(dst.renderer == "vulkan");
    CHECK(dst.features.size() == 1);
    CHECK(dst.Feature("shaderFx"));
    CHECK_FALSE(dst.Feature("docking"));
    CHECK(dst.binding_types.size() == 2);
    CHECK(dst.active_theme == "Light");
}

TEST_CASE("ListTemplates lists the template directories sorted") {
    std::string dir = JoinPath(SourceDir(), "templates");
    std::vector<std::string> names = ListTemplates(dir);
    CHECK(std::is_sorted(names.begin(), names.end()));
    for (const char* want : {"blank", "cheat-menu", "overlay-hud", "settings-dialog", "tool-dock"})
        CHECK(std::find(names.begin(), names.end(), want) != names.end());
    CHECK(ListTemplates(JoinPath(dir, "missing")).empty());
    TempDir tmp("templates");
    fs::create_directories(tmp.Sub(".hidden"));
    fs::create_directories(tmp.Sub("b"));
    fs::create_directories(tmp.Sub("a"));
    Spit(tmp.Sub("file.json"), "{}");
    CHECK(ListTemplates(tmp.path) == std::vector<std::string>{"a", "b"});
}

TEST_CASE("templates.json entries are consistent with the template directories") {
    std::string dir = JoinPath(SourceDir(), "templates");
    Json manifest;
    Error e;
    REQUIRE_MESSAGE(ReadJsonFile(JoinPath(dir, "templates.json"), manifest, &e), e.message);
    CHECK(manifest["kind"].AsString() == "templates");
    const Json& list = manifest["templates"];
    REQUIRE(list.IsArray());
    CHECK(list.Size() == ListTemplates(dir).size());
    std::vector<std::string> ids;
    for (const Json& t : list.arr) {
        std::string id = t["id"].AsString();
        CHECK(std::find(ids.begin(), ids.end(), id) == ids.end());
        ids.push_back(id);
        CHECK_FALSE(t["name"].AsString().empty());
        CHECK_FALSE(t["description"].AsString().empty());
        std::string tdir = JoinPath(dir, t["dir"].AsString());
        CHECK_MESSAGE(IsProjectDir(tdir), tdir);
        Json pj;
        REQUIRE(ReadJsonFile(JoinPath(tdir, "imforge.json"), pj, &e));
        CHECK(pj["name"].AsString() == "{{PROJECT_NAME}}");
        for (const auto& kv : t["features"].obj)
            CHECK_MESSAGE(pj["features"][kv.first].AsBool() == kv.second.AsBool(),
                          id << ": templates.json feature " << kv.first << " disagrees with the template's imforge.json");
    }
}

TEST_CASE("NewProjectFromTemplate for every template in templates.json") {
    std::string dir = JoinPath(SourceDir(), "templates");
    Json manifest;
    Error e;
    REQUIRE(ReadJsonFile(JoinPath(dir, "templates.json"), manifest, &e));
    for (const Json& t : manifest["templates"].arr) {
        std::string id = t["id"].AsString();
        CAPTURE(id);
        std::string tdir = JoinPath(dir, t["dir"].AsString());
        Project settings;
        REQUIRE_MESSAGE(LoadProject(tdir, settings, &e), e.message);
        TempDir tmp(id.c_str());
        std::string dest = tmp.Sub("MyApp");
        REQUIRE_MESSAGE(NewProjectFromTemplate(tdir, dest, "MyApp", settings, &e), e.message);
        REQUIRE(IsProjectDir(dest));
        Project p;
        REQUIRE_MESSAGE(LoadProject(dest, p, &e), e.message);
        CHECK(p.name == "MyApp");
        CHECK(p.renderer == settings.renderer);
        CHECK(p.window == settings.window);
        CHECK(p.imgui_branch == settings.imgui_branch);
        CHECK(p.features == settings.features);
        CHECK(p.codegen.nmspace == settings.codegen.nmspace);
        CHECK(p.panels.size() == settings.panels.size());
        CHECK(p.themes.size() == settings.themes.size());
        CHECK(p.components.size() == settings.components.size());
        CHECK_FALSE(p.panels.empty());
        CHECK_FALSE(p.themes.empty());
        CHECK_FALSE(p.active_theme.empty());
        bool theme_found = false;
        for (const Theme& th : p.themes) theme_found = theme_found || th.name == p.active_theme;
        CHECK(theme_found);
        ValidateLoadedProject(p);

        for (const auto& entry : fs::recursive_directory_iterator(tdir)) {
            if (!entry.is_regular_file()) continue;
            std::string rel = fs::relative(entry.path(), tdir).generic_string();
            std::string expected = ReplaceAll(Slurp(entry.path().generic_string()), "{{PROJECT_NAME}}", "MyApp");
            std::string got = Slurp(JoinPath(dest, rel));
            CHECK_MESSAGE(got.find("{{PROJECT_NAME}}") == std::string::npos, rel << " still contains the placeholder");
            if (rel.size() > 5 && rel.compare(rel.size() - 5, 5, ".json") == 0) CheckSameFile(id + "/" + rel, expected, got);
            else CHECK_MESSAGE(got == expected, id << "/" << rel << " differs from the template after substitution");
        }
    }
}

TEST_CASE("NewProjectFromTemplate with File > New settings keeps the template's platform and codegen") {
    std::string dir = JoinPath(SourceDir(), "templates");
    for (const std::string& id : ListTemplates(dir)) {
        CAPTURE(id);
        std::string tdir = JoinPath(dir, id);
        Project tpl;
        Error e;
        REQUIRE_MESSAGE(LoadProject(tdir, tpl, &e), e.message);
        Project settings = SettingsFromTemplate(tdir, &e);
        CHECK(e.message.empty());
        settings.live.transport = "pipe";
        TempDir tmp(("dlg_" + id).c_str());
        REQUIRE_MESSAGE(NewProjectFromTemplate(tdir, tmp.Sub("App"), "App", settings, &e), e.message);
        Project p;
        REQUIRE_MESSAGE(LoadProject(tmp.Sub("App"), p, &e), e.message);
        CHECK(p.window == tpl.window);
        CHECK(p.renderer == tpl.renderer);
        CHECK(p.imgui_branch == tpl.imgui_branch);
        CHECK(p.imgui_version == tpl.imgui_version);
        CHECK(p.codegen.nmspace == tpl.codegen.nmspace);
        CHECK(p.codegen.base_font_size == tpl.codegen.base_font_size);
        CHECK(p.live.transport == "pipe");
    }
}

TEST_CASE("NewProjectFromTemplate without a template creates a default project") {
    TempDir tmp("notemplate");
    Error e;
    Project settings;
    settings.renderer = "dx12";
    settings.window = "win32";
    REQUIRE_MESSAGE(NewProjectFromTemplate("", tmp.Sub("Fresh"), "Fresh", settings, &e), e.message);
    Project p;
    REQUIRE_MESSAGE(LoadProject(tmp.Sub("Fresh"), p, &e), e.message);
    CHECK(p.name == "Fresh");
    CHECK(p.renderer == "dx12");
    CHECK(p.window == "win32");
    REQUIRE(p.panels.size() == 1);
    CHECK(p.panels[0].name == "Main");
    REQUIRE(p.themes.size() == 1);
    CHECK(p.themes[0].name == "Dark");
    CHECK(p.active_theme == "Dark");
    CHECK(p.features.size() == 6);
    CHECK(p.Feature("animations"));
    CHECK_FALSE(p.Feature("docking"));
    ValidateLoadedProject(p);
    REQUIRE_MESSAGE(NewProjectFromTemplate(tmp.Sub("missing-template-dir"), tmp.Sub("Other"), "Other", settings, &e),
                    e.message);
    CHECK(IsProjectDir(tmp.Sub("Other")));
}

TEST_CASE("NewProjectFromTemplate error paths") {
    TempDir tmp("newerrors");
    Error e;
    Project settings;
    CHECK_FALSE(NewProjectFromTemplate("", tmp.Sub("X"), "", settings, &e));
    CHECK(e.message == "project name is empty");
    REQUIRE(NewProjectFromTemplate("", tmp.Sub("X"), "X", settings, &e));
    CHECK_FALSE(NewProjectFromTemplate("", tmp.Sub("X"), "X", settings, &e));
    CHECK(e.message.find("already contains an ImForge project") != std::string::npos);
    std::string blank = JoinPath(SourceDir(), "templates/blank");
    REQUIRE(NewProjectFromTemplate(blank, tmp.Sub("Y"), "Y", settings, &e));
    Project p;
    REQUIRE(LoadProject(tmp.Sub("Y"), p, &e));
    CHECK(p.name == "Y");
    CHECK(p.renderer == settings.renderer);
    CHECK(p.codegen.nmspace == settings.codegen.nmspace);
    CHECK(p.panels.size() == 1);
    CHECK(p.panels[0].name == "Main");
    CHECK(p.panels[0].root.Prop("title")->raw.AsString() == "Y");
}

TEST_CASE("NewProjectFromTemplate copies non-text files verbatim and renames placeholder paths") {
    TempDir tmp("copy");
    std::string tdir = tmp.Sub("tpl");
    Spit(JoinPath(tdir, "imforge.json"),
         "{\"schemaVersion\":1,\"kind\":\"project\",\"name\":\"{{PROJECT_NAME}}\",\"panels\":[\"panels/{{PROJECT_NAME}}.imb.json\"],\"themes\":[]}\n");
    Spit(JoinPath(tdir, "panels/{{PROJECT_NAME}}.imb.json"),
         "{\"schemaVersion\":1,\"kind\":\"panel\",\"name\":\"{{PROJECT_NAME}}\",\"root\":{\"id\":\"w\",\"type\":\"Window\",\"props\":{\"title\":\"{{PROJECT_NAME}}\"}}}\n");
    std::string bin(4, '\0');
    bin[1] = '\xff';
    Spit(JoinPath(tdir, "assets/blob.bin"), bin + "{{PROJECT_NAME}}");
    Spit(JoinPath(tdir, "README.md"), "# {{PROJECT_NAME}}\n");
    Error e;
    Project settings;
    REQUIRE_MESSAGE(NewProjectFromTemplate(tdir, tmp.Sub("out"), "Zed", settings, &e), e.message);
    CHECK(Slurp(tmp.Sub("out/assets/blob.bin")) == bin + "{{PROJECT_NAME}}");
    CHECK(Slurp(tmp.Sub("out/README.md")) == "# Zed\n");
    CHECK(fs::exists(tmp.Sub("out/panels/Zed.imb.json")));
    CHECK_FALSE(fs::exists(tmp.Sub("out/panels/{{PROJECT_NAME}}.imb.json")));
    Project p;
    REQUIRE_MESSAGE(LoadProject(tmp.Sub("out"), p, &e), e.message);
    CHECK(p.name == "Zed");
    REQUIRE(p.panels.size() == 1);
    CHECK(p.panels[0].name == "Zed");
    CHECK(p.panels[0].file == "panels/Zed.imb.json");
    REQUIRE(p.themes.size() == 1);
    CHECK(p.themes[0].name == "Dark");
    CHECK(fs::exists(tmp.Sub("out/themes/Dark.theme.json")));
}

TEST_CASE("ExportCode without codegen linked reports the limitation") {
    Project p;
    p.dir = ".";
    Error e;
    ExportReport rep;
    bool ok = ExportCode(p, &e, &rep);
    if (!rep.codegen_available) {
        CHECK_FALSE(ok);
        CHECK(e.message.find("codegen not available") != std::string::npos);
    } else {
        CHECK(ok);
    }
    Project none;
    CHECK_FALSE(ExportCode(none, &e));
    CHECK(e.message == "project has no directory");
}

TEST_CASE("examples/projects/Showcase validates with the bundled packs") {
    std::string src = JoinPath(SourceDir(), "examples/projects/Showcase");
    if (!IsProjectDir(src)) return;
    Project p;
    Error e;
    REQUIRE_MESSAGE(LoadProject(src, p, &e), e.message);
    ValidateLoadedProject(p);
}
