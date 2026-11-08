#pragma once

#include <string>
#include <vector>

#include "imforge/imforge_core.h"

namespace imforge::builder {

struct ExportConflict;

struct ExportReport {
    std::vector<std::string> written;
    std::vector<std::string> unchanged;
    std::vector<Diagnostic> diagnostics;
    std::vector<ExportConflict> conflicts;
    bool codegen_available = false;
    bool roundtrip_available = false;
};

bool ReadTextFile(const std::string& path, std::string& out, Error* err = nullptr);
bool WriteTextFile(const std::string& path, const std::string& content, Error* err = nullptr,
                   bool* changed = nullptr);
bool ReadJsonFile(const std::string& path, Json& out, Error* err = nullptr);
bool WriteJsonFile(const std::string& path, const Json& j, Error* err = nullptr, bool* changed = nullptr);
std::string JoinPath(const std::string& a, const std::string& b);
std::string NormalizePath(const std::string& p);
std::string FileStem(const std::string& path);
bool IsProjectDir(const std::string& dir);
bool IsSafeRelativePath(const std::string& rel);
std::string SanitizeFileStem(const std::string& name);
bool PathIsInside(const std::string& base, const std::string& path);
std::string SafeJoin(const std::string& base, const std::string& rel);
void ApproveExternalOutputDir(const std::string& dir);
bool OutputDirAllowed(const Project& project, const std::string& out_dir);
std::string DefaultPanelFile(const std::string& name);
std::string DefaultComponentFile(const std::string& name);
std::string DefaultThemeFile(const std::string& name);
std::string DefaultAnimationFile(const std::string& name);

bool LoadProject(const std::string& dir, Project& out, Error* err);
bool SaveProject(const Project& project, Error* err, std::vector<std::string>* written = nullptr);
bool NewProjectFromTemplate(const std::string& template_dir, const std::string& dest_dir, const std::string& name,
                            const Project& settings, Error* err);
bool ExportCode(Project& project, Error* err, ExportReport* report = nullptr);
bool ExportImblib(const Project& project, const std::string& imblib_path, const std::string& lib_name,
                  const std::vector<std::string>& component_names, Error* err);
std::string DefaultTemplatesDir();
std::vector<std::string> ListTemplates(const std::string& templates_dir);
void ApplyProjectSettings(Project& dst, const Project& settings);
Project SettingsFromTemplate(const std::string& template_dir, Error* err = nullptr);
Panel MakeDefaultPanel(const std::string& name);
Theme MakeDefaultTheme(const std::string& name);

}  // namespace imforge::builder
