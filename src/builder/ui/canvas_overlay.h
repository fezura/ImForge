#pragma once

#include "imgui.h"

namespace imforge::builder::overlay {

void HoverOutline(ImDrawList* dl, const ImVec2& mn, const ImVec2& mx, ImU32 col, float thickness = 1.0f);
void SelectionOutline(ImDrawList* dl, const ImVec2& mn, const ImVec2& mx, ImU32 col, ImU32 handle_fill,
                      float thickness = 2.0f, float handle = 6.0f);
void DashedRect(ImDrawList* dl, const ImVec2& mn, const ImVec2& mx, ImU32 col, float dash = 4.0f, float gap = 3.0f,
                float thickness = 1.0f);
void DashedLine(ImDrawList* dl, const ImVec2& a, const ImVec2& b, ImU32 col, float dash, float gap, float thickness);
ImVec2 LabelSize(const char* text, const ImVec2& pad);
void Label(ImDrawList* dl, const ImVec2& pos, const char* text, ImU32 bg, ImU32 fg, const ImVec2& pad,
           float rounding);
void DottedGrid(ImDrawList* dl, const ImVec2& mn, const ImVec2& mx, const ImVec2& origin, float step, ImU32 col,
                float dot = 1.0f);
void InsertMarker(ImDrawList* dl, const ImVec2& a, const ImVec2& b, ImU32 col, float thickness = 2.0f,
                  float cap = 4.0f);
void DropHighlight(ImDrawList* dl, const ImVec2& mn, const ImVec2& mx, ImU32 fill, ImU32 border,
                   float rounding = 2.0f, float thickness = 2.0f);
void BoundsRect(ImDrawList* dl, const ImVec2& mn, const ImVec2& mx, ImU32 col);
void CloseBox(ImDrawList* dl, const ImVec2& center, float radius, ImU32 bg, ImU32 fg);

struct TitleBarStyle {
    ImU32 bg = 0, text = 0, button = 0, button_bg = 0;
    float rounding = 0;
    ImVec2 pad;
    bool collapse_arrow = true;
    bool close_box = true;
};
void FakeTitleBar(ImDrawList* dl, const ImVec2& mn, const ImVec2& mx, const char* title, const TitleBarStyle& st);

}  // namespace imforge::builder::overlay
