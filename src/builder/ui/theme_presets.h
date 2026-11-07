#pragma once

#include <string>
#include <vector>

#include "imforge/imforge_core.h"

namespace imforge::builder {

struct ThemePresetInfo {
    const char* name;
    const char* description;
    unsigned swatch[4];
};

const std::vector<ThemePresetInfo>& ThemePresets();
const ThemePresetInfo* FindThemePreset(const std::string& name);
Theme MakeThemePreset(const std::string& name);
const char* const* ThemeStyleVarNames(size_t& count, const bool** is_vec2);
bool ParseThemeFromCode(const std::string& code, Theme& out, std::string* err, int* colors_found = nullptr,
                        int* vars_found = nullptr);

}  // namespace imforge::builder
