#pragma once
// RangeSlider, Knob, TickSlider, Stepper
#include <cstddef>
#include "imb_ctl_core.h"

namespace imb {
namespace ctl {

struct RangeSliderStyle {
    float width = 0.0f;
    float min_gap = 0.0f;
    ImU32 track = 0;
    ImU32 fill = 0;
    ImU32 thumb = 0;
    bool show_values = true;
    bool show_label = true;
};

namespace detail {

inline float TrackX(float v, float mn, float mx, float x0, float x1) {
    float t = mx > mn ? (v - mn) / (mx - mn) : 0.0f;
    return Lerp(x0, x1, Sat(t));
}

inline void Thumb(ImDrawList* dl, const ImVec2& c, float r, ImU32 thumb_raw, ImU32 ring_raw, float hot) {
    Shadow(dl, ImVec2(c.x - r, c.y - r), ImVec2(c.x + r, c.y + r), r, 0.9f);
    if (hot > 0.01f) dl->AddCircleFilled(c, r * (1.0f + 0.6f * hot), Final(ring_raw, 0.22f * hot), 24);
    dl->AddCircleFilled(c, r, Final(thumb_raw), 24);
    dl->AddCircle(c, r, Final(ring_raw, 0.9f), 24, Max(1.0f, Dpi()));
}

// value bubble above a point, scale/alpha come from t
inline void Bubble(ImDrawList* dl, const ImVec2& tip, const char* text, float t, ImU32 bg_raw) {
    if (t <= 0.01f) return;
    float dpi = Dpi();
    ImVec2 ts = ImGui::CalcTextSize(text);
    float pw = 5.0f * dpi, ph = 2.0f * dpi, arrow = 4.0f * dpi;
    float lift = (1.0f - t) * 4.0f * dpi;
    ImVec2 b(tip.x + ts.x * 0.5f + pw, tip.y - arrow - lift), a(tip.x - ts.x * 0.5f - pw, b.y - ts.y - ph * 2.0f);
    ImU32 bg = Final(bg_raw, t);
    dl->AddRectFilled(a, b, bg, 4.0f * dpi);
    dl->AddTriangleFilled(ImVec2(tip.x - arrow, b.y - 0.5f), ImVec2(tip.x + arrow, b.y - 0.5f),
                          ImVec2(tip.x, b.y + arrow - 0.5f), bg);
    dl->AddText(ImVec2(a.x + pw, a.y + ph), Final(OnRaw(bg_raw), t), text);
}

}  // namespace detail

// two thumbs on one track, *lo <= *hi always holds
inline bool RangeSlider(const char* label, float* lo, float* hi, float v_min, float v_max, const char* fmt = "%.2f",
                        const RangeSliderStyle& rs = RangeSliderStyle()) {
    float tmp_lo = v_min, tmp_hi = v_max;
    if (!lo) lo = &tmp_lo;
    if (!hi) hi = &tmp_hi;
    const ImGuiStyle& st = ImGui::GetStyle();
    float dpi = detail::Dpi();
    float h = ImGui::GetFrameHeight();
    const char* lend = detail::LabelEnd(label);
    char vb[72];
    float values_w = 0.0f;
    if (rs.show_values) {
        char a[32], b[32];
        detail::Fmt(a, 32, fmt, v_max);
        detail::Fmt(b, 32, fmt, v_min);
        values_w = detail::Max(detail::TextW(a), detail::TextW(b)) * 2.0f + detail::TextW(" - ") + st.ItemInnerSpacing.x;
    }
    float w = rs.width > 0.0f ? rs.width : ImGui::CalcItemWidth();
    float track_w = detail::Max(w - values_w, h * 2.0f);
    ImGuiID id = ImGui::GetID(label);
    ImGui::PushID(label);
    ImGui::BeginGroup();
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##track", ImVec2(track_w, h), detail::kNav);
    bool hovered = ImGui::IsItemHovered(), active = ImGui::IsItemActive();
    float r = detail::Max(6.0f * dpi, h * 0.34f);
    float x0 = pos.x + r, x1 = pos.x + track_w - r;
    ImGuiStorage* store = ImGui::GetStateStorage();
    ImGuiID which_key = detail::Sub(id, 1);
    int which = store->GetInt(which_key, 0);
    bool changed = false;
    float range = v_max - v_min;
    if (ImGui::IsItemActivated() && ImGui::IsMouseDown(0)) {
        float mx = ImGui::GetMousePos().x;
        float dl_ = detail::Abs(mx - detail::TrackX(*lo, v_min, v_max, x0, x1));
        float dh = detail::Abs(mx - detail::TrackX(*hi, v_min, v_max, x0, x1));
        which = (dh < dl_ || (dh == dl_ && mx > detail::TrackX(*hi, v_min, v_max, x0, x1))) ? 1 : 0;
        store->SetInt(which_key, which);
    }
    float gap = detail::Clamp(rs.min_gap, 0.0f, detail::Abs(range));
    if (active && ImGui::IsMouseDown(0) && x1 > x0) {
        float t = detail::Sat((ImGui::GetMousePos().x - x0) / (x1 - x0));
        float v = v_min + t * range;
        if (which == 0) v = detail::Clamp(v, v_min, detail::Max(v_min, *hi - gap));
        else v = detail::Clamp(v, detail::Min(v_max, *lo + gap), v_max);
        float& dst = which == 0 ? *lo : *hi;
        if (dst != v) {
            dst = v;
            changed = true;
        }
    }
    int d = detail::FocusedArrows(true, false);
    if (d) {
        float step = range / 100.0f;
        float& dst = which == 0 ? *lo : *hi;
        float v = which == 0 ? detail::Clamp(dst + step * (float)d, v_min, detail::Max(v_min, *hi - gap))
                             : detail::Clamp(dst + step * (float)d, detail::Min(v_max, *lo + gap), v_max);
        if (v != dst) {
            dst = v;
            changed = true;
        }
    }
    if (*lo > *hi) {
        *lo = *hi;
        changed = true;
    }

    ImDrawList* dl = ImGui::GetWindowDrawList();
    float cy = pos.y + h * 0.5f, th = detail::Max(4.0f * dpi, h * 0.18f);
    ImU32 fill = rs.fill ? rs.fill : detail::AccentRaw();
    dl->AddRectFilled(ImVec2(x0 - th * 0.5f, cy - th * 0.5f), ImVec2(x1 + th * 0.5f, cy + th * 0.5f),
                      detail::Final(detail::Pick(rs.track, ImGuiCol_FrameBg)), th * 0.5f);
    float xl = detail::TrackX(detail::SpringTo(detail::Sub(id, 2), *lo), v_min, v_max, x0, x1);
    float xh = detail::TrackX(detail::SpringTo(detail::Sub(id, 3), *hi), v_min, v_max, x0, x1);
    if (xl > xh) xl = xh;
    dl->AddRectFilled(ImVec2(xl, cy - th * 0.5f), ImVec2(xh, cy + th * 0.5f), detail::Final(fill), th * 0.5f);
    ImU32 thumb = rs.thumb ? rs.thumb : detail::ThumbRaw();
    float hot_lo = detail::Anim(detail::Sub(id, 4), active && which == 0);
    float hot_hi = detail::Anim(detail::Sub(id, 5), active && which == 1);
    float hov = detail::Anim(detail::Sub(id, 6), hovered && !active);
    detail::Thumb(dl, ImVec2(xl, cy), r, thumb, fill, detail::Max(hot_lo, hov * 0.4f));
    detail::Thumb(dl, ImVec2(xh, cy), r, thumb, fill, detail::Max(hot_hi, hov * 0.4f));
    if (hot_lo > 0.01f || hot_hi > 0.01f) {
        bool lo_hot = hot_lo >= hot_hi;
        detail::Fmt(vb, 64, fmt, lo_hot ? *lo : *hi);
        detail::Bubble(dl, ImVec2(lo_hot ? xl : xh, cy - r), vb, lo_hot ? hot_lo : hot_hi, fill);
    }
    if (rs.show_values) {
        char a[32], b[32];
        detail::Fmt(a, 32, fmt, *lo);
        detail::Fmt(b, 32, fmt, *hi);
        snprintf(vb, sizeof(vb), "%s - %s", a, b);
        ImGui::SameLine(0.0f, st.ItemInnerSpacing.x);
        ImVec2 p = ImGui::GetCursorScreenPos();
        float vw = values_w - st.ItemInnerSpacing.x;
        ImGui::Dummy(ImVec2(detail::Max(vw, 1.0f), h));
        detail::TextIn(dl, p, ImVec2(p.x + vw, p.y + h), vb, nullptr, detail::Final(detail::TextRaw()), 1.0f);
    }
    if (rs.show_label) detail::TrailingLabel(label, lend, h);
    ImGui::EndGroup();
    ImGui::PopID();
    return changed;
}

inline bool RangeSlider(const char* label, int* lo, int* hi, int v_min, int v_max, const char* fmt = "%d",
                        const RangeSliderStyle& rs = RangeSliderStyle()) {
    if (!lo || !hi) return false;
    float flo = (float)*lo, fhi = (float)*hi;
    char f[48];
    RangeSliderStyle s = rs;
    if (s.min_gap > 0.0f) s.min_gap = std::floor(s.min_gap + 0.5f);
    RangeSlider(label, &flo, &fhi, (float)v_min, (float)v_max, detail::IntFmt(fmt, f, 48), s);
    int nl = detail::RoundI(flo), nh = detail::RoundI(fhi);
    if (nl > nh) nl = nh;
    bool changed = nl != *lo || nh != *hi;
    *lo = nl;
    *hi = nh;
    return changed;
}

// v[0] is the low end, v[1] the high end
inline bool RangeSlider(const char* label, float v[2], float v_min, float v_max, const char* fmt = "%.2f",
                        const RangeSliderStyle& rs = RangeSliderStyle()) {
    return v ? RangeSlider(label, &v[0], &v[1], v_min, v_max, fmt, rs) : false;
}
inline bool RangeSlider(const char* label, ImVec2* v, float v_min, float v_max, const char* fmt = "%.2f",
                        const RangeSliderStyle& rs = RangeSliderStyle()) {
    return v ? RangeSlider(label, &v->x, &v->y, v_min, v_max, fmt, rs) : false;
}
inline bool RangeSlider(const char* label, int v[2], int v_min, int v_max, const char* fmt = "%d",
                        const RangeSliderStyle& rs = RangeSliderStyle()) {
    return v ? RangeSlider(label, &v[0], &v[1], v_min, v_max, fmt, rs) : false;
}

struct KnobStyle {
    float radius = 0.0f;
    int ticks = 11;
    float drag_px = 200.0f;
    float default_value = FLT_MAX;
    bool bipolar = false;
    bool show_value = true;
    bool show_label = true;
    ImU32 arc = 0;
    ImU32 track = 0;
    ImU32 body = 0;
};

// drag up/right to raise, Shift for fine steps, double click resets to default_value
inline bool Knob(const char* label, float* v, float v_min, float v_max, const char* fmt = "%.2f",
                 const KnobStyle& ks = KnobStyle()) {
    float tmp = v_min;
    if (!v) v = &tmp;
    const ImGuiStyle& st = ImGui::GetStyle();
    float dpi = detail::Dpi();
    float rad = ks.radius > 0.0f ? ks.radius : ImGui::GetFrameHeight() * 1.15f;
    const char* lend = detail::LabelEnd(label);
    bool with_label = ks.show_label && lend > label;
    float line = ImGui::GetFontSize() + st.ItemInnerSpacing.y;
    char vb[64];
    detail::Fmt(vb, 64, fmt, *v);
    float w = detail::Max(rad * 2.0f, detail::Max(with_label ? detail::TextW(label, lend) : 0.0f,
                                                  ks.show_value ? detail::TextW(vb) : 0.0f));
    float h = rad * 2.0f + (ks.show_value ? line : 0.0f) + (with_label ? line : 0.0f);
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGuiID id = ImGui::GetID(label);
    ImGui::InvisibleButton(label, ImVec2(w, h), detail::kNav);
    bool hovered = ImGui::IsItemHovered(), active = ImGui::IsItemActive();
    if (hovered || active) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
    const ImGuiIO& io = ImGui::GetIO();
    float range = v_max - v_min;
    float before = *v;
    if (active && !ImGui::IsItemActivated() && range != 0.0f) {
        float px = ks.drag_px * dpi;
        float delta = (io.MouseDelta.x - io.MouseDelta.y) / detail::Max(px, 1.0f) * range;
        if (io.KeyShift) delta *= 0.1f;
        *v = detail::Clamp(*v + delta, detail::Min(v_min, v_max), detail::Max(v_min, v_max));
    }
    if (hovered && ImGui::IsMouseDoubleClicked(0) && ks.default_value != FLT_MAX) *v = ks.default_value;
    int d = detail::FocusedArrows(true, true);
    if (d) *v = detail::Clamp(*v + range / 50.0f * (float)d, detail::Min(v_min, v_max), detail::Max(v_min, v_max));
    bool changed = *v != before;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 c(pos.x + w * 0.5f, pos.y + rad);
    float t = range != 0.0f ? detail::Sat((*v - v_min) / range) : 0.0f;
    float ta = detail::SpringTo(id, t);
    const float a0 = detail::kPi * 0.75f, a1 = detail::kPi * 2.25f;
    float arc_r = rad - 3.0f * dpi, arc_th = detail::Max(3.0f * dpi, rad * 0.12f);
    ImU32 arc = ks.arc ? ks.arc : detail::AccentRaw();
    ImU32 track = detail::Pick(ks.track, ImGuiCol_FrameBg);
    dl->PathArcTo(c, arc_r, a0, a1, 40);
    detail::Stroke(dl, detail::Final(track), arc_th);
    float from = ks.bipolar ? detail::Lerp(a0, a1, 0.5f) : a0, to = detail::Lerp(a0, a1, ta);
    if (detail::Abs(to - from) > 0.01f) {
        dl->PathArcTo(c, arc_r, detail::Min(from, to), detail::Max(from, to), 40);
        detail::Stroke(dl, detail::Final(arc), arc_th);
    }
    if (ks.ticks > 1) {
        for (int i = 0; i < ks.ticks; ++i) {
            float a = detail::Lerp(a0, a1, (float)i / (float)(ks.ticks - 1));
            float ca = std::cos(a), sa = std::sin(a);
            float r0 = rad + 1.5f * dpi, r1 = rad + (i == 0 || i == ks.ticks - 1 ? 4.5f : 3.0f) * dpi;
            bool lit = ks.bipolar ? false : (float)i / (float)(ks.ticks - 1) <= ta + 0.001f;
            dl->AddLine(ImVec2(c.x + ca * r0, c.y + sa * r0), ImVec2(c.x + ca * r1, c.y + sa * r1),
                        detail::Final(lit ? arc : detail::DimRaw(), lit ? 0.9f : 0.6f), detail::Max(1.0f, dpi));
        }
    }
    float body_r = arc_r - arc_th - 1.5f * dpi;
    float hk = detail::Anim(detail::Sub(id, 1), hovered || active);
    ImU32 body = ks.body ? ks.body : detail::Shade(detail::Solid(ImGuiCol_FrameBg), 0.06f + 0.06f * hk);
    detail::Shadow(dl, ImVec2(c.x - body_r, c.y - body_r), ImVec2(c.x + body_r, c.y + body_r), body_r, 1.0f);
    dl->AddCircleFilled(c, body_r, detail::Final(body), 32);
    ImU32 hi_col = detail::Shade(body, 0.14f);
    imb::RadialGradient(dl, ImVec2(c.x, c.y - body_r * 0.3f), body_r * 0.68f, detail::Final(hi_col, 0.8f),
                        detail::Final(hi_col, 0.0f), 32);
    dl->AddCircle(c, body_r, detail::Final(detail::BorderRaw(), 0.8f), 32, detail::Max(1.0f, dpi));
    float ang = detail::Lerp(a0, a1, ta), ca = std::cos(ang), sa = std::sin(ang);
    dl->AddLine(ImVec2(c.x + ca * body_r * 0.25f, c.y + sa * body_r * 0.25f),
                ImVec2(c.x + ca * body_r * 0.8f, c.y + sa * body_r * 0.8f), detail::Final(arc),
                detail::Max(2.0f * dpi, body_r * 0.12f));
    float y = pos.y + rad * 2.0f + st.ItemInnerSpacing.y * 0.5f;
    if (ks.show_value) {
        detail::TextIn(dl, ImVec2(pos.x, y), ImVec2(pos.x + w, y + ImGui::GetFontSize()), vb, nullptr,
                       detail::Final(active ? arc : detail::TextRaw()));
        y += line;
    }
    if (with_label)
        detail::TextIn(dl, ImVec2(pos.x, y), ImVec2(pos.x + w, y + ImGui::GetFontSize()), label, lend,
                       detail::Final(detail::DimRaw()));
    return changed;
}

inline bool Knob(const char* label, int* v, int v_min, int v_max, const char* fmt = "%d",
                 const KnobStyle& ks = KnobStyle()) {
    if (!v) return false;
    ImGuiID id = ImGui::GetID(label);
    ImGuiStorage* store = ImGui::GetStateStorage();
    ImGuiID key = detail::Sub(id, 9);
    float f = store->GetFloat(key, (float)*v);
    if (detail::RoundI(f) != *v) f = (float)*v;
    char ff[48];
    Knob(label, &f, (float)v_min, (float)v_max, detail::IntFmt(fmt, ff, 48), ks);
    store->SetFloat(key, f);
    int nv = detail::RoundI(f);
    bool changed = nv != *v;
    *v = nv;
    return changed;
}

struct TickSliderStyle {
    float width = 0.0f;
    bool snap = true;
    bool show_label = true;
    bool tick_labels = false;
    ImU32 fill = 0;
    ImU32 track = 0;
    ImU32 thumb = 0;
};

// ticks evenly spaced over the range, the bubble shows the value while dragging or stepping
inline bool TickSlider(const char* label, float* v, float v_min, float v_max, int ticks, const char* fmt = "%.1f",
                       const TickSliderStyle& ts = TickSliderStyle()) {
    float tmp = v_min;
    if (!v) v = &tmp;
    if (ticks < 2) ticks = 2;
    if (ticks > 101) ticks = 101;
    float dpi = detail::Dpi();
    float h = ImGui::GetFrameHeight();
    const char* lend = detail::LabelEnd(label);
    float w = ts.width > 0.0f ? ts.width : ImGui::CalcItemWidth();
    float tick_h = ts.tick_labels ? ImGui::GetFontSize() : 0.0f;
    ImGuiID id = ImGui::GetID(label);
    ImGui::PushID(label);
    ImGui::BeginGroup();
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##ticks", ImVec2(detail::Max(w, h), h + tick_h), detail::kNav);
    bool hovered = ImGui::IsItemHovered(), active = ImGui::IsItemActive(), focused = ImGui::IsItemFocused();
    float r = detail::Max(6.0f * dpi, h * 0.34f);
    float x0 = pos.x + r, x1 = pos.x + w - r;
    float range = v_max - v_min, step = range / (float)(ticks - 1);
    float before = *v;
    if (active && ImGui::IsMouseDown(0) && x1 > x0) {
        float t = detail::Sat((ImGui::GetMousePos().x - x0) / (x1 - x0));
        float nv = v_min + t * range;
        if (ts.snap) nv = v_min + detail::RoundTo(nv - v_min, step);
        *v = nv;
    }
    int d = detail::FocusedArrows(true, false);
    ImGuiStorage* store = ImGui::GetStateStorage();
    ImGuiID k_kb = detail::Sub(id, 3);
    if (d) {
        *v = detail::Clamp(v_min + detail::RoundTo(*v - v_min, step) + step * (float)d, detail::Min(v_min, v_max),
                           detail::Max(v_min, v_max));
        store->SetFloat(k_kb, (float)ImGui::GetTime() + 0.8f);
    }
    bool kb_recent = focused && (float)ImGui::GetTime() < store->GetFloat(k_kb, 0.0f);
    bool changed = *v != before;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    float cy = pos.y + h * 0.5f, th = detail::Max(4.0f * dpi, h * 0.16f);
    ImU32 fill = ts.fill ? ts.fill : detail::AccentRaw();
    float t = range != 0.0f ? detail::Sat((*v - v_min) / range) : 0.0f;
    float xa = detail::Lerp(x0, x1, detail::SpringTo(id, t));
    dl->AddRectFilled(ImVec2(x0 - th * 0.5f, cy - th * 0.5f), ImVec2(x1 + th * 0.5f, cy + th * 0.5f),
                      detail::Final(detail::Pick(ts.track, ImGuiCol_FrameBg)), th * 0.5f);
    dl->AddRectFilled(ImVec2(x0 - th * 0.5f, cy - th * 0.5f), ImVec2(xa, cy + th * 0.5f), detail::Final(fill), th * 0.5f);
    char tb[32];
    for (int i = 0; i < ticks; ++i) {
        float x = detail::Lerp(x0, x1, (float)i / (float)(ticks - 1));
        bool lit = x <= xa + 0.5f;
        dl->AddCircleFilled(ImVec2(x, cy), detail::Max(1.5f * dpi, th * 0.3f),
                            detail::Final(lit ? detail::OnRaw(fill) : detail::DimRaw(), lit ? 0.7f : 0.8f), 8);
        if (ts.tick_labels && (ticks <= 11 || i % ((ticks - 1) / 10 + 1) == 0 || i == ticks - 1)) {
            detail::Fmt(tb, 32, fmt, v_min + step * (float)i);
            float tw = detail::TextW(tb);
            dl->AddText(ImVec2(x - tw * 0.5f, pos.y + h - 1.0f), detail::Final(detail::DimRaw()), tb);
        }
    }
    float hot = detail::Anim(detail::Sub(id, 1), active || kb_recent);
    float hov = detail::Anim(detail::Sub(id, 2), hovered);
    detail::Thumb(dl, ImVec2(xa, cy), r, ts.thumb ? ts.thumb : detail::ThumbRaw(), fill,
                  detail::Max(hot, hov * 0.4f));
    if (hot > 0.01f) {
        detail::Fmt(tb, 32, fmt, *v);
        detail::Bubble(dl, ImVec2(xa, cy - r), tb, hot, fill);
    }
    if (ts.show_label) detail::TrailingLabel(label, lend, h);
    ImGui::EndGroup();
    ImGui::PopID();
    return changed;
}

inline bool TickSlider(const char* label, int* v, int v_min, int v_max, const char* fmt = "%d",
                       const TickSliderStyle& ts = TickSliderStyle()) {
    if (!v) return false;
    float f = (float)*v;
    char ff[48];
    int span = v_max - v_min;
    int ticks = span < 1 ? 2 : (span > 100 ? 101 : span + 1);
    TickSliderStyle s = ts;
    if (span <= 100) s.snap = true;
    TickSlider(label, &f, (float)v_min, (float)v_max, ticks, detail::IntFmt(fmt, ff, 48), s);
    int nv = detail::RoundI(f);
    bool changed = nv != *v;
    *v = nv;
    return changed;
}

struct StepperStyle {
    float width = 0.0f;
    float repeat_delay = 0.35f;
    float repeat_rate = 0.07f;
    ImU32 bg = 0;
    ImU32 button = 0;
    ImU32 text = 0;
    bool show_label = true;
};

namespace detail {

// press fires once, holding repeats faster and faster
inline int HoldRepeat(ImGuiID key, bool pressed_now, bool active, float delay, float rate) {
    ImGuiStorage* store = ImGui::GetStateStorage();
    const ImGuiIO& io = ImGui::GetIO();
    if (ImGui::IsItemActivated() && ImGui::IsMouseDown(0)) {
        store->SetFloat(key, delay);
        return 1;
    }
    if (!active || !ImGui::IsMouseDown(0)) return (pressed_now && !ImGui::IsItemDeactivated()) ? 1 : 0;
    float held = io.MouseDownDuration[0];
    float next = store->GetFloat(key, delay);
    int fires = 0;
    while (held >= next && fires < 16) {
        ++fires;
        float r = Max(rate * 0.3f, rate - (held - delay) * rate * 0.25f);
        next += r;
    }
    store->SetFloat(key, next);
    return fires;
}

inline bool StepButton(const char* id, ImGuiID anim, const ImVec2& size, bool plus, bool enabled, ImDrawFlags corners,
                       float rounding, ImU32 button_raw, float delay, float rate, int* fires) {
    ImVec2 a = ImGui::GetCursorScreenPos(), b(a.x + size.x, a.y + size.y);
    bool pressed = ImGui::InvisibleButton(id, size, kNav);
    bool hov = ImGui::IsItemHovered() && enabled, act = ImGui::IsItemActive() && enabled;
    *fires = enabled ? HoldRepeat(Sub(anim, 7), pressed, act, delay, rate) : 0;
    float hk = AnimTo(anim, act ? 1.0f : (hov ? 0.55f : 0.0f));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (hk > 0.01f) dl->AddRectFilled(a, b, Final(Shade(button_raw, 0.1f), hk), rounding, corners);
    float s = Min(size.x, size.y) * 0.2f;
    Plus(dl, Mid(a, b), s, Final(enabled ? TextRaw() : DimRaw(), enabled ? 1.0f : 0.6f), Max(1.5f, 1.4f * Dpi()), plus);
    return *fires > 0;
}

}  // namespace detail

// [-] value [+], click or hold the buttons; arrows work on the focused value
inline bool Stepper(const char* label, float* v, float v_min, float v_max, float step = 1.0f,
                    const char* fmt = "%.0f", const StepperStyle& ss = StepperStyle()) {
    float tmp = v_min;
    if (!v) v = &tmp;
    if (step <= 0.0f) step = 1.0f;
    float h = ImGui::GetFrameHeight();
    const char* lend = detail::LabelEnd(label);
    char vb[64];
    detail::Fmt(vb, 64, fmt, *v);
    char wa[32], wb[32];
    detail::Fmt(wa, 32, fmt, v_max);
    detail::Fmt(wb, 32, fmt, v_min);
    float val_w = detail::Max(detail::Max(detail::TextW(wa), detail::TextW(wb)), detail::TextW(vb)) +
                  ImGui::GetStyle().FramePadding.x * 2.0f;
    if (ss.width > 0.0f) val_w = detail::Max(ss.width - h * 2.0f, h);
    ImGuiID id = ImGui::GetID(label);
    ImGui::PushID(label);
    ImGui::BeginGroup();
    ImVec2 pos = ImGui::GetCursorScreenPos();
    float total = h * 2.0f + val_w, r = detail::Rounding(h);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImU32 bg = detail::Pick(ss.bg, ImGuiCol_FrameBg);
    detail::Frame(dl, pos, ImVec2(pos.x + total, pos.y + h), detail::Final(bg), r);
    ImU32 btn = detail::Pick(ss.button, ImGuiCol_Button);
    float before = *v;
    int fires = 0;
    detail::StepButton("##minus", detail::Sub(id, 1), ImVec2(h, h), false, *v > v_min, detail::kRoundLeft, r, btn,
                       ss.repeat_delay, ss.repeat_rate, &fires);
    if (fires) *v = detail::Clamp(*v - step * (float)fires, v_min, v_max);
    ImGui::SameLine(0.0f, 0.0f);
    ImVec2 va = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##value", ImVec2(val_w, h), detail::kNav);
    int d = detail::FocusedArrows(true, true);
    if (d) *v = detail::Clamp(*v + step * (float)d, v_min, v_max);
    ImGui::SameLine(0.0f, 0.0f);
    detail::StepButton("##plus", detail::Sub(id, 2), ImVec2(h, h), true, *v < v_max, detail::kRoundRight, r, btn,
                       ss.repeat_delay, ss.repeat_rate, &fires);
    if (fires) *v = detail::Clamp(*v + step * (float)fires, v_min, v_max);
    bool changed = *v != before;
    if (changed) {
        detail::Fmt(vb, 64, fmt, *v);
        imb::AnimateTo(detail::Sub(id, 3), *v > before ? 1.0f : -1.0f, 0.0f);
    }
    float bump = detail::AnimTo(detail::Sub(id, 3), 0.0f, 0.18f);
    float dy = bump * ImGui::GetFontSize() * 0.35f;
    ImVec2 vb_a(va.x, va.y + dy), vb_b(va.x + val_w, va.y + h + dy);
    dl->PushClipRect(va, ImVec2(va.x + val_w, va.y + h), true);
    detail::TextIn(dl, vb_a, vb_b, vb, nullptr, detail::Final(ss.text ? ss.text : detail::TextRaw(), 1.0f - detail::Abs(bump) * 0.6f));
    dl->PopClipRect();
    float sep_th = detail::Max(1.0f, detail::Dpi());
    dl->AddLine(ImVec2(va.x, pos.y + h * 0.25f), ImVec2(va.x, pos.y + h * 0.75f), detail::Final(detail::BorderRaw(), 0.7f), sep_th);
    dl->AddLine(ImVec2(va.x + val_w, pos.y + h * 0.25f), ImVec2(va.x + val_w, pos.y + h * 0.75f),
                detail::Final(detail::BorderRaw(), 0.7f), sep_th);
    if (ss.show_label) detail::TrailingLabel(label, lend, h);
    ImGui::EndGroup();
    ImGui::PopID();
    return changed;
}

inline bool Stepper(const char* label, int* v, int v_min, int v_max, int step = 1,
                    const StepperStyle& ss = StepperStyle()) {
    if (!v) return false;
    float f = (float)*v;
    Stepper(label, &f, (float)v_min, (float)v_max, (float)(step > 0 ? step : 1), "%.0f", ss);
    int nv = detail::RoundI(f);
    bool changed = nv != *v;
    *v = nv;
    return changed;
}

inline bool RangeSlider(const char* label, std::nullptr_t, float v_min, float v_max, const char* fmt = "%.2f",
                        const RangeSliderStyle& rs = RangeSliderStyle()) {
    return RangeSlider(label, static_cast<float*>(nullptr), v_min, v_max, fmt, rs);
}

inline bool RangeSlider(const char* label, std::nullptr_t, int v_min, int v_max, const char* fmt = "%d",
                        const RangeSliderStyle& rs = RangeSliderStyle()) {
    return RangeSlider(label, static_cast<int*>(nullptr), v_min, v_max, fmt, rs);
}

}  // namespace ctl
}  // namespace imb
