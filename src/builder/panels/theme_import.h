#pragma once

#include <string>
#include <vector>

#include "imgui.h"

#include "imforge/imforge_core.h"

namespace imforge::builder {

struct ThemeImportResult {
    int colors = 0;
    int vars = 0;
    std::string base;
    std::vector<std::string> warnings;
};

ThemeImportResult ImportThemeFromCode(const std::string& code, Theme& theme, bool replace_existing);
std::string ExportThemeAsCode(const Theme& theme, const std::string& function_name);
std::string ExportThemeText(const Project& project, const Theme& theme);
std::string ThemeFunctionName(const std::string& theme_name);

}  // namespace imforge::builder
