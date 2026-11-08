#pragma once

#include <string>
#include <vector>

#include "imforge/imforge_core.h"

namespace imforge::builder {

enum ApplyMode : unsigned {
    kApplyTheme = 1u << 0,
    kApplyClasses = 1u << 1,
    kApplyFonts = 1u << 2,
    kApplyFx = 1u << 3,
    kApplyMotions = 1u << 4,
    kApplyThemeOnly = kApplyTheme,
    kApplyThemeAndClasses = kApplyTheme | kApplyClasses,
    kApplyEverything = kApplyTheme | kApplyClasses | kApplyFonts | kApplyFx | kApplyMotions,
};

struct DesignApplyResult {
    std::string theme;
    int classes = 0, fonts = 0, fx = 0, motions = 0;
    std::vector<std::string> warnings;
};

bool ThemeFromDesign(const Json& design, Theme& out, Error* err = nullptr);
std::string DesignName(const Json& design);
std::string DesignTitle(const Json& design);
std::string DesignFontFile(const std::string& family, const std::string& weight);

// Theme becomes the active theme (replacing one with the same name), style classes are merged by
// name, fonts are matched by role. Motions and fx touch panels only, never components.
// The caller wraps this in one undo step (EditorContext::EditProject).
bool ApplyDesign(Project& project, const Json& design, unsigned mode, DesignApplyResult* result = nullptr,
                 Error* err = nullptr);

}  // namespace imforge::builder
