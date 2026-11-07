#pragma once

#include <string>

#include "imgui.h"

namespace imforge::builder {

struct BuilderColors {
    ImVec4 accent, accent_hover, accent_active, accent_dim;
    ImVec4 bg_window, bg_panel, bg_frame, bg_hover, bg_active;
    ImVec4 text, text_dim, text_disabled;
    ImVec4 border, separator;
    ImVec4 success, warning, error, info;
    ImVec4 selection, selection_border, hover_outline;
};

const BuilderColors& Colors();
void ApplyBuilderStyle(const std::string& theme, const std::string& accent_hex, float scale);
void PushDimText();
void PopDimText();
ImU32 ToU32(const ImVec4& c, float alpha_mul = 1.0f);
ImVec4 WithAlpha(const ImVec4& c, float a);

}  // namespace imforge::builder
