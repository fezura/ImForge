#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#ifndef IMFORGE_LIVE
#define IMFORGE_LIVE
#endif
#define IMFORGE_BACKEND_NAME "headless"
#define IMFORGE_LIVE_STATS_INTERVAL_MS 100

#include "integration_support.h"
#include "integration_tools.h"

#if __has_include("codegen.h")
#include "codegen.h"
#define ITEST_HAVE_CODEGEN 1
#endif
#if __has_include("roundtrip.h")
#include "roundtrip.h"
#define ITEST_HAVE_ROUNDTRIP 1
#endif
#if __has_include("type_database.h") && __has_include("panel_type_env.h")
#include "panel_type_env.h"
#include "type_database.h"
#define ITEST_HAVE_BINDING 1
#endif
#if __has_include("imforge/imforge_client.h")
#include "imforge/imforge_client.h"
#include "imforge/imforge_protocol.h"
#include "imforge/imforge_transport.h"
#define ITEST_HAVE_CLIENT 1
#endif


using namespace imforge;
using namespace imforge::itest;

namespace {

std::string Trim(const std::string& s) {
    size_t b = s.find_first_not_of(" \t\r\n"), e = s.find_last_not_of(" \t\r\n");
    return b == std::string::npos ? "" : s.substr(b, e - b + 1);
}

[[maybe_unused]] bool EndsWith(const std::string& s, const std::string& suffix) {
    return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

LoadedProject& Loaded(const std::string& name) {
    LoadedProject& lp = Example(name.c_str());
    for (const std::string& e : lp.errors) MESSAGE(name << ": " << e);
    REQUIRE_MESSAGE(lp.errors.empty(), name << " did not load");
    return lp;
}

std::string PathOf(const PropValue& pv) { return pv.raw.IsString() ? pv.raw.AsString() : ""; }

bool ReadBoundBool(PanelHarness& h, const std::string& path, bool& out) {
    if (path.compare(0, 6, "state.") == 0) {
        Value* v = h.inst.State(path.substr(6));
        if (!v) return false;
        out = v->ToBool();
        return true;
    }
    size_t dot = path.find('.');
    std::string root = path.substr(0, dot), rest = dot == std::string::npos ? "" : path.substr(dot + 1);
    for (const BindingRoot& r : h.roots) {
        if (r.name != root) continue;
        void* p = nullptr;
        CType t;
        std::string err;
        if (!ResolvePath(r.ptr, r.type, rest.c_str(), &p, &t, &err)) return false;
        Value v;
        if (!ReadValue(p, t, v)) return false;
        out = v.ToBool();
        return true;
    }
    return false;
}

}  // namespace

// load
static void CheckLoad(const std::string& name) {
    LoadedProject& lp = Loaded(name);
    CHECK(lp.project.name == name);
    CHECK(lp.project.panels.size() == lp.project.panel_files.size());
    CHECK(lp.project.components.size() == lp.project.component_files.size());
    CHECK(lp.project.themes.size() == lp.project.theme_files.size());
    CHECK(lp.project.panels.size() >= 2);
    CHECK(lp.project.themes.size() >= 1);
    CHECK_MESSAGE(lp.types.IsObject(), "types.json missing for " << name);
    CHECK(lp.types["kind"].AsString() == "types");
    for (const Panel& p : lp.project.panels) {
        CHECK(!p.name.empty());
        CHECK(p.root.type == "Window");
        std::vector<std::string> ids;
        CollectIds(p.root, ids);
        CHECK(ids.size() > 3);
    }
    MESSAGE(name << ": " << lp.project.panels.size() << " panels, " << lp.project.components.size()
                 << " components, " << lp.project.themes.size() << " themes, project imgui "
                 << lp.project.ImGuiVersionNum());
}

TEST_CASE("load: CheatMenu parses with zero errors") { CheckLoad("CheatMenu"); }
TEST_CASE("load: ToolDock parses with zero errors") { CheckLoad("ToolDock"); }

#ifdef ITEST_HAVE_BINDING
static void TypeCheckExpr(binding::PanelTypeEnv& env, const std::string& src, const std::string& where, int& checked) {
    expr::Ast ast;
    Error err;
    bool parsed = expr::Parse(src, ast, &err);
    CHECK_MESSAGE(parsed, where << ": parse error in '" << src << "': " << err.message);
    if (!parsed) return;
    expr::Type t;
    bool ok = expr::TypeCheck(ast, env, t, &err);
    CHECK_MESSAGE(ok, where << ": type error in '" << src << "': " << err.message << " " << env.last_error);
    ++checked;
}

static void TypeCheckInterpolation(binding::PanelTypeEnv& env, const std::string& text, const std::string& where,
                                   int& checked) {
    std::vector<expr::InterpSegment> segs;
    Error err;
    bool ok = expr::ParseInterpolation(text, segs, &err);
    CHECK_MESSAGE(ok, where << ": interpolation error in '" << text << "': " << err.message);
    for (const expr::InterpSegment& s : segs)
        if (s.is_expr && !s.is_icon) TypeCheckExpr(env, s.text_or_expr, where, checked);
}

static void CheckTypes(const std::string& name) {
    LoadedProject& lp = Loaded(name);
    binding::TypeDatabase db;
    Error err;
    REQUIRE_MESSAGE(db.Load(lp.types, &err), name << " types.json: " << err.message);
    for (const std::string& t : lp.project.binding_types)
        CHECK_MESSAGE(db.Struct(t) != nullptr, name << ": binding.types entry '" << t << "' is not in types.json");
    for (const Panel& panel : lp.project.panels) {
        binding::PanelTypeEnv env(db, panel.params, panel.state);
        int checked = 0;
        WalkAll(panel.root, [&](const Node& n) {
            std::string where = panel.name + "." + n.id;
            for (const auto& b : n.bind) {
                std::string path = PathOf(b.second);
                if (path.empty()) continue;
                CType t;
                bool ok = env.TypeOfPath(path, t);
                CHECK_MESSAGE(ok, where << ": binding '" << b.first << "' = '" << path << "' does not resolve: " << env.last_error);
                ++checked;
            }
            if (!n.visible_if.empty()) TypeCheckExpr(env, n.visible_if, where + ".visibleIf", checked);
            if (!n.enabled_if.empty()) TypeCheckExpr(env, n.enabled_if, where + ".enabledIf", checked);
            if (!n.tooltip.empty()) TypeCheckInterpolation(env, n.tooltip, where + ".tooltip", checked);
            for (const auto& p : n.props) {
                if (p.second.IsExpr()) TypeCheckExpr(env, p.second.Expr(), where + "." + p.first, checked);
                else if (p.second.raw.IsString() && p.first != "code" && p.first != "format")
                    TypeCheckInterpolation(env, p.second.raw.AsString(), where + "." + p.first, checked);
            }
            for (const auto& ev : n.events)
                for (const Action& a : ev.second.actions) {
                    std::string w = where + "." + ev.first + "." + a.type;
                    if (a.type == "set" || a.type == "toggle") {
                        CType t;
                        CHECK_MESSAGE(env.TypeOfPath(a.target, t), w << ": target '" << a.target << "' does not resolve: " << env.last_error);
                        ++checked;
                    }
                    if (a.value.IsObject() && a.value.Has("expr")) TypeCheckExpr(env, a.value["expr"].AsString(), w, checked);
                    else if (a.value.IsString() && a.type == "copyClipboard") TypeCheckInterpolation(env, a.value.AsString(), w, checked);
                }
        });
        CHECK_MESSAGE(checked > 0, panel.name << ": nothing to type-check?");
        MESSAGE(panel.name << ": " << checked << " paths/expressions type-checked");
    }
}

TEST_CASE("load: CheatMenu types.json + every binding path and expression type-checks") { CheckTypes("CheatMenu"); }
TEST_CASE("load: ToolDock types.json + every binding path and expression type-checks") { CheckTypes("ToolDock"); }
#else
TEST_CASE("load: types.json type-check SKIPPED (imforge_binding not on the include path)") {
    MESSAGE("SKIP: link integration_tests against imforge_binding to enable");
}
#endif

static void CheckValidate(const std::string& name) {
    LoadedProject& lp = Loaded(name);
    for (const Panel& panel : lp.project.panels) {
        std::vector<Diagnostic> diags;
        ValidatePanel(lp.project, panel, diags);
        int errors = 0;
        for (const Diagnostic& d : diags) {
            if (d.level == Diagnostic::Level::Error) ++errors;
            MESSAGE("validate " << panel.name << "." << d.node_id << (d.prop.empty() ? "" : "." + d.prop) << " ["
                                << (d.level == Diagnostic::Level::Error ? "error" : "warning") << "] " << d.message);
        }
        CHECK_MESSAGE(errors == 0, panel.name << ": " << errors << " validation errors");
    }
    for (const Component& c : lp.project.components) {
        std::vector<Diagnostic> diags;
        ValidateComponent(lp.project, c, diags);
        int errors = 0;
        for (const Diagnostic& d : diags) {
            if (d.level == Diagnostic::Level::Error) ++errors;
            MESSAGE("validate component " << c.name << "." << d.node_id << ": " << d.message);
        }
        CHECK_MESSAGE(errors == 0, c.name << ": " << errors << " validation errors");
    }
}

TEST_CASE("load: ValidatePanel reports no errors for CheatMenu") { CheckValidate("CheatMenu"); }
TEST_CASE("load: ValidatePanel reports no errors for ToolDock") { CheckValidate("ToolDock"); }

// serialization round trip
static void CheckRoundTrip(const std::string& name) {
    LoadedProject& lp = Loaded(name);
    Error err;
    for (const LoadedFile& f : lp.panel_files) {
        std::string norm = DumpJson(f.json);
        Panel p;
        REQUIRE_MESSAGE(PanelFromJson(f.json, p, &err), f.rel_path << ": " << err.message);
        std::string back = DumpJson(PanelToJson(p));
        CHECK_MESSAGE(bool(back == norm), f.rel_path << ": PanelToJson(PanelFromJson(x)) != x\n" << DiffReport(norm, back));
        CHECK_MESSAGE(bool(Trim(f.text) == Trim(norm)), f.rel_path << " is not stored in normalized form\n" << DiffReport(Trim(norm), Trim(f.text)));
        Panel p2;
        Json again;
        REQUIRE(ParseJson(back, again));
        REQUIRE(PanelFromJson(again, p2, &err));
        CHECK(bool(DumpJson(PanelToJson(p2)) == back));
    }
    for (const LoadedFile& f : lp.component_files) {
        std::string norm = DumpJson(f.json);
        Component c;
        REQUIRE_MESSAGE(ComponentFromJson(f.json, c, &err), f.rel_path << ": " << err.message);
        std::string back = DumpJson(ComponentToJson(c));
        CHECK_MESSAGE(bool(back == norm), f.rel_path << ": ComponentToJson(ComponentFromJson(x)) != x\n" << DiffReport(norm, back));
        CHECK_MESSAGE(bool(Trim(f.text) == Trim(norm)), f.rel_path << " is not stored in normalized form");
    }
    for (const LoadedFile& f : lp.theme_files) {
        std::string norm = DumpJson(f.json);
        Theme t;
        REQUIRE_MESSAGE(ThemeFromJson(f.json, t, &err), f.rel_path << ": " << err.message);
        std::string back = DumpJson(ThemeToJson(t));
        CHECK_MESSAGE(bool(back == norm), f.rel_path << ": ThemeToJson(ThemeFromJson(x)) != x\n" << DiffReport(norm, back));
        CHECK_MESSAGE(bool(Trim(f.text) == Trim(norm)), f.rel_path << " is not stored in normalized form");
    }
    {
        std::string norm = DumpJson(lp.project_file.json);
        Project p;
        REQUIRE_MESSAGE(ProjectFromJson(lp.project_file.json, p, &err), "imforge.json: " << err.message);
        std::string back = DumpJson(ProjectToJson(p));
        CHECK_MESSAGE(bool(back == norm), "imforge.json: ProjectToJson(ProjectFromJson(x)) != x\n" << DiffReport(norm, back));
        CHECK_MESSAGE(bool(Trim(lp.project_file.text) == Trim(norm)), "imforge.json is not stored in normalized form");
    }
}

TEST_CASE("serialize: CheatMenu files round-trip byte-identical") { CheckRoundTrip("CheatMenu"); }
TEST_CASE("serialize: ToolDock files round-trip byte-identical") { CheckRoundTrip("ToolDock"); }

// interpreter, headless
static void CheckDesignMode(const std::string& name) {
    LoadedProject& lp = Loaded(name);
    for (const Panel& panel : lp.project.panels) {
        PanelHarness h(lp.project, panel, true);
        h.Frames(5);
        h.PrintWarnings("design " + panel.name);
        CHECK_MESSAGE(h.Errors() == 0, panel.name << " (design mode) diagnostics:\n" << h.Describe());
        CHECK(h.gui.frames == 5);
        CHECK_MESSAGE(!h.seen.empty(), panel.name << ": no item hooks fired in design mode");
        bool has_param_bindings = false;
        WalkAll(panel.root, [&](const Node& n) {
            for (const auto& b : n.bind)
                if (b.second.raw.IsString() && b.second.raw.AsString().compare(0, 6, "state.") != 0) has_param_bindings = true;
        });
        if (has_param_bindings) CHECK_MESSAGE(!h.inst.mock.empty(), panel.name << ": design mode created no mock storage");
    }
}

static void CheckRuntimeMode(const std::string& name) {
    LoadedProject& lp = Loaded(name);
    for (const Panel& panel : lp.project.panels) {
        ExampleData d;
        PanelHarness h(lp.project, panel, false, d.Roots(panel));
        h.Frames(5);
        h.PrintWarnings("runtime " + panel.name);
        CHECK_MESSAGE(h.Errors() == 0, panel.name << " (runtime mode) diagnostics:\n" << h.Describe());
        CHECK(h.gui.frames == 5);
        CHECK_MESSAGE(!h.seen.empty(), panel.name << ": no item hooks fired in runtime mode");
        CHECK_MESSAGE(h.inst.mock.empty(), panel.name << ": runtime mode fell back to mock storage for: "
                                                       << [&] { std::string s; for (auto& m : h.inst.mock) s += m.first + " "; return s; }());
    }
}

TEST_CASE("interp: CheatMenu panels draw 5 frames in design mode without errors") { CheckDesignMode("CheatMenu"); }
TEST_CASE("interp: ToolDock panels draw 5 frames in design mode without errors") { CheckDesignMode("ToolDock"); }
TEST_CASE("interp: CheatMenu panels draw 5 frames in runtime mode with real roots") { CheckRuntimeMode("CheatMenu"); }
TEST_CASE("interp: ToolDock panels draw 5 frames in runtime mode with real roots") { CheckRuntimeMode("ToolDock"); }

static void CheckStateDefaults(const std::string& name) {
    LoadedProject& lp = Loaded(name);
    for (const Panel& panel : lp.project.panels) {
        ExampleData d;
        PanelHarness h(lp.project, panel, false, d.Roots(panel));
        h.Frame();
        for (const StateVar& sv : panel.state) {
            Value* v = h.inst.State(sv.name);
            REQUIRE_MESSAGE(v != nullptr, panel.name << ": state '" << sv.name << "' missing after the first frame");
            Value expect = Value::FromJson(sv.def, interp_detail::KindForTypeName(sv.type));
            CHECK_MESSAGE(v->kind == expect.kind, panel.name << ".state." << sv.name << " kind mismatch");
            CHECK_MESSAGE(v->ToString() == expect.ToString(), panel.name << ".state." << sv.name << " = '" << v->ToString()
                                                                         << "', expected '" << expect.ToString() << "'");
        }
        CHECK(h.inst.state.size() == panel.state.size());
    }
}

TEST_CASE("interp: CheatMenu state defaults are applied") { CheckStateDefaults("CheatMenu"); }
TEST_CASE("interp: ToolDock state defaults are applied") { CheckStateDefaults("ToolDock"); }

static void CheckVisibleLeaves(const std::string& name) {
    LoadedProject& lp = Loaded(name);
    for (const Panel& panel : lp.project.panels) {
        ExampleData d;
        PanelHarness h(lp.project, panel, false, d.Roots(panel));
        h.opts.animations = false;
        h.Frames(3);
        Expectation e = ExpectedVisibility(lp.project, panel, h.roots);
        REQUIRE(!e.visible_leaves.empty());
        std::set<std::string> missing, wrongly_visible;
        for (const std::string& id : e.visible_leaves)
            if (!h.seen.count(id)) missing.insert(id);
        for (const std::string& id : e.hidden)
            if (h.seen.count(id)) wrongly_visible.insert(id);
        CHECK_MESSAGE(missing.empty(), panel.name << ": no item hook for visible leaves: " << JoinSet(missing)
                                                  << "\n  seen: " << JoinSet(h.seen));
        CHECK_MESSAGE(wrongly_visible.empty(), panel.name << ": hooks fired for nodes that must be hidden: " << JoinSet(wrongly_visible));
        MESSAGE(panel.name << ": " << e.visible_leaves.size() << " expected leaves, " << h.seen.size() << " ids reported");
    }
}

TEST_CASE("interp: CheatMenu item hooks fire for every visible leaf and never for hidden ones") { CheckVisibleLeaves("CheatMenu"); }
TEST_CASE("interp: ToolDock item hooks fire for every visible leaf and never for hidden ones") { CheckVisibleLeaves("ToolDock"); }

static void CheckCheckboxClick(const std::string& name, const std::string& panel_name) {
    LoadedProject& lp = Loaded(name);
    const Panel* panel = FindPanel(lp.project, panel_name);
    REQUIRE(panel);
    const Node* cb = FirstOfType(lp.project, panel->root, "Checkbox");
    REQUIRE_MESSAGE(cb, panel_name << " has no Checkbox");
    const PropValue* bind = cb->Bind("value");
    REQUIRE(bind);
    std::string path = PathOf(*bind);
    REQUIRE(!path.empty());
    {
        ExampleData d;
        PanelHarness h(lp.project, *panel, false, d.Roots(*panel));
        h.opts.animations = false;
        h.Frames(3);
        REQUIRE_MESSAGE(h.HasRect(cb->id), panel_name << "." << cb->id << " reported no rect; seen: " << JoinSet(h.seen));
        bool before = false, after = false;
        REQUIRE(ReadBoundBool(h, path, before));
        REQUIRE(h.Click(cb->id));
        REQUIRE(ReadBoundBool(h, path, after));
        CHECK_MESSAGE(after != before, panel_name << "." << cb->id << ": click did not toggle '" << path << "'");
        REQUIRE(h.Click(cb->id));
        REQUIRE(ReadBoundBool(h, path, after));
        CHECK_MESSAGE(after == before, panel_name << "." << cb->id << ": second click did not toggle back");
        CHECK_MESSAGE(h.Errors() == 0, h.Describe());
    }
    {
        PanelHarness h(lp.project, *panel, true);
        h.opts.animations = false;
        h.Frames(3);
        REQUIRE_MESSAGE(h.HasRect(cb->id), panel_name << "." << cb->id << " reported no rect in design mode");
        bool state_path = path.compare(0, 6, "state.") == 0;
        bool before = false;
        if (state_path) REQUIRE(ReadBoundBool(h, path, before));
        REQUIRE(h.Click(cb->id));
        bool found = false, value = false;
        for (const auto& m : h.inst.mock) {
            if (m.first.find(path) == std::string::npos) continue;
            found = true;
            value = m.second.ToBool();
        }
        if (state_path) {
            bool after = !before;
            REQUIRE(ReadBoundBool(h, path, after));
            CHECK_MESSAGE(after != before, panel_name << "." << cb->id << ": design-mode click did not toggle state '" << path << "'");
        } else {
            CHECK_MESSAGE(found, panel_name << "." << cb->id << ": no mock storage for '" << path << "' after a design-mode click");
            CHECK_MESSAGE(value, panel_name << "." << cb->id << ": design-mode click did not toggle the mock bool");
        }
    }
}

TEST_CASE("interp: CheatMenu MainMenu first Checkbox click toggles its bound bool") { CheckCheckboxClick("CheatMenu", "MainMenu"); }
TEST_CASE("interp: ToolDock Inspector first Checkbox click toggles its bound bool") { CheckCheckboxClick("ToolDock", "Inspector"); }
TEST_CASE("interp: ToolDock LogView first Checkbox click toggles its bound state bool") { CheckCheckboxClick("ToolDock", "LogView"); }

TEST_CASE("interp: Overlay visibleIf nodes follow the bound data") {
    LoadedProject& lp = Loaded("CheatMenu");
    const Panel* panel = FindPanel(lp.project, "Overlay");
    REQUIRE(panel);
    ExampleData d;
    PanelHarness h(lp.project, *panel, false, d.Roots(*panel));
    h.opts.animations = false;
    h.Frames(2);
    CHECK_FALSE(h.seen.count("mode_text"));
    CHECK_FALSE(h.seen.count("esp_text"));
    CHECK_FALSE(h.seen.count("watermark"));
    CHECK_FALSE(h.seen.count("dead_text"));
    CHECK_FALSE(h.seen.count("low_ammo"));
    CHECK_FALSE(h.seen.count("crosshair"));
    CHECK(h.seen.count("hp_label"));
    CHECK(h.seen.count("hp_bar"));
    CHECK(h.seen.count("ammo_text"));

    d.cfg.aim.enabled = true;
    d.cfg.esp.enabled = true;
    d.cfg.misc.watermark = true;
    d.cfg.misc.crosshair = true;
    d.player.ammo = 3;
    h.Frames(2);
    CHECK(h.seen.count("mode_text"));
    CHECK(h.seen.count("esp_text"));
    CHECK(h.seen.count("watermark"));
    CHECK(h.seen.count("low_ammo"));
    CHECK(h.seen.count("crosshair"));
    CHECK_FALSE(h.seen.count("dead_text"));

    d.player.health = 0.0f;
    h.Frames(2);
    CHECK(h.seen.count("dead_text"));
    CHECK_FALSE(h.seen.count("hp_label"));
    CHECK_FALSE(h.seen.count("hp_bar"));
    CHECK_MESSAGE(h.Errors() == 0, h.Describe());
}

TEST_CASE("interp: MainMenu status text appears when the state string is non-empty") {
    LoadedProject& lp = Loaded("CheatMenu");
    const Panel* panel = FindPanel(lp.project, "MainMenu");
    REQUIRE(panel);
    ExampleData d;
    PanelHarness h(lp.project, *panel, false, d.Roots(*panel));
    h.opts.animations = false;
    h.Frames(2);
    CHECK_FALSE(h.seen.count("status_text"));
    Value* status = h.inst.State("status");
    REQUIRE(status);
    *status = Value("hello");
    h.Frames(2);
    CHECK(h.seen.count("status_text"));
    Value* adv = h.inst.State("showAdvanced");
    REQUIRE(adv);
    CHECK_FALSE(h.seen.count("aim_fov_preview"));
    *adv = Value(true);
    h.Frames(2);
    CHECK(h.seen.count("aim_fov_preview"));
    CHECK_MESSAGE(h.Errors() == 0, h.Describe());
}

// codegen
#ifdef ITEST_HAVE_CODEGEN
static codegen::Options OptionsFor(LoadedProject& lp, int imgui_version) {
    codegen::Options o = codegen::OptionsFromProject(lp.project);
    if (lp.types.IsObject()) o.types = &lp.types;
    if (imgui_version) o.imgui_version = imgui_version;
    return o;
}

static void ReportDiags(const std::string& ctx, const std::vector<Diagnostic>& diags, int& errors) {
    for (const Diagnostic& d : diags) {
        if (d.level == Diagnostic::Level::Error) ++errors;
        MESSAGE(ctx << " [" << (d.level == Diagnostic::Level::Error ? "error" : "warning") << "] " << d.panel << "." << d.node_id
                    << (d.prop.empty() ? "" : "." + d.prop) << ": " << d.message);
    }
}

static void WriteGenerated(const codegen::Result& r, const fs::path& dir) {
    for (const codegen::FileOut& f : r.files) REQUIRE(WriteFile(dir / f.path, f.content));
}

static void CheckCodegenDeterminism(const std::string& name) {
    LoadedProject& lp = Loaded(name);
    codegen::Options o = OptionsFor(lp, 0);
    codegen::Result a = codegen::GenerateAll(lp.project, o);
    codegen::Result b = codegen::GenerateAll(lp.project, o);
    int errors = 0;
    ReportDiags(name, a.diagnostics, errors);
    CHECK_MESSAGE(errors == 0, name << ": GenerateAll reported " << errors << " errors");
    CHECK_FALSE(a.HasErrors());
    REQUIRE(!a.files.empty());
    REQUIRE(a.files.size() == b.files.size());
    for (size_t i = 0; i < a.files.size(); ++i) {
        CHECK(a.files[i].path == b.files[i].path);
        CHECK_MESSAGE(bool(a.files[i].content == b.files[i].content), a.files[i].path << " differs between two runs\n"
                                                                                 << DiffReport(a.files[i].content, b.files[i].content));
        if (i) CHECK_MESSAGE(a.files[i - 1].path < a.files[i].path, "files not in ascending order: " << a.files[i - 1].path << " / " << a.files[i].path);
        CHECK_MESSAGE(a.files[i].content.find('\r') == std::string::npos, a.files[i].path << " contains CR");
        CHECK_MESSAGE((!a.files[i].content.empty() && a.files[i].content.back() == '\n'), a.files[i].path << " lacks a trailing newline");
        MESSAGE(name << ": " << a.files[i].path << " (" << a.files[i].content.size() << " bytes)");
    }
    for (const Panel& p : lp.project.panels) {
        bool have_cpp = false, have_h = false;
        for (const codegen::FileOut& f : a.files) {
            have_cpp = have_cpp || EndsWith(f.path, p.name + ".cpp");
            have_h = have_h || EndsWith(f.path, p.name + ".h");
        }
        CHECK_MESSAGE((have_cpp && have_h), name << ": no " << p.name << ".h/.cpp in GenerateAll output");
    }
    CHECK(a.literals.size() > 0);
    WriteGenerated(a, FreshDir(std::string(name) + "_gen"));
}

TEST_CASE("codegen: CheatMenu GenerateAll succeeds and is deterministic") { CheckCodegenDeterminism("CheatMenu"); }

TEST_CASE("codegen: plain string component arguments are passed verbatim (not printf-escaped)") {
    LoadedProject& lp = Loaded("CheatMenu");
    codegen::Options o = OptionsFor(lp, 0);
    int checked = 0;
    for (const Panel& panel : lp.project.panels) {
        codegen::Result r = codegen::GeneratePanel(lp.project, panel, o);
        const codegen::FileOut* cpp = nullptr;
        for (const codegen::FileOut& f : r.files)
            if (EndsWith(f.path, panel.name + ".cpp")) cpp = &f;
        REQUIRE(cpp);
        WalkAll(panel.root, [&](const Node& n) {
            if (n.type != "Component") return;
            const Component* c = lp.project.FindComponent(n.component);
            if (!c) return;
            for (const Param& p : c->params) {
                const PropValue* pv = n.Prop(p.name.c_str());
                if (p.type != "string" || !pv || !pv->raw.IsString()) continue;
                const std::string& v = pv->raw.AsString();
                if (v.find_first_of("{}\"\\") != std::string::npos) continue;
                std::string lit = "\"" + v + "\"";
                ++checked;
                CHECK_MESSAGE(cpp->content.find(lit) != std::string::npos,
                              panel.name << "." << n.id << ": component argument " << p.name << " should be " << lit
                                         << " in " << cpp->path);
            }
        });
    }
    CHECK(checked > 0);
}
TEST_CASE("codegen: ToolDock GenerateAll succeeds and is deterministic") { CheckCodegenDeterminism("ToolDock"); }

// Generates for `gen_version`, compiles every output against the ImGui checkout `tag`.
static void CheckGeneratedCompiles(const std::string& name, int gen_version, const std::string& tag) {
    if (!HaveCompiler()) {
        MESSAGE("SKIP: compiler '" << Compiler() << "' not available");
        return;
    }
    fs::path imgui = ImGuiCheckout(tag.c_str());
    if (imgui.empty()) {
        MESSAGE("SKIP: " << DepsRoot().string() << "/imgui-" << tag << " not found");
        return;
    }
    LoadedProject& lp = Loaded(name);
    codegen::Options o = OptionsFor(lp, gen_version);
    codegen::Result r = codegen::GenerateAll(lp.project, o);
    int errors = 0;
    ReportDiags(name, r.diagnostics, errors);
    REQUIRE(!r.files.empty());
    fs::path dir = FreshDir(std::string(name) + "_compile_" + std::to_string(o.imgui_version) + "_" + tag);
    fs::path gen = dir / "gen", stubs = dir / "stubs";
    WriteGenerated(r, gen);
    EmitTypeStubs(lp.types, stubs, ExampleStubOptions(name));
    EmitIconStub(lp, stubs);
    std::vector<fs::path> incs = GeneratedIncludeDirs(gen, stubs, imgui);
    int failures = 0, warnings = 0, compiled = 0;
    for (const codegen::FileOut& f : r.files) {
        bool is_cpp = EndsWith(f.path, ".cpp"), is_h = EndsWith(f.path, ".h");
        if (!is_cpp && !is_h) continue;
        CompileResult c = SyntaxCheck(gen / f.path, incs, {}, is_h);
        ++compiled;
        warnings += c.warnings;
        if (c.rc != 0 || c.errors || c.warnings) {
            ++failures;
            MESSAGE(name << "/" << f.path << " @ " << tag << " (generated for " << o.imgui_version << "): rc=" << c.rc
                         << " errors=" << c.errors << " warnings=" << c.warnings << "\n$ " << c.command << "\n"
                         << FirstLines(c.output, 40));
        }
    }
    CHECK(compiled > 0);
    CHECK_MESSAGE(failures == 0, name << " @ " << tag << ": " << failures << " of " << compiled << " generated files failed the syntax check");
    CHECK_MESSAGE(warnings == 0, name << " @ " << tag << ": " << warnings << " compiler warnings");
    MESSAGE(name << " @ " << tag << ": " << compiled << " files syntax-checked, " << failures << " failed");
}

TEST_CASE("codegen: CheatMenu output compiles against imgui v1.91.9b") { CheckGeneratedCompiles("CheatMenu", 0, "v1.91.9b"); }
TEST_CASE("codegen: ToolDock output compiles against imgui v1.91.9b") { CheckGeneratedCompiles("ToolDock", 0, "v1.91.9b"); }
TEST_CASE("codegen: CheatMenu output compiles against imgui v1.91.9b-docking") { CheckGeneratedCompiles("CheatMenu", 0, "v1.91.9b-docking"); }
TEST_CASE("codegen: ToolDock output compiles against imgui v1.91.9b-docking") { CheckGeneratedCompiles("ToolDock", 0, "v1.91.9b-docking"); }
TEST_CASE("codegen: CheatMenu generated for 1.85 compiles against imgui v1.85") { CheckGeneratedCompiles("CheatMenu", 18500, "v1.85"); }
TEST_CASE("codegen: ToolDock generated for 1.85 compiles against imgui v1.85") { CheckGeneratedCompiles("ToolDock", 18500, "v1.85"); }
TEST_CASE("codegen: CheatMenu generated for 1.91.9 still compiles against imgui v1.85 (version guards)") { CheckGeneratedCompiles("CheatMenu", 0, "v1.85"); }
TEST_CASE("codegen: ToolDock generated for 1.91.9 still compiles against imgui v1.85 (version guards)") { CheckGeneratedCompiles("ToolDock", 0, "v1.85"); }
#else
TEST_CASE("codegen: SKIPPED (imforge_codegen not on the include path)") {
    MESSAGE("SKIP: link integration_tests against imforge_codegen (tests/CMakeLists.txt) to enable");
}
#endif

// round trip
#if defined(ITEST_HAVE_CODEGEN) && defined(ITEST_HAVE_ROUNDTRIP)
TEST_CASE("roundtrip: scripted edits to MainMenu.cpp merge back into the panel, slots and locked bodies survive") {
    LoadedProject& lp = Loaded("CheatMenu");
    Panel panel = *FindPanel(lp.project, "MainMenu");
    codegen::Options o = OptionsFor(lp, 0);
    codegen::Result fresh = codegen::GeneratePanel(lp.project, panel, o);
    int errors = 0;
    ReportDiags("MainMenu", fresh.diagnostics, errors);
    REQUIRE(errors == 0);
    const codegen::FileOut* cpp = nullptr;
    for (const codegen::FileOut& f : fresh.files)
        if (EndsWith(f.path, "MainMenu.cpp")) cpp = &f;
    REQUIRE_MESSAGE(cpp, "GeneratePanel produced no MainMenu.cpp");
    fs::path dir = FreshDir("roundtrip");
    WriteFile(dir / "MainMenu.fresh.cpp", cpp->content);

    std::string edited = cpp->content;
    CHECK_MESSAGE(ReplaceInBlock(edited, "begin", "btn_about", {{"\"About\"", "\"About...\""}}), "btn_about block or its label literal not found");
    CHECK_MESSAGE(ReplaceInBlock(edited, "begin", "fov_value", {{"140.0f", "150.0f"}, {"140.f", "150.f"}, {"140", "150"}}),
                  "fov_value block or its max literal not found");
    CHECK_MESSAGE(ReplaceInBlock(edited, "begin", "window",
                                 {{"ImGuiWindowFlags_NoScrollWithMouse", "ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoResize"}}),
                  "window block or its flags not found");
    const std::string user_line = "    int imb_user_marker = 42; (void)imb_user_marker;";
    CHECK_MESSAGE(InsertLineAtBlockEnd(edited, "user", "before_draw", user_line), "user slot before_draw not found");
    const std::string locked_line = "ImGui::Separator();  // hand edit";
    CHECK_MESSAGE(ReplaceInBlock(edited, "locked", "ping_block",
                                 {{"GetServerName());\n", "GetServerName());\n    " + locked_line + "\n"}}),
                  "locked block ping_block or its last statement not found");
    WriteFile(dir / "MainMenu.edited.cpp", edited);

    std::string merged;
    roundtrip::MergeReport rep = roundtrip::Merge(panel, edited, fresh, merged);
    WriteFile(dir / "MainMenu.merged.cpp", merged);
    for (const Diagnostic& d : rep.diagnostics) MESSAGE("merge [" << (int)d.level << "] " << d.node_id << ": " << d.message);
    for (const roundtrip::Conflict& c : rep.conflicts) MESSAGE("conflict " << c.node_id << ": " << c.reason << "\n--- user\n" << c.user_code << "--- generated\n" << c.generated_code);
    CHECK_MESSAGE(rep.conflicts.empty(), rep.conflicts.size() << " conflicts");
    std::set<std::string> updated(rep.updated_nodes.begin(), rep.updated_nodes.end());
    CHECK_MESSAGE(updated.count("btn_about"), "btn_about not in updated_nodes: " << JoinSet(updated));
    CHECK_MESSAGE(updated.count("fov_value"), "fov_value not in updated_nodes: " << JoinSet(updated));
    CHECK_MESSAGE(updated.count("window"), "window not in updated_nodes: " << JoinSet(updated));

    Node* btn = FindNodeAll(panel.root, "btn_about");
    Node* fov = FindNodeAll(panel.root, "fov_value");
    Node* win = FindNodeAll(panel.root, "window");
    REQUIRE((btn && fov && win));
    REQUIRE((btn->Prop("label") && fov->Prop("max")));
    CHECK(btn->Prop("label")->raw.AsString() == "About...");
    CHECK(fov->Prop("max")->raw.AsDouble() == doctest::Approx(150.0));
    CHECK(HasFlag(*win, "flags", "NoResize"));
    CHECK(HasFlag(*win, "flags", "NoCollapse"));
    CHECK_MESSAGE(ContainsInBlock(merged, "user", "before_draw", user_line), "user slot body lost in merge");
    CHECK_MESSAGE(ContainsInBlock(merged, "locked", "ping_block", locked_line), "locked block edit lost in merge");
    CHECK_MESSAGE(ContainsInBlock(merged, "locked", "ping_block", "GetPing()"), "locked block original body lost in merge");
    CHECK(merged.find("\"About...\"") != std::string::npos);

    codegen::Result fresh2 = codegen::GeneratePanel(lp.project, panel, o);
    const codegen::FileOut* cpp2 = nullptr;
    for (const codegen::FileOut& f : fresh2.files)
        if (EndsWith(f.path, "MainMenu.cpp")) cpp2 = &f;
    REQUIRE(cpp2);
    CHECK(cpp2->content.find("\"About...\"") != std::string::npos);
    CHECK(cpp2->content.find("ImGuiWindowFlags_NoResize") != std::string::npos);
    Panel panel2 = panel;
    std::string merged2;
    roundtrip::MergeReport rep2 = roundtrip::Merge(panel2, merged, fresh2, merged2);
    WriteFile(dir / "MainMenu.merged2.cpp", merged2);
    for (const roundtrip::Conflict& c : rep2.conflicts) MESSAGE("second merge conflict " << c.node_id << ": " << c.reason);
    CHECK(rep2.conflicts.empty());
    CHECK_MESSAGE(rep2.updated_nodes.empty(), "second merge still updates: " << JoinSet(std::set<std::string>(rep2.updated_nodes.begin(), rep2.updated_nodes.end())));
    CHECK_MESSAGE(bool(merged2 == merged), "merge is not idempotent\n" << DiffReport(merged, merged2));
    CHECK(bool(DumpJson(PanelToJson(panel2)) == DumpJson(PanelToJson(panel))));

    roundtrip::ParsedFile parsed;
    Error perr;
    REQUIRE_MESSAGE(roundtrip::ParseGenerated(merged, parsed, &perr), perr.message);
    for (const roundtrip::Block& b : parsed.blocks)
        if (b.kind == roundtrip::Block::Kind::Begin || b.kind == roundtrip::Block::Kind::Locked)
            CHECK_MESSAGE(b.hash == roundtrip::BlockHash(b), "stale hash on block " << b.id << " after merge");
    std::string carried = roundtrip::CarryUserRegions(cpp2->content, parsed, nullptr);
    WriteFile(dir / "MainMenu.regenerated_carried.cpp", carried);
    CHECK_MESSAGE(bool(carried == merged), "merged text != regenerated text with user/locked regions carried\n"
                                               << DiffReport(carried, merged));
}

TEST_CASE("roundtrip: an unchanged file merges to itself for every panel of both projects") {
    for (const char* name : kExampleNames) {
        LoadedProject& lp = Loaded(name);
        for (const Panel& p : lp.project.panels) {
            Panel panel = p;
            codegen::Options o = OptionsFor(lp, 0);
            codegen::Result fresh = codegen::GeneratePanel(lp.project, panel, o);
            for (size_t i = 0; i < fresh.files.size(); ++i) {
                std::string merged;
                roundtrip::MergeReport rep = roundtrip::Merge(panel, fresh.files[i].content, fresh, i, merged);
                for (const roundtrip::Conflict& c : rep.conflicts) MESSAGE(p.name << "/" << fresh.files[i].path << " conflict " << c.node_id << ": " << c.reason);
                CHECK_MESSAGE(rep.conflicts.empty(), p.name << "/" << fresh.files[i].path);
                CHECK_MESSAGE(rep.updated_nodes.empty(), p.name << "/" << fresh.files[i].path);
                CHECK_MESSAGE(bool(merged == fresh.files[i].content), p.name << "/" << fresh.files[i].path << ": self-merge changed the file\n"
                                                                       << DiffReport(fresh.files[i].content, merged));
            }
            CHECK(bool(DumpJson(PanelToJson(panel)) == DumpJson(PanelToJson(p))));
        }
    }
}
#else
TEST_CASE("roundtrip: SKIPPED (imforge_roundtrip/imforge_codegen not on the include path)") {
    MESSAGE("SKIP: link integration_tests against imforge_roundtrip and imforge_codegen to enable");
}
#endif

#include "integration_cases_tools.h"
