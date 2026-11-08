#pragma once
// RadarMiniMap: draws points you hand it, relative to the center
#include "imb_game_core.h"

namespace imb {
namespace game {

enum RadarShape { RadarShape_Circle = 0, RadarShape_Square = 1 };

// x/y in radar units: -1..1 spans the range, +y is forward. yaw in degrees, 0 is forward
struct RadarPoint {
    float x = 0.0f;
    float y = 0.0f;
    int team = 1;
    float yaw = 0.0f;
    bool has_yaw = false;
};

struct RadarStyle {
    int shape = RadarShape_Circle;
    float rotation = 0.0f;
    float zoom = 1.0f;
    int rings = 3;
    bool cross = true;
    bool self_marker = true;
    bool clamp_to_edge = true;
    bool sweep = true;
    bool north = true;
    float point_radius = 0.0f;
    ImU32 bg = 0;
    ImU32 grid = 0;
};

namespace detail {

struct RadarFrame {
    ImDrawList* dl;
    ImVec2 c;
    float R, cs, sn, pr, t;
    bool square, clamp;
};

inline bool RadarProject(const RadarFrame& f, float x, float y, float zoom, ImVec2* out) {
    float dx = (x * f.cs - y * f.sn) * zoom;
    float dy = (x * f.sn + y * f.cs) * zoom;
    bool outside = false;
    if (f.square) {
        if (fabsf(dx) > 1.0f || fabsf(dy) > 1.0f) {
            outside = true;
            dx = Clamp(dx, -1.0f, 1.0f);
            dy = Clamp(dy, -1.0f, 1.0f);
        }
    } else {
        float len = sqrtf(dx * dx + dy * dy);
        if (len > 1.0f) {
            outside = true;
            dx /= len;
            dy /= len;
        }
    }
    float inset = f.R - f.pr - 1.0f;
    *out = ImVec2(f.c.x + dx * inset, f.c.y - dy * inset);
    return !outside;
}

inline void RadarBlip(const RadarFrame& f, const ImVec2& p, int team, float yaw_deg, bool has_yaw, bool inside,
                      int index, bool hot) {
    ImU32 col = TeamColor(team);
    float a = inside ? 1.0f : 0.45f;
    if (team != 0 && inside) {
        float ph = fmodf(f.t * 0.9f + (float)index * 0.37f, 1.0f);
        f.dl->AddCircle(p, f.pr * (1.2f + 1.6f * ph), Live(col, 0.5f * (1.0f - ph)), 20, 1.5f);
    }
    if (has_yaw) {
        float ang = yaw_deg * kPi / 180.0f;
        float ca = f.cs * cosf(ang) - f.sn * sinf(ang);
        float sa = f.sn * cosf(ang) + f.cs * sinf(ang);
        ImVec2 dir(-sa, -ca);
        ImVec2 tip(p.x + dir.x * f.pr * 2.4f, p.y + dir.y * f.pr * 2.4f);
        ImVec2 side(-dir.y * f.pr * 0.9f, dir.x * f.pr * 0.9f);
        f.dl->AddTriangleFilled(tip, ImVec2(p.x + side.x, p.y + side.y), ImVec2(p.x - side.x, p.y - side.y),
                                Live(col, 0.85f * a));
    }
    f.dl->AddCircleFilled(p, f.pr + 1.0f, Live(IM_COL32(0, 0, 0, 200), a), 16);
    f.dl->AddCircleFilled(p, f.pr, Live(col, a), 16);
    if (hot) f.dl->AddCircle(p, f.pr + 3.0f, Live(IM_COL32_WHITE), 20, 1.5f);
}

}  // namespace detail

// returns the index of the hovered point, -1 when none
inline int RadarMiniMap(const char* id, const RadarPoint* points, int count, const RadarStyle& st = RadarStyle(),
                        const ImVec2& size = ImVec2(0, 0)) {
    namespace d = detail;
    float fs = ImGui::GetFontSize();
    ImVec2 sz = d::ResolveSize(size, fs * 10.0f, fs * 10.0f);
    ImVec2 mn = ImGui::GetCursorScreenPos(), mx(mn.x + sz.x, mn.y + sz.y);
    ImGui::PushID(id);
    ImGui::Dummy(sz);
    bool hovered = ImGui::IsItemHovered();
    ImGui::PopID();

    d::RadarFrame f;
    f.dl = ImGui::GetWindowDrawList();
    f.c = ImVec2(floorf((mn.x + mx.x) * 0.5f), floorf((mn.y + mx.y) * 0.5f));
    f.R = d::ed::Min(sz.x, sz.y) * 0.5f - 1.0f;
    float rot = -st.rotation * d::kPi / 180.0f;
    f.cs = cosf(rot);
    f.sn = sinf(rot);
    f.pr = st.point_radius > 0.0f ? st.point_radius : d::ed::Max(2.5f * d::Dpi(), f.R * 0.045f);
    f.t = d::Time();
    f.square = st.shape == RadarShape_Square;
    f.clamp = st.clamp_to_edge;
    ImDrawList* dl = f.dl;
    ImVec2 bmn(f.c.x - f.R, f.c.y - f.R), bmx(f.c.x + f.R, f.c.y + f.R);
    float rnd = d::ed::CardRounding();
    ImU32 bg = st.bg ? d::Live(st.bg) : d::Live(GetStyle().overlay_bg);
    ImU32 grid = st.grid ? d::Live(st.grid) : d::Live(IM_COL32(255, 255, 255, 30));
    ImU32 accent = d::ed::Accent();

    if (f.square) dl->AddRectFilled(bmn, bmx, bg, rnd);
    else dl->AddCircleFilled(f.c, f.R, bg, 64);
    for (int i = 4; i >= 1; --i) {
        float k = (float)i / 4.0f;
        if (f.square) dl->AddRectFilled(ImVec2(f.c.x - f.R * k, f.c.y - f.R * k), ImVec2(f.c.x + f.R * k, f.c.y + f.R * k),
                                        d::ed::WithAlpha(accent, 0.025f), rnd * k);
        else dl->AddCircleFilled(f.c, f.R * k, d::ed::WithAlpha(accent, 0.025f), 48);
    }
    int rings = st.rings < 0 ? 0 : (st.rings > 8 ? 8 : st.rings);
    for (int i = 1; i <= rings; ++i) {
        float k = (float)i / (float)(rings + 1);
        if (f.square) d::ed::Outline(dl, ImVec2(f.c.x - f.R * k, f.c.y - f.R * k), ImVec2(f.c.x + f.R * k, f.c.y + f.R * k),
                                     grid, rnd * k, 1.0f);
        else dl->AddCircle(f.c, f.R * k, grid, 48, 1.0f);
    }
    if (st.cross) {
        dl->AddLine(ImVec2(f.c.x - f.R, f.c.y), ImVec2(f.c.x + f.R, f.c.y), grid);
        dl->AddLine(ImVec2(f.c.x, f.c.y - f.R), ImVec2(f.c.x, f.c.y + f.R), grid);
    }
    if (f.square) dl->PushClipRect(bmn, bmx, true);
    if (st.sweep) {
        float a = f.t * 1.6f;
        const int kSteps = 14;
        float span = 0.9f;
        float reach = f.square ? f.R * 1.42f : f.R;
        for (int i = 0; i < kSteps; ++i) {
            float a0 = a - span * (float)(i + 1) / (float)kSteps, a1 = a - span * (float)i / (float)kSteps;
            float alpha = 0.22f * (1.0f - (float)i / (float)kSteps);
            dl->AddTriangleFilled(f.c, ImVec2(f.c.x + cosf(a0) * reach, f.c.y + sinf(a0) * reach),
                                  ImVec2(f.c.x + cosf(a1) * reach, f.c.y + sinf(a1) * reach),
                                  d::ed::WithAlpha(accent, alpha));
        }
        dl->AddLine(f.c, ImVec2(f.c.x + cosf(a) * reach, f.c.y + sinf(a) * reach), d::ed::WithAlpha(accent, 0.7f), 1.5f);
    }
    ImVec2 mouse = ImGui::GetMousePos();
    int hot = -1;
    float best = (f.pr + 4.0f) * (f.pr + 4.0f);
    if (points && count > 0) {
        float zoom = st.zoom > 0.0f ? st.zoom : 1.0f;
        for (int pass = 0; pass < 2; ++pass) {
            for (int i = 0; i < count; ++i) {
                ImVec2 p;
                if (d::Fin(points[i].x) != points[i].x || d::Fin(points[i].y) != points[i].y) continue;
                bool inside = d::RadarProject(f, points[i].x, points[i].y, zoom, &p);
                if (!inside && !f.clamp) continue;
                if (pass == 0) {
                    if (!hovered) continue;
                    float dx = mouse.x - p.x, dy = mouse.y - p.y;
                    if (dx * dx + dy * dy < best) {
                        best = dx * dx + dy * dy;
                        hot = i;
                    }
                    continue;
                }
                d::RadarBlip(f, p, points[i].team, points[i].yaw, points[i].has_yaw, inside, i, i == hot);
            }
        }
    }
    if (st.self_marker) {
        float s = f.pr * 1.6f;
        ImVec2 tip(f.c.x, f.c.y - s * 1.3f), l(f.c.x - s, f.c.y + s), r(f.c.x + s, f.c.y + s), notch(f.c.x, f.c.y + s * 0.4f);
        dl->AddTriangleFilled(tip, l, notch, d::Live(IM_COL32_WHITE));
        dl->AddTriangleFilled(tip, notch, r, d::Live(IM_COL32_WHITE, 0.8f));
    }
    if (f.square) dl->PopClipRect();
    if (st.north) {
        float na = rot - d::kPi * 0.5f;
        float nr = f.R - fs * 0.55f;
        ImVec2 np(f.c.x + cosf(na) * nr, f.c.y + sinf(na) * nr);
        if (f.square) {
            np.x = d::Clamp(np.x, bmn.x + fs * 0.5f, bmx.x - fs * 0.5f);
            np.y = d::Clamp(np.y, bmn.y + fs * 0.5f, bmx.y - fs * 0.5f);
        }
        float tfs = fs * 0.7f;
        ImVec2 ts = d::TextSize(tfs, "N");
        d::TextShadow(dl, tfs, ImVec2(np.x - ts.x * 0.5f, np.y - ts.y * 0.5f), accent, "N");
    }
    if (f.square) d::ed::Outline(dl, bmn, bmx, d::ed::BorderCol(), rnd, 1.0f);
    else dl->AddCircle(f.c, f.R, d::ed::BorderCol(), 64, 1.0f);
    return hot;
}

// flat (x, y, team) triples, handy for float arrays and std::vector<float>
inline int RadarMiniMapPacked(const char* id, const float* xyt, int float_count, const RadarStyle& st = RadarStyle(),
                              const ImVec2& size = ImVec2(0, 0)) {
    namespace d = detail;
    RadarPoint buf[64];
    int n = 0;
    for (int i = 0; xyt && i + 2 < float_count && n < 64; i += 3) {
        buf[n].x = xyt[i];
        buf[n].y = xyt[i + 1];
        buf[n].team = d::SafeInt(xyt[i + 2]);
        ++n;
    }
    return RadarMiniMap(id, buf, n, st, size);
}

// flat arguments for generated code. rotation may be null
inline int RadarMiniMapPacked(const char* id, const float* xyt, int float_count, const float* rotation_deg, int shape,
                              float zoom, int rings, bool sweep, const ImVec2& size) {
    RadarStyle st;
    st.rotation = rotation_deg ? *rotation_deg : 0.0f;
    st.shape = shape;
    st.zoom = zoom;
    st.rings = rings;
    st.sweep = sweep;
    return RadarMiniMapPacked(id, xyt, float_count, st, size);
}

}  // namespace game
}  // namespace imb
