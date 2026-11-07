#include "builder_style.h"

#include <cmath>
#include <cstdlib>

namespace imforge::builder {

namespace {

BuilderColors& MutableColors() {
    static BuilderColors c;
    return c;
}

ImVec4 Hex(const std::string& hex, ImVec4 fallback) {
    if (hex.size() != 7 && hex.size() != 9) return fallback;
    if (hex[0] != '#') return fallback;
    unsigned v[4] = {0, 0, 0, 255};
    for (size_t i = 1; i + 1 < hex.size(); i += 2) {
        char buf[3] = {hex[i], hex[i + 1], 0};
        char* end = nullptr;
        unsigned long x = std::strtoul(buf, &end, 16);
        if (!end || *end) return fallback;
        v[(i - 1) / 2] = (unsigned)x;
    }
    return ImVec4(v[0] / 255.0f, v[1] / 255.0f, v[2] / 255.0f, v[3] / 255.0f);
}

ImVec4 Mix(const ImVec4& a, const ImVec4& b, float t) {
    return ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t);
}

ImVec4 Lighten(const ImVec4& c, float t) { return Mix(c, ImVec4(1, 1, 1, c.w), t); }
ImVec4 Darken(const ImVec4& c, float t) { return Mix(c, ImVec4(0, 0, 0, c.w), t); }

void FillDark(BuilderColors& c, const ImVec4& accent) {
    c.accent = accent;
    c.accent_hover = Lighten(accent, 0.12f);
    c.accent_active = Darken(accent, 0.12f);
    c.accent_dim = WithAlpha(accent, 0.35f);
    c.bg_window = ImVec4(0.094f, 0.098f, 0.114f, 1.0f);
    c.bg_panel = ImVec4(0.125f, 0.131f, 0.153f, 1.0f);
    c.bg_frame = ImVec4(0.165f, 0.173f, 0.200f, 1.0f);
    c.bg_hover = ImVec4(0.215f, 0.225f, 0.260f, 1.0f);
    c.bg_active = ImVec4(0.255f, 0.267f, 0.310f, 1.0f);
    c.text = ImVec4(0.90f, 0.91f, 0.93f, 1.0f);
    c.text_dim = ImVec4(0.58f, 0.60f, 0.66f, 1.0f);
    c.text_disabled = ImVec4(0.40f, 0.42f, 0.47f, 1.0f);
    c.border = ImVec4(0.0f, 0.0f, 0.0f, 0.45f);
    c.separator = ImVec4(0.22f, 0.23f, 0.27f, 1.0f);
    c.success = ImVec4(0.30f, 0.78f, 0.47f, 1.0f);
    c.warning = ImVec4(0.95f, 0.70f, 0.25f, 1.0f);
    c.error = ImVec4(0.92f, 0.33f, 0.33f, 1.0f);
    c.info = ImVec4(0.45f, 0.70f, 0.95f, 1.0f);
    c.selection = WithAlpha(accent, 0.22f);
    c.selection_border = accent;
    c.hover_outline = WithAlpha(Lighten(accent, 0.3f), 0.8f);
}

void FillLight(BuilderColors& c, const ImVec4& accent) {
    FillDark(c, accent);
    c.bg_window = ImVec4(0.93f, 0.94f, 0.95f, 1.0f);
    c.bg_panel = ImVec4(0.98f, 0.98f, 0.99f, 1.0f);
    c.bg_frame = ImVec4(0.88f, 0.89f, 0.91f, 1.0f);
    c.bg_hover = ImVec4(0.82f, 0.84f, 0.88f, 1.0f);
    c.bg_active = ImVec4(0.76f, 0.78f, 0.84f, 1.0f);
    c.text = ImVec4(0.12f, 0.13f, 0.15f, 1.0f);
    c.text_dim = ImVec4(0.42f, 0.44f, 0.50f, 1.0f);
    c.text_disabled = ImVec4(0.60f, 0.62f, 0.66f, 1.0f);
    c.border = ImVec4(0.0f, 0.0f, 0.0f, 0.18f);
    c.separator = ImVec4(0.78f, 0.80f, 0.84f, 1.0f);
}

}  // namespace

const BuilderColors& Colors() { return MutableColors(); }

ImU32 ToU32(const ImVec4& c, float alpha_mul) { return ImGui::GetColorU32(ImVec4(c.x, c.y, c.z, c.w * alpha_mul)); }
ImVec4 WithAlpha(const ImVec4& c, float a) { return ImVec4(c.x, c.y, c.z, a); }

void PushDimText() { ImGui::PushStyleColor(ImGuiCol_Text, Colors().text_dim); }
void PopDimText() { ImGui::PopStyleColor(); }

void ApplyBuilderStyle(const std::string& theme, const std::string& accent_hex, float scale) {
    BuilderColors& c = MutableColors();
    ImVec4 accent = Hex(accent_hex, ImVec4(0.31f, 0.55f, 1.0f, 1.0f));
    bool light = theme == "light";
    if (light) FillLight(c, accent);
    else FillDark(c, accent);

    ImGuiStyle& s = ImGui::GetStyle();
    s = ImGuiStyle();
    s.WindowPadding = ImVec2(10, 8);
    s.FramePadding = ImVec2(8, 5);
    s.CellPadding = ImVec2(6, 3);
    s.ItemSpacing = ImVec2(8, 6);
    s.ItemInnerSpacing = ImVec2(6, 4);
    s.IndentSpacing = 18;
    s.ScrollbarSize = 12;
    s.GrabMinSize = 10;
    s.WindowBorderSize = 1;
    s.ChildBorderSize = 1;
    s.PopupBorderSize = 1;
    s.FrameBorderSize = 0;
    s.TabBorderSize = 0;
    s.WindowRounding = 6;
    s.ChildRounding = 4;
    s.FrameRounding = 4;
    s.PopupRounding = 6;
    s.ScrollbarRounding = 8;
    s.GrabRounding = 4;
    s.TabRounding = 5;
    s.WindowTitleAlign = ImVec2(0.0f, 0.5f);
    s.WindowMenuButtonPosition = ImGuiDir_None;
    s.SeparatorTextBorderSize = 1;
    s.SeparatorTextPadding = ImVec2(16, 4);
    s.DisabledAlpha = 0.5f;
#ifdef IMGUI_HAS_DOCK
    s.DockingSeparatorSize = 2;
#endif
#if IMGUI_VERSION_NUM >= 19190
    s.TabCloseButtonMinWidthSelected = 0.0f;
    s.TabCloseButtonMinWidthUnselected = 0.0f;
#endif
#if IMGUI_VERSION_NUM >= 19110
    s.TabBarOverlineSize = 2.0f;
#endif

    ImVec4* col = s.Colors;
    col[ImGuiCol_Text] = c.text;
    col[ImGuiCol_TextDisabled] = c.text_disabled;
    col[ImGuiCol_WindowBg] = c.bg_panel;
    col[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
    col[ImGuiCol_PopupBg] = Lighten(c.bg_panel, light ? 0.0f : 0.03f);
    col[ImGuiCol_Border] = c.border;
    col[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
    col[ImGuiCol_FrameBg] = c.bg_frame;
    col[ImGuiCol_FrameBgHovered] = c.bg_hover;
    col[ImGuiCol_FrameBgActive] = c.bg_active;
    col[ImGuiCol_TitleBg] = c.bg_window;
    col[ImGuiCol_TitleBgActive] = c.bg_window;
    col[ImGuiCol_TitleBgCollapsed] = c.bg_window;
    col[ImGuiCol_MenuBarBg] = c.bg_window;
    col[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
    col[ImGuiCol_ScrollbarGrab] = c.bg_hover;
    col[ImGuiCol_ScrollbarGrabHovered] = c.bg_active;
    col[ImGuiCol_ScrollbarGrabActive] = c.accent_dim;
    col[ImGuiCol_CheckMark] = c.accent;
    col[ImGuiCol_SliderGrab] = c.accent;
    col[ImGuiCol_SliderGrabActive] = c.accent_hover;
    col[ImGuiCol_Button] = c.bg_frame;
    col[ImGuiCol_ButtonHovered] = c.bg_hover;
    col[ImGuiCol_ButtonActive] = c.accent_dim;
    col[ImGuiCol_Header] = c.selection;
    col[ImGuiCol_HeaderHovered] = WithAlpha(c.accent, 0.32f);
    col[ImGuiCol_HeaderActive] = WithAlpha(c.accent, 0.45f);
    col[ImGuiCol_Separator] = c.separator;
    col[ImGuiCol_SeparatorHovered] = c.accent_dim;
    col[ImGuiCol_SeparatorActive] = c.accent;
    col[ImGuiCol_ResizeGrip] = ImVec4(0, 0, 0, 0);
    col[ImGuiCol_ResizeGripHovered] = c.accent_dim;
    col[ImGuiCol_ResizeGripActive] = c.accent;
    col[ImGuiCol_Tab] = c.bg_window;
    col[ImGuiCol_TabHovered] = c.bg_hover;
#if IMGUI_VERSION_NUM >= 19104
    col[ImGuiCol_TabSelected] = c.bg_panel;
    col[ImGuiCol_TabSelectedOverline] = c.accent;
    col[ImGuiCol_TabDimmed] = c.bg_window;
    col[ImGuiCol_TabDimmedSelected] = c.bg_panel;
    col[ImGuiCol_TabDimmedSelectedOverline] = c.accent_dim;
#else
    col[ImGuiCol_TabActive] = c.bg_panel;
    col[ImGuiCol_TabUnfocused] = c.bg_window;
    col[ImGuiCol_TabUnfocusedActive] = c.bg_panel;
#endif
#ifdef IMGUI_HAS_DOCK
    col[ImGuiCol_DockingPreview] = WithAlpha(c.accent, 0.6f);
    col[ImGuiCol_DockingEmptyBg] = c.bg_window;
#endif
    col[ImGuiCol_PlotLines] = c.accent;
    col[ImGuiCol_PlotLinesHovered] = c.accent_hover;
    col[ImGuiCol_PlotHistogram] = c.accent;
    col[ImGuiCol_PlotHistogramHovered] = c.accent_hover;
    col[ImGuiCol_TableHeaderBg] = c.bg_window;
    col[ImGuiCol_TableBorderStrong] = c.separator;
    col[ImGuiCol_TableBorderLight] = WithAlpha(c.separator, 0.5f);
    col[ImGuiCol_TableRowBg] = ImVec4(0, 0, 0, 0);
    col[ImGuiCol_TableRowBgAlt] = WithAlpha(c.text, 0.025f);
    col[ImGuiCol_TextSelectedBg] = WithAlpha(c.accent, 0.35f);
    col[ImGuiCol_DragDropTarget] = c.accent;
    col[ImGuiCol_NavWindowingHighlight] = WithAlpha(c.accent, 0.7f);
    col[ImGuiCol_NavWindowingDimBg] = ImVec4(0, 0, 0, 0.4f);
    col[ImGuiCol_ModalWindowDimBg] = ImVec4(0, 0, 0, 0.55f);
#if IMGUI_VERSION_NUM >= 19110
    col[ImGuiCol_NavCursor] = c.accent;
    col[ImGuiCol_TextLink] = c.accent;
#else
    col[ImGuiCol_NavHighlight] = c.accent;
#endif
    if (scale > 0 && std::fabs(scale - 1.0f) > 0.01f) s.ScaleAllSizes(scale);
}

}  // namespace imforge::builder
