#pragma once
// EspBoxPreview, FovCirclePreview: previews of overlay settings drawn over a mock scene
#include "imb_game_core.h"

namespace imb {
namespace game {

enum EspBoxStyle { EspBox_Full = 0, EspBox_Corners = 1, EspBox_3D = 2 };
enum EspBarSide { EspBar_Left = 0, EspBar_Right = 1, EspBar_Top = 2, EspBar_Bottom = 3 };

// the settings a user edits for a box overlay. the preview only reads them
struct EspPreview {
    bool box = true;
    int box_style = EspBox_Corners;
    bool box_outline = true;
    bool box_fill = false;
    bool health_bar = true;
    int health_side = EspBar_Left;
    bool name = true;
    bool distance = true;
    bool head_dot = false;
    bool silhouette = true;
    float thickness = 1.5f;
    float health = 0.72f;
    float distance_m = 38.0f;
    char name_text[32] = "Player";
    ImVec4 box_color = ImVec4(1.0f, 0.34f, 0.34f, 1.0f);
    ImVec4 fill_color = ImVec4(1.0f, 0.34f, 0.34f, 0.12f);
    ImVec4 name_color = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
    ImVec4 distance_color = ImVec4(0.75f, 0.78f, 0.85f, 1.0f);
};

namespace detail {

inline void Limb(ImDrawList* dl, const ImVec2& a, const ImVec2& b, float w, ImU32 col) {
    dl->AddLine(a, b, col, w);
    dl->AddCircleFilled(a, w * 0.5f, col, 12);
    dl->AddCircleFilled(b, w * 0.5f, col, 12);
}

inline void Silhouette(ImDrawList* dl, float cx, float top, float fh, float t, ImU32 col) {
    float sway = sinf(t * 1.3f) * fh * 0.012f;
    float breathe = sinf(t * 2.1f) * fh * 0.006f;
    float limb = fh * 0.075f;
    ImVec2 head(cx + sway, top + fh * 0.075f);
    dl->AddCircleFilled(head, fh * 0.068f, col, 24);
    float sh_y = top + fh * 0.19f + breathe, hip_y = top + fh * 0.52f;
    float sh_w = fh * 0.12f, hip_w = fh * 0.07f;
    ImVec2 torso[4] = {ImVec2(cx - sh_w + sway, sh_y), ImVec2(cx + sh_w + sway, sh_y),
                       ImVec2(cx + hip_w, hip_y), ImVec2(cx - hip_w, hip_y)};
    dl->AddConvexPolyFilled(torso, 4, col);
    Limb(dl, ImVec2(cx + sway, sh_y - fh * 0.03f), ImVec2(cx + sway, sh_y), limb * 0.8f, col);
    Limb(dl, ImVec2(cx - sh_w + sway, sh_y + limb * 0.3f), ImVec2(cx - sh_w - fh * 0.03f, top + fh * 0.5f), limb, col);
    Limb(dl, ImVec2(cx + sh_w + sway, sh_y + limb * 0.3f), ImVec2(cx + sh_w + fh * 0.03f, top + fh * 0.5f), limb, col);
    Limb(dl, ImVec2(cx - hip_w * 0.6f, hip_y), ImVec2(cx - fh * 0.09f, top + fh * 0.96f), limb * 1.1f, col);
    Limb(dl, ImVec2(cx + hip_w * 0.6f, hip_y), ImVec2(cx + fh * 0.09f, top + fh * 0.96f), limb * 1.1f, col);
}

inline void BoxLines(ImDrawList* dl, const ImVec2& a, const ImVec2& b, int style, ImU32 col, float th) {
    if (style == EspBox_Full) {
        ed::Outline(dl, a, b, col, 0.0f, th);
        return;
    }
    float c = ed::Min(b.x - a.x, b.y - a.y) * 0.28f;
    dl->AddLine(a, ImVec2(a.x + c, a.y), col, th);
    dl->AddLine(a, ImVec2(a.x, a.y + c), col, th);
    dl->AddLine(ImVec2(b.x, a.y), ImVec2(b.x - c, a.y), col, th);
    dl->AddLine(ImVec2(b.x, a.y), ImVec2(b.x, a.y + c), col, th);
    dl->AddLine(ImVec2(a.x, b.y), ImVec2(a.x + c, b.y), col, th);
    dl->AddLine(ImVec2(a.x, b.y), ImVec2(a.x, b.y - c), col, th);
    dl->AddLine(b, ImVec2(b.x - c, b.y), col, th);
    dl->AddLine(b, ImVec2(b.x, b.y - c), col, th);
}

// slowly turning cuboid around the figure, oblique projection
inline void Box3D(ImDrawList* dl, float cx, float y0, float y1, float half_w, float yaw, ImU32 col,
                  float th, ImU32 fill) {
    float hd = half_w * 0.8f;
    float cs = cosf(yaw), sn = sinf(yaw);
    ImVec2 lo[4], hi[4];
    const float xs[4] = {-half_w, half_w, half_w, -half_w};
    const float zs[4] = {-hd, -hd, hd, hd};
    for (int i = 0; i < 4; ++i) {
        float rx = xs[i] * cs - zs[i] * sn;
        float rz = xs[i] * sn + zs[i] * cs;
        lo[i] = ImVec2(cx + rx, y1 - rz * 0.3f);
        hi[i] = ImVec2(cx + rx, y0 - rz * 0.3f);
    }
    if (fill & IM_COL32_A_MASK) {
        dl->AddConvexPolyFilled(lo, 4, fill);
        dl->AddConvexPolyFilled(hi, 4, fill);
    }
    Polyline(dl, lo, 4, col, th, true);
    Polyline(dl, hi, 4, col, th, true);
    for (int i = 0; i < 4; ++i) dl->AddLine(lo[i], hi[i], col, th);
}

}  // namespace detail

inline void EspBoxPreview(const char* id, const EspPreview* cfg, const ImVec2& size = ImVec2(0, 0)) {
    static const EspPreview kDefault = EspPreview();
    const EspPreview& e = cfg ? *cfg : kDefault;
    namespace d = detail;
    float fs = ImGui::GetFontSize();
    ImVec2 sz = d::ResolveSize(size, fs * 12.0f, fs * 15.0f);
    ImVec2 mn = ImGui::GetCursorScreenPos(), mx(mn.x + sz.x, mn.y + sz.y);
    ImGui::PushID(id);
    ImGui::Dummy(sz);
    ImGuiID hp_id = ImGui::GetID("##hp");
    ImGui::PopID();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float r = d::ed::CardRounding();
    d::MockViewport(dl, mn, mx, r);
    dl->PushClipRect(mn, mx, true);

    float t = d::Time();
    float dpi = d::Dpi();
    float fh = sz.y * 0.6f;
    float cx = mn.x + sz.x * 0.5f;
    float top = mn.y + sz.y * 0.2f;
    if (e.silhouette) d::Silhouette(dl, cx, top, fh, t, d::Live(IM_COL32(150, 160, 182, 120)));

    ImVec2 a(cx - fh * 0.24f, top - fh * 0.02f), b(cx + fh * 0.24f, top + fh * 0.99f);
    float th = d::ed::Max(1.0f, e.thickness * dpi);
    ImU32 col = ImGui::GetColorU32(e.box_color);
    ImU32 black = d::Live(IM_COL32(0, 0, 0, 200), e.box_color.w);
    if (e.box) {
        if (e.box_style == EspBox_3D) {
            float yaw = 0.55f + 0.35f * sinf(t * 0.7f);
            ImU32 fill = e.box_fill ? ImGui::GetColorU32(e.fill_color) : 0;
            if (e.box_outline) d::Box3D(dl, cx, a.y, b.y, fh * 0.2f, yaw, black, th + 2.0f, 0);
            d::Box3D(dl, cx, a.y, b.y, fh * 0.2f, yaw, col, th, fill);
        } else {
            if (e.box_fill) dl->AddRectFilled(a, b, ImGui::GetColorU32(e.fill_color));
            if (e.box_outline) d::BoxLines(dl, a, b, e.box_style, black, th + 2.0f);
            d::BoxLines(dl, a, b, e.box_style, col, th);
        }
    }
    if (e.head_dot) {
        float sway = sinf(t * 1.3f) * fh * 0.012f;
        ImVec2 head(cx + sway, top + fh * 0.075f);
        if (e.box_outline) dl->AddCircle(head, fh * 0.085f, black, 24, th + 2.0f);
        dl->AddCircle(head, fh * 0.085f, col, 24, th);
    }

    float label_fs = fs * 0.85f;
    float above = a.y, below = b.y;
    if (e.health_bar) {
        float hp = d::ed::AnimTo(hp_id, d::Fin01(e.health), 0.25f);
        float bw = d::ed::Max(2.0f, 3.0f * dpi), gap = 3.0f * dpi;
        bool vertical = e.health_side == EspBar_Left || e.health_side == EspBar_Right;
        ImVec2 bmn, bmx;
        if (e.health_side == EspBar_Right) {
            bmn = ImVec2(b.x + gap, a.y);
            bmx = ImVec2(b.x + gap + bw, b.y);
        } else if (e.health_side == EspBar_Top) {
            bmn = ImVec2(a.x, a.y - gap - bw);
            bmx = ImVec2(b.x, a.y - gap);
            above = bmn.y;
        } else if (e.health_side == EspBar_Bottom) {
            bmn = ImVec2(a.x, b.y + gap);
            bmx = ImVec2(b.x, b.y + gap + bw);
            below = bmx.y;
        } else {
            bmn = ImVec2(a.x - gap - bw, a.y);
            bmx = ImVec2(a.x - gap, b.y);
        }
        dl->AddRectFilled(ImVec2(bmn.x - 1, bmn.y - 1), ImVec2(bmx.x + 1, bmx.y + 1), d::Live(IM_COL32(0, 0, 0, 190)));
        ImU32 hc = d::Live(d::HealthColor(hp));
        if (vertical)
            dl->AddRectFilled(ImVec2(bmn.x, bmx.y - (bmx.y - bmn.y) * hp), bmx, hc);
        else
            dl->AddRectFilled(bmn, ImVec2(bmn.x + (bmx.x - bmn.x) * hp, bmx.y), hc);
        if (e.health < 0.995f) {
            char buf[12];
            std::snprintf(buf, sizeof(buf), "%d", d::SafeInt(d::Fin01(e.health) * 100.0f + 0.5f));
            float sm = fs * 0.65f;
            ImVec2 ts = d::TextSize(sm, buf);
            ImVec2 p = vertical ? ImVec2((bmn.x + bmx.x - ts.x) * 0.5f, bmx.y - (bmx.y - bmn.y) * hp - ts.y * 0.5f)
                                : ImVec2(bmn.x + (bmx.x - bmn.x) * hp - ts.x * 0.5f, (bmn.y + bmx.y - ts.y) * 0.5f);
            d::TextShadow(dl, sm, p, d::Live(IM_COL32_WHITE), buf);
        }
    }
    if (e.name && e.name_text[0]) {
        ImVec2 ts = d::TextSize(label_fs, e.name_text);
        d::TextShadow(dl, label_fs, ImVec2(cx - ts.x * 0.5f, above - ts.y - 2.0f * dpi),
                      ImGui::GetColorU32(e.name_color), e.name_text);
    }
    if (e.distance) {
        char buf[24];
        std::snprintf(buf, sizeof(buf), "%.0fm", (double)d::Fin(e.distance_m));
        ImVec2 ts = d::TextSize(label_fs, buf);
        d::TextShadow(dl, label_fs, ImVec2(cx - ts.x * 0.5f, below + 2.0f * dpi),
                      ImGui::GetColorU32(e.distance_color), buf);
    }
    dl->PopClipRect();
}

// one pointer per setting, the way generated code binds them. null keeps the EspPreview default
inline void EspBoxPreviewFields(const char* id, const bool* box, const int* box_style, const ImVec4* box_color,
                                const bool* health_bar, const int* health_side, const bool* name, const bool* distance,
                                const bool* outline, const bool* fill, const bool* head_dot, const float* health,
                                const char* name_text, float distance_m, float thickness,
                                const ImVec2& size = ImVec2(0, 0)) {
    EspPreview e;
    if (box) e.box = *box;
    if (box_style) e.box_style = *box_style;
    if (box_color) {
        e.box_color = *box_color;
        e.fill_color = ImVec4(box_color->x, box_color->y, box_color->z, box_color->w * 0.12f);
    }
    if (health_bar) e.health_bar = *health_bar;
    if (health_side) e.health_side = *health_side;
    if (name) e.name = *name;
    if (distance) e.distance = *distance;
    if (outline) e.box_outline = *outline;
    if (fill) e.box_fill = *fill;
    if (head_dot) e.head_dot = *head_dot;
    if (health) e.health = *health;
    if (name_text) {
        int i = 0;
        for (; name_text[i] && i < (int)sizeof(e.name_text) - 1; ++i) e.name_text[i] = name_text[i];
        e.name_text[i] = 0;
    }
    e.distance_m = distance_m;
    if (thickness > 0.0f) e.thickness = thickness;
    EspBoxPreview(id, &e, size);
}

struct FovPreviewStyle {
    float reference_height = 1080.0f;
    float min_radius = 10.0f;
    float max_radius = 600.0f;
    bool editable = true;
    bool filled = true;
    bool show_value = true;
    bool crosshair = true;
    ImVec4 color = ImVec4(1.0f, 1.0f, 1.0f, 0.85f);
};

// radius is in pixels on a screen `reference_height` tall; dragging the ring edits it.
// radius == nullptr keeps its own value in ImGui storage
inline bool FovCirclePreview(const char* id, float* radius, const FovPreviewStyle& st = FovPreviewStyle(),
                             const ImVec2& size = ImVec2(0, 0)) {
    namespace d = detail;
    float fs = ImGui::GetFontSize();
    float avail_w = ImGui::GetContentRegionAvail().x;
    float def_w = d::Clamp(avail_w, fs * 12.0f, fs * 22.0f);
    ImVec2 sz = d::ResolveSize(size, def_w, 0.0f);
    if (sz.y <= 0.0f) sz.y = floorf(sz.x * 9.0f / 16.0f);
    ImVec2 mn = ImGui::GetCursorScreenPos(), mx(mn.x + sz.x, mn.y + sz.y);
    ImGui::PushID(id);
    ImGuiStorage* storage = ImGui::GetStateStorage();
    ImGuiID local_key = ImGui::GetID("##local");
    float local = storage->GetFloat(local_key, 100.0f);
    float* target = radius ? radius : &local;
    bool editable = st.editable;
    if (editable) ImGui::InvisibleButton("##fov", sz);
    else ImGui::Dummy(sz);
    bool hovered = ImGui::IsItemHovered();
    bool active = editable && ImGui::IsItemActive();
    ImGuiID grab_key = ImGui::GetID("##grab");
    ImGuiID anim_id = ImGui::GetID("##r");
    ImGuiID hot_id = ImGui::GetID("##hot");
    ImGui::PopID();

    float ref = st.reference_height > 1.0f ? st.reference_height : 1080.0f;
    float scale = sz.y / ref;
    ImVec2 c((mn.x + mx.x) * 0.5f, (mn.y + mx.y) * 0.5f);
    *target = d::Fin(*target);
    float value = *target;
    float lo = d::ed::Min(st.min_radius, st.max_radius), hi = d::ed::Max(st.min_radius, st.max_radius);
    ImVec2 m = ImGui::GetMousePos();
    float dist = sqrtf((m.x - c.x) * (m.x - c.x) + (m.y - c.y) * (m.y - c.y));
    float grab_px = 6.0f * d::Dpi();
    bool changed = false;
    if (editable) {
        if (ImGui::IsItemActivated()) storage->SetBool(grab_key, fabsf(dist - value * scale) <= grab_px * 1.5f);
        if (active && storage->GetBool(grab_key, false)) {
            float nv = d::Clamp(dist / (scale > 0.0f ? scale : 1.0f), lo, hi);
            if (nv != *target) {
                *target = nv;
                changed = true;
            }
            value = nv;
        }
        if (!active) storage->SetBool(grab_key, false);
    }
    bool near_ring = hovered && fabsf(dist - value * scale) <= grab_px;
    bool grabbing = active && storage->GetBool(grab_key, false);
    if (editable && (near_ring || grabbing)) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    float rnd = d::ed::CardRounding();
    d::MockViewport(dl, mn, mx, rnd);
    dl->PushClipRect(mn, mx, true);
    float rr = d::ed::AnimTo(anim_id, value * scale, grabbing ? 0.0f : 0.12f);
    float hot = d::ed::Anim(hot_id, near_ring || grabbing, 0.12f);
    ImU32 base = ImGui::GetColorU32(st.color);
    ImU32 ring = d::ed::LerpColor(base, d::ed::Accent(), hot);
    float th = (1.5f + 1.0f * hot) * d::Dpi();
    if (st.filled) dl->AddCircleFilled(c, rr, d::ed::WithAlpha(ring, 0.08f + 0.06f * hot), 64);
    dl->AddCircle(c, rr + 1.0f, d::Live(IM_COL32(0, 0, 0, 110)), 64, th + 1.5f);
    dl->AddCircle(c, rr, ring, 64, th);
    float a0 = d::Time() * 0.9f;
    dl->PathClear();
    dl->PathArcTo(c, rr, a0, a0 + 0.7f, 20);
    d::Stroke(dl, d::ed::WithAlpha(ring, 0.9f), th + 1.0f, false);
    if (st.crosshair) {
        float k = 4.0f * d::Dpi();
        ImU32 cc = d::Live(IM_COL32(255, 255, 255, 200));
        dl->AddLine(ImVec2(c.x - k, c.y), ImVec2(c.x + k + 1.0f, c.y), cc);
        dl->AddLine(ImVec2(c.x, c.y - k), ImVec2(c.x, c.y + k + 1.0f), cc);
    }
    if (st.show_value) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.0f px", value);
        float tfs = fs * 0.8f;
        ImVec2 ts = d::TextSize(tfs, buf);
        float ang = 0.785f;
        ImVec2 p(c.x + cosf(ang) * rr + 4.0f * d::Dpi(), c.y + sinf(ang) * rr);
        if (p.x + ts.x > mx.x - 4.0f) p.x = mx.x - 4.0f - ts.x;
        if (p.y + ts.y > mx.y - 4.0f) p.y = mx.y - 4.0f - ts.y;
        d::TextShadow(dl, tfs, p, ring, buf);
    }
    dl->PopClipRect();
    if (!radius) storage->SetFloat(local_key, local);
    return changed;
}

// flat arguments for generated code
inline bool FovCirclePreview(const char* id, float* radius, float reference_height, ImU32 color, bool filled,
                             bool editable, const ImVec2& size) {
    FovPreviewStyle st;
    if (reference_height > 1.0f) st.reference_height = reference_height;
    if (color) st.color = ImGui::ColorConvertU32ToFloat4(color);
    st.filled = filled;
    st.editable = editable;
    return FovCirclePreview(id, radius, st, size);
}

}  // namespace game
}  // namespace imb
