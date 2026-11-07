#pragma once

#include <string>
#include <vector>

#include "imforge/imforge_core.h"

#include "../editor_context.h"

namespace imforge::builder::wizard {

struct LibraryEntry {
    std::string name, path, description, reason;
    int min_version = 0;
    bool importable = false;
    bool selected = false;
};

struct SettingsForm {
    Project* project = nullptr;
    std::string custom_version;
    std::string detect_path, detect_message;
    bool detect_ok = false;
    std::string types_text, include_paths_text;
    bool skip_bindings = false;
    std::vector<LibraryEntry> libraries;
    bool libraries_scanned = false;
    std::string theme_code, theme_message;
    int theme_choice = -1;
    Theme imported_theme;
    bool has_imported = false;
    std::string snippet, snippet_key;

    void Attach(Project& p);
    void SyncTexts();
    void ApplyTexts();
    Theme ChosenTheme() const;
};

const char* const* ImGuiVersionChoices(int& count);
int VersionChoiceIndex(const std::string& version);
bool IsIdentifier(const std::string& s);
bool WindowRendererCompatible(const std::string& window, const std::string& renderer);
const char* IncompatibilityReason(const std::string& window, const std::string& renderer);
bool DetectImGuiHeader(const std::string& path, std::string& version, bool& docking, std::string& message);
void ScanLibraries(SettingsForm& form);
std::vector<std::string> SplitList(const std::string& text, const char* separators);

bool DrawTargetStep(SettingsForm& form, EditorContext& ctx);
bool DrawPlatformStep(SettingsForm& form, EditorContext& ctx);
bool DrawFeaturesStep(SettingsForm& form, EditorContext& ctx);
bool DrawBindingsStep(SettingsForm& form, EditorContext& ctx);
bool DrawLiveStep(SettingsForm& form, EditorContext& ctx);
bool DrawCodegenStep(SettingsForm& form, EditorContext& ctx);
bool DrawThemeStep(SettingsForm& form, EditorContext& ctx, bool in_wizard);
void DrawCodeBox(const char* id, const std::string& text, EditorContext& ctx, float height);

bool ValidateTarget(const SettingsForm& form, std::string& msg);
bool ValidatePlatform(const SettingsForm& form, std::string& msg);
bool ValidateBindings(const SettingsForm& form, std::string& msg);
bool ValidateLive(const SettingsForm& form, std::string& msg);
bool ValidateCodegen(const SettingsForm& form, std::string& msg);

}  // namespace imforge::builder::wizard
