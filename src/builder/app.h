#pragma once

#include <string>
#include <vector>

#include "editor_context.h"
#include "platform/platform.h"

namespace imforge::builder {

class App {
public:
    explicit App(Platform& platform);
    ~App();

    bool Init(const char* project_dir);
    void Frame(bool& quit);
    void Shutdown();
    EditorContext& Ctx() { return ctx_; }

private:
    enum class Pending { None, NewProject, OpenProject, OpenDir, CloseProject, Quit };
    struct PathDialog {
        enum class Kind { None, OpenProject, SaveAs, Import, NewProject } kind = Kind::None;
        bool open = false, focus_input = false;
        std::string path, name, error, listed_dir;
        std::vector<std::string> dirs, files;
        std::vector<std::string> templates;
        int template_index = 0;
    };

    void SetupImGui();
    void LoadFonts();
    void ApplyStyle();
    void CheckDpi();
    void RebuildFontsIfDirty();
    void HandleCommands(bool& quit);
    void RequestWithUnsavedCheck(Pending action, const std::string& dir = std::string());
    void Proceed(Pending action, const std::string& dir);
    void DoSave(bool save_as);
    void DoExport();
    void OpenProjectDir(const std::string& dir);
    void StartNewProject();
    void UpdateWindowTitle();

    void DrawMainMenuBar(bool& quit);
    void HandleShortcuts(bool& quit);
    void DrawFileMenu(bool& quit);
    void DrawEditMenu();
    void DrawViewMenu();
    void DrawProjectMenu();
    void DrawLiveMenu();
    void DrawHelpMenu();

    void DrawDockHost();
    void BuildDefaultLayout(unsigned dockspace_id, const ImVec2& size);
    const char* DockAreaFor(const EditorPanel& panel) const;
    void DrawStatusBar();
    void DrawPanels();

    void DrawModals(bool& quit);
    void DrawUnsavedPrompt(bool& quit);
    void OpenPathDialog(PathDialog::Kind kind, const std::string& initial);
    void DrawPathDialog();
    void RefreshPathDialogListing();
    bool AcceptPathDialog();

    Platform& platform_;
    EditorContext ctx_;
    std::string ini_path_;
    std::string window_title_;
    bool imgui_ready_ = false;
    bool layout_dirty_ = false;
    int layout_focus_frames_ = 0;
    bool first_frame_ = true;
    bool show_about_ = false;
    bool show_unsaved_ = false;
    bool quit_confirmed_ = false;
    bool show_imgui_demo_ = false;
    bool show_metrics_ = false;
    float dpi_ = 0.0f;
    Pending pending_ = Pending::None;
    std::string pending_dir_;
    PathDialog path_dialog_;
};

}  // namespace imforge::builder
