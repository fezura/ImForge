#pragma once
// SwitchToggle, IconToggle, TriCheckbox, RatingStars
#include <cstddef>
#include "imb_ctl_core.h"

namespace imb {
namespace ctl {

enum class SwitchKind { Ios, Square, Icons };

struct SwitchStyle {
    SwitchKind kind = SwitchKind::Ios;
    float width = 0.0f;
    float height = 0.0f;
    ImU32 on = 0;
    ImU32 off = 0;
    ImU32 knob = 0;
    bool show_label = true;
};

// Ios: pill with a round knob that stretches while held. Square: boxy track and knob.
// Icons: Ios with a check / cross drawn in the knob
inline bool SwitchToggle(const char* label, bool* v, const SwitchStyle& ss = SwitchStyle()) {
    bool tmp = false;
    if (!v) v = &tmp;
    const ImGuiStyle& st = ImGui::GetStyle();
    float dpi = detail::Dpi();
    float fh = ImGui::GetFrameHeight();
    float h = ss.height > 0.0f ? ss.height : fh * 0.85f;
    float w = ss.width > 0.0f ? ss.width : h * 1.75f;
    const char* lend = detail::LabelEnd(label);
    float label_w = ss.show_label ? detail::LabelWidth(label, lend) : 0.0f;
    float total_h = detail::Max(h, fh);
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGuiID id = ImGui::GetID(label);
    bool pressed = ImGui::InvisibleButton(label, ImVec2(w + label_w, total_h), detail::kNav);
    bool hovered = ImGui::IsItemHovered(), held = ImGui::IsItemActive();
    if (pressed) *v = !*v;
    float t = detail::Clamp(detail::SpringTo(id, *v ? 1.0f : 0.0f), -0.1f, 1.1f);
    float tc = detail::Anim(detail::Sub(id, 1), *v);
    float press = detail::Anim(detail::Sub(id, 2), held, 0.1f);
    float hov = detail::Anim(detail::Sub(id, 3), hovered);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 a(pos.x, pos.y + (total_h - h) * 0.5f), b(a.x + w, a.y + h);
    bool square = ss.kind == SwitchKind::Square;
    float r = square ? detail::Max(2.0f * dpi, detail::Rounding(h) * 0.6f) : h * 0.5f;
    ImU32 on = ss.on ? ss.on : detail::AccentRaw();
    ImU32 off = detail::Pick(ss.off, ImGuiCol_FrameBg);
    ImU32 track = detail::Mix(detail::Shade(off, 0.06f * hov), detail::Shade(on, 0.08f * hov), tc);
    dl->AddRectFilled(a, b, detail::Final(track), r);
    if (tc < 0.99f) detail::Rect(dl, a, b, detail::Final(detail::BorderRaw(), (1.0f - tc) * 0.7f), r, detail::Max(1.0f, dpi));
    float inset = detail::Max(2.0f, h * 0.12f);
    float kh = h - inset * 2.0f, kw = kh * (1.0f + 0.28f * press);
    float x = detail::Lerp(a.x + inset, b.x - inset - kw, t);
    ImVec2 ka(x, a.y + inset), kb(x + kw, b.y - inset);
    float kr = square ? detail::Max(1.5f * dpi, r - inset * 0.6f) : kh * 0.5f;
    ImU32 knob = ss.knob ? ss.knob : detail::ThumbRaw();
    detail::Shadow(dl, ka, kb, kr, 0.9f);
    dl->AddRectFilled(ka, kb, detail::Final(knob), kr);
    if (ss.kind == SwitchKind::Icons) {
        ImVec2 c = detail::Mid(ka, kb);
        float s = kh * 0.26f, th = detail::Max(1.5f, 1.5f * dpi);
        if (tc > 0.02f) detail::Check(dl, c, s * 1.1f, detail::Final(on, tc), th, tc);
        if (tc < 0.98f) detail::Cross(dl, c, s * (1.0f - tc) * 0.85f, detail::Final(detail::DimRaw(), 1.0f - tc), th);
    }
    if (label_w > 0.0f)
        dl->AddText(ImVec2(pos.x + w + st.ItemInnerSpacing.x, pos.y + (total_h - ImGui::GetFontSize()) * 0.5f),
                    detail::Final(detail::TextRaw()), label, lend);
    return pressed;
}

inline bool SwitchToggle(const char* label, bool* v, SwitchKind kind) {
    SwitchStyle ss;
    ss.kind = kind;
    return SwitchToggle(label, v, ss);
}

struct IconToggleStyle {
    float size = 0.0f;
    ImU32 on = 0;
    ImU32 off = 0;
    ImU32 bg = 0;
    bool show_label = false;
};

// a square button showing a glyph (icon font text or plain letters), lit while *v is true.
// without icons the visible part of the label is drawn
inline bool IconToggle(const char* label, bool* v, const char* icon_on = nullptr, const char* icon_off = nullptr,
                       const IconToggleStyle& is = IconToggleStyle()) {
    bool tmp = false;
    if (!v) v = &tmp;
    const ImGuiStyle& st = ImGui::GetStyle();
    float dpi = detail::Dpi();
    float s = is.size > 0.0f ? is.size : ImGui::GetFrameHeight();
    const char* lend = detail::LabelEnd(label);
    float label_w = is.show_label ? detail::LabelWidth(label, lend) : 0.0f;
    const char* glyph = (icon_on && *icon_on) ? icon_on : label;
    const char* glyph_end = glyph == label ? lend : nullptr;
    if (!*v && icon_off && *icon_off) {
        glyph = icon_off;
        glyph_end = nullptr;
    }
    float gw = detail::TextW(glyph, glyph_end);
    float w = detail::Max(s, gw + st.FramePadding.x * 2.0f);
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGuiID id = ImGui::GetID(label);
    bool pressed = ImGui::InvisibleButton(label, ImVec2(w + label_w, s), detail::kNav);
    bool hovered = ImGui::IsItemHovered(), held = ImGui::IsItemActive();
    if (pressed) *v = !*v;
    float t = detail::Anim(id, *v);
    float hk = detail::AnimTo(detail::Sub(id, 1), held ? 1.0f : (hovered ? 0.5f : 0.0f));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 b(pos.x + w, pos.y + s);
    float r = detail::Rounding(s);
    ImU32 on = is.on ? is.on : detail::AccentRaw();
    ImU32 bg = detail::Pick(is.bg, ImGuiCol_FrameBg);
    ImU32 fill = detail::Mix(detail::Shade(bg, 0.08f * hk), detail::WithAlpha(on, 0.25f + 0.1f * hk), t);
    if (t > 0.01f) imb::Glow(dl, pos, b, r, detail::Final(on, 0.35f * t), 4.0f * dpi * t);
    detail::Frame(dl, pos, b, detail::Final(fill), r);
    if (t > 0.01f) detail::Rect(dl, pos, b, detail::Final(on, t), r, detail::Max(1.0f, dpi));
    ImU32 gc = detail::Mix(is.off ? is.off : detail::DimRaw(), detail::Shade(on, 0.3f), t);
    float pop = 1.0f + 0.12f * detail::Sat(1.0f - detail::Abs(t - 0.5f) * 2.0f);
    ImVec2 ts = ImGui::CalcTextSize(glyph, glyph_end);
    float fs = ImGui::GetFontSize() * pop;
    ImVec2 gp(pos.x + (w - ts.x * pop) * 0.5f, pos.y + (s - ts.y * pop) * 0.5f);
    dl->AddText(ImGui::GetFont(), fs, gp, detail::Final(gc), glyph, glyph_end);
    if (label_w > 0.0f)
        dl->AddText(ImVec2(b.x + st.ItemInnerSpacing.x, pos.y + (s - ImGui::GetFontSize()) * 0.5f),
                    detail::Final(detail::TextRaw()), label, lend);
    return pressed;
}

struct TriCheckboxStyle {
    bool cycle_mixed = false;
    ImU32 fill = 0;
    ImU32 mark = 0;
};

// *state: 0 off, 1 on, 2 mixed. clicks go mixed/off to on, on to off; with cycle_mixed
// they walk off, on, mixed
inline bool TriCheckbox(const char* label, int* state, const TriCheckboxStyle& ts = TriCheckboxStyle()) {
    int tmp = 0;
    if (!state) state = &tmp;
    const ImGuiStyle& st = ImGui::GetStyle();
    float dpi = detail::Dpi();
    float sz = ImGui::GetFrameHeight();
    const char* lend = detail::LabelEnd(label);
    float label_w = detail::LabelWidth(label, lend);
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGuiID id = ImGui::GetID(label);
    bool pressed = ImGui::InvisibleButton(label, ImVec2(sz + label_w, sz), detail::kNav);
    bool hovered = ImGui::IsItemHovered(), held = ImGui::IsItemActive();
    if (pressed) {
        if (ts.cycle_mixed) *state = (*state + 1) % 3;
        else *state = *state == 1 ? 0 : 1;
    }
    int s = detail::ClampI(*state, 0, 2);
    float on = detail::Anim(id, s != 0);
    float chk = detail::Anim(detail::Sub(id, 1), s == 1, 0.22f);
    float mix = detail::Anim(detail::Sub(id, 2), s == 2);
    float hk = detail::AnimTo(detail::Sub(id, 3), held ? 1.0f : (hovered ? 0.5f : 0.0f));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float inset = detail::Max(1.0f, sz * 0.08f);
    ImVec2 a(pos.x + inset, pos.y + inset), b(pos.x + sz - inset, pos.y + sz - inset);
    float r = detail::Max(2.0f * dpi, detail::Rounding(sz) * 0.8f);
    ImU32 fill = ts.fill ? ts.fill : detail::AccentRaw();
    ImU32 bg = detail::Mix(detail::Shade(detail::StyleRaw(ImGuiCol_FrameBg), 0.08f * hk), detail::Shade(fill, 0.06f * hk), on);
    float squash = 1.0f - 0.08f * (held ? 1.0f : 0.0f);
    ImVec2 c = detail::Mid(a, b);
    float half = (b.x - a.x) * 0.5f * squash;
    ImVec2 sa(c.x - half, c.y - half), sb(c.x + half, c.y + half);
    dl->AddRectFilled(sa, sb, detail::Final(bg), r);
    detail::Rect(dl, sa, sb, detail::Final(detail::Mix(detail::BorderRaw(), fill, on), 0.9f), r, detail::Max(1.0f, dpi));
    ImU32 mark = ts.mark ? ts.mark : detail::OnRaw(fill);
    float th = detail::Max(1.6f, 1.8f * dpi);
    if (chk > 0.01f) detail::Check(dl, c, half * 0.62f, detail::Final(mark, detail::Sat(chk * 1.5f)), th, chk);
    if (mix > 0.01f) {
        float hw = half * 0.5f * mix;
        dl->AddLine(ImVec2(c.x - hw, c.y), ImVec2(c.x + hw, c.y), detail::Final(mark, mix), th);
    }
    if (label_w > 0.0f)
        dl->AddText(ImVec2(pos.x + sz + st.ItemInnerSpacing.x, pos.y + (sz - ImGui::GetFontSize()) * 0.5f),
                    detail::Final(detail::TextRaw()), label, lend);
    return pressed;
}

struct RatingStyle {
    float size = 0.0f;
    float spacing = -1.0f;
    bool allow_clear = true;
    bool show_value = false;
    ImU32 on = 0;
    ImU32 off = 0;
};

// step 1 for whole stars, 0.5 for halves. clicking the current value again clears it
inline bool RatingStars(const char* label, float* value, int max_stars = 5, float step = 1.0f,
                        const RatingStyle& rs = RatingStyle()) {
    float tmp = 0.0f;
    if (!value) value = &tmp;
    if (max_stars < 1) max_stars = 1;
    if (max_stars > 20) max_stars = 20;
    if (step <= 0.0f || step > 1.0f) step = 1.0f;
    const ImGuiStyle& st = ImGui::GetStyle();
    float dpi = detail::Dpi();
    float s = rs.size > 0.0f ? rs.size : ImGui::GetFrameHeight();
    float gap = rs.spacing >= 0.0f ? rs.spacing : 2.0f * dpi;
    const char* lend = detail::LabelEnd(label);
    float label_w = detail::LabelWidth(label, lend);
    char vb[16];
    snprintf(vb, sizeof(vb), step < 1.0f ? "%.1f" : "%.0f", (double)*value);
    float value_w = rs.show_value ? detail::TextW("0.0") + st.ItemInnerSpacing.x : 0.0f;
    float row_w = s * (float)max_stars + gap * (float)(max_stars - 1);
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGuiID id = ImGui::GetID(label);
    bool pressed = ImGui::InvisibleButton(label, ImVec2(row_w + value_w + label_w, s), detail::kNav);
    bool hovered = ImGui::IsItemHovered();
    float hover_v = -1.0f;
    float mx = ImGui::GetMousePos().x - pos.x;
    if (hovered && mx >= 0.0f && mx < row_w) {
        int i = detail::ClampI((int)(mx / (s + gap)), 0, max_stars - 1);
        float frac = detail::Sat((mx - (float)i * (s + gap)) / s);
        hover_v = (float)i + std::ceil(frac / step - 0.001f) * step;
        if (hover_v <= (float)i) hover_v = (float)i + step;
    }
    float before = *value;
    if (pressed && hover_v > 0.0f) *value = (rs.allow_clear && detail::Abs(*value - hover_v) < 0.001f) ? 0.0f : hover_v;
    int d = detail::FocusedArrows(true, true);
    if (d) *value = detail::Clamp(detail::RoundTo(*value, step) + step * (float)d, 0.0f, (float)max_stars);
    bool changed = *value != before;
    float shown = hover_v > 0.0f ? hover_v : *value;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImU32 on = rs.on ? rs.on : GetStyle().star;
    ImU32 off = rs.off ? rs.off : detail::StyleRaw(ImGuiCol_FrameBg);
    float th = detail::Max(1.0f, dpi);
    for (int i = 0; i < max_stars; ++i) {
        float fill = detail::AnimTo(detail::Sub(id, i), detail::Sat(shown - (float)i), 0.12f);
        float pop = detail::Anim(detail::Sub(id, 40 + i), (float)i < *value, 0.25f);
        float scale = 1.0f + 0.18f * detail::Sat(1.0f - detail::Abs(pop - 0.5f) * 2.0f) * (pop < 0.999f ? 1.0f : 0.0f);
        ImVec2 c(pos.x + (float)i * (s + gap) + s * 0.5f, pos.y + s * 0.5f);
        float ro = s * 0.46f * scale;
        detail::StarFilled(dl, c, ro, detail::Final(off | IM_COL32_A_MASK, 0.9f));
        if (fill > 0.01f) {
            ImU32 col = hover_v > 0.0f ? detail::Shade(on, 0.15f) : on;
            if (fill >= 0.99f) {
                detail::StarFilled(dl, c, ro, detail::Final(col));
            } else {
                dl->PushClipRect(ImVec2(c.x - ro, c.y - ro), ImVec2(c.x - ro + ro * 2.0f * fill, c.y + ro), true);
                detail::StarFilled(dl, c, ro, detail::Final(col));
                dl->PopClipRect();
            }
        }
        detail::StarOutline(dl, c, ro, detail::Final(fill > 0.01f ? on : detail::BorderRaw(), 0.8f), th);
    }
    float x = pos.x + row_w;
    if (rs.show_value) {
        dl->AddText(ImVec2(x + st.ItemInnerSpacing.x, pos.y + (s - ImGui::GetFontSize()) * 0.5f),
                    detail::Final(detail::DimRaw()), vb);
        x += value_w;
    }
    if (label_w > 0.0f)
        dl->AddText(ImVec2(x + st.ItemInnerSpacing.x, pos.y + (s - ImGui::GetFontSize()) * 0.5f),
                    detail::Final(detail::TextRaw()), label, lend);
    return changed;
}

inline bool RatingStars(const char* label, int* value, int max_stars = 5, const RatingStyle& rs = RatingStyle()) {
    if (!value) return false;
    float f = (float)*value;
    if (!RatingStars(label, &f, max_stars, 1.0f, rs)) return false;
    *value = detail::RoundI(f);
    return true;
}

inline bool RatingStars(const char* label, std::nullptr_t, int max_stars = 5, const RatingStyle& rs = RatingStyle()) {
    return RatingStars(label, static_cast<int*>(nullptr), max_stars, rs);
}

}  // namespace ctl
}  // namespace imb
