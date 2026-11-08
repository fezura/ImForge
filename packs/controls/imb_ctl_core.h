#pragma once
// style, colors, draw and input helpers shared by the controls pack
#include "imgui.h"
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "imforge/imb_runtime.h"

// same signature since 1.7x, declared here so the pack doesn't need imgui_internal.h
namespace ImGui {
IMGUI_API void NavMoveRequestCancel();
}

namespace imb {
namespace ctl {

// 0 colors come from the ImGui style, negative sizes mean "derive from the font"
struct Style {
    ImU32 accent = 0;
    ImU32 on_accent = 0;
    ImU32 track = 0;
    ImU32 thumb = 0;
    ImU32 text = 0;
    ImU32 text_dim = 0;
    ImU32 border = 0;
    ImU32 axis[4] = {IM_COL32(226, 82, 82, 255), IM_COL32(118, 196, 72, 255), IM_COL32(72, 138, 236, 255),
                     IM_COL32(190, 190, 196, 255)};
    ImU32 star = IM_COL32(255, 189, 46, 255);
    float rounding = -1.0f;
    float anim_duration = 0.15f;
    float spring_stiffness = 280.0f;
    float spring_damping = 26.0f;
};

inline Style& GetStyle() {
    static Style s;
    return s;
}

namespace detail {

const int kMaxItems = 64;
const float kPi = 3.14159265358979f;

#if IMGUI_VERSION_NUM >= 19140
const ImGuiButtonFlags kNav = ImGuiButtonFlags_EnableNav;
#else
const ImGuiButtonFlags kNav = 0;
#endif

#if IMGUI_VERSION_NUM >= 19300
constexpr ImDrawFlags kRoundAll = ImDrawFlags_RoundAll;
constexpr ImDrawFlags kRoundLeft = ImDrawFlags_RoundLeft;
constexpr ImDrawFlags kRoundRight = ImDrawFlags_RoundRight;
constexpr ImDrawFlags kRoundNone = ImDrawFlags_RoundNone;
#else
constexpr ImDrawFlags kRoundAll = ImDrawFlags_RoundCornersAll;
constexpr ImDrawFlags kRoundLeft = ImDrawFlags_RoundCornersLeft;
constexpr ImDrawFlags kRoundRight = ImDrawFlags_RoundCornersRight;
constexpr ImDrawFlags kRoundNone = ImDrawFlags_RoundCornersNone;
#endif

inline float Dpi() { return ImGui::GetFontSize() / 13.0f; }
inline float Max(float a, float b) { return a > b ? a : b; }
inline float Min(float a, float b) { return a < b ? a : b; }
inline float Clamp(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline int ClampI(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline float Sat(float v) { return Clamp(v, 0.0f, 1.0f); }
inline float Lerp(float a, float b, float t) { return a + (b - a) * t; }
inline float Abs(float v) { return v < 0.0f ? -v : v; }
inline ImVec2 Add(const ImVec2& a, float x, float y) { return ImVec2(a.x + x, a.y + y); }
inline ImVec2 Mid(const ImVec2& a, const ImVec2& b) { return ImVec2((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f); }

inline ImGuiID Sub(ImGuiID id, int k) {
    ImGuiID h = id ^ (0x9E3779B9u * (ImGuiID)(k + 1));
    h ^= h >> 15;
    h *= 0x2C1B3C6Du;
    h ^= h >> 12;
    return h ? h : 1u;
}

// colors: Raw* are plain ImU32 for lerping, Final() applies style.Alpha once
inline ImU32 WithAlpha(ImU32 c, float mul) {
    int a = (int)((float)((c >> IM_COL32_A_SHIFT) & 0xFF) * mul + 0.5f);
    a = a < 0 ? 0 : (a > 255 ? 255 : a);
    return (c & ~IM_COL32_A_MASK) | ((ImU32)a << IM_COL32_A_SHIFT);
}
inline ImU32 Final(ImU32 raw, float mul = 1.0f) { return ImGui::GetColorU32(WithAlpha(raw, mul)); }
inline ImU32 StyleRaw(ImGuiCol c) { return ImGui::ColorConvertFloat4ToU32(ImGui::GetStyleColorVec4(c)); }
inline ImU32 Pick(ImU32 v, ImGuiCol fallback) { return v ? v : StyleRaw(fallback); }
inline ImU32 Mix(ImU32 a, ImU32 b, float t) { return imb::LerpCol(a, b, t); }
inline ImU32 AccentRaw() { return Pick(GetStyle().accent, ImGuiCol_CheckMark); }
inline ImU32 TextRaw() { return Pick(GetStyle().text, ImGuiCol_Text); }
inline ImU32 DimRaw() { return Pick(GetStyle().text_dim, ImGuiCol_TextDisabled); }
inline ImU32 TrackRaw() { return Pick(GetStyle().track, ImGuiCol_FrameBg); }
inline ImU32 BorderRaw() { return Pick(GetStyle().border, ImGuiCol_Border); }
inline ImU32 ThumbRaw() { return GetStyle().thumb ? GetStyle().thumb : IM_COL32_WHITE; }
inline ImU32 AxisRaw(int i) { return GetStyle().axis[i & 3]; }

inline bool IsBright(ImU32 c) {
    float r = (float)((c >> IM_COL32_R_SHIFT) & 0xFF), g = (float)((c >> IM_COL32_G_SHIFT) & 0xFF);
    float b = (float)((c >> IM_COL32_B_SHIFT) & 0xFF);
    return 0.299f * r + 0.587f * g + 0.114f * b > 150.0f;
}
inline ImU32 Opaque(ImU32 c) { return c | IM_COL32_A_MASK; }
inline ImU32 OnRaw(ImU32 bg) {
    if (GetStyle().on_accent) return GetStyle().on_accent;
    return IsBright(Opaque(bg)) ? IM_COL32(20, 20, 26, 255) : IM_COL32_WHITE;
}
// lighter on dark themes, darker on light ones
inline ImU32 Shade(ImU32 c, float t) { return Mix(c, IsBright(Opaque(c)) ? IM_COL32(0, 0, 0, 255) : IM_COL32_WHITE, t); }

inline float Rounding(float h, float r = -1.0f) {
    if (r < 0.0f) r = GetStyle().rounding;
    if (r < 0.0f) r = ImGui::GetStyle().FrameRounding;
    return Min(r, h * 0.5f);
}

inline float Anim(ImGuiID id, bool on, float duration = -1.0f) {
    return imb::Transition(id, on, duration < 0.0f ? GetStyle().anim_duration : duration);
}
inline float AnimTo(ImGuiID id, float target, float duration = -1.0f) {
    return imb::AnimateTo(id, target, duration < 0.0f ? GetStyle().anim_duration : duration);
}
inline float SpringTo(ImGuiID id, float target) {
    return imb::Spring(id, target, GetStyle().spring_stiffness, GetStyle().spring_damping);
}

// drawing wrappers (thickness/flags swapped places in 1.92.8)
inline void Rect(ImDrawList* dl, const ImVec2& a, const ImVec2& b, ImU32 col, float r, float th,
                 ImDrawFlags f = 0) {
#if IMGUI_VERSION_NUM >= 19280
    dl->AddRect(a, b, col, r, th, f);
#else
    dl->AddRect(a, b, col, r, f, th);
#endif
}
inline void Stroke(ImDrawList* dl, ImU32 col, float th, bool closed = false) {
    ImDrawFlags f = closed ? ImDrawFlags_Closed : 0;
#if IMGUI_VERSION_NUM >= 19280
    dl->PathStroke(col, th, f);
#else
    dl->PathStroke(col, f, th);
#endif
}

inline void Shadow(ImDrawList* dl, const ImVec2& a, const ImVec2& b, float r, float strength = 1.0f) {
    if (strength <= 0.01f) return;
    imb::Shadow(dl, a, b, r, Final(IM_COL32(0, 0, 0, 90), strength), 3.0f * Dpi(), ImVec2(0, 1.0f * Dpi()));
}

inline void Checker(ImDrawList* dl, const ImVec2& a, const ImVec2& b, float cell) {
    dl->AddRectFilled(a, b, Final(IM_COL32(204, 204, 204, 255)));
    ImU32 dark = Final(IM_COL32(128, 128, 128, 255));
    int row = 0;
    for (float y = a.y; y < b.y; y += cell, ++row)
        for (float x = a.x + ((row & 1) ? cell : 0.0f); x < b.x; x += cell * 2.0f)
            dl->AddRectFilled(ImVec2(x, y), ImVec2(Min(x + cell, b.x), Min(y + cell, b.y)), dark);
}

// gradient bar with rounded ends, color_at(t) for t in 0..1. built from columns so any
// number of color stops look right
template <class F>
inline void GradientBar(ImDrawList* dl, const ImVec2& a, const ImVec2& b, float rounding, bool vertical,
                        F color_at, int columns = 48) {
    float len = vertical ? b.y - a.y : b.x - a.x, thick = vertical ? b.x - a.x : b.y - a.y;
    if (len <= 0.0f || thick <= 0.0f) return;
    float r = Min(rounding, Min(len, thick) * 0.5f);
    if (columns < 2) columns = 2;
    ImVec2 uv = ImGui::GetIO().Fonts->TexUvWhitePixel;
    dl->PrimReserve(columns * 6, (columns + 1) * 2);
    ImDrawIdx base = (ImDrawIdx)dl->_VtxCurrentIdx;
    for (int i = 0; i <= columns; ++i) {
        float t = (float)i / (float)columns, d = t * len, inset = 0.0f;
        float e = d < r ? r - d : (d > len - r ? d - (len - r) : 0.0f);
        if (e > 0.0f) inset = r - std::sqrt(Max(r * r - e * e, 0.0f));
        ImU32 c = Final(color_at(t));
        if (vertical) {
            dl->PrimWriteVtx(ImVec2(a.x + inset, a.y + d), uv, c);
            dl->PrimWriteVtx(ImVec2(b.x - inset, a.y + d), uv, c);
        } else {
            dl->PrimWriteVtx(ImVec2(a.x + d, a.y + inset), uv, c);
            dl->PrimWriteVtx(ImVec2(a.x + d, b.y - inset), uv, c);
        }
    }
    for (int i = 0; i < columns; ++i) {
        ImDrawIdx k = (ImDrawIdx)(base + i * 2);
        dl->PrimWriteIdx(k);
        dl->PrimWriteIdx((ImDrawIdx)(k + 1));
        dl->PrimWriteIdx((ImDrawIdx)(k + 3));
        dl->PrimWriteIdx(k);
        dl->PrimWriteIdx((ImDrawIdx)(k + 3));
        dl->PrimWriteIdx((ImDrawIdx)(k + 2));
    }
}

// vector icons, s = half size
inline void Chevron(ImDrawList* dl, const ImVec2& c, float s, float angle, ImU32 col, float th) {
    float ca = std::cos(angle), sa = std::sin(angle);
    const float px[3] = {-s, 0.0f, s}, py[3] = {-s * 0.5f, s * 0.5f, -s * 0.5f};
    for (int i = 0; i < 3; ++i) dl->PathLineTo(ImVec2(c.x + px[i] * ca - py[i] * sa, c.y + px[i] * sa + py[i] * ca));
    Stroke(dl, col, th);
}
inline void Cross(ImDrawList* dl, const ImVec2& c, float s, ImU32 col, float th) {
    dl->AddLine(ImVec2(c.x - s, c.y - s), ImVec2(c.x + s, c.y + s), col, th);
    dl->AddLine(ImVec2(c.x + s, c.y - s), ImVec2(c.x - s, c.y + s), col, th);
}
inline void Plus(ImDrawList* dl, const ImVec2& c, float s, ImU32 col, float th, bool vertical_bar = true) {
    dl->AddLine(ImVec2(c.x - s, c.y), ImVec2(c.x + s, c.y), col, th);
    if (vertical_bar) dl->AddLine(ImVec2(c.x, c.y - s), ImVec2(c.x, c.y + s), col, th);
}
// t reveals the stroke from the short leg to the long one
inline void Check(ImDrawList* dl, const ImVec2& c, float s, ImU32 col, float th, float t = 1.0f) {
    if (t <= 0.0f) return;
    ImVec2 p0(c.x - s * 0.85f, c.y + s * 0.02f), p1(c.x - s * 0.25f, c.y + s * 0.6f), p2(c.x + s * 0.9f, c.y - s * 0.65f);
    float l0 = std::sqrt((p1.x - p0.x) * (p1.x - p0.x) + (p1.y - p0.y) * (p1.y - p0.y));
    float l1 = std::sqrt((p2.x - p1.x) * (p2.x - p1.x) + (p2.y - p1.y) * (p2.y - p1.y));
    float d = Sat(t) * (l0 + l1);
    float k = d <= l0 ? d / l0 : (d - l0) / l1;
    ImVec2 pts[3] = {p0, p1, p2};
    int n = d <= l0 ? 1 : 2;
    pts[n] = ImVec2(Lerp(pts[n - 1].x, pts[n].x, k), Lerp(pts[n - 1].y, pts[n].y, k));
    for (int i = 0; i <= n; ++i) dl->PathLineTo(pts[i]);
    Stroke(dl, col, th);
}
inline void Magnifier(ImDrawList* dl, const ImVec2& c, float s, ImU32 col, float th) {
    ImVec2 o(c.x - s * 0.2f, c.y - s * 0.2f);
    dl->AddCircle(o, s * 0.62f, col, 16, th);
    dl->AddLine(ImVec2(o.x + s * 0.45f, o.y + s * 0.45f), ImVec2(c.x + s * 0.85f, c.y + s * 0.85f), col, th);
}
inline void StarPoints(const ImVec2& c, float ro, float ri, ImVec2* out) {
    for (int i = 0; i < 10; ++i) {
        float a = -kPi * 0.5f + (float)i * kPi / 5.0f, r = (i & 1) ? ri : ro;
        out[i] = ImVec2(c.x + std::cos(a) * r, c.y + std::sin(a) * r);
    }
}
inline void StarFilled(ImDrawList* dl, const ImVec2& c, float ro, ImU32 col) {
    ImVec2 p[10];
    StarPoints(c, ro, ro * 0.48f, p);
#if IMGUI_VERSION_NUM >= 19000
    dl->AddConcavePolyFilled(p, 10, col);
#else
    ImVec2 inner[5] = {p[1], p[3], p[5], p[7], p[9]};
    dl->AddConvexPolyFilled(inner, 5, col);
    for (int i = 0; i < 5; ++i) dl->AddTriangleFilled(p[(i * 2 + 9) % 10], p[i * 2], p[i * 2 + 1], col);
#endif
}
inline void StarOutline(ImDrawList* dl, const ImVec2& c, float ro, ImU32 col, float th) {
    ImVec2 p[10];
    StarPoints(c, ro, ro * 0.48f, p);
    for (int i = 0; i < 10; ++i) dl->PathLineTo(p[i]);
    Stroke(dl, col, th, true);
}

inline const char* LabelEnd(const char* label) {
    if (!label) return label;
    const char* p = strstr(label, "##");
    return p ? p : label + strlen(label);
}

inline void TextIn(ImDrawList* dl, const ImVec2& a, const ImVec2& b, const char* s, const char* e, ImU32 col,
                   float align_x = 0.5f, float pad_x = 0.0f) {
    if (!s || (e && e <= s) || !*s) return;
    ImVec2 ts = ImGui::CalcTextSize(s, e);
    float x = a.x + pad_x + Max(0.0f, b.x - a.x - pad_x * 2.0f - ts.x) * align_x;
    ImVec4 clip(a.x, a.y, b.x, b.y);
    dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(x, a.y + (b.y - a.y - ts.y) * 0.5f), col, s, e, 0.0f,
                &clip);
}

// label text after the widget as its own layout item, vertically centered on h
inline void TrailingLabel(const char* label, const char* end, float h) {
    if (!label || end <= label) return;
    ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
    ImVec2 ts = ImGui::CalcTextSize(label, end);
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(ts.x, h));
    ImGui::GetWindowDrawList()->AddText(ImVec2(p.x, p.y + (h - ts.y) * 0.5f), Final(TextRaw()), label, end);
}

inline float TextW(const char* s, const char* e = nullptr) { return s ? ImGui::CalcTextSize(s, e).x : 0.0f; }

inline float LabelWidth(const char* label, const char* end) {
    if (!label || end <= label) return 0.0f;
    return ImGui::CalcTextSize(label, end).x + ImGui::GetStyle().ItemInnerSpacing.x;
}

// zero separated "a\0b\0" into pointers, at most cap entries
inline int SplitZero(const char* s, const char** out, int cap) {
    int n = 0;
    while (s && *s && n < cap) {
        out[n++] = s;
        s += strlen(s) + 1;
    }
    return n;
}

inline int Fmt(char* buf, int size, const char* fmt, float v) {
    int n = snprintf(buf, (size_t)size, fmt && *fmt ? fmt : "%.2f", (double)v);
    return n < 0 ? 0 : (n >= size ? size - 1 : n);
}

inline float RoundTo(float v, float step) { return step > 0.0f ? std::floor(v / step + 0.5f) * step : v; }
inline int RoundI(float v) { return (int)std::floor(v + 0.5f); }

// "%3d px" to "%3.0f px" so int overloads can reuse the float widgets
inline const char* IntFmt(const char* fmt, char* out, int size) {
    if (!fmt || !*fmt) fmt = "%d";
    const char* p = fmt;
    while ((p = strchr(p, '%')) != nullptr && p[1] == '%') p += 2;
    if (!p) {
        snprintf(out, (size_t)size, "%s", fmt);
        return out;
    }
    const char* q = p + 1;
    while (*q && strchr("-+ #0", *q)) ++q;
    while (*q >= '0' && *q <= '9') ++q;
    const char* spec_end = q;
    if (*q == '.') {
        ++q;
        while (*q >= '0' && *q <= '9') ++q;
    }
    if (*q == 'd' || *q == 'i' || *q == 'u')
        snprintf(out, (size_t)size, "%.*s.0f%s", (int)(spec_end - fmt), fmt, q + 1);
    else
        snprintf(out, (size_t)size, "%s", fmt);
    return out;
}

// a style color composited over the window background, for surfaces that must be opaque
inline ImU32 Solid(ImGuiCol c) {
    ImVec4 bg = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg), fg = ImGui::GetStyleColorVec4(c);
    ImVec4 o(Lerp(bg.x, fg.x, fg.w), Lerp(bg.y, fg.y, fg.w), Lerp(bg.z, fg.z, fg.w), 1.0f);
    return ImGui::ColorConvertFloat4ToU32(o);
}

inline bool KeyPressed(ImGuiKey k, bool repeat = true) {
#if IMGUI_VERSION_NUM >= 18700
    return ImGui::IsKeyPressed(k, repeat);
#else
    int i = ImGui::GetKeyIndex(k);
    return i >= 0 && i < IM_ARRAYSIZE(ImGui::GetIO().KeysDown) && ImGui::IsKeyPressed(i, repeat);
#endif
}

// letters: ImGuiKey_A.. on 1.87+, the ASCII code as native index before that
inline bool LetterPressed(char c, bool repeat = false) {
#if IMGUI_VERSION_NUM >= 18700
    return ImGui::IsKeyPressed((ImGuiKey)(ImGuiKey_A + (c - 'A')), repeat);
#else
    return ImGui::IsKeyPressed((int)c, repeat);
#endif
}

// arrow keys on the focused last item; cancels the nav move they would start
inline int FocusedArrows(bool horizontal = true, bool vertical = false) {
    if (!ImGui::IsItemFocused()) return 0;
    int d = 0;
    if (horizontal) d += (KeyPressed(ImGuiKey_RightArrow) ? 1 : 0) - (KeyPressed(ImGuiKey_LeftArrow) ? 1 : 0);
    if (vertical) d += (KeyPressed(ImGuiKey_UpArrow) ? 1 : 0) - (KeyPressed(ImGuiKey_DownArrow) ? 1 : 0);
    if (d != 0) ImGui::NavMoveRequestCancel();
    return d;
}

// a few persistent text buffers for widgets with their own input line, recycled by age
struct TextSlot {
    ImGuiID id;
    int frame;
    char buf[128];
};
inline char* TextBuf(ImGuiID id, int* out_size = nullptr) {
    static TextSlot slots[32];
    int frame = ImGui::GetFrameCount(), lru = 0;
    for (int i = 0; i < 32; ++i) {
        if (slots[i].id == id) {
            slots[i].frame = frame;
            if (out_size) *out_size = (int)sizeof(slots[i].buf);
            return slots[i].buf;
        }
        if (slots[i].frame < slots[lru].frame) lru = i;
    }
    TextSlot& s = slots[lru];
    s.id = id;
    s.frame = frame;
    s.buf[0] = 0;
    if (out_size) *out_size = (int)sizeof(s.buf);
    return s.buf;
}

// frame + 1px border, the way ImGui frames look when FrameBorderSize > 0
inline void Frame(ImDrawList* dl, const ImVec2& a, const ImVec2& b, ImU32 fill, float r, ImDrawFlags f = 0) {
    dl->AddRectFilled(a, b, fill, r, f);
    float bs = ImGui::GetStyle().FrameBorderSize;
    if (bs > 0.0f) Rect(dl, a, b, Final(BorderRaw()), r, bs, f);
}

}  // namespace detail

// key: 0 none, >0 ImGuiKey (1.87+) or native io.KeysDown index (1.85/1.86), mouse buttons are
// kMouseKey + button. chords add the kMod* bits on top
const int kModCtrl = 1 << 16;
const int kModShift = 1 << 17;
const int kModAlt = 1 << 18;
const int kModSuper = 1 << 19;
const int kModMask = kModCtrl | kModShift | kModAlt | kModSuper;
const int kMouseKey = 0x7F00;

inline const char* KeyName(int key) {
    static char buf[32];
    key &= ~kModMask;
    if (key <= 0) return "None";
    if (key >= kMouseKey && key < kMouseKey + 5) {
        snprintf(buf, sizeof(buf), "Mouse%d", key - kMouseKey + 1);
        return buf;
    }
#if IMGUI_VERSION_NUM >= 18700
    if (key >= ImGuiKey_NamedKey_BEGIN && key < ImGuiKey_NamedKey_END) return ImGui::GetKeyName((ImGuiKey)key);
#else
    static const struct { int k; const char* n; } kNamed[] = {
        {ImGuiKey_Tab, "Tab"},         {ImGuiKey_LeftArrow, "Left"},     {ImGuiKey_RightArrow, "Right"},
        {ImGuiKey_UpArrow, "Up"},      {ImGuiKey_DownArrow, "Down"},     {ImGuiKey_PageUp, "PageUp"},
        {ImGuiKey_PageDown, "PageDown"}, {ImGuiKey_Home, "Home"},        {ImGuiKey_End, "End"},
        {ImGuiKey_Insert, "Insert"},   {ImGuiKey_Delete, "Delete"},      {ImGuiKey_Backspace, "Backspace"},
        {ImGuiKey_Space, "Space"},     {ImGuiKey_Enter, "Enter"},        {ImGuiKey_Escape, "Escape"},
        {ImGuiKey_KeyPadEnter, "KeypadEnter"}};
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

}  // namespace ctl
}  // namespace imb
