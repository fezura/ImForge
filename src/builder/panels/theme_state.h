#pragma once

namespace imforge {
namespace builder {

struct ThemePreviewState {
    bool apply_to_canvas = true;
};

inline ThemePreviewState& GetThemePreviewState() {
    static ThemePreviewState state;
    return state;
}

}  // namespace builder
}  // namespace imforge
