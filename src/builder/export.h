#pragma once

#include <string>
#include <vector>

#include "imforge/imforge_core.h"

#if __has_include("codegen.h")
#include "codegen.h"
#define IMFORGE_EXPORT_HAS_CODEGEN 1
#else
#define IMFORGE_EXPORT_HAS_CODEGEN 0
#endif

#if __has_include("roundtrip.h")
#include "roundtrip.h"
#define IMFORGE_EXPORT_HAS_ROUNDTRIP 1
#else
#define IMFORGE_EXPORT_HAS_ROUNDTRIP 0
#endif

namespace imforge::builder {

struct EditorContext;

#if IMFORGE_EXPORT_HAS_CODEGEN
codegen::Options MakeCodegenOptions(const Project& project);
#endif

bool CodegenAvailable();
bool RoundtripAvailable();
std::string ExportOutputDir(const Project& project);
std::string FileMarkerValue(const std::string& content, const char* key);
bool HasFileMarker(const std::string& content);
std::string Fnv1a32HexOf(const std::string& text);
bool WriteFileAtomic(const std::string& path, const std::string& content, std::string* err);

struct ExportPreviewEntry {
    std::string rel, abs;
    bool exists = false, unmarked = false, changed = false;
};
std::vector<ExportPreviewEntry> PreviewExport(EditorContext& ctx, std::vector<Diagnostic>* diagnostics = nullptr);

bool ExportProject(EditorContext& ctx, std::string* err, bool overwrite_unmarked = false);
bool ImportFromCode(EditorContext& ctx, const std::string& cpp_path, std::string* err = nullptr);
bool ResolveConflict(EditorContext& ctx, size_t index, bool keep_user_code, std::string* err);

bool ExportSelfCheck(const std::string& project_dir, const std::string& out_dir, std::string& report);

void OpenExportDialog(EditorContext& ctx);
void ShowExportDialog(EditorContext& ctx);
void RequestOverwriteConfirm(EditorContext& ctx, const std::vector<std::string>& files);

}  // namespace imforge::builder
