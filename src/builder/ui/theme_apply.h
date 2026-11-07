#pragma once

#include <string>
#include <vector>

#include "imgui.h"

#include "imforge/imforge_core.h"

namespace imforge::builder {

struct StyleScope {
    int colors = 0;
    int vars = 0;
};

int ImGuiColIndexByName(const char* name);
int ImGuiStyleVarIndexByName(const char* name, bool* is_vec2 = nullptr);
const char* ResolveColorAlias(const char* name);
const char* ColorGroupOf(const char* name);
const char* const* ColorGroupNames(size_t& count);

bool StyleVarValue(const Json& v, bool vec2, float& f, ImVec2& v2);
Json StyleVarJson(bool vec2, float f, ImVec2 v2);
Json DefaultStyleVarJson(const char* name);

StyleScope PushThemeStyle(const Theme& theme);
StyleScope PushStyleOverride(const StyleOverride& style);
StyleScope PushStyleClasses(const Project& project, const std::vector<std::string>& classes);
void PopStyleScope(const StyleScope& scope);

void ApplyThemeToStyle(const Theme& theme, ImGuiStyle& style);
Theme ThemeFromStyle(const ImGuiStyle& style, const std::string& name);

const Theme* FindThemeByName(const Project& project, const std::string& name);
Theme* FindThemeByName(Project& project, const std::string& name);
const StyleOverride* FindStyleClass(const Project& project, const std::string& name);

}  // namespace imforge::builder
