#pragma once
// ColorPalette, HueStrip, SatValBox, ColorPickerMini
#include <cstddef>
#include "imb_ctl_core.h"

namespace imb {
namespace ctl {

// "#rgb", "#rgba", "#rrggbb" or "#rrggbbaa", leading # optional
inline bool ParseHexColor(const char* s, ImVec4* out) {
    if (!s || !out) return false;
    if (*s == '#') ++s;
    size_t n = strlen(s);
    if (n != 3 && n != 4 && n != 6 && n != 8) return false;
    unsigned v[8] = {0, 0, 0, 0, 0, 0, 15, 15};
    for (size_t i = 0; i < n; ++i) {
        char c = s[i];
        if (c >= '0' && c <= '9') v[i] = (unsigned)(c - '0');
        else if (c >= 'a' && c <= 'f') v[i] = (unsigned)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') v[i] = (unsigned)(c - 'A' + 10);
        else return false;
    }
    float rgba[4];
    if (n <= 4) {
        for (int i = 0; i < 4; ++i) rgba[i] = (float)(v[i] * 17u) / 255.0f;
        if (n == 3) rgba[3] = 1.0f;
    } else {
        for (int i = 0; i < 4; ++i) rgba[i] = (float)(v[i * 2] * 16u + v[i * 2 + 1]) / 255.0f;
        if (n == 6) rgba[3] = 1.0f;
    }
    *out = ImVec4(rgba[0], rgba[1], rgba[2], rgba[3]);
    return true;
}

inline void FormatHexColor(const ImVec4& c, char* buf, int size, bool alpha = true) {
    int r = detail::RoundI(detail::Sat(c.x) * 255.0f), g = detail::RoundI(detail::Sat(c.y) * 255.0f);
    int b = detail::RoundI(detail::Sat(c.z) * 255.0f), a = detail::RoundI(detail::Sat(c.w) * 255.0f);
    if (alpha) snprintf(buf, (size_t)size, "#%02x%02x%02x%02x", r, g, b, a);
    else snprintf(buf, (size_t)size, "#%02x%02x%02x", r, g, b);
}

struct PaletteStyle {
    int columns = 0;
    float swatch = 0.0f;
    float spacing = -1.0f;
    bool tooltip = true;
};

namespace detail {

inline bool SameColor(const ImVec4& a, const ImVec4& b) {
    return Abs(a.x - b.x) < 0.002f && Abs(a.y - b.y) < 0.002f && Abs(a.z - b.z) < 0.002f && Abs(a.w - b.w) < 0.002f;
}

inline void Swatch(ImDrawList* dl, const ImVec2& a, const ImVec2& b, const ImVec4& col, float r) {
    if (col.w < 1.0f) {
        dl->PushClipRect(a, b, true);
        Checker(dl, a, b, Max(3.0f, (b.x - a.x) * 0.25f));
        dl->PopClipRect();
    }
    dl->AddRectFilled(a, b, ImGui::GetColorU32(col), r);
}

}  // namespace detail

// swatch grid; *selected is the index, *color (optional) receives the picked color and selects the
// swatch it matches. columns 0 fits as many as the width allows
inline bool ColorPalette(const char* label, int* selected, ImVec4* color, const ImVec4* colors, int count,
                         const PaletteStyle& ps = PaletteStyle()) {
    if (!colors || count <= 0) return false;
    const ImGuiStyle& st = ImGui::GetStyle();
    float dpi = detail::Dpi();
    float sz = ps.swatch > 0.0f ? ps.swatch : ImGui::GetFrameHeight();
    float gap = ps.spacing >= 0.0f ? ps.spacing : st.ItemInnerSpacing.x;
    int cols = ps.columns;
    if (cols <= 0) cols = detail::ClampI((int)((ImGui::GetContentRegionAvail().x + gap) / (sz + gap)), 1, count);
    int sel = selected ? *selected : -1;
    if (color && (sel < 0 || sel >= count || !detail::SameColor(colors[sel], *color))) {
        sel = -1;
        for (int i = 0; i < count; ++i)
            if (detail::SameColor(colors[i], *color)) {
                sel = i;
                break;
            }
    }
    ImGuiID id = ImGui::GetID(label);
    ImGui::PushID(label);
    ImGui::BeginGroup();
    ImGui::BeginGroup();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float r = detail::Max(detail::Rounding(sz), sz * 0.22f);
    bool changed = false;
    for (int i = 0; i < count; ++i) {
        if (i % cols != 0) ImGui::SameLine(0.0f, gap);
        else if (i > 0) ImGui::SetCursorPosY(ImGui::GetCursorPosY() - st.ItemSpacing.y + gap);
        ImVec2 a = ImGui::GetCursorScreenPos();
        ImGui::PushID(i);
        bool pressed = ImGui::InvisibleButton("##sw", ImVec2(sz, sz), detail::kNav);
        bool hov = ImGui::IsItemHovered();
        if (ps.tooltip && detail::Anim(detail::Sub(id, 2000 + i), hov, 0.5f) >= 1.0f && hov) {
            char hex[16];
            FormatHexColor(colors[i], hex, 16);
            ImGui::SetTooltip("%s", hex);
        }
        ImGui::PopID();
        if (pressed && sel != i) {
            sel = i;
            changed = true;
        }
        float k = detail::Anim(detail::Sub(id, i), sel == i);
        float g = detail::AnimTo(detail::Sub(id, 1000 + i), hov ? 1.0f : 0.0f);
        float grow = (g * 0.08f - k * 0.1f) * sz;
        ImVec2 sa(a.x - grow * 0.5f, a.y - grow * 0.5f), sb(a.x + sz + grow * 0.5f, a.y + sz + grow * 0.5f);
        detail::Swatch(dl, sa, sb, colors[i], r);
        detail::Rect(dl, sa, sb, detail::Final(detail::BorderRaw(), 0.5f), r, 1.0f);
        if (k > 0.01f) {
            float e = 2.0f * dpi;
            detail::Rect(dl, ImVec2(a.x - e, a.y - e), ImVec2(a.x + sz + e, a.y + sz + e), detail::Final(detail::TextRaw(), k),
                         r + e, detail::Max(1.5f, 1.5f * dpi));
            ImU32 mark = detail::IsBright(ImGui::ColorConvertFloat4ToU32(ImVec4(colors[i].x, colors[i].y, colors[i].z, 1)))
                             ? IM_COL32(20, 20, 26, 255) : IM_COL32_WHITE;
            detail::Check(dl, ImVec2(a.x + sz * 0.5f, a.y + sz * 0.5f), sz * 0.22f, detail::Final(mark, k),
                          detail::Max(1.5f, 1.8f * dpi), k);
        }
    }
    ImGui::EndGroup();
    detail::TrailingLabel(label, detail::LabelEnd(label), sz);
    ImGui::EndGroup();
    ImGui::PopID();
    if (changed) {
        if (selected) *selected = sel;
        if (color) *color = colors[sel];
    }
    return changed;
}

inline bool ColorPalette(const char* label, int* selected, const ImVec4* colors, int count,
                         const PaletteStyle& ps = PaletteStyle()) {
    return ColorPalette(label, selected, nullptr, colors, count, ps);
}

// colors as hex strings, handy for generated code and manifests
inline bool ColorPalette(const char* label, int* selected, ImVec4* color, const char* const* hex_colors, int count,
                         const PaletteStyle& ps = PaletteStyle()) {
    if (!hex_colors || count <= 0) return false;
    if (count > detail::kMaxItems) count = detail::kMaxItems;
    ImVec4 cols[detail::kMaxItems];
    for (int i = 0; i < count; ++i)
        if (!ParseHexColor(hex_colors[i], &cols[i])) cols[i] = ImVec4(0, 0, 0, 0);
    return ColorPalette(label, selected, color, cols, count, ps);
}

inline bool ColorPalette(const char* label, int* selected, ImVec4* color, const char* const* hex_colors, int count,
                         int columns) {
    PaletteStyle ps;
    ps.columns = columns;
    return ColorPalette(label, selected, color, hex_colors, count, ps);
}

struct HueStripStyle {
    ImVec2 size = ImVec2(0, 0);
    bool vertical = false;
};

namespace detail {

inline ImU32 HueRaw(float h) {
    float r, g, b;
    ImGui::ColorConvertHSVtoRGB(h - std::floor(h), 1.0f, 1.0f, r, g, b);
    return ImGui::ColorConvertFloat4ToU32(ImVec4(r, g, b, 1.0f));
}

inline void Marker(ImDrawList* dl, const ImVec2& a, const ImVec2& b, bool vertical, float t, float hot) {
    float dpi = Dpi();
    float th = Max(2.0f, 2.0f * dpi) + hot * dpi;
    ImVec2 ma, mb;
    if (vertical) {
        float y = Lerp(a.y, b.y, t);
        ma = ImVec2(a.x - 2.0f * dpi, y - th);
        mb = ImVec2(b.x + 2.0f * dpi, y + th);
    } else {
        float x = Lerp(a.x, b.x, t);
        ma = ImVec2(x - th, a.y - 2.0f * dpi);
        mb = ImVec2(x + th, b.y + 2.0f * dpi);
    }
    Shadow(dl, ma, mb, th, 0.8f);
    dl->AddRectFilled(ma, mb, Final(IM_COL32_WHITE), th);
    Rect(dl, ma, mb, Final(IM_COL32(0, 0, 0, 120)), th, 1.0f);
}

// 0..1 along the item while it is held; arrows step by 1/100
inline bool DragAlong(float* t, const ImVec2& a, const ImVec2& b, bool vertical) {
    float before = *t;
    if (ImGui::IsItemActive() && ImGui::IsMouseDown(0)) {
        ImVec2 m = ImGui::GetMousePos();
        *t = vertical ? Sat((m.y - a.y) / Max(b.y - a.y, 1.0f)) : Sat((m.x - a.x) / Max(b.x - a.x, 1.0f));
    }
    int d = FocusedArrows(!vertical, vertical);
    if (d) *t = Sat(*t + 0.01f * (float)(vertical ? -d : d));
    return *t != before;
}

}  // namespace detail

// hue in 0..1
inline bool HueStrip(const char* label, float* hue, const HueStripStyle& hs = HueStripStyle()) {
    float tmp = 0.0f;
    if (!hue) hue = &tmp;
    float fh = ImGui::GetFrameHeight();
    ImVec2 size = hs.size;
    if (size.x <= 0.0f) size.x = hs.vertical ? fh * 0.7f : ImGui::CalcItemWidth();
    if (size.y <= 0.0f) size.y = hs.vertical ? fh * 6.0f : fh * 0.7f;
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGuiID id = ImGui::GetID(label);
    ImGui::InvisibleButton(label, size, detail::kNav);
    ImVec2 b(pos.x + size.x, pos.y + size.y);
    float h = detail::Sat(*hue);
    bool changed = detail::DragAlong(&h, pos, b, hs.vertical);
    if (changed) *hue = h;
    float hot = detail::Anim(id, ImGui::IsItemActive() || ImGui::IsItemHovered());
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float r = detail::Min(size.x, size.y) * 0.5f;
    detail::GradientBar(dl, pos, b, r, hs.vertical, [](float t) { return detail::HueRaw(t); }, 48);
    detail::Marker(dl, pos, b, hs.vertical, h, hot);
    return changed;
}

// saturation left to right, value bottom to top, for a fixed hue
inline bool SatValBox(const char* label, float hue, float* sat, float* val, const ImVec2& size_arg = ImVec2(0, 0)) {
    float ts = 1.0f, tv = 1.0f;
    if (!sat) sat = &ts;
    if (!val) val = &tv;
    float side = ImGui::GetFrameHeight() * 6.0f;
    ImVec2 size(size_arg.x > 0.0f ? size_arg.x : side, size_arg.y > 0.0f ? size_arg.y : side);
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGuiID id = ImGui::GetID(label);
    ImGui::InvisibleButton(label, size, detail::kNav);
    ImVec2 b(pos.x + size.x, pos.y + size.y);
    float s0 = *sat, v0 = *val;
    if (ImGui::IsItemActive() && ImGui::IsMouseDown(0)) {
        ImVec2 m = ImGui::GetMousePos();
        *sat = detail::Sat((m.x - pos.x) / size.x);
        *val = 1.0f - detail::Sat((m.y - pos.y) / size.y);
    }
    if (ImGui::IsItemFocused()) {
        int dx = (detail::KeyPressed(ImGuiKey_RightArrow) ? 1 : 0) - (detail::KeyPressed(ImGuiKey_LeftArrow) ? 1 : 0);
        int dy = (detail::KeyPressed(ImGuiKey_UpArrow) ? 1 : 0) - (detail::KeyPressed(ImGuiKey_DownArrow) ? 1 : 0);
        if (dx || dy) {
            ImGui::NavMoveRequestCancel();
            *sat = detail::Sat(*sat + 0.01f * (float)dx);
            *val = detail::Sat(*val + 0.01f * (float)dy);
        }
    }
    bool changed = *sat != s0 || *val != v0;
    float hot = detail::Anim(id, ImGui::IsItemActive());
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImU32 hue_col = detail::Final(detail::HueRaw(hue));
    ImU32 white = detail::Final(IM_COL32_WHITE), black = detail::Final(IM_COL32_BLACK);
    ImU32 clear = detail::Final(IM_COL32(0, 0, 0, 0));
    dl->AddRectFilledMultiColor(pos, b, white, hue_col, hue_col, white);
    dl->AddRectFilledMultiColor(pos, b, clear, clear, black, black);
    detail::Rect(dl, pos, b, detail::Final(detail::BorderRaw(), 0.8f), 0.0f, 1.0f);
    ImVec2 c(detail::Lerp(pos.x, b.x, detail::Sat(*sat)), detail::Lerp(b.y, pos.y, detail::Sat(*val)));
    float rr = (5.0f + 2.0f * hot) * detail::Dpi();
    float rgb[3];
    ImGui::ColorConvertHSVtoRGB(hue, *sat, *val, rgb[0], rgb[1], rgb[2]);
    dl->PushClipRect(ImVec2(pos.x - rr * 2.0f, pos.y - rr * 2.0f), ImVec2(b.x + rr * 2.0f, b.y + rr * 2.0f), true);
    dl->AddCircleFilled(c, rr, ImGui::GetColorU32(ImVec4(rgb[0], rgb[1], rgb[2], 1.0f)), 20);
    dl->AddCircle(c, rr, white, 20, detail::Max(2.0f, 2.0f * detail::Dpi()));
    dl->AddCircle(c, rr + 1.5f, detail::Final(IM_COL32(0, 0, 0, 90)), 20, 1.0f);
    dl->PopClipRect();
    return changed;
}

struct MiniPickerStyle {
    float size = 0.0f;
    bool alpha = true;
    bool hex = true;
};

namespace detail {

// hue survives while saturation or value is 0
inline void KeepHue(ImGuiID key, const ImVec4& c, float* h, float* s, float* v) {
    ImGuiStorage* store = ImGui::GetStateStorage();
    ImGui::ColorConvertRGBtoHSV(c.x, c.y, c.z, *h, *s, *v);
    float kept = store->GetFloat(key, *h);
    if (*s <= 0.0001f || *v <= 0.0001f) *h = kept;
}

}  // namespace detail

// SatValBox + vertical HueStrip (+ alpha strip) with a preview and hex text underneath
inline bool ColorPickerMini(const char* label, ImVec4* color, const MiniPickerStyle& ms = MiniPickerStyle()) {
    ImVec4 tmp(1, 1, 1, 1);
    if (!color) color = &tmp;
    const ImGuiStyle& st = ImGui::GetStyle();
    float fh = ImGui::GetFrameHeight();
    float side = ms.size > 0.0f ? ms.size : fh * 6.0f;
    float strip = fh * 0.7f, gap = st.ItemInnerSpacing.x;
    ImGuiID id = ImGui::GetID(label);
    ImGuiID hue_key = detail::Sub(id, 1);
    float h, s, v;
    detail::KeepHue(hue_key, *color, &h, &s, &v);
    ImGui::PushID(label);
    ImGui::BeginGroup();
    ImGui::BeginGroup();
    bool changed = false;
    if (SatValBox("##sv", h, &s, &v, ImVec2(side, side))) changed = true;
    ImGui::SameLine(0.0f, gap);
    HueStripStyle hs;
    hs.vertical = true;
    hs.size = ImVec2(strip, side);
    if (HueStrip("##hue", &h, hs)) changed = true;
    float a = color->w;
    if (ms.alpha) {
        ImGui::SameLine(0.0f, gap);
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("##alpha", ImVec2(strip, side), detail::kNav);
        ImVec2 q(p.x + strip, p.y + side);
        float t = 1.0f - a;
        if (detail::DragAlong(&t, p, q, true)) {
            a = 1.0f - t;
            changed = true;
        }
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->PushClipRect(p, q, true);
        detail::Checker(dl, p, q, strip * 0.5f);
        dl->PopClipRect();
        ImU32 base = ImGui::ColorConvertFloat4ToU32(ImVec4(color->x, color->y, color->z, 1.0f));
        detail::GradientBar(dl, p, q, 0.0f, true, [base](float k) { return detail::WithAlpha(base, 1.0f - k); }, 8);
        detail::Rect(dl, p, q, detail::Final(detail::BorderRaw(), 0.8f), 0.0f, 1.0f);
        detail::Marker(dl, p, q, true, 1.0f - a, detail::Anim(detail::Sub(id, 2), ImGui::IsItemActive()));
    }
    if (changed) {
        ImGui::GetStateStorage()->SetFloat(hue_key, h);
        float r, g, b;
        ImGui::ColorConvertHSVtoRGB(h, s, v, r, g, b);
        *color = ImVec4(r, g, b, a);
    }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    float pw = fh * 1.6f;
    ImGui::Dummy(ImVec2(pw, fh));
    float r = detail::Rounding(fh);
    detail::Swatch(dl, p, ImVec2(p.x + pw, p.y + fh), *color, r);
    detail::Rect(dl, p, ImVec2(p.x + pw, p.y + fh), detail::Final(detail::BorderRaw()), r, 1.0f);
    if (ms.hex) {
        ImGui::SameLine(0.0f, gap);
        char* buf = detail::TextBuf(detail::Sub(id, 3));
        ImGuiStorage* store = ImGui::GetStateStorage();
        ImGuiID editing = detail::Sub(id, 4);
        if (!store->GetBool(editing, false)) FormatHexColor(*color, buf, 16, ms.alpha);
        ImGui::SetNextItemWidth(detail::Max(side + (strip + gap) * (ms.alpha ? 2.0f : 1.0f) - pw - gap, fh * 3.0f));
        if (ImGui::InputText("##hex", buf, 16, ImGuiInputTextFlags_CharsNoBlank | ImGuiInputTextFlags_AutoSelectAll)) {
            ImVec4 parsed;
            if (ParseHexColor(buf, &parsed)) {
                if (!ms.alpha) parsed.w = color->w;
                *color = parsed;
                changed = true;
            }
        }
        store->SetBool(editing, ImGui::IsItemActive());
    }
    ImGui::EndGroup();
    detail::TrailingLabel(label, detail::LabelEnd(label), fh);
    ImGui::EndGroup();
    ImGui::PopID();
    return changed;
}

inline bool ColorPickerMini(const char* label, float rgba[4], const MiniPickerStyle& ms = MiniPickerStyle()) {
    if (!rgba) return false;
    ImVec4 c(rgba[0], rgba[1], rgba[2], rgba[3]);
    if (!ColorPickerMini(label, &c, ms)) return false;
    rgba[0] = c.x;
    rgba[1] = c.y;
    rgba[2] = c.z;
    rgba[3] = c.w;
    return true;
}

inline bool SatValBox(const char* label, std::nullptr_t, float* sat, float* val, const ImVec2& size_arg = ImVec2(0, 0)) {
    return SatValBox(label, 0.0f, sat, val, size_arg);
}

inline bool ColorPickerMini(const char* label, std::nullptr_t, const MiniPickerStyle& ms = MiniPickerStyle()) {
    return ColorPickerMini(label, static_cast<ImVec4*>(nullptr), ms);
}

}  // namespace ctl
}  // namespace imb
