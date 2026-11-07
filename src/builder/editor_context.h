#pragma once

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "imgui.h"

#include "imforge/imforge_core.h"
#include "live_server.h"
#include "panels/editor_panel.h"
#include "prefs.h"
#include "undo.h"

#if __has_include("imforge/imforge_interp.h")
#include "imforge/imforge_interp.h"
#define IMFORGE_HAS_INTERP 1
#else
#define IMFORGE_HAS_INTERP 0
#endif

#if __has_include("type_database.h")
#include "type_database.h"
#define IMFORGE_HAS_TYPEDB 1
#else
#define IMFORGE_HAS_TYPEDB 0
#endif

namespace imforge::builder {

enum class LogLevel { Debug = 0, Info = 1, Warning = 2, Error = 3 };

struct LogEntry {
    LogLevel level = LogLevel::Info;
    double time = 0;
    std::string source;
    std::string message;
};

class LogBuffer {
public:
    static constexpr size_t kCapacity = 4000;
    void Push(LogEntry e);
    void Clear();
    size_t Size() const { return count_; }
    const LogEntry& At(size_t i) const { return entries_[(head_ + i) % entries_.size()]; }
    unsigned Count(LogLevel l) const { return counts_[(int)l]; }
    unsigned Generation() const { return generation_; }

private:
    std::vector<LogEntry> entries_;
    size_t head_ = 0, count_ = 0;
    unsigned counts_[4] = {0, 0, 0, 0};
    unsigned generation_ = 0;
};

#if IMFORGE_HAS_TYPEDB
using TypeDatabase = ::imforge::binding::TypeDatabase;
#else
struct TypeDatabase {
    Json raw;
    bool Load(const Json& j, Error* err = nullptr) {
        raw = j;
        bool ok = j.IsObject() && j["structs"].IsObject();
        if (!ok && err) err->message = "types.json: expected an object with \"structs\"";
        return ok;
    }
    void Clear() { raw = Json(); }
    bool Empty() const { return raw["structs"].Size() == 0; }
    std::vector<std::string> StructNames() const {
        std::vector<std::string> out;
        for (const auto& kv : raw["structs"].obj) out.push_back(kv.first);
        return out;
    }
};
#endif

struct ExportConflict {
    std::string panel, node_id, reason, user_code, generated_code;
};

struct Commands {
    bool new_project = false, open_project = false, save = false, save_as = false, export_code = false;
    bool import_imblib = false, quit = false, reset_layout = false, show_about = false, reload_fonts = false;
    std::string open_dir, import_path;
};

enum class FontSlot { Ui = 0, Heading = 1, Mono = 2, Count = 3 };

struct EditorContext {
    Project project;
    std::string project_path;
    bool has_project = false;
    DocKind active_kind = DocKind::None;
    std::string active_panel;
    std::vector<std::string> selection;
    std::string hovered_id;
    UndoStack undo;
    bool dirty = false;
    unsigned edit_serial = 0;
    LiveServer live;
    TypeDatabase types;
    Prefs prefs;
    std::vector<Diagnostic> diagnostics;
    std::vector<ExportConflict> conflicts;
    ImFont* fonts[(int)FontSlot::Count] = {nullptr, nullptr, nullptr};
    WidgetCatalog catalog;
#if IMFORGE_HAS_INTERP
    Interpreter interp;
    std::map<std::string, PanelInstance> instances;
#else
    struct LiveState {
        bool interpreter_available = false;
    } live_state;
#endif
    LogBuffer log;
    Commands commands;
    std::vector<std::function<void(const char*)>> on_project_changed;
    std::vector<std::function<void(DocKind, const std::string&)>> on_document_changed;
    std::vector<std::function<void()>> on_selection_changed;
    std::vector<std::unique_ptr<EditorPanel>> panels;
    double time = 0, delta_time = 0;
    float dpi_scale = 1.0f;
    int frame = 0;
    bool validation_pending = false;
    std::string status_message;
    double status_message_until = 0;

    void RegisterPanel(std::unique_ptr<EditorPanel> panel);
    void CreateRegisteredPanels();
    EditorPanel* FindPanel(const char* name);
    bool OpenPanel(const char* name, bool focus = true);
    void ToggleWizard();

    Panel* ActivePanel();
    const Panel* ActivePanel() const;
    Component* ActiveComponent();
    Node* ActiveRoot();
    Panel* FindPanelDoc(const std::string& name);
    Component* FindComponentDoc(const std::string& name);
    Theme* ActiveTheme();
    void SetActivePanel(const std::string& name);
    void SetActiveComponent(const std::string& name);
    void CloseActiveDocument();

    void Select(const std::string& id, bool additive = false);
    void Deselect(const std::string& id);
    void ClearSelection();
    bool IsSelected(const std::string& id) const;
    const std::string& SelectedId() const;
    Node* SelectedNode();
    std::vector<Node*> SelectedNodes();
    Node* FindActiveNode(const std::string& id);

    void BeginEdit(const char* label);
    void BeginEdit(const char* label, DocKind kind, const std::string& name);
    void BeginComponentsEdit(const char* label, const std::vector<std::string>& panels = {});
    void EndEdit();
    void CancelEdit();
    bool InEdit() const { return edit_depth_ > 0; }
    template <class F>
    void Edit(const char* label, F&& f) {
        BeginEdit(label);
        f();
        EndEdit();
    }
    template <class F>
    void EditProject(const char* label, F&& f) {
        BeginEdit(label, DocKind::Project, std::string());
        f();
        EndEdit();
    }
    void Undo();
    void Redo();
    const char* UndoLabel() const;
    const char* RedoLabel() const;
    Json Snapshot(DocKind kind, const std::string& name) const;
    bool Restore(DocKind kind, const std::string& name, const Json& snapshot);
    void MarkDirty();
    void DocumentChanged(DocKind kind, const std::string& name);

    void RequestCreateComponent(const std::vector<std::string>& ids);
    std::vector<std::string> component_extract_request;

    bool DeleteSelection();
    bool DuplicateSelection();
    bool CopySelection();
    bool CutSelection();
    bool Paste();
    bool CanPaste() const;
    bool CanAcceptChild(const Node& parent, const std::string& child_type) const;
    Node* InsertNode(const std::string& type, const std::string& parent_id, int index = -1, const char* undo_label = "Insert");
    bool MoveNode(const std::string& id, const std::string& new_parent_id, int index);
    bool RenameNode(const std::string& id, const std::string& new_id);

    bool LoadProjectDir(const std::string& dir, std::string* err);
    bool SaveProject(std::string* err);
    bool SaveProjectAs(const std::string& dir, std::string* err);
    bool NewProject(const std::string& template_dir, const std::string& dest_dir, const std::string& name,
                    const Project& settings, std::string* err);
    void CloseProject();
    bool ExportCode(std::string* err);
    bool ImportImblib(const std::string& path, std::string* err);
    void NotifyProjectChanged(const char* what);
    void Revalidate();
    std::string ProjectDisplayName() const;
    std::string WindowTitle() const;

    void Log(LogLevel level, const char* fmt, ...);
    void LogSource(LogLevel level, const char* source, const char* fmt, ...);
    void SetStatus(const std::string& msg, double seconds = 4.0);

    void BeginFrame();
    void PushLive(bool everything = false);
    void SetPickerMode(bool on);
    bool PickerMode() const { return picker_; }
    void OnLiveSelect(const std::string& panel, const std::string& id);
    void OnLiveHover(const std::string& panel, const std::string& id);
    ImFont* Font(FontSlot slot) const { return fonts[(int)slot]; }

private:
    struct PendingEdit {
        std::string label;
        DocKind kind;
        std::string name;
        Json before;
        bool covered = false;
    };
    std::vector<PendingEdit> edits_;
    int edit_depth_ = 0;
    Json clipboard_;
    bool picker_ = false;
    void ApplyUndoEntry(const UndoEntry& e, bool undo);
    void FixActiveComponent(const std::string& preferred);
    void ComponentsChanged();
    void ResetDocumentState();
    void AfterProjectLoaded();
    void LoadTypesJson();
    void PruneSelection();
};

}  // namespace imforge::builder
