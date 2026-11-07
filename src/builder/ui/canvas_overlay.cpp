#include "canvas_overlay.h"

#include <cmath>

#include "imgui_internal.h"

namespace imforge::builder::overlay {

void HoverOutline(ImDrawList* dl, const ImVec2& mn, const ImVec2& mx, ImU32 col, float thickness) {
    dl->AddRect(ImVec2(mn.x - 1, mn.y - 1), ImVec2(mx.x + 1, mx.y + 1), col, 2.0f, 0, thickness);
}

void SelectionOutline(ImDrawList* dl, const ImVec2& mn, const ImVec2& mx, ImU32 col, ImU32 handle_fill,
                      float thickness, float handle) {
    ImVec2 a(mn.x - 1, mn.y - 1), b(mx.x + 1, mx.y + 1);
    dl->AddRect(a, b, col, 1.0f, 0, thickness);
    const float h = handle * 0.5f;
    const ImVec2 pts[8] = {
        ImVec2(a.x, a.y), ImVec2((a.x + b.x) * 0.5f, a.y), ImVec2(b.x, a.y), ImVec2(a.x, (a.y + b.y) * 0.5f),
        ImVec2(b.x, (a.y + b.y) * 0.5f), ImVec2(a.x, b.y), ImVec2((a.x + b.x) * 0.5f, b.y), ImVec2(b.x, b.y),
    };
    bool mid = (b.x - a.x) > handle * 4 && (b.y - a.y) > handle * 4;
    for (int i = 0; i < 8; ++i) {
        bool is_mid = (i == 1 || i == 3 || i == 4 || i == 6);
        if (is_mid && !mid) continue;
        ImVec2 p = pts[i];
        dl->AddRectFilled(ImVec2(p.x - h, p.y - h), ImVec2(p.x + h, p.y + h), handle_fill, 1.0f);
        dl->AddRect(ImVec2(p.x - h, p.y - h), ImVec2(p.x + h, p.y + h), col, 1.0f, 0, 1.0f);
    }
}

void DashedLine(ImDrawList* dl, const ImVec2& a, const ImVec2& b, ImU32 col, float dash, float gap, float thickness) {
    float dx = b.x - a.x, dy = b.y - a.y;
    float len = std::sqrt(dx * dx + dy * dy);
    if (len < 0.5f) return;
    float ux = dx / len, uy = dy / len;
    float period = dash + gap;
    if (period <= 0.0f) {
        dl->AddLine(a, b, col, thickness);
        return;
    }
    for (float t = 0.0f; t < len; t += period) {
        float e = t + dash;
        if (e > len) e = len;
        dl->AddLine(ImVec2(a.x + ux * t, a.y + uy * t), ImVec2(a.x + ux * e, a.y + uy * e), col, thickness);
    }
}

void DashedRect(ImDrawList* dl, const ImVec2& mn, const ImVec2& mx, ImU32 col, float dash, float gap,
                float thickness) {
    ImVec2 a(mn.x - 1, mn.y - 1), b(mx.x + 1, mx.y + 1);
    DashedLine(dl, ImVec2(a.x, a.y), ImVec2(b.x, a.y), col, dash, gap, thickness);
    DashedLine(dl, ImVec2(b.x, a.y), ImVec2(b.x, b.y), col, dash, gap, thickness);
    DashedLine(dl, ImVec2(b.x, b.y), ImVec2(a.x, b.y), col, dash, gap, thickness);
    DashedLine(dl, ImVec2(a.x, b.y), ImVec2(a.x, a.y), col, dash, gap, thickness);
}

ImVec2 LabelSize(const char* text, const ImVec2& pad) {
    ImVec2 ts = ImGui::CalcTextSize(text);
    return ImVec2(ts.x + pad.x * 2, ts.y + pad.y * 2);
}

void Label(ImDrawList* dl, const ImVec2& pos, const char* text, ImU32 bg, ImU32 fg, const ImVec2& pad,
           float rounding) {
    ImVec2 size = LabelSize(text, pad);
    dl->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), bg, rounding);
    dl->AddText(ImVec2(pos.x + pad.x, pos.y + pad.y), fg, text);
}

void DottedGrid(ImDrawList* dl, const ImVec2& mn, const ImVec2& mx, const ImVec2& origin, float step, ImU32 col,
                float dot) {
    if (step < 1.0f) return;
    while (step < 12.0f) step *= 2.0f;
    float x0 = origin.x + std::floor((mn.x - origin.x) / step) * step;
    float y0 = origin.y + std::floor((mn.y - origin.y) / step) * step;
    if (x0 < mn.x) x0 += step;
    if (y0 < mn.y) y0 += step;
    int cols = (int)((mx.x - x0) / step) + 1, rows = (int)((mx.y - y0) / step) + 1;
    if (cols <= 0 || rows <= 0) return;
    if (cols * rows > 12000) return;
    dl->PrimReserve(cols * rows * 6, cols * rows * 4);
    for (int j = 0; j < rows; ++j) {
        float y = y0 + j * step;
        for (int i = 0; i < cols; ++i) {
            float x = x0 + i * step;
            dl->PrimRect(ImVec2(x - dot * 0.5f, y - dot * 0.5f), ImVec2(x + dot * 0.5f, y + dot * 0.5f), col);
        }
    }
}

void InsertMarker(ImDrawList* dl, const ImVec2& a, const ImVec2& b, ImU32 col, float thickness, float cap) {
    dl->AddLine(a, b, col, thickness);
    bool horizontal = std::fabs(b.y - a.y) < std::fabs(b.x - a.x);
    if (horizontal) {
        dl->AddLine(ImVec2(a.x, a.y - cap), ImVec2(a.x, a.y + cap), col, thickness);
        dl->AddLine(ImVec2(b.x, b.y - cap), ImVec2(b.x, b.y + cap), col, thickness);
    } else {
        dl->AddLine(ImVec2(a.x - cap, a.y), ImVec2(a.x + cap, a.y), col, thickness);
        dl->AddLine(ImVec2(b.x - cap, b.y), ImVec2(b.x + cap, b.y), col, thickness);
    }
    float cx = (a.x + b.x) * 0.5f, cy = (a.y + b.y) * 0.5f;
    dl->AddCircleFilled(ImVec2(cx, cy), thickness + 1.5f, col);
}

void DropHighlight(ImDrawList* dl, const ImVec2& mn, const ImVec2& mx, ImU32 fill, ImU32 border, float rounding,
                   float thickness) {
    dl->AddRectFilled(mn, mx, fill, rounding);
    dl->AddRect(mn, mx, border, rounding, 0, thickness);
}

void BoundsRect(ImDrawList* dl, const ImVec2& mn, const ImVec2& mx, ImU32 col) {
    dl->AddRect(mn, mx, col, 0.0f, 0, 1.0f);
}

void CloseBox(ImDrawList* dl, const ImVec2& c, float r, ImU32 bg, ImU32 fg) {
    dl->AddCircleFilled(c, r, bg, 12);
    float k = r * 0.45f;
    dl->AddLine(ImVec2(c.x - k, c.y - k), ImVec2(c.x + k, c.y + k), fg, 1.5f);
    dl->AddLine(ImVec2(c.x + k, c.y - k), ImVec2(c.x - k, c.y + k), fg, 1.5f);
}

void FakeTitleBar(ImDrawList* dl, const ImVec2& mn, const ImVec2& mx, const char* title, const TitleBarStyle& st) {
    float h = mx.y - mn.y;
    dl->AddRectFilled(mn, mx, st.bg, st.rounding, ImDrawFlags_RoundCornersTop);
    float x = mn.x + st.pad.x;
    float font = ImGui::GetFontSize();
    if (st.collapse_arrow) {
        ImGui::RenderArrow(dl, ImVec2(x, mn.y + st.pad.y), st.text, ImGuiDir_Down, 1.0f);
        x += font + st.pad.x;
    }
    float right = mx.x - st.pad.x;
    if (st.close_box) {
        float r = font * 0.5f;
        ImVec2 c(right - r, mn.y + h * 0.5f);
        CloseBox(dl, c, r, st.button_bg, st.button);
        right = c.x - r - st.pad.x;
    }
    if (title && *title && right > x) {
        ImVec4 clip(x, mn.y, right, mx.y);
        dl->AddText(nullptr, 0.0f, ImVec2(x, mn.y + (h - font) * 0.5f), st.text, title, nullptr, 0.0f, &clip);
    }
}

}  // namespace imforge::builder::overlay
