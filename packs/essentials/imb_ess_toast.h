#pragma once
// PushToast from anywhere, DrawToasts once per frame
#include "imb_ess_core.h"

namespace imb {
namespace ess {

enum class ToastType { Info, Success, Warning, Error };
enum class ToastCorner { TopLeft, TopRight, BottomLeft, BottomRight };

namespace detail {

struct Toast {
    char text[256];
    ToastType type;
    float duration;
    float elapsed;
};

inline ImVector<Toast>& Toasts() {
    static ImVector<Toast> v;
    return v;
}

inline ImU32 ToastColor(ToastType type) {
    const Style& s = GetStyle();
    switch (type) {
        case ToastType::Success: return s.toast_success;
        case ToastType::Warning: return s.toast_warning;
        case ToastType::Error: return s.toast_error;
        default: return s.toast_info;
    }
}

}  // namespace detail

inline void PushToast(const char* text, ToastType type = ToastType::Info, float seconds = 3.0f) {
    ImVector<detail::Toast>& v = detail::Toasts();
    if (v.Size >= 8) v.erase(v.begin());
    detail::Toast t;
    snprintf(t.text, sizeof(t.text), "%s", text ? text : "");
    t.type = type;
    t.duration = seconds > 0.0f ? seconds : 3.0f;
    t.elapsed = 0.0f;
    v.push_back(t);
}

inline int ToastCount() { return detail::Toasts().Size; }
inline void ClearToasts() { detail::Toasts().clear(); }

inline void DrawToasts(ToastCorner corner = ToastCorner::BottomRight) {
    ImVector<detail::Toast>& v = detail::Toasts();
    if (v.Size == 0) return;
    const float kIn = 0.25f, kOut = 0.3f;
    float dt = ImGui::GetIO().DeltaTime;
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    ImGuiViewport* vp = ImGui::GetMainViewport();
    float font = ImGui::GetFontSize();
    const Style& es = GetStyle();
    float width = es.toast_width > 0.0f ? es.toast_width : font * 24.0f;
    float pad = font * 0.6f, margin = font, gap = font * 0.5f;
    float bar_h = detail::Max(2.0f, font * 0.2f), stripe = font * 0.3f, icon_r = font * 0.3f;
    float r = detail::Max(detail::Rounding(), font * 0.3f);
    bool right = corner == ToastCorner::TopRight || corner == ToastCorner::BottomRight;
    bool bottom = corner == ToastCorner::BottomLeft || corner == ToastCorner::BottomRight;
    float y = bottom ? vp->Pos.y + vp->Size.y - margin : vp->Pos.y + margin;
    float text_w = width - stripe - pad * 2.5f - icon_r * 2.0f;
    for (int i = v.Size - 1; i >= 0; --i) {
        detail::Toast& t = v[i];
        t.elapsed += dt;
        if (t.elapsed >= t.duration) {
            v.erase(v.Data + i);
            continue;
        }
        float u = 1.0f - detail::Clamp01(t.elapsed / kIn);
        float eased_in = 1.0f - u * u * u;
        float remaining = t.duration - t.elapsed;
        float alpha = detail::Min(eased_in, detail::Clamp01(remaining / kOut));
        ImVec2 ts = ImGui::CalcTextSize(t.text, nullptr, false, text_w);
        float h = ts.y + pad * 2.0f + bar_h;
        float slide = (1.0f - eased_in) * width * 0.6f;
        float x0 = right ? vp->Pos.x + vp->Size.x - margin - width + slide
                         : vp->Pos.x + margin - slide;
        float y0 = bottom ? y - h : y;
        ImVec2 mn(x0, y0), mx(x0 + width, y0 + h);
        ImU32 tc = detail::ToastColor(t.type);
        detail::Shadow(dl, mn, mx, r, IM_COL32(0, 0, 0, (int)(90.0f * alpha)), font * 0.6f);
        dl->AddRectFilled(mn, mx, detail::Col(es.toast_bg, ImGuiCol_PopupBg, alpha), r);
        dl->AddRectFilled(mn, ImVec2(mn.x + stripe, mx.y), detail::WithAlpha(tc, alpha), r,
                          detail::kRoundLeft);
        float cy = mn.y + pad + ImGui::GetTextLineHeight() * 0.5f;
        float cx = mn.x + stripe + pad + icon_r;
        dl->AddCircleFilled(ImVec2(cx, cy), icon_r, detail::WithAlpha(tc, alpha));
        dl->AddCircleFilled(ImVec2(cx, cy), icon_r * 0.4f,
                            IM_COL32(255, 255, 255, (int)(220.0f * alpha)));
        dl->AddText(ImGui::GetFont(), font, ImVec2(cx + icon_r + pad * 0.5f, mn.y + pad),
                    detail::TextCol(alpha), t.text, nullptr, text_w);
        float frac = detail::Clamp01(remaining / t.duration);
        ImDrawFlags bar_flags = frac > 0.98f ? detail::kRoundBottom : detail::kRoundBottomLeft;
        dl->AddRectFilled(ImVec2(mn.x, mx.y - bar_h), ImVec2(mn.x + width * frac, mx.y),
                          detail::WithAlpha(tc, 0.8f * alpha), r, bar_flags);
        dl->AddRect(mn, mx, detail::BorderCol(alpha), r);
        y = bottom ? y0 - gap : y0 + h + gap;
    }
}

}  // namespace ess
}  // namespace imb
