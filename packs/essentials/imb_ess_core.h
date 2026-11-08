#pragma once
// style, helpers, KeyName, Spinner, HotkeyLabel, GradientHeader, GlowButton
#include "imgui.h"
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>
#ifndef IMB_ESS_NO_RUNTIME
#include "imforge/imb_runtime.h"
#endif

namespace imb {
namespace ess {

// 0 colors use the ImGui style, 0 sizes scale with the font
struct Style {
    float rounding = 0.0f;
    float card_rounding = 0.0f;
    float anim_duration = 0.15f;
    float glow_size = 0.0f;
    float toast_width = 0.0f;
    ImU32 accent = 0;
    ImU32 track = 0;
    ImU32 knob = 0;
    ImU32 text = 0;
    ImU32 text_dim = 0;
    ImU32 border = 0;
    ImU32 card_bg = 0;
    ImU32 card_header = 0;
    ImU32 keycap_bg = 0;
    ImU32 toast_bg = 0;
    ImU32 toast_info = IM_COL32(52, 152, 219, 255);
    ImU32 toast_success = IM_COL32(46, 204, 113, 255);
    ImU32 toast_warning = IM_COL32(241, 196, 15, 255);
    ImU32 toast_error = IM_COL32(231, 76, 60, 255);
};

inline Style& GetStyle() {
    static Style s;
    return s;
}

namespace detail {

inline ImVec2 Add(const ImVec2& a, const ImVec2& b) { return ImVec2(a.x + b.x, a.y + b.y); }
inline float Lerp(float a, float b, float t) { return a + (b - a) * t; }
inline float Clamp01(float t) { return t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t); }
inline float Max(float a, float b) { return a > b ? a : b; }
inline float Min(float a, float b) { return a < b ? a : b; }

#if IMGUI_VERSION_NUM >= 19300
constexpr ImDrawFlags kRoundAll = ImDrawFlags_RoundAll;
constexpr ImDrawFlags kRoundTop = ImDrawFlags_RoundTop;
constexpr ImDrawFlags kRoundBottom = ImDrawFlags_RoundBottom;
constexpr ImDrawFlags kRoundLeft = ImDrawFlags_RoundLeft;
constexpr ImDrawFlags kRoundTopLeft = ImDrawFlags_RoundTopLeft;
constexpr ImDrawFlags kRoundBottomLeft = ImDrawFlags_RoundBottomLeft;
constexpr ImDrawFlags kRoundBottomRight = ImDrawFlags_RoundBottomRight;
#else
constexpr ImDrawFlags kRoundAll = ImDrawFlags_RoundCornersAll;
constexpr ImDrawFlags kRoundTop = ImDrawFlags_RoundCornersTop;
constexpr ImDrawFlags kRoundBottom = ImDrawFlags_RoundCornersBottom;
constexpr ImDrawFlags kRoundLeft = ImDrawFlags_RoundCornersLeft;
constexpr ImDrawFlags kRoundTopLeft = ImDrawFlags_RoundCornersTopLeft;
constexpr ImDrawFlags kRoundBottomLeft = ImDrawFlags_RoundCornersBottomLeft;
constexpr ImDrawFlags kRoundBottomRight = ImDrawFlags_RoundCornersBottomRight;
#endif

inline void Outline(ImDrawList* dl, const ImVec2& mn, const ImVec2& mx, ImU32 col, float rounding,
                    float thickness) {
#if IMGUI_VERSION_NUM >= 19280
    dl->AddRect(mn, mx, col, rounding, thickness, 0);
#else
    dl->AddRect(mn, mx, col, rounding, 0, thickness);
#endif
}

inline const char* LabelEnd(const char* label) {
    const char* p = strstr(label, "##");
    return p ? p : label + strlen(label);
}

inline ImU32 WithAlpha(ImU32 c, float mul) {
    float a = (float)((c >> IM_COL32_A_SHIFT) & 0xFF) * mul;
    int ai = (int)(a + 0.5f);
    ai = ai < 0 ? 0 : (ai > 255 ? 255 : ai);
    return (c & ~IM_COL32_A_MASK) | ((ImU32)ai << IM_COL32_A_SHIFT);
}

inline ImU32 LerpColor(ImU32 a, ImU32 b, float t) {
#ifdef IMB_ESS_NO_RUNTIME
    ImU32 r = 0;
    for (int s = 0; s < 32; s += 8) {
        int ca = (int)((a >> s) & 0xFF), cb = (int)((b >> s) & 0xFF);
        r |= (ImU32)(ca + (int)((float)(cb - ca) * t)) << s;
    }
    return r;
#else
    return imb::LerpCol(a, b, t);
#endif
}

// style.Alpha is always applied
inline ImU32 Col(ImU32 v, ImGuiCol fallback, float alpha_mul = 1.0f) {
    if (v == 0) return ImGui::GetColorU32(fallback, alpha_mul);
    return ImGui::GetColorU32(WithAlpha(v, alpha_mul));
}

inline ImU32 Accent(float alpha_mul = 1.0f) {
    return Col(GetStyle().accent, ImGuiCol_CheckMark, alpha_mul);
}
inline ImU32 AccentRaw() {
    Style& s = GetStyle();
    if (s.accent) return s.accent;
    return ImGui::ColorConvertFloat4ToU32(ImGui::GetStyleColorVec4(ImGuiCol_CheckMark));
}
inline ImU32 TextCol(float alpha_mul = 1.0f) {
    return Col(GetStyle().text, ImGuiCol_Text, alpha_mul);
}
inline ImU32 TextDim(float alpha_mul = 1.0f) {
    return Col(GetStyle().text_dim, ImGuiCol_TextDisabled, alpha_mul);
}
inline ImU32 BorderCol(float alpha_mul = 1.0f) {
    return Col(GetStyle().border, ImGuiCol_Border, alpha_mul);
}
inline ImU32 KnobCol(float alpha_mul = 1.0f) {
    Style& s = GetStyle();
    return ImGui::GetColorU32(WithAlpha(s.knob ? s.knob : IM_COL32_WHITE, alpha_mul));
}
inline ImU32 CardBg() {
    Style& s = GetStyle();
    if (s.card_bg) return ImGui::GetColorU32(s.card_bg);
    ImVec4 bg = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
    ImVec4 tx = ImGui::GetStyleColorVec4(ImGuiCol_Text);
    ImVec4 c(Lerp(bg.x, tx.x, 0.06f), Lerp(bg.y, tx.y, 0.06f), Lerp(bg.z, tx.z, 0.06f), 1.0f);
    return ImGui::GetColorU32(c);
}

inline float Rounding() {
    float r = GetStyle().rounding;
    return r > 0.0f ? r : ImGui::GetStyle().FrameRounding;
}
inline float CardRounding() {
    float r = GetStyle().card_rounding;
    return r > 0.0f ? r : ImGui::GetStyle().ChildRounding;
}
inline float GlowSize() {
    float g = GetStyle().glow_size;
    return g > 0.0f ? g : ImGui::GetFontSize() * 0.6f;
}

#ifdef IMB_ESS_NO_RUNTIME
struct AnimSlot {
    ImGuiID id;
    float v;
    int frame;
};
inline float FallbackAnim(ImGuiID id, float target, float duration, bool unit) {
    static ImVector<AnimSlot> slots;
    int frame = ImGui::GetFrameCount();
    for (int i = 0; i < slots.Size; ++i) {
        AnimSlot& s = slots[i];
        if (s.id != id) continue;
        if (s.frame == frame) return s.v;
        float dt = ImGui::GetIO().DeltaTime;
        if (unit) {
            float step = duration > 0.0f ? dt / duration : 1.0f;
            if (s.v < target) s.v = Min(s.v + step, target);
            else s.v = Max(s.v - step, target);
        } else {
            float k = duration > 0.0f ? Min(1.0f, dt / duration * 4.0f) : 1.0f;
            s.v += (target - s.v) * k;
        }
        s.frame = frame;
        return s.v;
    }
    AnimSlot n;
    n.id = id;
    n.v = target;
    n.frame = frame;
    slots.push_back(n);
    return target;
}
#endif

inline float Anim(ImGuiID id, bool on, float duration) {
#ifdef IMB_ESS_NO_RUNTIME
    float t = FallbackAnim(id, on ? 1.0f : 0.0f, duration, true);
    float u = 1.0f - t;
    return 1.0f - u * u * u;
#else
    return imb::Transition(id, on, duration);
#endif
}

inline float AnimTo(ImGuiID id, float target, float duration) {
#ifdef IMB_ESS_NO_RUNTIME
    return FallbackAnim(id, target, duration, false);
#else
    return imb::AnimateTo(id, target, duration);
#endif
}

inline void Gradient(ImDrawList* dl, const ImVec2& mn, const ImVec2& mx, ImU32 c0, ImU32 c1,
                     bool vertical, float rounding) {
#ifdef IMB_ESS_NO_RUNTIME
    (void)rounding;
    if (vertical) dl->AddRectFilledMultiColor(mn, mx, c0, c0, c1, c1);
    else dl->AddRectFilledMultiColor(mn, mx, c0, c1, c1, c0);
#else
    imb::GradientRect(dl, mn, mx, c0, c1, vertical, rounding);
#endif
}

inline void Glow(ImDrawList* dl, const ImVec2& mn, const ImVec2& mx, float rounding, ImU32 color,
                 float size) {
#ifdef IMB_ESS_NO_RUNTIME
    for (int i = 1; i <= 3; ++i) {
        float e = size * (float)i / 3.0f;
        Outline(dl, ImVec2(mn.x - e, mn.y - e), ImVec2(mx.x + e, mx.y + e),
                WithAlpha(color, 0.35f / (float)i), rounding + e, size / 3.0f);
    }
#else
    imb::Glow(dl, mn, mx, rounding, color, size);
#endif
}

inline void Shadow(ImDrawList* dl, const ImVec2& mn, const ImVec2& mx, float rounding, ImU32 color,
                   float size) {
#ifdef IMB_ESS_NO_RUNTIME
    for (int i = 1; i <= 3; ++i) {
        float e = size * (float)i / 3.0f;
        Outline(dl, ImVec2(mn.x - e, mn.y - e + 2.0f), ImVec2(mx.x + e, mx.y + e + 2.0f),
                WithAlpha(color, 0.5f / (float)i), rounding + e, size / 3.0f);
    }
#else
    imb::Shadow(dl, mn, mx, rounding, color, size);
#endif
}

inline void TextIn(ImDrawList* dl, const ImVec2& mn, const ImVec2& mx, const char* b, const char* e,
                   ImU32 col, float align_x = 0.5f, float pad_x = 0.0f) {
    ImVec2 ts = ImGui::CalcTextSize(b, e);
    float x = mn.x + pad_x + (mx.x - mn.x - pad_x * 2.0f - ts.x) * align_x;
    ImVec2 pos(x, mn.y + (mx.y - mn.y - ts.y) * 0.5f);
    ImVec4 clip(mn.x, mn.y, mx.x, mx.y);
    dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), pos, col, b, e, 0.0f, &clip);
}

inline bool IsBright(ImU32 c) {
    float r = (float)((c >> IM_COL32_R_SHIFT) & 0xFF);
    float g = (float)((c >> IM_COL32_G_SHIFT) & 0xFF);
    float b = (float)((c >> IM_COL32_B_SHIFT) & 0xFF);
    return (0.299f * r + 0.587f * g + 0.114f * b) > 160.0f;
}

inline ImU32 DarkText() { return ImGui::GetColorU32(IM_COL32(20, 20, 24, 255)); }

inline int SplitZero(const char* s, const char** out, int cap) {
    int n = 0;
    while (s && *s && n < cap) {
        out[n++] = s;
        s += strlen(s) + 1;
    }
    return n;
}

}  // namespace detail

// key: 0 none, -1..-5 Mouse1..Mouse5, >0 ImGuiKey (1.87+) or io.KeysDown index (1.85/1.86)
inline const char* KeyName(int key) {
    static char buf[32];
    if (key == 0) return "None";
    if (key < 0) {
        int b = -key;
        if (b < 1 || b > 5) return "None";
        snprintf(buf, sizeof(buf), "Mouse%d", b);
        return buf;
    }
#if IMGUI_VERSION_NUM >= 18700
    if (key >= ImGuiKey_NamedKey_BEGIN && key < ImGuiKey_NamedKey_END)
        return ImGui::GetKeyName((ImGuiKey)key);
#else
    static const struct { int k; const char* n; } kNamed[] = {
        {ImGuiKey_Tab, "Tab"},         {ImGuiKey_LeftArrow, "Left"},
        {ImGuiKey_RightArrow, "Right"}, {ImGuiKey_UpArrow, "Up"},
        {ImGuiKey_DownArrow, "Down"},   {ImGuiKey_PageUp, "PageUp"},
        {ImGuiKey_PageDown, "PageDown"}, {ImGuiKey_Home, "Home"},
        {ImGuiKey_End, "End"},          {ImGuiKey_Insert, "Insert"},
        {ImGuiKey_Delete, "Delete"},    {ImGuiKey_Backspace, "Backspace"},
        {ImGuiKey_Space, "Space"},      {ImGuiKey_Enter, "Enter"},
        {ImGuiKey_Escape, "Escape"},    {ImGuiKey_KeyPadEnter, "KeypadEnter"}};
    const ImGuiIO& io = ImGui::GetIO();
    for (int i = 0; i < IM_ARRAYSIZE(kNamed); ++i)
        if (io.KeyMap[kNamed[i].k] == key) return kNamed[i].n;
    if ((key >= '0' && key <= '9') || (key >= 'A' && key <= 'Z')) {
        buf[0] = (char)key;
        buf[1] = 0;
        return buf;
    }
#endif
    snprintf(buf, sizeof(buf), "Key %d", key);
    return buf;
}

inline void Spinner(const char* id, float radius, float thickness, ImU32 color) {
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGui::PushID(id);
    ImGui::Dummy(ImVec2(radius * 2.0f, radius * 2.0f));
    ImGui::PopID();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 c(pos.x + radius, pos.y + radius);
    float r = detail::Max(radius - thickness * 0.5f, 1.0f);
    float t = (float)ImGui::GetTime();
    float a0 = t * 5.0f;
    float arc = 1.0f + 0.75f * sinf(t * 2.5f);
    ImU32 col = detail::Col(color, ImGuiCol_CheckMark);
    dl->AddCircle(c, r, detail::WithAlpha(col, 0.15f), 0, thickness);
    dl->PathClear();
    dl->PathArcTo(c, r, a0, a0 + arc * 3.14159265f, 0);
#if IMGUI_VERSION_NUM >= 19280
    dl->PathStroke(col, thickness, 0);
#else
    dl->PathStroke(col, 0, thickness);
#endif
}

inline void Spinner(const char* id, float radius, float thickness, const ImVec4& color) {
    Spinner(id, radius, thickness, ImGui::ColorConvertFloat4ToU32(color));
}

inline void HotkeyLabel(const char* text, int key) {
    const ImGuiStyle& st = ImGui::GetStyle();
    ImGui::TextUnformatted(text);
    ImGui::SameLine(0.0f, st.ItemInnerSpacing.x);
    const char* name = KeyName(key);
    float font = ImGui::GetFontSize();
    ImVec2 ts = ImGui::CalcTextSize(name);
    ImVec2 size(ts.x + font * 0.8f, ImGui::GetFrameHeight());
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGui::Dummy(size);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 mx = detail::Add(pos, size);
    float r = detail::Max(detail::Rounding(), 3.0f);
    float lip = detail::Max(2.0f, font * 0.15f);
    dl->AddRectFilled(pos, mx, detail::BorderCol(), r);
    ImU32 cap = detail::Col(GetStyle().keycap_bg, ImGuiCol_FrameBg);
    dl->AddRectFilled(pos, ImVec2(mx.x, mx.y - lip), cap, r);
    dl->AddRect(pos, mx, detail::BorderCol(0.6f), r);
    detail::TextIn(dl, pos, ImVec2(mx.x, mx.y - lip), name, nullptr, detail::TextCol());
}

inline void GradientHeader(const char* label, ImU32 c0, ImU32 c1, float height = 0.0f,
                           bool centered = false) {
    const ImGuiStyle& st = ImGui::GetStyle();
    float h = height > 0.0f ? height : ImGui::GetFrameHeight();
    float w = detail::Max(ImGui::GetContentRegionAvail().x, 1.0f);
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGui::PushID(label);
    ImGui::Dummy(ImVec2(w, h));
    ImGui::PopID();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 mx(pos.x + w, pos.y + h);
    ImU32 a = c0 ? ImGui::GetColorU32(c0) : detail::Accent();
    ImU32 b = c1 ? ImGui::GetColorU32(c1) : detail::Accent(0.0f);
    detail::Gradient(dl, pos, mx, a, b, false, detail::Rounding());
    const char* end = detail::LabelEnd(label);
    ImU32 tc = detail::IsBright(a) ? detail::DarkText() : detail::TextCol();
    detail::TextIn(dl, pos, mx, label, end, tc, centered ? 0.5f : 0.0f, st.FramePadding.x);
}

inline void GradientHeader(const char* label, const ImVec4& c0, const ImVec4& c1,
                           float height = 0.0f, bool centered = false) {
    GradientHeader(label, ImGui::ColorConvertFloat4ToU32(c0), ImGui::ColorConvertFloat4ToU32(c1),
                   height, centered);
}

inline bool GlowButton(const char* label, const ImVec2& size = ImVec2(0, 0), ImU32 color = 0) {
    const ImGuiStyle& st = ImGui::GetStyle();
    const char* end = detail::LabelEnd(label);
    ImVec2 ts = ImGui::CalcTextSize(label, end);
    ImVec2 sz(size.x != 0.0f ? size.x : ts.x + st.FramePadding.x * 2.0f,
              size.y != 0.0f ? size.y : ts.y + st.FramePadding.y * 2.0f);
    ImGuiID id = ImGui::GetID(label);
    bool pressed = ImGui::InvisibleButton(label, sz);
    bool hovered = ImGui::IsItemHovered();
    bool held = ImGui::IsItemActive();
    ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
    float t = detail::Anim(id, hovered || held, GetStyle().anim_duration);
    ImU32 base = color ? color : detail::AccentRaw();
    float r = detail::Rounding();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (t > 0.01f) {
        ImU32 glow = ImGui::GetColorU32(detail::WithAlpha(base, 0.6f * t));
        detail::Glow(dl, mn, mx, r, glow, detail::GlowSize() * (0.6f + 0.4f * t));
    }
    float lift = held ? 0.22f : 0.12f * t;
    dl->AddRectFilled(mn, mx, ImGui::GetColorU32(detail::LerpColor(base, IM_COL32_WHITE, lift)), r);
    dl->AddRect(mn, mx, ImGui::GetColorU32(IM_COL32(255, 255, 255, (int)(40.0f + 60.0f * t))), r);
    ImU32 tc = detail::IsBright(base) ? detail::DarkText() : ImGui::GetColorU32(IM_COL32_WHITE);
    detail::TextIn(dl, mn, mx, label, end, tc, 0.5f, st.FramePadding.x);
    return pressed;
}

inline bool GlowButton(const char* label, const ImVec2& size, const ImVec4& color) {
    return GlowButton(label, size, ImGui::ColorConvertFloat4ToU32(color));
}

}  // namespace ess
}  // namespace imb
