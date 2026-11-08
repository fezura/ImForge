#pragma once
// RingProgress, RadialGauge, LoadingBar, SkeletonBlock
#include "imb_disp_core.h"

namespace imb {
namespace disp {

struct RingStyle {
    float radius = 0.0f;
    float thickness = 0.0f;
    ImU32 color = 0;
    ImU32 color2 = 0;  // gradient end along the arc, 0 = solid
    ImU32 track = 0;
    bool show_value = true;
    const char* format = "%.0f%%";
    float start_angle = -90.0f;  // degrees, -90 is 12 o'clock
    float spin_speed = 1.0f;
};

// fraction < 0 spins (indeterminate). Returns the fraction currently drawn.
inline float RingProgress(const char* label, float fraction, const RingStyle& rs = RingStyle()) {
    using namespace detail;
    float font = ImGui::GetFontSize();
    float r = rs.radius > 0.0f ? rs.radius : font * 2.2f;
    float th = rs.thickness > 0.0f ? rs.thickness : Max(3.0f, r * 0.2f);
    ImGuiID id = ImGui::GetID(label);
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(r * 2.0f, r * 2.0f));
    if (!ImGui::IsItemVisible()) return fraction;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 c(pos.x + r, pos.y + r);
    float rr = r - th * 0.5f;
    ImU32 col = rs.color ? ImGui::GetColorU32(rs.color) : Accent();
    ImU32 track = rs.track ? ImGui::GetColorU32(rs.track) : TrackCol();
    dl->AddCircle(c, rr, track, ArcSegments(rr, kPi * 2.0f), th);
    float a_start = rs.start_angle * kPi / 180.0f;
    bool spinning = fraction < 0.0f;
    float shown = fraction;
    const char* end = LabelEnd(label);
    if (spinning) {
        float t = Now() * rs.spin_speed;
        float head = t * 4.4f;
        float len = kPi * (0.18f + 0.62f * (0.5f + 0.5f * sinf(t * 2.3f)));
        int v0 = dl->VtxBuffer.Size;
        Arc(dl, c, rr, head, head + len, col, th, true);
        if (rs.color2) RecolorArc(dl, v0, c, head, len, ImGui::GetColorU32(rs.color2), col);
    } else {
        shown = Tween(id, Fin01(fraction));
        float span = Clamp01(shown) * kPi * 2.0f;
        int v0 = dl->VtxBuffer.Size;
        if (span >= kPi * 2.0f - 1e-3f) dl->AddCircle(c, rr, col, ArcSegments(rr, span), th);
        else if (span > 1e-3f) Arc(dl, c, rr, a_start, a_start + span, col, th, true);
        if (rs.color2 && span > 1e-3f) RecolorArc(dl, v0, c, a_start, span, col, ImGui::GetColorU32(rs.color2));
    }
    bool has_label = end > label;
    char buf[32];
    buf[0] = 0;
    if (!spinning && rs.show_value) Format(buf, sizeof(buf), rs.format, Clamp01(shown) * 100.0f);
    float big = font * Clamp(r / font * 0.42f, 0.9f, 2.2f);
    if (buf[0] && has_label) {
        ImVec2 vs = TextSize(big, buf), ls = TextSize(font * 0.85f, label, end);
        float top = c.y - (vs.y + ls.y) * 0.5f;
        Text(dl, big, ImVec2(c.x - vs.x * 0.5f, top), TextCol(), buf);
        Text(dl, font * 0.85f, ImVec2(c.x - ls.x * 0.5f, top + vs.y), TextDim(), label, end);
    } else if (buf[0]) {
        TextCentered(dl, big, c, TextCol(), buf);
    } else if (has_label) {
        TextCentered(dl, font, c, TextDim(), label, end);
    }
    return shown;
}

inline float RingProgress(const char* label, float fraction, float radius, float thickness = 0.0f,
                          ImU32 color = 0, ImU32 color2 = 0, bool show_value = true) {
    RingStyle rs;
    rs.radius = radius;
    rs.thickness = thickness;
    rs.color = color;
    rs.color2 = color2;
    rs.show_value = show_value;
    return RingProgress(label, fraction, rs);
}

struct GaugeZone {
    float from, to;
    ImU32 color;
};

struct GaugeStyle {
    float radius = 0.0f;
    float thickness = 0.0f;
    float sweep = 270.0f;  // degrees, opening at the bottom
    int major_ticks = 5;   // intervals between labelled ticks
    int minor_ticks = 4;   // per major interval
    bool show_labels = true;
    const char* format = "%.0f";
    const char* tick_format = "%.0f";
    const GaugeZone* zones = nullptr;
    int zone_count = 0;
    ImU32 color = 0;   // value arc, 0 = accent or the zone the value is in
    ImU32 track = 0;
    ImU32 needle = 0;
    bool value_arc = true;
};

// value text under the hub, label under that. Returns the (spring animated) value the needle shows.
inline float RadialGauge(const char* label, float value, float v_min, float v_max,
                         const GaugeStyle& gs = GaugeStyle()) {
    using namespace detail;
    value = Fin(value);
    v_min = Fin(v_min);
    v_max = Fin(v_max);
    float font = ImGui::GetFontSize();
    float r = gs.radius > 0.0f ? gs.radius : font * 4.0f;
    float th = gs.thickness > 0.0f ? gs.thickness : Max(3.0f, r * 0.11f);
    float sweep = Clamp(gs.sweep, 30.0f, 360.0f) * kPi / 180.0f;
    float gap = kPi * 2.0f - sweep;
    float a0 = kPi * 0.5f + gap * 0.5f;
    float below = Max(r * cosf(gap * 0.5f) + th * 0.5f, r * 0.42f + font * 2.3f);
    float w = r * 2.0f + th, h = r + th * 0.5f + below;
    ImGuiID id = ImGui::GetID(label);
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(w, h));
    float range = v_max - v_min;
    if (range == 0.0f) range = 1.0f;
    float frac = Clamp01((value - v_min) / range);
    float shown_f = Clamp(SpringTo(id, frac), -0.03f, 1.03f);
    float shown = v_min + shown_f * range;
    if (!ImGui::IsItemVisible()) return shown;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 c(pos.x + w * 0.5f, pos.y + r + th * 0.5f);
    float rr = r - th * 0.5f;
    ImU32 track = gs.track ? ImGui::GetColorU32(gs.track) : TrackCol();
    Arc(dl, c, rr, a0, a0 + sweep, track, th, true);
    ImU32 value_col = gs.color ? ImGui::GetColorU32(gs.color) : Accent();
    float zr = rr - th * 0.5f - Max(2.0f, th * 0.35f);
    float zth = Max(2.0f, th * 0.3f);
    for (int i = 0; i < gs.zone_count && gs.zones; ++i) {
        const GaugeZone& z = gs.zones[i];
        float f0 = Clamp01((z.from - v_min) / range), f1 = Clamp01((z.to - v_min) / range);
        Arc(dl, c, zr, a0 + f0 * sweep, a0 + f1 * sweep, ImGui::GetColorU32(WithAlpha(z.color, 0.85f)), zth, false);
        if (!gs.color && value >= z.from && value <= z.to) value_col = ImGui::GetColorU32(z.color);
    }
    if (gs.value_arc && shown_f > 0.0f) Arc(dl, c, rr, a0, a0 + Clamp01(shown_f) * sweep, value_col, th, true);
    int majors = MaxI(gs.major_ticks, 1), minors = MaxI(gs.minor_ticks, 1);
    float tick_out = zr - zth * 0.5f - 2.0f;
    float sm = font * 0.8f;
    // skip labels when they'd touch: compare the widest one with the arc between major ticks
    int label_step = 1;
    if (gs.show_labels) {
        char a[24], b[24];
        Format(a, sizeof(a), gs.tick_format, v_min);
        Format(b, sizeof(b), gs.tick_format, v_max);
        float lw = Max(TextSize(sm, a).x, TextSize(sm, b).x) + sm;
        float gap_px = (tick_out - th - sm) * sweep / (float)majors;
        if (gap_px > 1.0f) label_step = MaxI(1, (int)ceilf(lw / gap_px));
    }
    for (int i = 0; i <= majors * minors; ++i) {
        bool major = i % minors == 0;
        float f = (float)i / (float)(majors * minors);
        float a = a0 + f * sweep;
        float len = major ? th * 0.9f : th * 0.45f;
        dl->AddLine(Polar(c, tick_out, a), Polar(c, tick_out - len, a), major ? TextDim() : TextDim(0.5f),
                    major ? 1.5f : 1.0f);
        if (major && gs.show_labels && r > font * 2.5f && (i / minors) % label_step == 0) {
            char buf[24];
            Format(buf, sizeof(buf), gs.tick_format, v_min + f * range);
            TextCentered(dl, sm, Polar(c, tick_out - len - sm * 0.95f, a), TextDim(), buf);
        }
    }
    float na = a0 + shown_f * sweep;
    float hub = Max(3.0f, th * 0.55f);
    ImU32 ncol = gs.needle ? ImGui::GetColorU32(gs.needle) : TextCol();
    ImVec2 tip = Polar(c, tick_out - th * 0.4f, na);
    ImVec2 l = Polar(c, hub * 0.55f, na - kPi * 0.5f), rt = Polar(c, hub * 0.55f, na + kPi * 0.5f);
    ImVec2 tail = Polar(c, hub * 1.6f, na + kPi);
    dl->AddTriangleFilled(l, tip, rt, ncol);
    dl->AddTriangleFilled(rt, tail, l, ncol);
    dl->AddCircleFilled(c, hub, ncol);
    dl->AddCircleFilled(c, hub * 0.45f, value_col);
    char vbuf[32];
    Format(vbuf, sizeof(vbuf), gs.format, shown);
    float big = font * Clamp(r / font * 0.32f, 1.0f, 2.0f);
    ImVec2 vs = TextSize(big, vbuf);
    float vy = c.y + r * 0.42f;
    Text(dl, big, ImVec2(c.x - vs.x * 0.5f, vy - vs.y * 0.5f), TextCol(), vbuf);
    const char* end = LabelEnd(label);
    if (end > label) {
        ImVec2 ls = TextSize(sm, label, end);
        Text(dl, sm, ImVec2(c.x - ls.x * 0.5f, vy + vs.y * 0.5f), TextDim(), label, end);
    }
    return shown;
}

// three zones: good up to warn_from, warning up to danger_from, danger above
inline float RadialGauge(const char* label, float value, float v_min, float v_max, float warn_from,
                         float danger_from, float radius = 0.0f, const char* format = "%.0f") {
    const Style& s = GetStyle();
    GaugeZone zones[3] = {{v_min, warn_from, s.success}, {warn_from, danger_from, s.warning},
                          {danger_from, v_max, s.danger}};
    GaugeStyle gs;
    gs.radius = radius;
    gs.format = format;
    if (warn_from < v_max || danger_from < v_max) {
        gs.zones = zones;
        gs.zone_count = 3;
    }
    return RadialGauge(label, value, v_min, v_max, gs);
}

struct LoadingBarStyle {
    ImVec2 size = ImVec2(0, 0);  // 0 width fills, 0 height = 0.4 x font size
    int segments = 0;            // > 0 draws discrete steps
    float segment_gap = 0.0f;
    ImU32 color = 0;
    ImU32 color2 = 0;  // gradient end, 0 = a lighter accent
    ImU32 track = 0;
    float speed = 1.0f;
    bool glow = true;
    bool sheen = true;
};

// fraction < 0 is indeterminate. Returns the fraction currently drawn.
inline float LoadingBar(const char* id_label, float fraction, const LoadingBarStyle& ls = LoadingBarStyle()) {
    using namespace detail;
    float font = ImGui::GetFontSize();
    float w = ResolveW(ls.size.x, font * 2.0f);
    float h = ls.size.y > 0.0f ? ls.size.y : Max(4.0f, font * 0.4f);
    ImGuiID id = ImGui::GetID(id_label);
    ImVec2 mn = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(w, h));
    bool spinning = fraction < 0.0f;
    float shown = spinning ? fraction : Tween(id, Fin01(fraction));
    if (!ImGui::IsItemVisible()) return shown;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 mx(mn.x + w, mn.y + h);
    float rad = h * 0.5f;
    ImU32 c0 = ls.color ? ImGui::GetColorU32(ls.color) : Accent();
    ImU32 c1 = ls.color2 ? ImGui::GetColorU32(ls.color2) : Lighten(c0, 0.35f);
    ImU32 track = ls.track ? ImGui::GetColorU32(ls.track) : TrackCol();
    float t = Now() * ls.speed;
    if (ls.segments > 0) {
        int n = MinI(ls.segments, 256);
        float g = ls.segment_gap > 0.0f ? ls.segment_gap : Max(2.0f, font * 0.25f);
        float sw = (w - g * (float)(n - 1)) / (float)n;
        if (sw < 1.0f) sw = 1.0f;
        float filled = spinning ? -1.0f : shown * (float)n;
        float pulse = 0.55f + 0.45f * (0.5f + 0.5f * sinf(t * 5.0f));
        float sr = Min(rad, sw * 0.5f);
        for (int i = 0; i < n; ++i) {
            ImVec2 a(mn.x + (float)i * (sw + g), mn.y), b(a.x + sw, mx.y);
            dl->AddRectFilled(a, b, track, sr);
            float k;
            if (spinning) {
                float head = fmodf(t * 1.4f, 1.0f) * (float)(n + 2) - 1.0f;
                float d = fabsf((float)i - head);
                k = Clamp01(1.0f - d / 1.5f);
            } else {
                k = Clamp01(filled - (float)i);
            }
            if (k <= 0.0f) continue;
            ImU32 sc = Mix(c0, c1, n > 1 ? (float)i / (float)(n - 1) : 0.0f);
            if (!spinning && k < 1.0f) sc = WithAlpha(sc, 0.35f + 0.65f * k * pulse);
            if (spinning) sc = WithAlpha(sc, k);
            float fw = spinning ? sw : sw * Max(k, 0.0f);
            dl->AddRectFilled(a, ImVec2(a.x + Max(fw, sr * 2.0f), b.y), sc, sr);
        }
        return shown;
    }
    dl->AddRectFilled(mn, mx, track, rad);
    if (spinning) {
        float band = w * 0.32f;
        float p = fmodf(t * 0.75f, 1.0f);
        float e = p < 0.5f ? 4.0f * p * p * p : 1.0f - powf(-2.0f * p + 2.0f, 3.0f) * 0.5f;
        float x0 = mn.x - band + e * (w + band), x1 = x0 + band;
        float a = Max(x0, mn.x), b = Min(x1, mx.x);
        if (b - a > 1.0f) {
            int v0 = dl->VtxBuffer.Size;
            dl->AddRectFilled(ImVec2(a, mn.y), ImVec2(b, mx.y), IM_COL32_WHITE, Min(rad, (b - a) * 0.5f));
            Recolor(dl, v0, [&](const ImVec2& q) {
                float u = Clamp01((q.x - x0) / band);
                float fade = Clamp01(Min(u, 1.0f - u) * 4.0f);
                return WithAlpha(Mix(c0, c1, u), fade);
            });
        }
        return shown;
    }
    float fw = w * Clamp01(shown);
    if (fw < 0.5f) return shown;
    ImVec2 fmx(mn.x + Max(fw, Min(h, w)), mx.y);
    if (ls.glow) imb::Glow(dl, mn, fmx, rad, WithAlpha(c0, 0.35f), Max(2.0f, h * 0.8f));
    imb::GradientRect(dl, mn, fmx, c0, c1, false, rad);
    if (ls.sheen && fw > h * 2.0f) {
        float band = Max(h * 3.0f, fw * 0.18f);
        float p = fmodf(t * 0.45f, 1.4f) / 1.4f;
        float x0 = mn.x - band + p * (fw + band * 2.0f);
        float a = Max(x0, mn.x), b = Min(x0 + band, fmx.x);
        if (b - a > 1.0f) {
            int v0 = dl->VtxBuffer.Size;
            dl->AddRectFilled(ImVec2(a, mn.y), ImVec2(b, mx.y), IM_COL32_WHITE, Min(rad, (b - a) * 0.5f));
            Recolor(dl, v0, [&](const ImVec2& q) {
                float u = Clamp01((q.x - x0) / band);
                return IM_COL32(255, 255, 255, (int)(70.0f * Clamp01(Min(u, 1.0f - u) * 2.0f)));
            });
        }
    }
    return shown;
}

inline float LoadingBar(const char* id_label, float fraction, int segments, const ImVec2& size = ImVec2(0, 0),
                        ImU32 color = 0) {
    LoadingBarStyle ls;
    ls.segments = segments;
    ls.size = size;
    ls.color = color;
    return LoadingBar(id_label, fraction, ls);
}

enum class SkeletonShape { Rect, Line, Circle };

struct SkeletonStyle {
    ImU32 base = 0;
    ImU32 shine = 0;
    float rounding = -1.0f;  // < 0 = frame rounding (lines and circles are always pills)
    float speed = 1.0f;
    int lines = 1;            // Line shape: number of text lines, the last one is shorter
    float line_gap = 0.0f;
};

namespace detail {

// filled rounded rect built from vertical strips so a horizontal gradient can run through it
template <class F>
inline void StripFill(ImDrawList* dl, const ImVec2& mn, const ImVec2& mx, float rounding, F color_at) {
    float w = mx.x - mn.x, h = mx.y - mn.y;
    if (w <= 0.0f || h <= 0.0f) return;
    float r = Min(rounding, Min(w, h) * 0.5f);
    int n = MaxI(2, MinI((int)(w / 3.0f), 160));
    ImVec2 uv = ImGui::GetFontTexUvWhitePixel();
    dl->PrimReserve(n * 6, (n + 1) * 2);
    unsigned base = dl->_VtxCurrentIdx;
    for (int i = 0; i <= n; ++i) {
        float x = mn.x + w * (float)i / (float)n;
        float d = 0.0f;
        if (x < mn.x + r) d = mn.x + r - x;
        else if (x > mx.x - r) d = x - (mx.x - r);
        float inset = d > 0.0f ? r - sqrtf(Max(r * r - d * d, 0.0f)) : 0.0f;
        ImU32 c = color_at(x);
        dl->PrimWriteVtx(ImVec2(x, mn.y + inset), uv, c);
        dl->PrimWriteVtx(ImVec2(x, mx.y - inset), uv, c);
    }
    for (int i = 0; i < n; ++i) {
        ImDrawIdx a = (ImDrawIdx)(base + (unsigned)i * 2);
        dl->PrimWriteIdx(a);
        dl->PrimWriteIdx((ImDrawIdx)(a + 1));
        dl->PrimWriteIdx((ImDrawIdx)(a + 3));
        dl->PrimWriteIdx(a);
        dl->PrimWriteIdx((ImDrawIdx)(a + 3));
        dl->PrimWriteIdx((ImDrawIdx)(a + 2));
    }
}

}  // namespace detail

inline void SkeletonBlock(SkeletonShape shape, const ImVec2& size = ImVec2(0, 0), const SkeletonStyle& ss = SkeletonStyle()) {
    using namespace detail;
    float font = ImGui::GetFontSize();
    int lines = shape == SkeletonShape::Line ? MaxI(1, MinI(ss.lines, 64)) : 1;
    float line_h = size.y > 0.0f ? size.y : font * 0.8f;
    float gap = ss.line_gap > 0.0f ? ss.line_gap : font * 0.55f;
    ImVec2 sz;
    if (shape == SkeletonShape::Circle) {
        float d = size.x > 0.0f ? size.x : (size.y > 0.0f ? size.y : ImGui::GetFrameHeight() * 2.0f);
        sz = ImVec2(d, d);
    } else if (shape == SkeletonShape::Line) {
        sz = ImVec2(ResolveW(size.x, font), line_h * (float)lines + gap * (float)(lines - 1));
    } else {
        sz = ImVec2(ResolveW(size.x, font), size.y > 0.0f ? size.y : font * 4.0f);
    }
    ImVec2 mn = ImGui::GetCursorScreenPos();
    ImGui::Dummy(sz);
    if (!ImGui::IsItemVisible()) return;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImU32 base = ss.base ? ImGui::GetColorU32(ss.base) : CardBg(0.09f);
    ImU32 shine = ss.shine ? ImGui::GetColorU32(ss.shine) : CardBg(0.18f);
    // band position comes from the window, so every block in it shimmers in sync
    ImVec2 wp = ImGui::GetWindowPos();
    float ww = Max(ImGui::GetWindowSize().x, 1.0f);
    float band = Max(font * 6.0f, ww * 0.25f);
    float cycle = 1.6f / Max(ss.speed, 0.01f);
    float p = fmodf(Now(), cycle) / cycle;
    float cx = wp.x - band + p * (ww + band * 2.0f);
    auto color_at = [&](float x) {
        float d = fabsf(x - cx) / (band * 0.5f);
        float k = Clamp01(1.0f - d);
        return Mix(base, shine, k * k * (3.0f - 2.0f * k));
    };
    for (int i = 0; i < lines; ++i) {
        ImVec2 a, b;
        float rounding;
        if (shape == SkeletonShape::Circle) {
            a = mn;
            b = Add(mn, sz);
            rounding = sz.x * 0.5f;
        } else if (shape == SkeletonShape::Line) {
            float lw = (lines > 1 && i == lines - 1) ? sz.x * 0.6f : sz.x;
            a = ImVec2(mn.x, mn.y + (float)i * (line_h + gap));
            b = ImVec2(mn.x + lw, a.y + line_h);
            rounding = line_h * 0.5f;
        } else {
            a = mn;
            b = Add(mn, sz);
            rounding = ss.rounding >= 0.0f ? ss.rounding : Rounding();
        }
        StripFill(dl, a, b, rounding, color_at);
        // a thin outline in the base color smooths the strip edges
        if (shape == SkeletonShape::Circle) dl->AddCircle(ImVec2((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f), sz.x * 0.5f - 0.5f, base, 0, 1.0f);
        else Outline(dl, ImVec2(a.x + 0.5f, a.y + 0.5f), ImVec2(b.x - 0.5f, b.y - 0.5f), base, Max(rounding - 0.5f, 0.0f), 1.0f);
    }
}

inline void SkeletonBlock(SkeletonShape shape, const ImVec2& size, int lines) {
    SkeletonStyle ss;
    ss.lines = lines;
    SkeletonBlock(shape, size, ss);
}

}  // namespace disp
}  // namespace imb
