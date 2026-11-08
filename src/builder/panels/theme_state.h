#pragma once

namespace imforge {
struct Theme;
namespace builder {

struct ThemePreviewState {
    bool apply_to_canvas = true;
    const Theme* override_theme = nullptr;  // temporary canvas theme (Library design preview)
};

inline ThemePreviewState& GetThemePreviewState() {
    static ThemePreviewState state;
    return state;
}

}  // namespace builder
}  // namespace imforge
