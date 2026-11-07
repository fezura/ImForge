#if defined(_MSC_VER) && !defined(_CRT_SECURE_NO_WARNINGS)
#define _CRT_SECURE_NO_WARNINGS
#endif
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

#include "imforge/imforge_transport.h"

#include "../src/builder/prefs.cpp"
#include "../src/builder/project_io.cpp"
#include "../src/builder/live_server.cpp"
#include "../src/builder/imblib.cpp"
#include "../src/builder/editor_context.cpp"
#include "../src/builder/editor_commands.cpp"
#include "../src/builder/ui/tree_ops.cpp"
#include "../src/builder/panels/components_ops.cpp"
#include "../src/builder/panels/components_actions.cpp"

namespace imforge::builder {
std::string ExeDir() { return std::filesystem::current_path().generic_string(); }
}  // namespace imforge::builder

using namespace imforge;
using namespace imforge::builder;
using namespace imforge::builder::components;

namespace {

namespace fs = std::filesystem;

Node MakeNode(const char* id, const char* type, const char* prop = nullptr, const char* value = nullptr) {
    Node n;
    n.id = id;
    n.type = type;
    if (prop) n.SetProp(prop, Json(value));
    return n;
}

Node Instance(const char* id, const char* component) {
    Node n = MakeNode(id, "Component");
    n.component = component;
    return n;
}

Panel MakePanel(const std::string& name, bool with_card) {
    Panel p;
    p.name = name;
    p.file = "panels/" + name + ".imp.json";
    p.root = MakeNode("root", "Window", "title", name.c_str());
    p.root.children.push_back(MakeNode("btn", "Button", "label", "Hello"));
    p.root.children.push_back(MakeNode("txt", "Text", "text", "World"));
    if (with_card) p.root.children.push_back(Instance("card1", "Card"));
    return p;
}

std::string State(const EditorContext& ctx) {
    Json o = Json::Object();
    Json comps = Json::Array();
    for (const Component& c : ctx.project.components) {
        Json cj = ComponentToJson(c);
        cj.Set("file", Json(c.file));
        comps.Push(std::move(cj));
    }
    Json panels = Json::Array();
    for (const Panel& p : ctx.project.panels) panels.Push(PanelToJson(p));
    Json libs = Json::Array();
    for (const std::string& l : ctx.project.libraries) libs.Push(Json(l));
    o.Set("components", std::move(comps));
    o.Set("panels", std::move(panels));
    o.Set("libraries", std::move(libs));
    return DumpJson(o);
}

struct Fixture {
    EditorContext ctx;
    std::vector<DocKind> doc_kinds;
    int project_notifications = 0;

    Fixture() {
        ctx.project.name = "Demo";
        ctx.project.widget_catalog = &ctx.catalog;
        Component card = MakeEmptyComponent("Card", true);
        card.root.children.push_back(MakeNode("caption", "Text", "text", "Card"));
        ctx.project.components.push_back(card);
        Component frame = MakeEmptyComponent("Frame", false);
        frame.root.children.push_back(Instance("inner", "Card"));
        ctx.project.components.push_back(frame);
        ctx.project.panels.push_back(MakePanel("Main", true));
        ctx.project.panels.push_back(MakePanel("Other", true));
        ctx.project.panels.push_back(MakePanel("Third", false));
        ctx.has_project = true;
        ctx.interp.project = &ctx.project;
        ctx.SetActivePanel("Main");
        ctx.on_document_changed.push_back([this](DocKind k, const std::string&) { doc_kinds.push_back(k); });
        ctx.on_project_changed.push_back([this](const char*) { ++project_notifications; });
    }

    void Tick() { ctx.time += 1.0; }
    const Panel& PanelNamed(const char* name) { return *ctx.FindPanelDoc(name); }
};

bool PanelHasNode(const Panel& p, const char* id) { return FindNode(p.root, id) != nullptr; }

}  // namespace

TEST_CASE("new component is a single undo step") {
    Fixture f;
    std::string before = State(f.ctx);
    std::string err;
    f.Tick();
    REQUIRE_MESSAGE(NewComponent(f.ctx, "Badge", true, &err), err);
    std::string after = State(f.ctx);
    REQUIRE(f.ctx.FindComponentDoc("Badge"));
    CHECK(f.ctx.undo.Size() == 1);
    CHECK(std::string(f.ctx.UndoLabel()) == "New component");
    CHECK(f.ctx.dirty);
    f.ctx.Undo();
    CHECK(f.ctx.FindComponentDoc("Badge") == nullptr);
    CHECK(State(f.ctx) == before);
    f.ctx.Redo();
    REQUIRE(f.ctx.FindComponentDoc("Badge"));
    CHECK(f.ctx.FindComponentDoc("Badge")->slots == std::vector<std::string>{"content"});
    CHECK(State(f.ctx) == after);
    CHECK_FALSE(NewComponent(f.ctx, "Badge", false, &err));
    CHECK_FALSE(NewComponent(f.ctx, "9bad", false, &err));
    CHECK(f.ctx.undo.Size() == 1);
}

TEST_CASE("duplicate component undo and redo") {
    Fixture f;
    std::string before = State(f.ctx);
    std::string err;
    f.Tick();
    REQUIRE_MESSAGE(DuplicateComponent(f.ctx, "Card", "Card2", &err), err);
    REQUIRE(f.ctx.FindComponentDoc("Card2"));
    CHECK(f.ctx.FindComponentDoc("Card2")->file == "components/Card2.imc.json");
    std::string after = State(f.ctx);
    f.ctx.Undo();
    CHECK(State(f.ctx) == before);
    f.ctx.Redo();
    CHECK(State(f.ctx) == after);
}

TEST_CASE("rename updates instances across panels and undoes atomically") {
    Fixture f;
    std::string before = State(f.ctx);
    std::string err;
    f.Tick();
    REQUIRE_MESSAGE(RenameComponent(f.ctx, "Card", "Tile", &err), err);
    std::string after = State(f.ctx);
    CHECK(f.ctx.FindComponentDoc("Card") == nullptr);
    REQUIRE(f.ctx.FindComponentDoc("Tile"));
    CHECK(f.ctx.FindComponentDoc("Tile")->file == "components/Tile.imc.json");
    CHECK(FindNode(f.PanelNamed("Main").root, "card1")->component == "Tile");
    CHECK(FindNode(f.PanelNamed("Other").root, "card1")->component == "Tile");
    CHECK(FindNode(f.ctx.FindComponentDoc("Frame")->root, "inner")->component == "Tile");
    CHECK(f.ctx.undo.Size() == 1);
    const UndoEntry* e = f.ctx.undo.PeekUndo();
    REQUIRE(e);
    CHECK(e->kind == DocKind::Components);
    CHECK(e->before["panels"].Has("Main"));
    CHECK(e->before["panels"].Has("Other"));
    CHECK_FALSE(e->before["panels"].Has("Third"));
    f.ctx.Undo();
    CHECK(State(f.ctx) == before);
    f.ctx.Redo();
    CHECK(State(f.ctx) == after);
}

TEST_CASE("rename while editing the component keeps the editor on it through undo and redo") {
    Fixture f;
    std::string err;
    f.ctx.SetActiveComponent("Card");
    f.Tick();
    REQUIRE_MESSAGE(RenameComponent(f.ctx, "Card", "Tile", &err), err);
    CHECK(f.ctx.active_kind == DocKind::Component);
    CHECK(f.ctx.active_panel == "Tile");
    f.ctx.Undo();
    CHECK(f.ctx.active_kind == DocKind::Component);
    CHECK(f.ctx.active_panel == "Card");
    REQUIRE(f.ctx.ActiveComponent());
    f.ctx.Redo();
    CHECK(f.ctx.active_kind == DocKind::Component);
    CHECK(f.ctx.active_panel == "Tile");
}

TEST_CASE("delete component undo restores the definition and its file") {
    Fixture f;
    std::string before = State(f.ctx);
    f.Tick();
    REQUIRE(DeleteComponent(f.ctx, "Card"));
    CHECK(f.ctx.FindComponentDoc("Card") == nullptr);
    std::string after = State(f.ctx);
    f.ctx.Undo();
    REQUIRE(f.ctx.FindComponentDoc("Card"));
    CHECK(f.ctx.FindComponentDoc("Card")->file == "components/Card.imc.json");
    CHECK(State(f.ctx) == before);
    f.ctx.Redo();
    CHECK(State(f.ctx) == after);
    CHECK_FALSE(DeleteComponent(f.ctx, "Missing"));
}

TEST_CASE("deleting the component being edited falls back to a panel") {
    Fixture f;
    f.ctx.SetActiveComponent("Card");
    f.Tick();
    REQUIRE(DeleteComponent(f.ctx, "Card"));
    CHECK(f.ctx.active_kind == DocKind::Panel);
    CHECK(f.ctx.active_panel == "Main");
    f.ctx.Undo();
    CHECK(f.ctx.FindComponentDoc("Card"));
}

TEST_CASE("create from selection restores the panel and the component list together") {
    Fixture f;
    std::string before = State(f.ctx);
    std::string err;
    ExtractPlan plan;
    Node* root = f.ctx.ActiveRoot();
    REQUIRE(root);
    REQUIRE_MESSAGE(PlanExtract(*root, {"btn", "txt"}, &f.ctx.catalog, &f.ctx.project, plan, &err), err);
    plan.name = "Pair";
    std::string inst;
    f.Tick();
    REQUIRE_MESSAGE(ExtractComponent(f.ctx, plan, &inst, &err), err);
    std::string after = State(f.ctx);
    REQUIRE(f.ctx.FindComponentDoc("Pair"));
    CHECK_FALSE(PanelHasNode(f.PanelNamed("Main"), "btn"));
    CHECK(FindNode(f.PanelNamed("Main").root, inst)->component == "Pair");
    CHECK(f.ctx.SelectedId() == inst);
    CHECK(f.ctx.undo.Size() == 1);
    f.ctx.Undo();
    CHECK(f.ctx.FindComponentDoc("Pair") == nullptr);
    CHECK(PanelHasNode(f.PanelNamed("Main"), "btn"));
    CHECK(PanelHasNode(f.PanelNamed("Main"), "txt"));
    CHECK(State(f.ctx) == before);
    CHECK(f.ctx.selection.empty());
    f.ctx.Redo();
    CHECK(State(f.ctx) == after);
    plan.name = "Pair";
    CHECK_FALSE(ExtractComponent(f.ctx, plan, nullptr, &err));
    CHECK(f.ctx.undo.Size() == 1);
}

TEST_CASE("create from selection inside a component is one step") {
    Fixture f;
    f.ctx.SetActiveComponent("Card");
    std::string before = State(f.ctx);
    std::string err;
    ExtractPlan plan;
    REQUIRE_MESSAGE(PlanExtract(*f.ctx.ActiveRoot(), {"caption"}, &f.ctx.catalog, &f.ctx.project, plan, &err), err);
    plan.name = "Caption";
    f.Tick();
    REQUIRE_MESSAGE(ExtractComponent(f.ctx, plan, nullptr, &err), err);
    REQUIRE(f.ctx.FindComponentDoc("Caption"));
    CHECK(FindNode(f.ctx.FindComponentDoc("Card")->root, "caption") == nullptr);
    CHECK(f.ctx.undo.Size() == 1);
    f.ctx.Undo();
    CHECK(State(f.ctx) == before);
    CHECK(f.ctx.active_panel == "Card");
}

TEST_CASE("params and slots edits are undoable even when another component is open") {
    Fixture f;
    f.ctx.SetActiveComponent("Frame");
    std::string before = State(f.ctx);
    Component* card = f.ctx.FindComponentDoc("Card");
    REQUIRE(card);
    f.Tick();
    EditDoc(f.ctx, *card, "Add parameter", [&] { card->params.push_back(Param{"title", "string", Json("")}); });
    f.Tick();
    EditDoc(f.ctx, *card, "Add slot", [&] { card->slots.push_back("footer"); });
    CHECK(f.ctx.undo.Size() == 2);
    std::string after = State(f.ctx);
    f.ctx.Undo();
    CHECK(f.ctx.FindComponentDoc("Card")->slots == std::vector<std::string>{"content"});
    f.ctx.Undo();
    CHECK(State(f.ctx) == before);
    f.ctx.Redo();
    f.ctx.Redo();
    CHECK(State(f.ctx) == after);
}

TEST_CASE("imblib import is undoable") {
    fs::path dir = fs::temp_directory_path() /
                   ("imforge_component_undo_" + std::to_string((long long)std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(dir);
    std::string lib_path = (dir / "kit.imblib").generic_string();
    {
        Project src;
        WidgetCatalog cat;
        src.name = "Kit";
        src.dir = dir.generic_string();
        src.widget_catalog = &cat;
        src.components.push_back(MakeEmptyComponent("Gauge", false));
        src.components.push_back(MakeEmptyComponent("Card", false));
        ImblibExportOptions opts;
        opts.name = "kit";
        opts.component_names = {"Gauge", "Card"};
        std::string err;
        REQUIRE_MESSAGE(imblib::Export(src, &cat, opts, lib_path, &err), err);
    }
    Fixture f;
    std::string before = State(f.ctx);
    std::string err;
    f.Tick();
    REQUIRE_MESSAGE(f.ctx.ImportImblib(lib_path, &err), err);
    std::string after = State(f.ctx);
    CHECK(f.ctx.FindComponentDoc("Gauge"));
    CHECK(f.ctx.FindComponentDoc("kit.Card"));
    CHECK(f.ctx.project.libraries.size() == 1);
    CHECK(f.ctx.undo.Size() == 1);
    CHECK(std::string(f.ctx.UndoLabel()) == "Import library");
    f.ctx.Undo();
    CHECK(State(f.ctx) == before);
    CHECK(f.ctx.project.libraries.empty());
    f.ctx.Redo();
    CHECK(State(f.ctx) == after);
    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST_CASE("undo and redo refresh panels, prune stale instances and report Project changes") {
    Fixture f;
    std::string err;
    f.Tick();
    REQUIRE(NewComponent(f.ctx, "Badge", false, &err));
    f.ctx.instances["component:Badge"] = PanelInstance();
    f.ctx.instances["panel:Main"] = PanelInstance();
    int notified = f.project_notifications;
    f.doc_kinds.clear();
    f.ctx.Undo();
    CHECK(f.project_notifications == notified + 1);
    CHECK(f.ctx.instances.count("component:Badge") == 0);
    CHECK(f.ctx.instances.count("panel:Main") == 1);
    REQUIRE_FALSE(f.doc_kinds.empty());
    for (DocKind k : f.doc_kinds) CHECK(k != DocKind::Components);
}

TEST_CASE("a components edit covers nested panel and component edits") {
    Fixture f;
    f.Tick();
    f.ctx.BeginComponentsEdit("Batch", {"Main"});
    f.ctx.BeginEdit("Inner panel", DocKind::Panel, "Main");
    f.ctx.FindPanelDoc("Main")->root.children.pop_back();
    f.ctx.EndEdit();
    f.ctx.BeginEdit("Inner component", DocKind::Component, "Card");
    f.ctx.FindComponentDoc("Card")->slots.clear();
    f.ctx.EndEdit();
    f.ctx.EndEdit();
    CHECK(f.ctx.undo.Size() == 1);
    CHECK(std::string(f.ctx.UndoLabel()) == "Batch");
}

TEST_CASE("mixed component operations unwind in order") {
    Fixture f;
    std::string s0 = State(f.ctx);
    std::string err;
    f.Tick();
    REQUIRE(NewComponent(f.ctx, "Badge", true, &err));
    std::string s1 = State(f.ctx);
    f.Tick();
    REQUIRE(RenameComponent(f.ctx, "Card", "Tile", &err));
    std::string s2 = State(f.ctx);
    f.Tick();
    REQUIRE(DeleteComponent(f.ctx, "Frame"));
    std::string s3 = State(f.ctx);
    CHECK(f.ctx.undo.Size() == 3);
    f.ctx.Undo();
    CHECK(State(f.ctx) == s2);
    f.ctx.Undo();
    CHECK(State(f.ctx) == s1);
    f.ctx.Undo();
    CHECK(State(f.ctx) == s0);
    f.ctx.Redo();
    f.ctx.Redo();
    f.ctx.Redo();
    CHECK(State(f.ctx) == s3);
}
