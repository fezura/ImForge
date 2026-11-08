#pragma once
// shared style and drawing helpers for the gaming pack
#include "imb_ess_core.h"
#include <cstddef>
#include <ctime>

// tests define this to record the rect of each sub-item inside composite widgets
#ifndef IMB_GAME_ITEM_HOOK
#define IMB_GAME_ITEM_HOOK(name) ((void)0)
#endif

namespace imb {
namespace game {

enum OverlayCorner {
    Corner_None = -1,
    Corner_TopLeft = 0,
    Corner_TopRight = 1,
    Corner_BottomLeft = 2,
    Corner_BottomRight = 3
};

// colors used across the pack. accent and rounding come from imb::ess::GetStyle()
struct Style {
    ImU32 team[4] = {IM_COL32(74, 163, 255, 255), IM_COL32(255, 86, 86, 255),
                     IM_COL32(72, 214, 120, 255), IM_COL32(245, 196, 60, 255)};
    ImU32 neutral = IM_COL32(186, 190, 200, 255);
    ImU32 good = IM_COL32(72, 214, 120, 255);
    ImU32 warn = IM_COL32(245, 190, 60, 255);
    ImU32 bad = IM_COL32(236, 72, 72, 255);
    ImU32 armor = IM_COL32(92, 162, 255, 255);
    ImU32 trail = IM_COL32(255, 238, 205, 255);
    ImU32 accent2 = IM_COL32(41, 211, 255, 255);
    ImU32 overlay_bg = IM_COL32(13, 15, 20, 225);
    ImU32 viewport_top = IM_COL32(44, 54, 74, 255);
    ImU32 viewport_bottom = IM_COL32(16, 18, 24, 255);
};

inline Style& GetStyle() {
    static Style s;
    return s;
}

// generated code passes nullptr for the :count of an unbound array
inline int CountOf(int n) { return n; }
inline int CountOf(std::nullptr_t) { return 0; }

namespace detail {

namespace ed = ::imb::ess::detail;

const float kPi = 3.14159265358979f;
const ImDrawFlags kRoundRight = ed::kRoundAll & ~ed::kRoundLeft;

inline float Dpi() { return ImGui::GetFontSize() / 13.0f; }

inline float Dt() {
#ifdef IMB_ESS_NO_RUNTIME
    return ImGui::GetIO().DeltaTime;
#else
    return imb::DeltaTime();
#endif
}

inline float Time() { return (float)ImGui::GetTime(); }

inline float Clamp(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

// NaN and inf become 0 so user values can't poison animation state or vertex data
inline float Fin(float v) { return (v == v && v - v == 0.0f) ? v : 0.0f; }
inline float Fin01(float v) { return v != v ? 0.0f : Clamp(v, 0.0f, 1.0f); }

// float to int without the undefined behavior on huge or non-finite input
inline int SafeInt(float v) { return (int)Clamp(Fin(v), -1.0e9f, 1.0e9f); }

inline ImU32 U32(const ImVec4& c) { return ImGui::ColorConvertFloat4ToU32(c); }

// applies style.Alpha
inline ImU32 Live(ImU32 c, float alpha_mul = 1.0f) { return ImGui::GetColorU32(ed::WithAlpha(c, alpha_mul)); }

inline ImU32 TeamColor(int team) {
    const Style& s = GetStyle();
    return team >= 0 && team < 4 ? s.team[team] : s.neutral;
}

inline ImU32 Accent2() {
    ImU32 a = GetStyle().accent2;
    return a ? a : ed::AccentRaw();
}

inline ImU32 HealthColor(float t) {
    const Style& s = GetStyle();
    t = ed::Clamp01(t);
    if (t < 0.5f) return ed::LerpColor(s.bad, s.warn, t * 2.0f);
    return ed::LerpColor(s.warn, s.good, (t - 0.5f) * 2.0f);
}

// 0 good, 1 warn, 2 bad as a float so it can be animated
inline float Grade(float v, float good, float warn, bool higher_is_better) {
    if (higher_is_better) return v >= good ? 0.0f : (v >= warn ? 1.0f : 2.0f);
    return v <= good ? 0.0f : (v <= warn ? 1.0f : 2.0f);
}

inline ImU32 GradeColor(float g) {
    const Style& s = GetStyle();
    if (g <= 1.0f) return ed::LerpColor(s.good, s.warn, ed::Clamp01(g));
    return ed::LerpColor(s.warn, s.bad, ed::Clamp01(g - 1.0f));
}

inline float SpringTo(ImGuiID id, float target) {
#ifdef IMB_ESS_NO_RUNTIME
    return ed::AnimTo(id, target, 0.12f);
#else
    return imb::Spring(id, target, 260.0f, 24.0f);
#endif
}

inline void Stroke(ImDrawList* dl, ImU32 col, float thickness, bool closed) {
    ImDrawFlags f = closed ? ImDrawFlags_Closed : 0;
#if IMGUI_VERSION_NUM >= 19280
    dl->PathStroke(col, thickness, f);
#else
    dl->PathStroke(col, f, thickness);
#endif
}

inline void Polyline(ImDrawList* dl, const ImVec2* pts, int n, ImU32 col, float thickness, bool closed) {
    ImDrawFlags f = closed ? ImDrawFlags_Closed : 0;
#if IMGUI_VERSION_NUM >= 19280
    dl->AddPolyline(pts, n, col, thickness, f);
#else
    dl->AddPolyline(pts, n, col, f, thickness);
#endif
}

inline ImDrawList* OverlayDrawList() {
#ifdef IMGUI_HAS_VIEWPORT
    return ImGui::GetForegroundDrawList(ImGui::GetMainViewport());
#else
    return ImGui::GetForegroundDrawList();
#endif
}

// 0 picks the default, negative fills the available region minus that much (like ImGui sizes)
inline float SizeAxis(float v, float avail, float def) {
    if (v > 0.0f) return v;
    if (v < 0.0f) return ed::Max(avail + v, 4.0f);
    return def;
}

inline ImVec2 ResolveSize(const ImVec2& size, float def_w, float def_h) {
    ImVec2 avail = ImGui::GetContentRegionAvail();
    return ImVec2(SizeAxis(size.x, avail.x, def_w), SizeAxis(size.y, avail.y, def_h));
}

inline ImVec2 TextSize(float size, const char* b, const char* e = nullptr) {
    return ImGui::GetFont()->CalcTextSizeA(size, FLT_MAX, 0.0f, b, e);
}

inline void Text(ImDrawList* dl, float size, const ImVec2& pos, ImU32 col, const char* b,
                 const char* e = nullptr) {
    dl->AddText(ImGui::GetFont(), size, pos, col, b, e);
}

// text with a 1px drop shadow, readable over any background
inline void TextShadow(ImDrawList* dl, float size, const ImVec2& pos, ImU32 col, const char* b,
                       const char* e = nullptr) {
    float a = (float)((col >> IM_COL32_A_SHIFT) & 0xFF) / 255.0f;
    float o = ed::Max(1.0f, size / 13.0f);
    dl->AddText(ImGui::GetFont(), size, ImVec2(pos.x + o, pos.y + o), IM_COL32(0, 0, 0, (int)(170.0f * a)),
                b, e);
    dl->AddText(ImGui::GetFont(), size, pos, col, b, e);
}

// a dim fake scene so previews read like they sit on top of a game
inline void MockViewport(ImDrawList* dl, const ImVec2& mn, const ImVec2& mx, float rounding) {
    const Style& s = GetStyle();
    float w = mx.x - mn.x, h = mx.y - mn.y;
    ImU32 top = Live(s.viewport_top), bottom = Live(s.viewport_bottom);
    dl->AddRectFilled(mn, mx, bottom, rounding);
    float horizon = mn.y + h * 0.56f;
    dl->PushClipRect(mn, mx, true);
    ed::Gradient(dl, ImVec2(mn.x, mn.y), ImVec2(mx.x, horizon), top, bottom, true, 0.0f);
    ImU32 prop = Live(IM_COL32(8, 10, 14, 150));
    dl->AddRectFilled(ImVec2(mn.x + w * 0.04f, horizon - h * 0.22f), ImVec2(mn.x + w * 0.2f, horizon), prop);
    dl->AddRectFilled(ImVec2(mn.x + w * 0.2f, horizon - h * 0.12f), ImVec2(mn.x + w * 0.3f, horizon), prop);
    dl->AddRectFilled(ImVec2(mx.x - w * 0.24f, horizon - h * 0.3f), ImVec2(mx.x - w * 0.08f, horizon), prop);
    ImU32 grid = Live(IM_COL32(255, 255, 255, 18));
    ImVec2 vp(mn.x + w * 0.5f, horizon);
    for (int i = -6; i <= 6; ++i) {
        float x = vp.x + (float)i * w * 0.22f;
        dl->AddLine(vp, ImVec2(x, mx.y), grid);
    }
    for (int i = 1; i <= 6; ++i) {
        float k = (float)i / 6.0f;
        float y = horizon + (mx.y - horizon) * k * k;
        dl->AddLine(ImVec2(mn.x, y), ImVec2(mx.x, y), grid);
    }
    dl->AddLine(ImVec2(mn.x, horizon), ImVec2(mx.x, horizon), Live(IM_COL32(255, 255, 255, 34)));
    dl->PopClipRect();
    ed::Outline(dl, mn, mx, ed::BorderCol(), rounding, 1.0f);
}

// Corner_None lays the box out as an item at the cursor, any corner floats it over the main
// viewport through the foreground draw list (no item, nothing to hover)
struct OverlayBox {
    ImDrawList* dl;
    ImVec2 mn, mx;
    bool is_item;
};

inline OverlayBox PlaceOverlay(const char* id, int corner, const ImVec2& size, const ImVec2& margin) {
    OverlayBox b;
    if (corner < Corner_TopLeft || corner > Corner_BottomRight) {
        b.mn = ImGui::GetCursorScreenPos();
        ImGui::PushID(id);
        ImGui::Dummy(size);
        ImGui::PopID();
        b.dl = ImGui::GetWindowDrawList();
        b.is_item = true;
    } else {
        ImGuiViewport* vp = ImGui::GetMainViewport();
        float d = 10.0f * Dpi();
        float mx = margin.x != 0.0f ? margin.x : d, my = margin.y != 0.0f ? margin.y : d;
        bool right = (corner & 1) != 0, bottom = (corner & 2) != 0;
        b.mn.x = right ? vp->WorkPos.x + vp->WorkSize.x - size.x - mx : vp->WorkPos.x + mx;
        b.mn.y = bottom ? vp->WorkPos.y + vp->WorkSize.y - size.y - my : vp->WorkPos.y + my;
        b.dl = OverlayDrawList();
        b.is_item = false;
    }
    b.mx = ImVec2(b.mn.x + size.x, b.mn.y + size.y);
    return b;
}

// local wall clock as HH:MM:SS
inline const char* ClockText(char* buf, int cap) {
    std::time_t now = std::time(nullptr);
    std::tm tm_now;
#ifdef _WIN32
    localtime_s(&tm_now, &now);
#else
    localtime_r(&now, &tm_now);
#endif
    std::snprintf(buf, (size_t)cap, "%02d:%02d:%02d", tm_now.tm_hour, tm_now.tm_min, tm_now.tm_sec);
    return buf;
}

inline void LoadVec4(ImGuiStorage* s, ImGuiID k, ImVec4& v) {
    v = ImVec4(s->GetFloat(k, v.x), s->GetFloat(k + 1, v.y), s->GetFloat(k + 2, v.z), s->GetFloat(k + 3, v.w));
}

inline void StoreVec4(ImGuiStorage* s, ImGuiID k, const ImVec4& v) {
    s->SetFloat(k, v.x);
    s->SetFloat(k + 1, v.y);
    s->SetFloat(k + 2, v.z);
    s->SetFloat(k + 3, v.w);
}

inline bool MouseIn(const ImVec2& mn, const ImVec2& mx) {
    ImVec2 m = ImGui::GetMousePos();
    return m.x >= mn.x && m.y >= mn.y && m.x < mx.x && m.y < mx.y;
}

// toggle track + knob in the essentials look, purely visual
inline void DrawSwitch(ImDrawList* dl, const ImVec2& mn, float h, float t, ImU32 on, float alpha) {
    float w = h * 1.8f;
    ImVec2 mx(mn.x + w, mn.y + h);
    ImU32 off = ed::Col(::imb::ess::GetStyle().track, ImGuiCol_FrameBg, alpha);
    dl->AddRectFilled(mn, mx, ed::LerpColor(off, Live(on, alpha), t), h * 0.5f);
    float r = h * 0.5f - ed::Max(1.5f, h * 0.12f);
    ImVec2 c(ed::Lerp(mn.x + h * 0.5f, mx.x - h * 0.5f, t), mn.y + h * 0.5f);
    dl->AddCircleFilled(ImVec2(c.x, c.y + 1.0f), r, ImGui::GetColorU32(IM_COL32(0, 0, 0, (int)(50.0f * alpha))));
    dl->AddCircleFilled(c, r, ed::KnobCol(alpha));
}

}  // namespace detail
}  // namespace game
}  // namespace imb
