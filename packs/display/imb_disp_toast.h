#pragma once
// PushToast from anywhere, ToastStack once per frame
#include "imb_disp_core.h"
#if defined(__has_include)
#if __has_include("imforge/imb_motion.h")
#include "imforge/imb_motion.h"
#define IMB_DISP_HAS_MOTION 1
#endif
#endif

namespace imb {
namespace disp {

enum class ToastKind { Info, Success, Warning, Error };
enum class ToastCorner { TopLeft, TopRight, BottomLeft, BottomRight, TopCenter, BottomCenter };

struct ToastStackStyle {
    float width = 0.0f;  // 0 = 22 x font size
    int max_visible = 5;  // the rest wait their turn
    bool pause_on_hover = true;
    float margin = 0.0f;  // 0 = font size
    // motion presets for the slide in/out (imb_motion.h with imb_motion_presets.h included).
    // null picks one that matches the corner; without the presets a built-in slide is used
    const char* enter = nullptr;
    const char* exit = nullptr;
};

namespace detail {

const int kMaxToasts = 16;

struct Toast {
    int handle;
    ToastKind kind;
    float duration, life, age, exit, y, progress;
    bool dismissed, placed;
    char title[96];
    char text[256];
};

struct ToastList {
    Toast items[kMaxToasts];
    int count = 0;
    int next = 1;
};

inline ToastList& Toasts() {
    static ToastList l;
    return l;
}

inline Toast* FindToast(int handle) {
    ToastList& l = Toasts();
    for (int i = 0; i < l.count; ++i)
        if (l.items[i].handle == handle) return &l.items[i];
    return nullptr;
}

inline ImU32 KindColor(ToastKind k) {
    const Style& s = GetStyle();
    switch (k) {
        case ToastKind::Success: return s.success;
        case ToastKind::Warning: return s.warning;
        case ToastKind::Error: return s.danger;
        default: return s.info;
    }
}

inline void KindIcon(ImDrawList* dl, const ImVec2& c, float r, ToastKind k, ImU32 col) {
    float th = Max(1.5f, r * 0.2f);
    dl->AddCircleFilled(c, r, WithAlpha(col, 0.2f));
    switch (k) {
        case ToastKind::Success: CheckMark(dl, c, r * 0.6f, col, th); break;
        case ToastKind::Error: Cross(dl, c, r * 0.36f, col, th); break;
        case ToastKind::Warning:
            dl->AddLine(ImVec2(c.x, c.y - r * 0.45f), ImVec2(c.x, c.y + r * 0.12f), col, th);
            dl->AddCircleFilled(ImVec2(c.x, c.y + r * 0.42f), th * 0.65f, col);
            break;
        default:
            dl->AddCircleFilled(ImVec2(c.x, c.y - r * 0.42f), th * 0.65f, col);
            dl->AddLine(ImVec2(c.x, c.y - r * 0.12f), ImVec2(c.x, c.y + r * 0.45f), col, th);
            break;
    }
}

}  // namespace detail

// returns a handle for SetToastProgress / DismissToast. At most 16 are kept, the oldest goes first.
inline int PushToast(const char* title, const char* text = nullptr, ToastKind kind = ToastKind::Info,
                     float seconds = 4.0f) {
    detail::ToastList& l = detail::Toasts();
    if (l.count == detail::kMaxToasts) {
        memmove(&l.items[0], &l.items[1], sizeof(detail::Toast) * (size_t)(detail::kMaxToasts - 1));
        --l.count;
    }
    detail::Toast& t = l.items[l.count++];
    memset(&t, 0, sizeof(t));
    t.handle = l.next++;
    if (l.next <= 0) l.next = 1;
    t.kind = kind;
    t.duration = seconds > 0.0f ? seconds : 4.0f;
    t.progress = -1.0f;
    snprintf(t.title, sizeof(t.title), "%s", title ? title : "");
    snprintf(t.text, sizeof(t.text), "%s", text ? text : "");
    return t.handle;
}

// turns the toast into a progress toast; it stays until the fraction reaches 1, then fades shortly after
inline void SetToastProgress(int handle, float fraction) {
    detail::Toast* t = detail::FindToast(handle);
    if (!t) return;
    bool was_done = t->progress >= 1.0f;
    t->progress = detail::Clamp01(fraction);
    if (t->progress >= 1.0f && !was_done) t->duration = t->life + 1.2f;
}

inline void DismissToast(int handle) {
    if (detail::Toast* t = detail::FindToast(handle)) t->dismissed = true;
}

inline int ToastCount() { return detail::Toasts().count; }
inline void ClearToasts() { detail::Toasts().count = 0; }

inline void ToastStack(ToastCorner corner = ToastCorner::BottomRight, const ToastStackStyle& ts = ToastStackStyle()) {
    using namespace detail;
    ToastList& l = Toasts();
    if (l.count == 0) return;
    const float kIn = 0.4f, kOut = 0.28f;
    float dt = imb::DeltaTime();
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    ImGuiViewport* vp = ImGui::GetMainViewport();
    float font = ImGui::GetFontSize();
    float width = ts.width > 0.0f ? ts.width : font * 22.0f;
    float margin = ts.margin > 0.0f ? ts.margin : font;
    float pad = font * 0.75f, gap = font * 0.5f, icon_r = font * 0.75f;
    float bar_h = Max(2.0f, font * 0.18f);
    float r = Max(CardRounding(), font * 0.4f);
    bool center = corner == ToastCorner::TopCenter || corner == ToastCorner::BottomCenter;
    bool right = corner == ToastCorner::TopRight || corner == ToastCorner::BottomRight;
    bool bottom = corner == ToastCorner::BottomLeft || corner == ToastCorner::BottomRight || corner == ToastCorner::BottomCenter;
    float base_x = center ? vp->Pos.x + (vp->Size.x - width) * 0.5f
                          : (right ? vp->Pos.x + vp->Size.x - margin - width : vp->Pos.x + margin);
    float text_x = pad + icon_r * 2.0f + pad * 0.8f;
    float text_w = width - text_x - pad * 1.8f;
    ImVec2 mouse = ImGui::GetMousePos();
    float out_len = kOut;
#ifdef IMB_DISP_HAS_MOTION
    const char* def_in = center ? (bottom ? "FadeInUp" : "FadeInDown") : (right ? "FadeInRight" : "FadeInLeft");
    const char* def_out = center ? (bottom ? "FadeOutDown" : "FadeOutUp") : (right ? "FadeOutRight" : "FadeOutLeft");
    const imb::AnimDef* in_def = imb::FindAnim(ts.enter ? ts.enter : def_in);
    const imb::AnimDef* out_def = imb::FindAnim(ts.exit ? ts.exit : def_out);
    if (!in_def || !out_def) in_def = out_def = nullptr;
    if (out_def) out_len = out_def->delay + out_def->duration;
#endif
    float stack = 0.0f;
    int shown = 0;
    int max_visible = MaxI(ts.max_visible, 1);
    for (int i = l.count - 1; i >= 0; --i) {
        Toast& t = l.items[i];
        if (shown >= max_visible) {
            t.placed = false;
            continue;
        }
        ++shown;
        float title_h = t.title[0] ? font : 0.0f;
        float body_h = t.text[0] ? ImGui::GetFont()->CalcTextSizeA(font, FLT_MAX, text_w, t.text).y : 0.0f;
        float h = pad * 2.0f + Max(icon_r * 2.0f, title_h + (title_h > 0.0f && body_h > 0.0f ? font * 0.2f : 0.0f) + body_h) + bar_h;
        float target = stack;
        stack += h + gap;
        if (!t.placed) {
            t.y = target;
            t.placed = true;
        } else {
            t.y += (target - t.y) * Min(1.0f, dt * 14.0f);
        }
        float y0 = bottom ? vp->Pos.y + vp->Size.y - margin - t.y - h : vp->Pos.y + margin + t.y;
        ImVec2 mn(base_x, y0), mx(base_x + width, y0 + h);
        bool hovered = mouse.x >= mn.x && mouse.x < mx.x && mouse.y >= mn.y && mouse.y < mx.y;
        t.age += dt;
        bool leaving = t.dismissed || t.life >= t.duration;
        bool waiting = t.progress >= 0.0f && t.progress < 1.0f;
        if (!leaving && !waiting && !(hovered && ts.pause_on_hover)) t.life += dt;
        if (leaving) t.exit += dt;
        if (t.exit >= out_len) {
            memmove(&l.items[i], &l.items[i + 1], sizeof(Toast) * (size_t)(l.count - i - 1));
            --l.count;
            continue;
        }
        float alpha;
        ImVec2 off(0, 0), scale(1, 1);
#ifdef IMB_DISP_HAS_MOTION
        if (in_def) {
            imb::Pose pose = t.exit > 0.0f ? imb::SampleAnim(*out_def, t.exit) : imb::SampleAnim(*in_def, t.age);
            float dpi = font / 13.0f;
            off = ImVec2(pose.translate.x * dpi, pose.translate.y * dpi);
            scale = ImVec2(pose.scale_x, pose.scale_y);
            alpha = Clamp01(pose.opacity * pose.tint.w);
        } else
#endif
        {
            float in = imb::EaseApply(imb::Ease::OutBack, Clamp01(t.age / kIn));
            float out = imb::EaseApply(imb::Ease::InCubic, Clamp01(t.exit / kOut));
            alpha = Min(Clamp01(t.age / (kIn * 0.6f)), 1.0f - out);
            float travel = center ? (h + margin) : (width + margin);
            float d = (1.0f - in) * travel + out * travel * 0.6f;
            if (center) off.y = bottom ? d : -d;
            else off.x = right ? d : -d;
        }
        mn = Add(mn, off);
        mx = Add(mx, off);
        // the card is drawn on the foreground list, so a small input window sits under it: clicks on
        // the toast (and its close button) never reach the windows below
        bool over_x = false, close_hit = false;
        float xr = font * 0.24f;
        ImVec2 xc(mx.x - pad * 0.9f, mn.y + pad * 0.9f);
        if (!leaving) {
            char wname[32];
            snprintf(wname, sizeof(wname), "##imb_toast_%d", t.handle);
            ImGui::SetNextWindowPos(mn);
            ImGui::SetNextWindowSize(ImVec2(Max(mx.x - mn.x, 1.0f), Max(mx.y - mn.y, 1.0f)));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(1, 1));
            ImGuiWindowFlags wf = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings |
                                  ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoFocusOnAppearing |
                                  ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoBackground |
                                  ImGuiWindowFlags_NoScrollWithMouse;
            ImGui::Begin(wname, nullptr, wf);
            ImGui::PopStyleVar(3);
            ImGui::SetCursorScreenPos(ImVec2(xc.x - xr * 2.2f, xc.y - xr * 2.2f));
            if (ImGui::InvisibleButton("##close", ImVec2(xr * 4.4f, xr * 4.4f))) close_hit = true;
            over_x = ImGui::IsItemHovered();
            ImGui::SetCursorScreenPos(mn);
            ImGui::InvisibleButton("##body", ImVec2(Max(mx.x - mn.x, 1.0f), Max(mx.y - mn.y, 1.0f)));
            ImGui::End();
        }
        int vtx0 = dl->VtxBuffer.Size;
        ImU32 kc = KindColor(t.kind);
        imb::Shadow(dl, mn, mx, r, IM_COL32(0, 0, 0, (int)(110.0f * alpha)), font * 0.8f);
        dl->AddRectFilled(mn, mx, ImGui::GetColorU32(ImGuiCol_PopupBg, alpha), r);
        ImVec2 ic(mn.x + pad + icon_r, mn.y + pad + icon_r);
        KindIcon(dl, ic, icon_r, t.kind, WithAlpha(kc, alpha));
        float ty = mn.y + pad + (title_h + body_h < icon_r * 2.0f ? (icon_r * 2.0f - title_h - body_h) * 0.5f : 0.0f);
        if (t.title[0]) {
            Text(dl, font, ImVec2(mn.x + text_x, ty), TextCol(alpha), t.title);
            ty += font * 1.2f;
        }
        if (t.text[0])
            dl->AddText(ImGui::GetFont(), font, ImVec2(mn.x + text_x, ty), TextDim(alpha), t.text, nullptr, text_w);
        if (hovered && !leaving) {
            if (over_x) dl->AddCircleFilled(xc, xr * 2.0f, TextCol(0.12f * alpha));
            Cross(dl, xc, xr, TextDim(alpha), Max(1.2f, font * 0.08f));
            if (close_hit) t.dismissed = true;
        }
        float frac = t.progress >= 0.0f ? t.progress : Clamp01(1.0f - t.life / t.duration);
        float bw = (width - r) * frac;
        dl->AddRectFilled(ImVec2(mn.x + r * 0.5f, mx.y - bar_h - 1.0f), ImVec2(mx.x - r * 0.5f, mx.y - 1.0f),
                          TrackCol(0.6f * alpha), bar_h * 0.5f);
        if (bw > 0.5f)
            dl->AddRectFilled(ImVec2(mn.x + r * 0.5f, mx.y - bar_h - 1.0f), ImVec2(mn.x + r * 0.5f + bw, mx.y - 1.0f),
                              WithAlpha(kc, 0.9f * alpha), bar_h * 0.5f);
        Outline(dl, mn, mx, BorderCol(alpha * 0.8f), r, 1.0f);
        if (scale.x != 1.0f || scale.y != 1.0f) {
            ImVec2 c((mn.x + mx.x) * 0.5f, (mn.y + mx.y) * 0.5f);
            for (int v = vtx0; v < dl->VtxBuffer.Size; ++v) {
                ImVec2& p = dl->VtxBuffer[v].pos;
                p = ImVec2(c.x + (p.x - c.x) * scale.x, c.y + (p.y - c.y) * scale.y);
            }
        }
    }
}

}  // namespace disp
}  // namespace imb
