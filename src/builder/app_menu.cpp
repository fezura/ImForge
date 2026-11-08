#include <cstdio>
#include <cstring>

#include "imgui.h"

#include "app.h"
#include "builder_style.h"
#include "os_util.h"
#include "panels/dialogs.h"
#include "panels/panels_state.h"
#include "project_io.h"
#include "ui/ui_helpers.h"

namespace imforge::builder {

namespace {

bool Chord(ImGuiKeyChord chord, bool allow_in_text = false) {
    ImGuiIO& io = ImGui::GetIO();
    if (!allow_in_text && io.WantTextInput) return false;
#if IMGUI_VERSION_NUM >= 19070
    return ImGui::Shortcut(chord, ImGuiInputFlags_RouteGlobal);
#else
    return ImGui::IsKeyChordPressed(chord);
#endif
}

const char* kDocsUrl = "https://github.com/fezura/ImForge/blob/main/docs/USER_GUIDE.md";

}  // namespace

void App::HandleShortcuts(bool& quit) {
    (void)quit;
    bool has_doc = ctx_.has_project && ctx_.active_kind != DocKind::None;
    bool has_sel = has_doc && !ctx_.selection.empty();
    if (Chord(ImGuiMod_Ctrl | ImGuiKey_N, true)) RequestWithUnsavedCheck(Pending::NewProject);
    if (Chord(ImGuiMod_Ctrl | ImGuiKey_O, true)) RequestWithUnsavedCheck(Pending::OpenProject);
    if (Chord(ImGuiMod_Ctrl | ImGuiKey_S, true)) DoSave(false);
    if (Chord(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_S, true)) DoSave(true);
    if (Chord(ImGuiMod_Ctrl | ImGuiKey_E, true)) DoExport();
    if (Chord(ImGuiKey_F5, true)) ctx_.PushLive(false);
    if (Chord(ImGuiMod_Shift | ImGuiKey_F5, true)) ctx_.PushLive(true);
    if (Chord(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_P, true)) ctx_.SetPickerMode(!ctx_.PickerMode());
    if (Chord(ImGuiMod_Ctrl | ImGuiKey_L)) ctx_.OpenPanel("Library");
    if (Chord(ImGuiMod_Ctrl | ImGuiKey_Z)) ctx_.Undo();
    if (Chord(ImGuiMod_Ctrl | ImGuiKey_Y) || Chord(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z)) ctx_.Redo();
    if (has_sel && Chord(ImGuiKey_Delete)) ctx_.DeleteSelection();
    if (has_sel && Chord(ImGuiMod_Ctrl | ImGuiKey_D)) ctx_.DuplicateSelection();
    if (has_sel && Chord(ImGuiMod_Ctrl | ImGuiKey_C)) ctx_.CopySelection();
    if (has_sel && Chord(ImGuiMod_Ctrl | ImGuiKey_X)) ctx_.CutSelection();
    if (has_doc && Chord(ImGuiMod_Ctrl | ImGuiKey_V)) ctx_.Paste();
}

void App::DrawMainMenuBar(bool& quit) {
    if (!ImGui::BeginMainMenuBar()) return;
    if (ImGui::BeginMenu("File")) {
        DrawFileMenu(quit);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Edit")) {
        DrawEditMenu();
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("View")) {
        DrawViewMenu();
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Project")) {
        DrawProjectMenu();
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Live")) {
        DrawLiveMenu();
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Help")) {
        DrawHelpMenu();
        ImGui::EndMenu();
    }
    if (ctx_.has_project) {
        const char* name = ctx_.project.name.c_str();
        float w = ImGui::CalcTextSize(name).x + (ctx_.dirty ? ImGui::CalcTextSize(" *").x : 0) + ImGui::GetStyle().ItemSpacing.x * 2;
        ImGui::SameLine(ImGui::GetWindowWidth() - w);
        ui::TextDim(ctx_.dirty ? "%s *" : "%s", name);
    }
    ImGui::EndMainMenuBar();
}

void App::DrawFileMenu(bool& quit) {
    (void)quit;
    bool has_project = ctx_.has_project;
    if (ImGui::MenuItem("New Project...", "Ctrl+N")) RequestWithUnsavedCheck(Pending::NewProject);
    if (ImGui::MenuItem("Open...", "Ctrl+O")) RequestWithUnsavedCheck(Pending::OpenProject);
    if (ImGui::BeginMenu("Open Recent", !ctx_.prefs.recent_projects.empty())) {
        std::string picked;
        for (const std::string& r : ctx_.prefs.recent_projects)
            if (ImGui::MenuItem(r.c_str())) picked = r;
        ImGui::Separator();
        if (ImGui::MenuItem("Clear list")) {
            ctx_.prefs.recent_projects.clear();
            SavePrefs(ctx_.prefs);
        }
        ImGui::EndMenu();
        if (!picked.empty()) RequestWithUnsavedCheck(Pending::OpenDir, picked);
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Save", "Ctrl+S", false, has_project)) DoSave(false);
    if (ImGui::MenuItem("Save As...", "Ctrl+Shift+S", false, has_project)) DoSave(true);
    if (ImGui::MenuItem("Close Project", nullptr, false, has_project)) RequestWithUnsavedCheck(Pending::CloseProject);
    ImGui::Separator();
    if (ImGui::MenuItem("Export Code...", "Ctrl+E", false, has_project)) DoExport();
    if (ImGui::MenuItem("Import .imblib...", nullptr, false, has_project)) ctx_.commands.import_imblib = true;
    if (has_project && ImGui::MenuItem("Open Project Folder")) OpenExternal(ctx_.project.dir);
    ImGui::Separator();
    if (ImGui::MenuItem("Exit", "Alt+F4")) ctx_.commands.quit = true;
}

void App::DrawEditMenu() {
    bool has_doc = ctx_.has_project && ctx_.active_kind != DocKind::None;
    bool has_sel = has_doc && !ctx_.selection.empty();
    char label[256];
    std::snprintf(label, sizeof(label), "Undo%s%s", ctx_.undo.CanUndo() ? " " : "", ctx_.UndoLabel());
    if (ImGui::MenuItem(label, "Ctrl+Z", false, ctx_.undo.CanUndo())) ctx_.Undo();
    std::snprintf(label, sizeof(label), "Redo%s%s", ctx_.undo.CanRedo() ? " " : "", ctx_.RedoLabel());
    if (ImGui::MenuItem(label, "Ctrl+Y", false, ctx_.undo.CanRedo())) ctx_.Redo();
    ImGui::Separator();
    if (ImGui::MenuItem("Cut", "Ctrl+X", false, has_sel)) ctx_.CutSelection();
    if (ImGui::MenuItem("Copy", "Ctrl+C", false, has_sel)) ctx_.CopySelection();
    if (ImGui::MenuItem("Paste", "Ctrl+V", false, has_doc && ctx_.CanPaste())) ctx_.Paste();
    if (ImGui::MenuItem("Duplicate", "Ctrl+D", false, has_sel)) ctx_.DuplicateSelection();
    if (ImGui::MenuItem("Delete", "Del", false, has_sel)) ctx_.DeleteSelection();
    ImGui::Separator();
    if (ImGui::MenuItem("Select None", nullptr, false, has_sel)) ctx_.ClearSelection();
    if (ImGui::BeginMenu("History", ctx_.undo.Size() > 0)) {
        const auto& entries = ctx_.undo.Entries();
        for (size_t i = 0; i < entries.size(); ++i) {
            bool done = i < ctx_.undo.Cursor();
            std::snprintf(label, sizeof(label), "%s%s##h%d", done ? "" : "(redo) ", entries[i].label.c_str(), (int)i);
            if (ImGui::MenuItem(label, nullptr, i + 1 == ctx_.undo.Cursor())) {
                while (ctx_.undo.Cursor() > i + 1) ctx_.Undo();
                while (ctx_.undo.Cursor() < i + 1) ctx_.Redo();
            }
        }
        ImGui::EndMenu();
    }
}

void App::DrawViewMenu() {
    for (auto& p : ctx_.panels) {
        if (!p->show_in_view_menu) continue;
        const char* shortcut = std::strcmp(p->Name(), "Library") == 0 ? "Ctrl+L" : nullptr;
        if (ImGui::MenuItem(p->Name(), shortcut, &p->open) && p->open) ImGui::SetWindowFocus(p->Name());
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Reset Layout")) layout_dirty_ = true;
    if (ImGui::BeginMenu("UI Scale")) {
        const float scales[] = {0.75f, 1.0f, 1.25f, 1.5f, 2.0f};
        for (float s : scales) {
            char label[32];
            std::snprintf(label, sizeof(label), "%d%%", (int)(s * 100));
            if (ImGui::MenuItem(label, nullptr, ctx_.prefs.ui_scale == s)) {
                ctx_.prefs.ui_scale = s;
                ctx_.commands.reload_fonts = true;
                SavePrefs(ctx_.prefs);
            }
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Editor Theme")) {
        if (ImGui::MenuItem("Dark", nullptr, ctx_.prefs.theme == "dark")) {
            ctx_.prefs.theme = "dark";
            ApplyStyle();
            SavePrefs(ctx_.prefs);
        }
        if (ImGui::MenuItem("Light", nullptr, ctx_.prefs.theme == "light")) {
            ctx_.prefs.theme = "light";
            ApplyStyle();
            SavePrefs(ctx_.prefs);
        }
        ImGui::EndMenu();
    }
    ImGui::Separator();
    ImGui::MenuItem("ImGui Demo", nullptr, &show_imgui_demo_);
    ImGui::MenuItem("ImGui Metrics", nullptr, &show_metrics_);
}

void App::DrawProjectMenu() {
    bool has_project = ctx_.has_project;
    if (ImGui::MenuItem("Settings...", nullptr, false, has_project)) ctx_.OpenPanel("Project Settings");
    if (ImGui::MenuItem("Themes...", nullptr, false, has_project)) ctx_.OpenPanel("Theme Editor");
    if (ImGui::MenuItem("Style Classes...", nullptr, false, has_project)) ctx_.OpenPanel("Style Classes");
    if (ImGui::MenuItem("Fonts...", nullptr, false, has_project)) ctx_.OpenPanel("Fonts");
    if (ImGui::MenuItem("Components...", nullptr, false, has_project)) ctx_.OpenPanel("Components");
    if (ImGui::MenuItem("Animation Timeline...", nullptr, false, has_project)) ctx_.OpenPanel("Anim Timeline");
    ImGui::Separator();
    if (ImGui::BeginMenu("Panels", has_project)) {
        std::string pick;
        for (const Panel& p : ctx_.project.panels) {
            bool active = ctx_.active_kind == DocKind::Panel && ctx_.active_panel == p.name;
            if (ImGui::MenuItem(p.name.c_str(), nullptr, active)) pick = p.name;
        }
        if (!pick.empty()) ctx_.SetActivePanel(pick);
        ImGui::Separator();
        if (ImGui::MenuItem("New Panel...")) {
            GetPanelFileOps().request_new = true;
            ctx_.OpenPanel("Panels");
        }
        if (ImGui::MenuItem("Manage Panels...")) ctx_.OpenPanel("Panels");
        if (ImGui::MenuItem("Panel Settings...", nullptr, false, ctx_.active_kind == DocKind::Panel)) ctx_.OpenPanel("Panel Settings");
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Edit Component", has_project && !ctx_.project.components.empty())) {
        std::string pick;
        for (const Component& c : ctx_.project.components) {
            bool active = ctx_.active_kind == DocKind::Component && ctx_.active_panel == c.name;
            if (ImGui::MenuItem(c.name.c_str(), nullptr, active)) pick = c.name;
        }
        if (!pick.empty()) ctx_.SetActiveComponent(pick);
        ImGui::EndMenu();
    }
}

void App::DrawLiveMenu() {
    LiveServer& live = ctx_.live;
    bool avail = live.TransportAvailable();
    if (!avail) ui::TextDim("Transport not compiled in");
    if (!live.IsRunning()) {
        if (ImGui::MenuItem("Start Server", nullptr, false, avail && ctx_.has_project)) {
            std::string err;
            if (!live.Start(ctx_.project.live, ctx_.prefs.live_bind_host, &err)) ctx_.Log(LogLevel::Error, "live server: %s", err.c_str());
            else ctx_.Log(LogLevel::Info, "live server listening on %s", live.Url().c_str());
        }
    } else {
        char label[256];
        std::snprintf(label, sizeof(label), "Stop Server (%s)", live.Url().c_str());
        if (ImGui::MenuItem(label)) {
            live.Stop();
            ctx_.Log(LogLevel::Info, "live server stopped");
        }
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Push Panel", "F5", false, live.IsConnected())) ctx_.PushLive(false);
    if (ImGui::MenuItem("Push Everything", "Shift+F5", false, live.IsConnected())) ctx_.PushLive(true);
    bool picker = ctx_.PickerMode();
    if (ImGui::MenuItem("Picker Mode", "Ctrl+Shift+P", &picker, live.IsConnected())) ctx_.SetPickerMode(picker);
    ImGui::Separator();
    if (ImGui::MenuItem("Live Panel")) ctx_.OpenPanel("Live");
    const LiveClientInfo& ci = live.Client();
    if (live.IsConnected()) {
        ui::TextDim("%s (pid %d), ImGui %d, %.0f fps", ci.app.empty() ? "client" : ci.app.c_str(), ci.pid, ci.imgui_version, ci.fps);
    } else {
        ui::TextDim("Status: %s", live.StatusText());
    }
}

void App::DrawHelpMenu() {
    if (ImGui::MenuItem("Documentation")) OpenExternal(kDocsUrl);
    if (ImGui::MenuItem("Keyboard Shortcuts")) {
        ctx_.Log(LogLevel::Info,
                 "shortcuts: Ctrl+N new, Ctrl+O open, Ctrl+S save, Ctrl+Shift+S save as, Ctrl+E export, Ctrl+Z/Y undo/redo, "
                 "Ctrl+X/C/V, Ctrl+D duplicate, Del delete, F5 push panel, Shift+F5 push all, Ctrl+Shift+P picker");
        ctx_.OpenPanel("Log");
    }
    ImGui::Separator();
    if (ImGui::MenuItem("About ImForge")) show_about_ = true;
}

}  // namespace imforge::builder
