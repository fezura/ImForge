#pragma once

#include <string>
#include <vector>

#include "imgui.h"

#include "imforge/imforge_core.h"

namespace imforge::builder {

struct EditorContext;

struct LoadedProjectFont {
    std::string name, file, resolved, error;
    float size = 0;
    ImFont* font = nullptr;
    bool icons_merged = false;
};

ImFont* FindProjectFont(const std::string& name);
const LoadedProjectFont* FindLoadedProjectFont(const std::string& name);
const std::vector<LoadedProjectFont>& LoadedProjectFonts();

void AddProjectFontsToAtlas(const Project& project, ImFontAtlas* atlas, float scale);
bool RebuildProjectFonts(EditorContext& ctx);

std::string ResolveFontFile(const std::string& file, const std::string& project_dir);
std::string ResolveIconFontFile(const std::string& merge_icons, const std::string& project_dir);
bool LooksLikeFontFile(const std::string& path);

const ImWchar* GlyphRangesByName(const std::string& ranges, ImFontAtlas* atlas);
const ImWchar* IconRangesFor(const std::string& merge_icons);
const char* const* GlyphRangeNames(size_t& count);
const char* const* MergeIconNames(size_t& count);

}  // namespace imforge::builder
