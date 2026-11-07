#pragma once

#include <string>
#include <vector>

#include "imforge/imforge_core.h"

namespace imforge::builder {

struct EditorContext;

struct LibraryInfo {
    std::string name, version, file, error;
    int min_imgui_version = 18500;
    std::vector<std::string> components, widgets, headers, dependencies;
    bool valid = false;
};

struct ImblibImportResult {
    std::string library;
    std::vector<std::string> components, widgets, files, warnings;
};

struct ImblibExportOptions {
    std::string name, version = "1.0.0";
    int min_imgui_version = 18500;
    std::vector<std::string> component_names, widget_types, header_files;
};

namespace imblib {

bool Import(Project& project, WidgetCatalog* catalog, const std::string& path, std::string* err,
            ImblibImportResult* result = nullptr);
bool Export(const Project& project, const WidgetCatalog* catalog, const ImblibExportOptions& opts,
            const std::string& out_path, std::string* err, std::vector<std::string>* warnings = nullptr);
bool BuildArchive(const Project& project, const WidgetCatalog* catalog, const ImblibExportOptions& opts,
                  std::string& bytes, std::string* err, std::vector<std::string>* warnings = nullptr);
bool ReadLibraryInfo(const std::string& path, LibraryInfo& out);
std::string LibraryNameFromPath(const std::string& path);
std::string SanitizeLibraryName(const std::string& name);

}  // namespace imblib

bool ImportImblib(EditorContext& ctx, const std::string& path, std::string* err);
bool ExportImblib(EditorContext& ctx, const std::vector<std::string>& component_names,
                  const std::vector<std::string>& widget_types, const std::string& out_path, std::string* err);
std::vector<LibraryInfo> ListLibraries(const Project& project);

void OpenImblibImportDialog(EditorContext& ctx);
void OpenImblibExportDialog(EditorContext& ctx, const std::vector<std::string>& preselect_components);
void DrawImblibDialogs(EditorContext& ctx);

}  // namespace imforge::builder
