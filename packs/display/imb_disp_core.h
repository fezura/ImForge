#pragma once
// style, colors and draw helpers shared by the display widgets
#include "imgui.h"
#include "imforge/imb_runtime.h"
#include <cfloat>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace imb {
namespace disp {

enum class Status { None, Online, Away, Busy, Offline };

// 0 colors use the ImGui style, 0 sizes scale with the font
struct Style {
    float rounding = 0.0f;
    float card_rounding = 0.0f;
    float anim_duration = 0.45f;
    float spring_stiffness = 170.0f;
    float spring_damping = 17.0f;
    bool intro = true;  // values sweep in from zero the first time a widget is seen
    ImU32 accent = 0;
    ImU32 track = 0;
    ImU32 text = 0;
    ImU32 text_dim = 0;
    ImU32 border = 0;
    ImU32 card_bg = 0;
    ImU32 success = IM_COL32(46, 204, 113, 255);
    ImU32 warning = IM_COL32(241, 180, 15, 255);
    ImU32 danger = IM_COL32(231, 76, 60, 255);
    ImU32 info = IM_COL32(52, 152, 219, 255);
    ImU32 away = IM_COL32(245, 166, 35, 255);
    ImU32 offline = IM_COL32(128, 134, 146, 255);
};

inline Style& GetStyle() {
    static Style s;
    return s;
}

namespace detail {

const float kPi = 3.14159265358979f;

inline ImVec2 Add(const ImVec2& a, const ImVec2& b) { return ImVec2(a.x + b.x, a.y + b.y); }
inline float Lerp(float a, float b, float t) { return a + (b - a) * t; }
inline float Clamp(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline float Clamp01(float v) { return Clamp(v, 0.0f, 1.0f); }
inline float Max(float a, float b) { return a > b ? a : b; }
inline float Min(float a, float b) { return a < b ? a : b; }
inline int MaxI(int a, int b) { return a > b ? a : b; }
inline int MinI(int a, int b) { return a < b ? a : b; }

#if IMGUI_VERSION_NUM >= 19300
constexpr ImDrawFlags kRoundAll = ImDrawFlags_RoundAll;
constexpr ImDrawFlags kRoundTop = ImDrawFlags_RoundTop;
constexpr ImDrawFlags kRoundBottom = ImDrawFlags_RoundBottom;
constexpr ImDrawFlags kRoundLeft = ImDrawFlags_RoundLeft;
constexpr ImDrawFlags kRoundRight = ImDrawFlags_RoundRight;
constexpr ImDrawFlags kRoundTopLeft = ImDrawFlags_RoundTopLeft;
constexpr ImDrawFlags kRoundTopRight = ImDrawFlags_RoundTopRight;
#else
constexpr ImDrawFlags kRoundAll = ImDrawFlags_RoundCornersAll;
constexpr ImDrawFlags kRoundTop = ImDrawFlags_RoundCornersTop;
constexpr ImDrawFlags kRoundBottom = ImDrawFlags_RoundCornersBottom;
constexpr ImDrawFlags kRoundLeft = ImDrawFlags_RoundCornersLeft;
constexpr ImDrawFlags kRoundRight = ImDrawFlags_RoundCornersRight;
constexpr ImDrawFlags kRoundTopLeft = ImDrawFlags_RoundCornersTopLeft;
constexpr ImDrawFlags kRoundTopRight = ImDrawFlags_RoundCornersTopRight;
#endif

// 1.92.8 swapped the thickness/flags order of AddRect, PathStroke and AddPolyline
inline void Outline(ImDrawList* dl, const ImVec2& mn, const ImVec2& mx, ImU32 col, float rounding,
                    float thickness, ImDrawFlags flags = 0) {
#if IMGUI_VERSION_NUM >= 19280
    dl->AddRect(mn, mx, col, rounding, thickness, flags);
#else
    dl->AddRect(mn, mx, col, rounding, flags, thickness);
#endif
}

inline void Stroke(ImDrawList* dl, ImU32 col, float thickness, bool closed = false) {
    ImDrawFlags f = closed ? (ImDrawFlags)ImDrawFlags_Closed : (ImDrawFlags)0;
#if IMGUI_VERSION_NUM >= 19280
    dl->PathStroke(col, thickness, f);
#else
    dl->PathStroke(col, f, thickness);
#endif
}

inline const char* LabelEnd(const char* label) {
    if (!label) return label;
    const char* p = strstr(label, "##");
    return p ? p : label + strlen(label);
}

inline bool HasText(const char* s) { return s && *s && LabelEnd(s) > s; }

inline ImU32 WithAlpha(ImU32 c, float mul) {
    float a = (float)((c >> IM_COL32_A_SHIFT) & 0xFF) * mul;
    int ai = (int)(a + 0.5f);
    ai = ai < 0 ? 0 : (ai > 255 ? 255 : ai);
    return (c & ~IM_COL32_A_MASK) | ((ImU32)ai << IM_COL32_A_SHIFT);
}

inline ImU32 Mix(ImU32 a, ImU32 b, float t) { return imb::LerpCol(a, b, t); }

// keeps alpha
inline ImU32 Lighten(ImU32 c, float t) {
    return (Mix(c, IM_COL32_WHITE, t) & ~IM_COL32_A_MASK) | (c & IM_COL32_A_MASK);
}

inline ImU32 Raw(ImGuiCol idx) { return ImGui::ColorConvertFloat4ToU32(ImGui::GetStyleColorVec4(idx)); }

// v == 0 picks the ImGui style color. style.Alpha is always applied
inline ImU32 Col(ImU32 v, ImGuiCol fallback, float alpha_mul = 1.0f) {
    if (v == 0) return ImGui::GetColorU32(fallback, alpha_mul);
    return ImGui::GetColorU32(WithAlpha(v, alpha_mul));
}

inline ImU32 Pick(ImU32 v, ImU32 fallback, float alpha_mul = 1.0f) {
    return ImGui::GetColorU32(WithAlpha(v ? v : fallback, alpha_mul));
}

inline ImU32 AccentRaw() {
    const Style& s = GetStyle();
    return s.accent ? s.accent : Raw(ImGuiCol_CheckMark);
}
inline ImU32 Accent(float alpha_mul = 1.0f) { return Col(GetStyle().accent, ImGuiCol_CheckMark, alpha_mul); }
inline ImU32 TextCol(float alpha_mul = 1.0f) { return Col(GetStyle().text, ImGuiCol_Text, alpha_mul); }
inline ImU32 TextDim(float alpha_mul = 1.0f) { return Col(GetStyle().text_dim, ImGuiCol_TextDisabled, alpha_mul); }
inline ImU32 BorderCol(float alpha_mul = 1.0f) { return Col(GetStyle().border, ImGuiCol_Border, alpha_mul); }
inline ImU32 TrackCol(float alpha_mul = 1.0f) { return Col(GetStyle().track, ImGuiCol_FrameBg, alpha_mul); }

// a step lighter than the window background, follows light and dark themes
inline ImU32 CardBg(float amount = 0.06f) {
    const Style& s = GetStyle();
    if (s.card_bg) return ImGui::GetColorU32(s.card_bg);
    ImVec4 bg = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
    ImVec4 tx = ImGui::GetStyleColorVec4(ImGuiCol_Text);
    ImVec4 c(Lerp(bg.x, tx.x, amount), Lerp(bg.y, tx.y, amount), Lerp(bg.z, tx.z, amount), 1.0f);
    return ImGui::GetColorU32(c);
}

inline ImU32 WindowBg() {
    ImVec4 bg = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
    bg.w = 1.0f;
    return ImGui::GetColorU32(bg);
}

inline bool IsBright(ImU32 c) {
    float r = (float)((c >> IM_COL32_R_SHIFT) & 0xFF);
    float g = (float)((c >> IM_COL32_G_SHIFT) & 0xFF);
    float b = (float)((c >> IM_COL32_B_SHIFT) & 0xFF);
    return (0.299f * r + 0.587f * g + 0.114f * b) > 165.0f;
}

inline ImU32 OnColor(ImU32 bg, float alpha_mul = 1.0f) {
    ImU32 c = IsBright(bg) ? IM_COL32(24, 24, 28, 255) : IM_COL32_WHITE;
    return ImGui::GetColorU32(WithAlpha(c, alpha_mul));
}

inline float Rounding() {
    float r = GetStyle().rounding;
    return r > 0.0f ? r : ImGui::GetStyle().FrameRounding;
}

inline float CardRounding() {
    float r = GetStyle().card_rounding;
    if (r > 0.0f) return r;
    return Max(Max(ImGui::GetStyle().ChildRounding, ImGui::GetStyle().FrameRounding), ImGui::GetFontSize() * 0.35f);
}

inline float Avail() { return Max(ImGui::GetContentRegionAvail().x, 1.0f); }

// 0 = fill, negative = fill minus that much
inline float ResolveW(float w, float min_w) {
    if (w > 0.0f) return w;
    float a = Avail() + w;
    return Max(a, min_w);
}

inline float Now() { return (float)ImGui::GetTime(); }

inline ImVec2 TextSize(float size, const char* b, const char* e = nullptr) {
    return ImGui::GetFont()->CalcTextSizeA(size, FLT_MAX, 0.0f, b, e);
}

inline void Text(ImDrawList* dl, float size, const ImVec2& pos, ImU32 col, const char* b, const char* e = nullptr) {
    dl->AddText(ImGui::GetFont(), size, ImVec2(std::floor(pos.x + 0.5f), std::floor(pos.y + 0.5f)), col, b, e);
}

inline void TextCentered(ImDrawList* dl, float size, const ImVec2& center, ImU32 col, const char* b,
                         const char* e = nullptr) {
    ImVec2 ts = TextSize(size, b, e);
    Text(dl, size, ImVec2(center.x - ts.x * 0.5f, center.y - ts.y * 0.5f), col, b, e);
}

inline void TextClipped(ImDrawList* dl, const ImVec2& pos, const ImVec2& clip_max, ImU32 col, const char* b,
                        const char* e = nullptr) {
    ImVec4 clip(pos.x, pos.y, clip_max.x, clip_max.y);
    dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), pos, col, b, e, 0.0f, &clip);
}

// NaN and inf become 0 so a bad value can't poison animation state
inline float Fin(float v) { return (v == v && v - v == 0.0f) ? v : 0.0f; }
inline float Fin01(float v) { return v != v ? 0.0f : Clamp01(v); }

// one float conversion (and %%) is all a format may hold, anything else falls back to %.0f
inline bool SafeFormat(const char* f) {
    int conv = 0;
    for (; *f; ++f) {
        if (*f != '%') continue;
        ++f;
        if (*f == '%') continue;
        while (*f == '-' || *f == '+' || *f == ' ' || *f == '#' || *f == '0') ++f;
        while (*f >= '0' && *f <= '9') ++f;
        if (*f == '.') {
            ++f;
            while (*f >= '0' && *f <= '9') ++f;
        }
        if (*f != 'f' && *f != 'F' && *f != 'e' && *f != 'E' && *f != 'g' && *f != 'G') return false;
        if (++conv > 1) return false;
    }
    return conv == 1;
}

inline int Format(char* buf, int cap, const char* fmt, float v) {
    if (!fmt || !*fmt || !SafeFormat(fmt)) fmt = "%.0f";
    int n = snprintf(buf, (size_t)cap, fmt, (double)Fin(v));
    if (n < 0) {
        buf[0] = 0;
        return 0;
    }
    return n >= cap ? cap - 1 : n;
}

// m:ss.t, or h:mm:ss past an hour
inline void FormatTime(char* buf, int cap, float seconds) {
    seconds = Fin(seconds);
    if (seconds < 0.0f) seconds = 0.0f;
    if (seconds > 1.0e8f) seconds = 1.0e8f;
    int tenths = (int)(seconds * 10.0f + 0.5f);
    int total = tenths / 10;
    int h = total / 3600, m = (total / 60) % 60, s = total % 60;
    if (h > 0) snprintf(buf, (size_t)cap, "%d:%02d:%02d", h, m, s);
    else snprintf(buf, (size_t)cap, "%d:%02d.%d", m, s, tenths % 10);
}

inline int ArcSegments(float r, float span) {
    int n = (int)(r * (span < 0.0f ? -span : span) / 2.5f);
    return n < 6 ? 6 : (n > 256 ? 256 : n);
}

inline ImVec2 Polar(const ImVec2& c, float r, float a) { return ImVec2(c.x + cosf(a) * r, c.y + sinf(a) * r); }

inline void Arc(ImDrawList* dl, const ImVec2& c, float r, float a0, float a1, ImU32 col, float th,
                bool round_caps) {
    if (a1 < a0) {
        float t = a0;
        a0 = a1;
        a1 = t;
    }
    if (a1 - a0 < 1e-4f || r <= 0.0f) return;
    dl->PathArcTo(c, r, a0, a1, ArcSegments(r, a1 - a0));
    Stroke(dl, col, th);
    if (round_caps) {
        dl->AddCircleFilled(Polar(c, r, a0), th * 0.5f, col);
        dl->AddCircleFilled(Polar(c, r, a1), th * 0.5f, col);
    }
}

// recolor vertices written since vtx0, keeping each vertex's own alpha (AA fringes)
template <class F> inline void Recolor(ImDrawList* dl, int vtx0, F color_at) {
    for (int i = vtx0; i < dl->VtxBuffer.Size; i++) {
        ImDrawVert& v = dl->VtxBuffer[i];
        ImU32 c = color_at(v.pos);
        ImU32 a = (v.col >> IM_COL32_A_SHIFT) & 0xFF;
        ImU32 ca = (c >> IM_COL32_A_SHIFT) & 0xFF;
        v.col = (c & ~IM_COL32_A_MASK) | (((a * ca + 127) / 255) << IM_COL32_A_SHIFT);
    }
}

// gradient along an arc that starts at a0 and spans `span` radians
inline void RecolorArc(ImDrawList* dl, int vtx0, const ImVec2& c, float a0, float span, ImU32 c0, ImU32 c1) {
    const float two_pi = kPi * 2.0f;
    float gap_mid = span + (two_pi - span) * 0.5f;
    Recolor(dl, vtx0, [&](const ImVec2& p) {
        float rel = atan2f(p.y - c.y, p.x - c.x) - a0;
        rel = fmodf(rel, two_pi);
        if (rel < 0.0f) rel += two_pi;
        if (rel > gap_mid) rel = 0.0f;
        return Mix(c0, c1, span > 0.0f ? Clamp01(rel / span) : 0.0f);
    });
}

// widget state lives in the window's ImGuiStorage, like tree node open flags
inline ImGuiStorage* Store() { return ImGui::GetStateStorage(); }

inline bool FirstSight(ImGuiID id) {
    ImGuiStorage* st = Store();
    ImGuiID key = id ^ 0x6d2b79f5u;
    if (st->GetInt(key, 0) != 0) return false;
    st->SetInt(key, 1);
    return true;
}

inline float Tween(ImGuiID id, float target, float from = 0.0f, float duration = -1.0f) {
    const Style& s = GetStyle();
    float d = duration >= 0.0f ? duration : s.anim_duration;
    if (s.intro && FirstSight(id)) imb::AnimateTo(id, from, d);
    return imb::AnimateTo(id, target, d);
}

inline float SpringTo(ImGuiID id, float target, float from = 0.0f) {
    const Style& s = GetStyle();
    if (s.intro && FirstSight(id ^ 0x1b873593u)) imb::Spring(id, from, s.spring_stiffness, s.spring_damping);
    return imb::Spring(id, target, s.spring_stiffness, s.spring_damping);
}

// float time of the last change of an int value kept in storage, -1 when it never changed
inline float ChangeTime(ImGuiID id, int value) {
    ImGuiStorage* st = Store();
    ImGuiID vk = id ^ 0x2545f491u, tk = id ^ 0x9e3779b1u, sk = id ^ 0x85ebca6bu;
    if (st->GetInt(sk, 0) == 0) {
        st->SetInt(sk, 1);
        st->SetInt(vk, value);
        st->SetFloat(tk, -1.0f);
        return -1.0f;
    }
    if (st->GetInt(vk, value) != value) {
        st->SetInt(vk, value);
        st->SetFloat(tk, Now());
    }
    return st->GetFloat(tk, -1.0f);
}

inline ImU32 HashColor(const char* b, const char* e, float sat = 0.52f, float val = 0.80f) {
    unsigned h = 2166136261u;
    for (const char* p = b; p && p < e; ++p) {
        unsigned char ch = (unsigned char)*p;
        if (ch >= 'A' && ch <= 'Z') ch = (unsigned char)(ch + 32);
        h ^= ch;
        h *= 16777619u;
    }
    h ^= h >> 13;
    float hue = (float)(h % 360u) / 360.0f, r, g, bl;
    ImGui::ColorConvertHSVtoRGB(hue, sat, val, r, g, bl);
    return IM_COL32((int)(r * 255.0f + 0.5f), (int)(g * 255.0f + 0.5f), (int)(bl * 255.0f + 0.5f), 255);
}

inline ImU32 StatusColor(Status s) {
    const Style& st = GetStyle();
    switch (s) {
        case Status::Online: return st.success;
        case Status::Away: return st.away;
        case Status::Busy: return st.danger;
        case Status::Offline: return st.offline;
        default: return 0;
    }
}

inline void CheckMark(ImDrawList* dl, const ImVec2& c, float r, ImU32 col, float th) {
    dl->PathLineTo(ImVec2(c.x - r * 0.55f, c.y + r * 0.02f));
    dl->PathLineTo(ImVec2(c.x - r * 0.15f, c.y + r * 0.42f));
    dl->PathLineTo(ImVec2(c.x + r * 0.60f, c.y - r * 0.40f));
    Stroke(dl, col, th);
}

inline void Cross(ImDrawList* dl, const ImVec2& c, float r, ImU32 col, float th) {
    dl->AddLine(ImVec2(c.x - r, c.y - r), ImVec2(c.x + r, c.y + r), col, th);
    dl->AddLine(ImVec2(c.x + r, c.y - r), ImVec2(c.x - r, c.y + r), col, th);
}

inline void Chevron(ImDrawList* dl, const ImVec2& c, float r, ImU32 col, float th) {
    dl->PathLineTo(ImVec2(c.x - r * 0.4f, c.y - r));
    dl->PathLineTo(ImVec2(c.x + r * 0.5f, c.y));
    dl->PathLineTo(ImVec2(c.x - r * 0.4f, c.y + r));
    Stroke(dl, col, th);
}

inline void DashedLine(ImDrawList* dl, const ImVec2& a, const ImVec2& b, ImU32 col, float th, float dash,
                       float gap) {
    float dx = b.x - a.x, dy = b.y - a.y, len = sqrtf(dx * dx + dy * dy);
    if (!(len > 0.0f) || !(len < 1.0e6f) || dash <= 0.0f || dash + gap <= 0.0f) return;
    float ux = dx / len, uy = dy / len;
    for (float d = 0.0f; d < len; d += dash + gap) {
        float e = Min(d + dash, len);
        dl->AddLine(ImVec2(a.x + ux * d, a.y + uy * d), ImVec2(a.x + ux * e, a.y + uy * e), col, th);
    }
}

// "a\0b\0c\0" into pointers, stops at an empty entry
inline int SplitZero(const char* s, const char** out, int cap) {
    int n = 0;
    while (s && *s && n < cap) {
        out[n++] = s;
        s += strlen(s) + 1;
    }
    return n;
}

}  // namespace detail

inline const char* StatusName(Status s) {
    switch (s) {
        case Status::Online: return "Online";
        case Status::Away: return "Away";
        case Status::Busy: return "Busy";
        case Status::Offline: return "Offline";
        default: return "";
    }
}

}  // namespace disp
}  // namespace imb
