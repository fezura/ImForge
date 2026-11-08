#if defined(_MSC_VER) && !defined(_CRT_SECURE_NO_WARNINGS)
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "editor_context.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>

#include "imblib.h"
#include "project_io.h"

namespace imforge::builder {

namespace {

std::vector<PanelFactory>& Factories() {
    static std::vector<PanelFactory> f;
    return f;
}

std::vector<std::string> SplitDocList(const std::string& list) {
    std::vector<std::string> out;
    size_t start = 0;
    while (start < list.size()) {
        size_t nl = list.find('\n', start);
        if (nl == std::string::npos) nl = list.size();
        if (nl > start) out.push_back(list.substr(start, nl - start));
        start = nl + 1;
    }
    return out;
}

bool DocListCovers(const std::string& outer, const std::vector<std::string>& inner) {
    std::vector<std::string> have = SplitDocList(outer);
    for (const std::string& n : inner)
        if (std::find(have.begin(), have.end(), n) == have.end()) return false;
    return true;
}

}  // namespace

bool RegisterPanelFactory(int priority, const char* type_name, PanelFactoryFn create) {
    Factories().push_back(PanelFactory{priority, type_name, create});
    return true;
}

static FILE* LogMirrorFile() {
    static FILE* f = nullptr;
    static bool tried = false;
    if (!tried) {
        tried = true;
        if (const char* path = std::getenv("IMFORGE_LOG_FILE"))
            if (*path) f = std::fopen(path, "w");
    }
    return f;
}

void LogBuffer::Push(LogEntry e) {
    if (FILE* f = LogMirrorFile()) {
        static const char* const kNames[] = {"debug", "info", "warn", "error"};
        std::fprintf(f, "[%8.3f] %-5s %s: %s\n", e.time, kNames[(int)e.level & 3], e.source.c_str(), e.message.c_str());
        std::fflush(f);
    }
    if (entries_.empty()) entries_.resize(kCapacity);
    size_t slot = (head_ + count_) % entries_.size();
    if (count_ == entries_.size()) {
        LogEntry& old = entries_[head_];
        if (counts_[(int)old.level] > 0) --counts_[(int)old.level];
        head_ = (head_ + 1) % entries_.size();
    } else {
        ++count_;
    }
    ++counts_[(int)e.level];
    entries_[slot] = std::move(e);
    ++generation_;
}

void LogBuffer::Clear() {
    head_ = count_ = 0;
    for (unsigned& c : counts_) c = 0;
    ++generation_;
}

void EditorContext::RegisterPanel(std::unique_ptr<EditorPanel> panel) {
    if (!panel) return;
    for (const auto& p : panels)
        if (std::string(p->Name()) == panel->Name()) return;
    panels.push_back(std::move(panel));
}

void EditorContext::CreateRegisteredPanels() {
    std::vector<PanelFactory> f = Factories();
    std::stable_sort(f.begin(), f.end(), [](const PanelFactory& a, const PanelFactory& b) {
        if (a.priority != b.priority) return a.priority < b.priority;
        return std::strcmp(a.type_name, b.type_name) < 0;
    });
    for (const PanelFactory& pf : f) {
        std::unique_ptr<EditorPanel> p = pf.create();
        if (!p) continue;
        Log(LogLevel::Debug, "panel registered: %s (%s, priority %d)", p->Name(), pf.type_name, pf.priority);
        RegisterPanel(std::move(p));
    }
}

EditorPanel* EditorContext::FindPanel(const char* name) {
    for (auto& p : panels)
        if (std::strcmp(p->Name(), name) == 0) return p.get();
    return nullptr;
}

bool EditorContext::OpenPanel(const char* name, bool focus) {
    EditorPanel* p = FindPanel(name);
    if (!p) {
        Log(LogLevel::Warning, "panel \"%s\" is not available in this build", name);
        return false;
    }
    p->open = true;
    if (focus) ImGui::SetWindowFocus(p->Name());
    return true;
}

void EditorContext::ToggleWizard() {
    if (EditorPanel* w = FindPanel("Wizard")) w->open = !w->open;
    else Log(LogLevel::Warning, "the project wizard is not available in this build");
}

Panel* EditorContext::ActivePanel() {
    return active_kind == DocKind::Panel ? FindPanelDoc(active_panel) : nullptr;
}
const Panel* EditorContext::ActivePanel() const { return const_cast<EditorContext*>(this)->ActivePanel(); }
Component* EditorContext::ActiveComponent() {
    return active_kind == DocKind::Component ? FindComponentDoc(active_panel) : nullptr;
}
Node* EditorContext::ActiveRoot() {
    if (Panel* p = ActivePanel()) return &p->root;
    if (Component* c = ActiveComponent()) return &c->root;
    return nullptr;
}
Panel* EditorContext::FindPanelDoc(const std::string& name) {
    for (Panel& p : project.panels)
        if (p.name == name) return &p;
    return nullptr;
}
Component* EditorContext::FindComponentDoc(const std::string& name) {
    for (Component& c : project.components)
        if (c.name == name) return &c;
    return nullptr;
}
Theme* EditorContext::ActiveTheme() {
    for (Theme& t : project.themes)
        if (t.name == project.active_theme) return &t;
    return project.themes.empty() ? nullptr : &project.themes.front();
}

void EditorContext::SetActivePanel(const std::string& name) {
    if (active_kind == DocKind::Panel && active_panel == name) return;
    active_kind = FindPanelDoc(name) ? DocKind::Panel : DocKind::None;
    active_panel = active_kind == DocKind::Panel ? name : std::string();
    ClearSelection();
    validation_pending = true;
    DocumentChanged(active_kind, active_panel);
}

void EditorContext::SetActiveComponent(const std::string& name) {
    if (active_kind == DocKind::Component && active_panel == name) return;
    active_kind = FindComponentDoc(name) ? DocKind::Component : DocKind::None;
    active_panel = active_kind == DocKind::Component ? name : std::string();
    ClearSelection();
    validation_pending = true;
    DocumentChanged(active_kind, active_panel);
}

void EditorContext::CloseActiveDocument() {
    active_kind = DocKind::None;
    active_panel.clear();
    ClearSelection();
    diagnostics.clear();
    DocumentChanged(DocKind::None, std::string());
}

void EditorContext::Select(const std::string& id, bool additive) {
    if (!additive) {
        if (selection.size() == 1 && selection[0] == id) return;
        selection.clear();
    }
    if (!id.empty() && !IsSelected(id)) selection.push_back(id);
    for (auto& f : on_selection_changed) f();
    if (live.IsConnected()) live.SetPickerMode(picker_, SelectedId(), active_kind == DocKind::Panel ? active_panel : "");
}

void EditorContext::Deselect(const std::string& id) {
    auto it = std::find(selection.begin(), selection.end(), id);
    if (it == selection.end()) return;
    selection.erase(it);
    for (auto& f : on_selection_changed) f();
}

void EditorContext::ClearSelection() {
    if (selection.empty()) return;
    selection.clear();
    for (auto& f : on_selection_changed) f();
}

bool EditorContext::IsSelected(const std::string& id) const {
    return std::find(selection.begin(), selection.end(), id) != selection.end();
}

const std::string& EditorContext::SelectedId() const {
    static const std::string empty;
    return selection.empty() ? empty : selection.back();
}

Node* EditorContext::FindActiveNode(const std::string& id) {
    Node* root = ActiveRoot();
    return root && !id.empty() ? FindNode(*root, id) : nullptr;
}

Node* EditorContext::SelectedNode() { return FindActiveNode(SelectedId()); }

std::vector<Node*> EditorContext::SelectedNodes() {
    std::vector<Node*> out;
    for (const std::string& id : selection)
        if (Node* n = FindActiveNode(id)) out.push_back(n);
    return out;
}

void EditorContext::PruneSelection() {
    Node* root = ActiveRoot();
    size_t before = selection.size();
    if (!root) selection.clear();
    else
        selection.erase(std::remove_if(selection.begin(), selection.end(),
                                       [&](const std::string& id) { return FindNode(*root, id) == nullptr; }),
                        selection.end());
    if (selection.size() != before)
        for (auto& f : on_selection_changed) f();
}

Json EditorContext::Snapshot(DocKind kind, const std::string& name) const {
    switch (kind) {
    case DocKind::Panel:
        for (const Panel& p : project.panels)
            if (p.name == name) return PanelToJson(p);
        return Json();
    case DocKind::Component:
        for (const Component& c : project.components)
            if (c.name == name) return ComponentToJson(c);
        return Json();
    case DocKind::Project: {
        Json o = Json::Object();
        o.Set("project", ProjectToJson(project));
        Json themes = Json::Array();
        for (const Theme& t : project.themes) {
            Json tj = ThemeToJson(t);
            tj.Set("file", Json(t.file));
            themes.Push(std::move(tj));
        }
        o.Set("themes", std::move(themes));
        Json panels_j = Json::Array();
        for (const Panel& p : project.panels) {
            Json pj = PanelToJson(p);
            pj.Set("file", Json(p.file));
            panels_j.Push(std::move(pj));
        }
        o.Set("panels", std::move(panels_j));
        Json anims = Json::Array();
        for (const Json& a : project.animations) anims.Push(a);
        o.Set("animations", std::move(anims));
        o.Set("activePanel", Json(active_panel));
        return o;
    }
    case DocKind::Components: {
        Json o = Json::Object();
        Json comps = Json::Array();
        for (const Component& c : project.components) {
            Json cj = ComponentToJson(c);
            cj.Set("file", Json(c.file));
            comps.Push(std::move(cj));
        }
        o.Set("components", std::move(comps));
        Json libs = Json::Array();
        for (const std::string& l : project.libraries) libs.Push(Json(l));
        o.Set("libraries", std::move(libs));
        Json panels_j = Json::Object();
        for (const std::string& pn : SplitDocList(name))
            for (const Panel& p : project.panels)
                if (p.name == pn) panels_j.Set(pn, PanelToJson(p));
        o.Set("panels", std::move(panels_j));
        o.Set("activeComponent", Json(active_kind == DocKind::Component ? active_panel : std::string()));
        return o;
    }
    default: return Json();
    }
}

bool EditorContext::Restore(DocKind kind, const std::string& name, const Json& snap) {
    Error err;
    switch (kind) {
    case DocKind::Panel: {
        Panel* p = FindPanelDoc(name);
        if (!p) return false;
        Panel tmp;
        tmp.file = p->file;
        if (!PanelFromJson(snap, tmp, &err)) {
            Log(LogLevel::Error, "undo: cannot restore panel %s: %s", name.c_str(), err.message.c_str());
            return false;
        }
        if (active_kind == DocKind::Panel && active_panel == name) active_panel = tmp.name;
        *p = std::move(tmp);
        break;
    }
    case DocKind::Component: {
        Component* c = FindComponentDoc(name);
        if (!c) return false;
        Component tmp;
        tmp.file = c->file;
        if (!ComponentFromJson(snap, tmp, &err)) {
            Log(LogLevel::Error, "undo: cannot restore component %s: %s", name.c_str(), err.message.c_str());
            return false;
        }
        if (active_kind == DocKind::Component && active_panel == name) active_panel = tmp.name;
        *c = std::move(tmp);
        break;
    }
    case DocKind::Project: {
        Project tmp;
        tmp.dir = project.dir;
        tmp.widget_catalog = project.widget_catalog;
        if (!ProjectFromJson(snap["project"], tmp, &err)) {
            Log(LogLevel::Error, "undo: cannot restore project: %s", err.message.c_str());
            return false;
        }
        bool panels_ok = snap["panels"].IsArray();
        std::vector<Panel> restored_panels;
        if (panels_ok) {
            for (const Json& pj : snap["panels"].arr) {
                Panel p;
                p.file = pj["file"].AsString("");
                if (!PanelFromJson(pj, p, &err)) {
                    Log(LogLevel::Error, "undo: cannot restore panel list: %s", err.message.c_str());
                    panels_ok = false;
                    break;
                }
                restored_panels.push_back(std::move(p));
            }
        }
        tmp.panels = panels_ok ? std::move(restored_panels) : std::move(project.panels);
        tmp.components = std::move(project.components);
        for (const Json& tj : snap["themes"].arr) {
            Theme t;
            t.file = tj["file"].AsString("");
            if (ThemeFromJson(tj, t, &err)) tmp.themes.push_back(std::move(t));
        }
        for (const Json& aj : snap["animations"].arr) tmp.animations.push_back(aj);
        project = std::move(tmp);
        if (active_kind == DocKind::Panel && !FindPanelDoc(active_panel)) {
            std::string fallback = snap["activePanel"].AsString("");
            if (!FindPanelDoc(fallback)) fallback = project.panels.empty() ? std::string() : project.panels.front().name;
            active_kind = fallback.empty() ? DocKind::None : DocKind::Panel;
            active_panel = fallback;
        }
        break;
    }
    case DocKind::Components: {
        std::vector<Component> comps;
        for (const Json& cj : snap["components"].arr) {
            Component c;
            c.file = cj["file"].AsString("");
            if (!ComponentFromJson(cj, c, &err)) {
                Log(LogLevel::Error, "undo: cannot restore component list: %s", err.message.c_str());
                return false;
            }
            comps.push_back(std::move(c));
        }
        std::vector<std::pair<Panel*, Panel>> panels_r;
        for (const auto& kv : snap["panels"].obj) {
            Panel* p = FindPanelDoc(kv.first);
            Panel tmp;
            if (p) tmp.file = p->file;
            if (!p || !PanelFromJson(kv.second, tmp, &err)) {
                Log(LogLevel::Error, "undo: cannot restore panel %s: %s", kv.first.c_str(),
                    p ? err.message.c_str() : "panel not found");
                return false;
            }
            panels_r.emplace_back(p, std::move(tmp));
        }
        project.components = std::move(comps);
        for (auto& pr : panels_r) *pr.first = std::move(pr.second);
        project.libraries.clear();
        for (const Json& l : snap["libraries"].arr) project.libraries.push_back(l.AsString(""));
        FixActiveComponent(snap["activeComponent"].AsString(""));
        break;
    }
    default: return false;
    }
    ++edit_serial;
    MarkDirty();
    validation_pending = true;
    PruneSelection();
    DocumentChanged(kind, name);
    if (kind == DocKind::Components) ComponentsChanged();
    return true;
}

void EditorContext::FixActiveComponent(const std::string& preferred) {
    if (active_kind != DocKind::Component || FindComponentDoc(active_panel)) return;
    if (!preferred.empty() && FindComponentDoc(preferred)) {
        active_panel = preferred;
        return;
    }
    active_panel = project.panels.empty() ? std::string() : project.panels.front().name;
    active_kind = active_panel.empty() ? DocKind::None : DocKind::Panel;
}

void EditorContext::ComponentsChanged() {
#if IMFORGE_HAS_INTERP
    for (auto it = instances.begin(); it != instances.end();) {
        bool stale = it->first.compare(0, 10, "component:") == 0 && !FindComponentDoc(it->first.substr(10));
        it = stale ? instances.erase(it) : std::next(it);
    }
#endif
    if (live.IsConnected()) live.PushAll(project);
    NotifyProjectChanged("components");
}

void EditorContext::BeginEdit(const char* label) {
    BeginEdit(label, active_kind == DocKind::None ? DocKind::Project : active_kind, active_panel);
}

void EditorContext::BeginEdit(const char* label, DocKind kind, const std::string& name) {
    PendingEdit e;
    e.label = label ? label : "Edit";
    e.kind = kind;
    e.name = name;
    if (!edits_.empty()) {
        const PendingEdit& outer = edits_.back();
        e.covered = (outer.kind == kind && outer.name == name) ||
                    (outer.kind == DocKind::Project && (kind == DocKind::Panel || kind == DocKind::Project)) ||
                    (outer.kind == DocKind::Components &&
                     (kind == DocKind::Component || (kind == DocKind::Panel && DocListCovers(outer.name, {name})) ||
                      (kind == DocKind::Components && DocListCovers(outer.name, SplitDocList(name)))));
    }
    if (!e.covered) e.before = Snapshot(kind, name);
    edits_.push_back(std::move(e));
    ++edit_depth_;
}

void EditorContext::BeginComponentsEdit(const char* label, const std::vector<std::string>& panel_names) {
    std::vector<std::string> sorted = panel_names;
    std::sort(sorted.begin(), sorted.end());
    sorted.erase(std::unique(sorted.begin(), sorted.end()), sorted.end());
    std::string list;
    for (const std::string& n : sorted) list += (list.empty() ? "" : "\n") + n;
    BeginEdit(label, DocKind::Components, list);
}

void EditorContext::EndEdit() {
    if (edits_.empty()) return;
    PendingEdit e = std::move(edits_.back());
    edits_.pop_back();
    --edit_depth_;
    if (e.covered) {
        ++edit_serial;
        return;
    }
    std::string name = e.name;
    if (e.kind == DocKind::Panel && active_kind == DocKind::Panel && (name.empty() || !FindPanelDoc(name))) name = active_panel;
    if (e.kind == DocKind::Component && active_kind == DocKind::Component && (name.empty() || !FindComponentDoc(name)))
        name = active_panel;
    if (e.kind == DocKind::Components) FixActiveComponent(std::string());
    Json after = Snapshot(e.kind, name);
    if (after == e.before) return;
    undo.Push(e.label, e.kind, name, std::move(e.before), std::move(after), time);
    ++edit_serial;
    MarkDirty();
    validation_pending = true;
    PruneSelection();
    DocumentChanged(e.kind, name);
    if (e.kind == DocKind::Components) ComponentsChanged();
}

void EditorContext::CancelEdit() {
    if (edits_.empty()) return;
    PendingEdit e = std::move(edits_.back());
    edits_.pop_back();
    --edit_depth_;
    if (e.covered) return;
    Restore(e.kind, e.name, e.before);
}

void EditorContext::ApplyUndoEntry(const UndoEntry& e, bool is_undo) {
    const Json& target = is_undo ? e.before : e.after;
    const Json& current = is_undo ? e.after : e.before;
    std::string name = e.name;
    if (e.kind == DocKind::Panel || e.kind == DocKind::Component) {
        std::string cur_name = current["name"].AsString(e.name);
        if (!cur_name.empty()) name = cur_name;
        if (e.kind == DocKind::Panel && !FindPanelDoc(name)) name = e.name;
        if (e.kind == DocKind::Component && !FindComponentDoc(name)) name = e.name;
        if (e.kind == DocKind::Panel) SetActivePanel(name);
        else SetActiveComponent(name);
    }
    if (!Restore(e.kind, name, target)) return;
    SetStatus(std::string(is_undo ? "Undo: " : "Redo: ") + e.label);
}

void EditorContext::RequestCreateComponent(const std::vector<std::string>& ids) {
    component_extract_request = ids;
    if (!ids.empty() && ids != selection) {
        selection = ids;
        for (auto& f : on_selection_changed) f();
    }
    if (OpenPanel("Components")) SetStatus("Components: use \"From selection...\" to extract the component");
}

void EditorContext::Undo() {
    if (edit_depth_ > 0) return;
    if (const UndoEntry* e = undo.Undo()) ApplyUndoEntry(*e, true);
}

void EditorContext::Redo() {
    if (edit_depth_ > 0) return;
    if (const UndoEntry* e = undo.Redo()) ApplyUndoEntry(*e, false);
}

const char* EditorContext::UndoLabel() const {
    const UndoEntry* e = undo.PeekUndo();
    return e ? e->label.c_str() : "";
}
const char* EditorContext::RedoLabel() const {
    const UndoEntry* e = undo.PeekRedo();
    return e ? e->label.c_str() : "";
}

void EditorContext::MarkDirty() { dirty = has_project; }

void EditorContext::DocumentChanged(DocKind kind, const std::string& name) {
    bool list = kind == DocKind::Components;
    for (auto& f : on_document_changed) f(list ? DocKind::Project : kind, list ? std::string() : name);
}

void EditorContext::ResetDocumentState() {
    undo.Clear();
    selection.clear();
    hovered_id.clear();
    diagnostics.clear();
    conflicts.clear();
    edits_.clear();
    edit_depth_ = 0;
    dirty = false;
#if IMFORGE_HAS_INTERP
    instances.clear();
#endif
}

void EditorContext::LoadTypesJson() {
    types.Clear();
    if (project.types_json.empty()) return;
    std::string path = JoinPath(project.dir, project.types_json);
    Json j;
    Error err;
    if (!ReadJsonFile(path, j, &err)) {
        Log(LogLevel::Info, "types.json not loaded: %s", err.message.c_str());
        return;
    }
    if (!types.Load(j, &err)) Log(LogLevel::Warning, "types.json not loaded: %s (%s)", err.message.c_str(), path.c_str());
    else Log(LogLevel::Info, "loaded type database %s", path.c_str());
}

void EditorContext::AfterProjectLoaded() {
    has_project = true;
    project_path = project.dir;
    project.widget_catalog = &catalog;
#if IMFORGE_HAS_INTERP
    interp.project = &project;
#endif
    ResetDocumentState();
    LoadTypesJson();
    active_kind = DocKind::None;
    active_panel.clear();
    if (!project.panels.empty()) SetActivePanel(project.panels.front().name);
    AddRecentProject(prefs, project.dir);
    SavePrefs(prefs);
    validation_pending = true;
    NotifyProjectChanged("loaded");
    if (live.IsConnected()) live.PushAll(project);
    if (project.live.enabled && !live.IsRunning()) {
        std::string lerr;
        if (!live.Start(project.live, prefs.live_bind_host, &lerr)) Log(LogLevel::Warning, "live server: %s", lerr.c_str());
        else Log(LogLevel::Info, "live server listening on %s", live.Url().c_str());
    }
}

bool EditorContext::LoadProjectDir(const std::string& dir, std::string* err) {
    Error e;
    Project loaded;
    loaded.widget_catalog = &catalog;
    if (!LoadProject(dir, loaded, &e)) {
        if (err) *err = e.message;
        Log(LogLevel::Error, "open project failed: %s", e.message.c_str());
        return false;
    }
    project = std::move(loaded);
    AfterProjectLoaded();
    Log(LogLevel::Info, "opened project \"%s\" (%s): %d panels, %d components, %d themes", project.name.c_str(),
        project.dir.c_str(), (int)project.panels.size(), (int)project.components.size(), (int)project.themes.size());
    SetStatus("Opened " + project.name);
    return true;
}

bool EditorContext::SaveProject(std::string* err) {
    if (!has_project) {
        if (err) *err = "no project open";
        return false;
    }
    Error e;
    std::vector<std::string> written;
    if (!imforge::builder::SaveProject(project, &e, &written)) {
        if (err) *err = e.message;
        Log(LogLevel::Error, "save failed: %s", e.message.c_str());
        return false;
    }
    for (Panel& p : project.panels)
        if (p.file.empty()) p.file = DefaultPanelFile(p.name);
    for (Component& c : project.components)
        if (c.file.empty()) c.file = DefaultComponentFile(c.name);
    for (Theme& t : project.themes)
        if (t.file.empty()) t.file = DefaultThemeFile(t.name);
    dirty = false;
    Log(LogLevel::Info, "saved %s (%d files changed)", project.dir.c_str(), (int)written.size());
    SetStatus("Saved");
    NotifyProjectChanged("saved");
    return true;
}

bool EditorContext::SaveProjectAs(const std::string& dir, std::string* err) {
    if (!has_project) {
        if (err) *err = "no project open";
        return false;
    }
    std::string old = project.dir;
    project.dir = NormalizePath(dir);
    if (!SaveProject(err)) {
        project.dir = old;
        return false;
    }
    project_path = project.dir;
    AddRecentProject(prefs, project.dir);
    SavePrefs(prefs);
    NotifyProjectChanged("moved");
    return true;
}

bool EditorContext::NewProject(const std::string& template_dir, const std::string& dest_dir, const std::string& name,
                               const Project& settings, std::string* err) {
    Error e;
    if (!NewProjectFromTemplate(template_dir, dest_dir, name, settings, &e)) {
        if (err) *err = e.message;
        Log(LogLevel::Error, "new project failed: %s", e.message.c_str());
        return false;
    }
    Log(LogLevel::Info, "created project \"%s\" in %s", name.c_str(), dest_dir.c_str());
    return LoadProjectDir(dest_dir, err);
}

void EditorContext::CloseProject() {
    if (!has_project) return;
    Log(LogLevel::Info, "closed project \"%s\"", project.name.c_str());
    project = Project();
    project.widget_catalog = &catalog;
    project_path.clear();
    has_project = false;
    active_kind = DocKind::None;
    active_panel.clear();
    ResetDocumentState();
    types.Clear();
    NotifyProjectChanged("closed");
}

bool EditorContext::ExportCode(std::string* err) {
    if (!has_project) {
        if (err) *err = "no project open";
        return false;
    }
    Error e;
    ExportReport rep;
    bool ok = imforge::builder::ExportCode(project, &e, &rep);
    conflicts = rep.conflicts;
    for (const Diagnostic& d : rep.diagnostics) {
        LogLevel lvl = d.level == Diagnostic::Level::Error     ? LogLevel::Error
                       : d.level == Diagnostic::Level::Warning ? LogLevel::Warning
                                                               : LogLevel::Info;
        LogSource(lvl, "codegen", "%s%s%s", d.panel.empty() ? "" : (d.panel + ": ").c_str(),
                  d.node_id.empty() ? "" : (d.node_id + ": ").c_str(), d.message.c_str());
    }
    if (!ok) {
        if (err) *err = e.message;
        Log(LogLevel::Error, "export failed: %s", e.message.c_str());
        return false;
    }
    Log(LogLevel::Info, "exported code to %s: %d files written, %d unchanged, %d conflicts",
        JoinPath(project.dir, project.codegen.output_dir).c_str(), (int)rep.written.size(), (int)rep.unchanged.size(),
        (int)rep.conflicts.size());
    SetStatus("Exported " + std::to_string(rep.written.size()) + " files");
    if (!rep.conflicts.empty()) OpenPanel("Conflicts");
    NotifyProjectChanged("exported");
    return true;
}

bool EditorContext::ImportImblib(const std::string& path, std::string* err) {
    if (!has_project) {
        if (err) *err = "no project open";
        return false;
    }
    ImblibImportResult res;
    std::string e;
    BeginComponentsEdit("Import library");
    bool ok = imblib::Import(project, &catalog, path, &e, &res);
    EndEdit();
    if (!ok) {
        LogSource(LogLevel::Error, "imblib", "import failed: %s", e.c_str());
        if (err) *err = e;
        return false;
    }
    for (const std::string& w : res.warnings) LogSource(LogLevel::Warning, "imblib", "%s", w.c_str());
    LogSource(LogLevel::Info, "imblib", "imported library \"%s\" from %s: %d components, %d widgets, %d files",
              res.library.c_str(), path.c_str(), (int)res.components.size(), (int)res.widgets.size(), (int)res.files.size());
    SetStatus("Imported library " + res.library);
    return true;
}

void EditorContext::NotifyProjectChanged(const char* what) {
    for (auto& f : on_project_changed) f(what);
    for (auto& p : panels) p->OnProjectChanged(*this);
}

void EditorContext::Revalidate() {
    validation_pending = false;
    diagnostics.clear();
    if (!has_project) return;
    if (Panel* p = ActivePanel()) ValidatePanel(project, *p, diagnostics);
    else if (Component* c = ActiveComponent()) ValidateComponent(project, *c, diagnostics);
    if (FILE* f = LogMirrorFile()) {
        for (const Diagnostic& d : diagnostics)
            std::fprintf(f, "[%8.3f] diag  %s: %s%s%s%s%s\n", time,
                         d.level == Diagnostic::Level::Error ? "error" : d.level == Diagnostic::Level::Warning ? "warn" : "info",
                         d.panel.c_str(), d.panel.empty() ? "" : "/", d.node_id.c_str(), d.node_id.empty() ? "" : ": ",
                         d.message.c_str());
        std::fflush(f);
    }
}

std::string EditorContext::ProjectDisplayName() const {
    if (!has_project) return "No project";
    return project.name.empty() ? std::filesystem::path(project.dir).filename().string() : project.name;
}

std::string EditorContext::WindowTitle() const {
    std::string t = "ImForge";
    if (has_project) t += " - " + ProjectDisplayName() + (dirty ? " *" : "");
    return t;
}

static void MirrorToStderr(const LogEntry& e) {
    static const bool enabled = std::getenv("IMFORGE_LOG_STDERR") != nullptr;
    if (!enabled) return;
    static const char* const names[4] = {"debug", "info", "warn", "error"};
    std::fprintf(stderr, "[%8.3f] %-5s %s%s%s\n", e.time, names[(int)e.level], e.source.c_str(), e.source.empty() ? "" : ": ",
                 e.message.c_str());
    std::fflush(stderr);
}

void EditorContext::Log(LogLevel level, const char* fmt, ...) {
    char buf[2048];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    LogEntry e;
    e.level = level;
    e.time = time;
    e.source = "builder";
    e.message = buf;
    MirrorToStderr(e);
    log.Push(std::move(e));
}

void EditorContext::LogSource(LogLevel level, const char* source, const char* fmt, ...) {
    char buf[2048];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    LogEntry e;
    e.level = level;
    e.time = time;
    e.source = source ? source : "";
    e.message = buf;
    MirrorToStderr(e);
    log.Push(std::move(e));
}

void EditorContext::SetStatus(const std::string& msg, double seconds) {
    status_message = msg;
    status_message_until = time + seconds;
}

void EditorContext::BeginFrame() {
    double now = ImGui::GetTime();
    delta_time = now - time;
    time = now;
    ++frame;
    if (validation_pending) Revalidate();
    if (!status_message.empty() && time > status_message_until) status_message.clear();
}

void EditorContext::PushLive(bool everything) {
    if (!live.IsRunning()) {
        std::string lerr;
        if (!live.Start(project.live, prefs.live_bind_host, &lerr)) {
            Log(LogLevel::Warning, "live server: %s", lerr.c_str());
            return;
        }
        Log(LogLevel::Info, "live server listening on %s", live.Url().c_str());
    }
    if (!live.IsConnected()) {
        SetStatus("No live client connected");
        return;
    }
    if (everything || active_kind != DocKind::Panel) {
        live.PushAll(project);
        SetStatus("Pushed project to live client");
        return;
    }
    if (Panel* p = ActivePanel()) {
        live.PushPanel(*p);
        SetStatus("Pushed " + p->name);
    }
}

void EditorContext::SetPickerMode(bool on) {
    picker_ = on;
    if (live.IsConnected()) live.SetPickerMode(on, SelectedId(), active_kind == DocKind::Panel ? active_panel : "");
    SetStatus(on ? "Picker mode: click a widget in the live app" : "Picker mode off");
}

void EditorContext::OnLiveSelect(const std::string& panel, const std::string& id) {
    if (!panel.empty() && FindPanelDoc(panel)) SetActivePanel(panel);
    Select(id);
    if (picker_) picker_ = false;
}

void EditorContext::OnLiveHover(const std::string& panel, const std::string& id) {
    if (active_kind == DocKind::Panel && panel == active_panel) hovered_id = id;
}

}  // namespace imforge::builder
