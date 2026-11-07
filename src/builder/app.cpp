#include "app.h"

#include <cmath>
#include <cstdio>
#include <filesystem>

#include "builder_fonts.h"
#include "builder_style.h"
#include "export.h"
#include "imforge/imforge_version.h"
#include "live_bridge.h"
#include "panels/dialogs.h"
#include "panels/wizard.h"
#include "project_io.h"
#include "ui/anim_preview_driver.h"
#include "ui/fonts_state.h"
#include "ui/project_fonts.h"

namespace imforge::builder {

namespace fs = std::filesystem;

App::App(Platform& platform) : platform_(platform) {}

App::~App() { Shutdown(); }

void App::SetupImGui() {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
#ifdef IMGUI_HAS_DOCK
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
    io.ConfigViewportsNoAutoMerge = false;
    io.ConfigViewportsNoTaskBarIcon = true;
    io.ConfigDockingWithShift = false;
#endif
    io.ConfigWindowsMoveFromTitleBarOnly = true;
#if IMGUI_VERSION_NUM >= 19000
    io.ConfigDebugIsDebuggerPresent = false;
#endif
    ini_path_ = ImGuiIniPath();
    io.IniFilename = ini_path_.c_str();
    imgui_ready_ = true;
}

void App::LoadFonts() {
    float scale = dpi_ > 0 ? dpi_ : 1.0f;
    bool ok = LoadBuilderFonts(scale);
    Fonts& f = GetFonts();
    ctx_.fonts[(int)FontSlot::Ui] = f.ui;
    ctx_.fonts[(int)FontSlot::Heading] = f.heading;
    ctx_.fonts[(int)FontSlot::Mono] = f.mono;
    for (const std::string& w : f.warnings) ctx_.Log(LogLevel::Warning, "fonts: %s", w.c_str());
    if (ok) ctx_.Log(LogLevel::Info, "fonts loaded from %s (scale %.2f)", f.dir.c_str(), scale);
}

void App::ApplyStyle() { ApplyBuilderStyle(ctx_.prefs.theme, ctx_.prefs.accent, dpi_ > 0 ? dpi_ : 1.0f); }

bool App::Init(const char* project_dir) {
    std::string prefs_err;
    if (!LoadPrefs(ctx_.prefs, &prefs_err)) ctx_.Log(LogLevel::Debug, "prefs: %s (using defaults)", prefs_err.c_str());
    SetupImGui();
    if (!platform_.Init("ImForge", ctx_.prefs.window_w, ctx_.prefs.window_h)) {
        std::fprintf(stderr, "ImForge: platform init failed (%s)\n", platform_.Name());
        return false;
    }
    if (ctx_.prefs.window_maximized) platform_.SetMaximized(true);
    dpi_ = platform_.DpiScale() * ctx_.prefs.ui_scale;
    ctx_.dpi_scale = dpi_;
    LoadFonts();
    ApplyStyle();
    ctx_.Log(LogLevel::Info, "ImForge %s, %s, Dear ImGui %s, config %s", IMFORGE_VERSION, platform_.Name(), IMGUI_VERSION,
             ConfigDir().c_str());
    ctx_.project.widget_catalog = &ctx_.catalog;
    ctx_.CreateRegisteredPanels();
    GlobalLiveBridge().Attach(ctx_);
#if IMFORGE_HAS_INTERP
    ctx_.interp.hooks.force_trigger = [](const Node& node, const char* trigger, bool& forced) {
        int state = AnimPreviewForcedState(node, trigger);
        if (state < 0) return false;
        forced = state != 0;
        return true;
    };
#endif
    if (!ctx_.live.TransportAvailable())
        ctx_.Log(LogLevel::Warning, "live attach transport not compiled in (imforge_transport.h missing at build time)");
    std::error_code ec;
    layout_dirty_ = !fs::exists(ini_path_, ec);

    std::string dir = project_dir ? project_dir : "";
    if (dir.empty() && ctx_.prefs.open_last_project && !ctx_.prefs.recent_projects.empty() &&
        IsProjectDir(ctx_.prefs.recent_projects.front()))
        dir = ctx_.prefs.recent_projects.front();
    if (!dir.empty()) {
        std::string err;
        if (!ctx_.LoadProjectDir(dir, &err)) {
            ctx_.SetStatus("Could not open " + dir + ": " + err, 8.0);
            if (project_dir && !IsProjectDir(dir)) ctx_.Log(LogLevel::Error, "%s is not an ImForge project directory", dir.c_str());
        }
    }
    if (!ctx_.has_project && ctx_.prefs.show_wizard_on_start) {
        if (EditorPanel* w = ctx_.FindPanel("Wizard")) w->open = true;
    } else if (EditorPanel* w = ctx_.FindPanel("Wizard")) {
        w->open = false;
    }
    UpdateWindowTitle();
    return true;
}

void App::Shutdown() {
    if (!imgui_ready_) return;
    int w = 0, h = 0;
    platform_.GetWindowSize(w, h);
    if (w > 0 && h > 0 && !platform_.IsMaximized()) {
        ctx_.prefs.window_w = w;
        ctx_.prefs.window_h = h;
    }
    ctx_.prefs.window_maximized = platform_.IsMaximized();
    SavePrefs(ctx_.prefs);
    ctx_.live.Stop();
    ctx_.panels.clear();
    platform_.Shutdown();
    ImGui::DestroyContext();
    imgui_ready_ = false;
}

void App::CheckDpi() {
    float target = platform_.DpiScale() * ctx_.prefs.ui_scale;
    if (target <= 0.0f) target = 1.0f;
    if (std::fabs(target - dpi_) < 0.01f && !ctx_.commands.reload_fonts) return;
    ctx_.commands.reload_fonts = false;
    dpi_ = target;
    ctx_.dpi_scale = dpi_;
    LoadFonts();
    FontsDirtyFlag() = true;
    ApplyStyle();
    ctx_.Log(LogLevel::Info, "display scale changed to %.2f; fonts rebuilt", dpi_);
}

void App::RebuildFontsIfDirty() {
    if (!FontsDirtyFlag()) return;
    RebuildProjectFonts(ctx_);
    platform_.RebuildFontAtlas();
    FontsDirtyFlag() = false;
}

void App::UpdateWindowTitle() {
    std::string t = ctx_.WindowTitle();
    if (t == window_title_) return;
    window_title_ = t;
    platform_.SetTitle(t.c_str());
}

void App::Frame(bool& quit) {
    if (quit && !quit_confirmed_) {
        quit = false;
        RequestWithUnsavedCheck(Pending::Quit);
    }
    CheckDpi();
    RebuildFontsIfDirty();
    platform_.BeginFrame();
    ctx_.live.Poll(ctx_);
    ctx_.BeginFrame();
    GlobalLiveBridge().Update(ctx_);
    HandleCommands(quit);
    HandleShortcuts(quit);
    DrawMainMenuBar(quit);
    DrawStatusBar();
    DrawDockHost();
    DrawPanels();
    DrawModals(quit);
    if (show_imgui_demo_) ImGui::ShowDemoWindow(&show_imgui_demo_);
    if (show_metrics_) ImGui::ShowMetricsWindow(&show_metrics_);
    UpdateWindowTitle();
    const ImVec4 clear = Colors().bg_window;
    platform_.EndFrame(ImVec4(clear.x, clear.y, clear.z, 1.0f));
    first_frame_ = false;
    if (quit_confirmed_) quit = true;
}

void App::HandleCommands(bool& quit) {
    Commands c = ctx_.commands;
    ctx_.commands = Commands();
    if (c.new_project) RequestWithUnsavedCheck(Pending::NewProject);
    if (c.open_project) {
        if (!c.open_dir.empty()) RequestWithUnsavedCheck(Pending::OpenDir, c.open_dir);
        else RequestWithUnsavedCheck(Pending::OpenProject);
    }
    if (c.save) DoSave(false);
    if (c.save_as) DoSave(true);
    if (c.export_code) DoExport();
    if (c.import_imblib) {
        if (!c.import_path.empty()) {
            std::string err;
            if (!ctx_.ImportImblib(c.import_path, &err)) ctx_.SetStatus("Import failed: " + err, 8.0);
        } else {
            OpenPathDialog(PathDialog::Kind::Import, ctx_.has_project ? ctx_.project.dir : HomeDirOrCwd());
        }
    }
    if (c.reset_layout) layout_dirty_ = true;
    if (c.show_about) show_about_ = true;
    if (c.quit) {
        quit = false;
        RequestWithUnsavedCheck(Pending::Quit);
    }
}

void App::RequestWithUnsavedCheck(Pending action, const std::string& dir) {
    pending_ = action;
    pending_dir_ = dir;
    if (ctx_.has_project && ctx_.dirty) {
        show_unsaved_ = true;
        return;
    }
    Proceed(action, dir);
}

void App::Proceed(Pending action, const std::string& dir) {
    pending_ = Pending::None;
    switch (action) {
    case Pending::NewProject: StartNewProject(); break;
    case Pending::OpenProject:
        OpenPathDialog(PathDialog::Kind::OpenProject, ctx_.has_project ? fs::path(ctx_.project.dir).parent_path().string() : HomeDirOrCwd());
        break;
    case Pending::OpenDir: OpenProjectDir(dir); break;
    case Pending::CloseProject: ctx_.CloseProject(); break;
    case Pending::Quit: quit_confirmed_ = true; break;
    default: break;
    }
}

void App::StartNewProject() {
    if (ctx_.FindPanel("Wizard")) {
        OpenWizard();
        return;
    }
    OpenPathDialog(PathDialog::Kind::NewProject, ctx_.has_project ? fs::path(ctx_.project.dir).parent_path().string() : HomeDirOrCwd());
}

void App::OpenProjectDir(const std::string& dir) {
    std::string err;
    if (ctx_.LoadProjectDir(dir, &err)) {
        if (EditorPanel* w = ctx_.FindPanel("Wizard")) w->open = false;
        return;
    }
    ctx_.SetStatus("Could not open " + dir + ": " + err, 8.0);
    if (!IsProjectDir(dir)) RemoveRecentProject(ctx_.prefs, dir);
}

void App::DoSave(bool save_as) {
    if (!ctx_.has_project) {
        ctx_.SetStatus("No project open");
        return;
    }
    if (save_as) {
        OpenPathDialog(PathDialog::Kind::SaveAs, ctx_.project.dir);
        return;
    }
    std::string err;
    if (!ctx_.SaveProject(&err)) ctx_.SetStatus("Save failed: " + err, 8.0);
}

void App::DoExport() {
    if (!ctx_.has_project) {
        ctx_.SetStatus("No project open");
        return;
    }
    OpenExportDialog(ctx_);
}

}  // namespace imforge::builder
