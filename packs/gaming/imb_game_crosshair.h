#pragma once
// Crosshair style, DrawCrosshair, CrosshairPreview, CrosshairEditor
#include "imb_game_core.h"
#include "imb_ess_controls.h"
#include <cmath>

namespace imb {
namespace game {

// sizes in pixels at 100% scale
struct Crosshair {
    float gap = 4.0f;
    float length = 7.0f;
    float thickness = 2.0f;
    bool dot = false;
    float dot_size = 2.0f;
    bool outline = true;
    float outline_thickness = 1.0f;
    bool t_style = false;
    bool dynamic = false;
    ImVec4 color = ImVec4(0.3f, 1.0f, 0.45f, 1.0f);
    ImVec4 outline_color = ImVec4(0.0f, 0.0f, 0.0f, 0.85f);
};

namespace detail {

inline float Snap(float v) { return floorf(Clamp(Fin(v), -4096.0f, 4096.0f) + 0.5f); }

inline void CrossRect(ImDrawList* dl, float x0, float y0, float x1, float y1, float grow, ImU32 col) {
    dl->AddRectFilled(ImVec2(x0 - grow, y0 - grow), ImVec2(x1 + grow, y1 + grow), col);
}

inline void CrossPass(ImDrawList* dl, const ImVec2& c, const Crosshair& ch, float th, float gap, float len,
                      float dot, float grow, ImU32 col) {
    float a0 = -floorf(th * 0.5f), a1 = a0 + th;
    if (len > 0.0f) {
        CrossRect(dl, c.x + gap, c.y + a0, c.x + gap + len, c.y + a1, grow, col);
        CrossRect(dl, c.x - gap - len, c.y + a0, c.x - gap, c.y + a1, grow, col);
        CrossRect(dl, c.x + a0, c.y + gap, c.x + a1, c.y + gap + len, grow, col);
        if (!ch.t_style) CrossRect(dl, c.x + a0, c.y - gap - len, c.x + a1, c.y - gap, grow, col);
    }
    if (ch.dot) {
        float d0 = -floorf(dot * 0.5f);
        CrossRect(dl, c.x + d0, c.y + d0, c.x + d0 + dot, c.y + d0 + dot, grow, col);
    }
}

// periodic kick, like the spread of a weapon firing a short burst
inline float SpreadKick(float t) {
    float p = fmodf(t, 1.4f);
    if (p > 0.6f) return 0.0f;
    float shot = fmodf(p, 0.15f);
    return expf(-shot * 18.0f) * (1.0f - p / 0.6f);
}

}  // namespace detail

// draws the crosshair pixel snapped around `center`. scale multiplies every size
inline void DrawCrosshair(ImDrawList* dl, const ImVec2& center, const Crosshair& ch, float scale = 1.0f,
                          float extra_gap = 0.0f, float alpha = 1.0f) {
    namespace d = detail;
    float s = scale > 0.0f ? scale : 1.0f;
    float th = d::ed::Max(1.0f, d::Snap(ch.thickness * s));
    float gap = d::Snap((ch.gap + extra_gap) * s);
    float len = d::Snap(ch.length * s);
    float dot = d::ed::Max(1.0f, d::Snap(ch.dot_size * s));
    ImVec2 c(floorf(center.x), floorf(center.y));
    if (ch.outline) {
        float o = d::ed::Max(1.0f, d::Snap(ch.outline_thickness * s));
        ImVec4 oc = ch.outline_color;
        oc.w *= alpha;
        d::CrossPass(dl, c, ch, th, gap, len, dot, o, ImGui::GetColorU32(oc));
    }
    ImVec4 fc = ch.color;
    fc.w *= alpha;
    d::CrossPass(dl, c, ch, th, gap, len, dot, 0.0f, ImGui::GetColorU32(fc));
}

// click cycles the backdrop (scene, bright sky, checker) to check contrast. zoom 0 picks one
inline bool CrosshairPreview(const char* id, const Crosshair* ch, const ImVec2& size = ImVec2(0, 0),
                             float zoom = 0.0f) {
    static const Crosshair kDefault = Crosshair();
    const Crosshair& c = ch ? *ch : kDefault;
    namespace d = detail;
    float fs = ImGui::GetFontSize();
    ImVec2 sz = d::ResolveSize(size, fs * 8.0f, fs * 8.0f);
    ImVec2 mn = ImGui::GetCursorScreenPos(), mx(mn.x + sz.x, mn.y + sz.y);
    ImGui::PushID(id);
    bool clicked = ImGui::InvisibleButton("##ch", sz);
    bool hovered = ImGui::IsItemHovered();
    ImGuiStorage* storage = ImGui::GetStateStorage();
    ImGuiID bg_key = ImGui::GetID("##bg");
    ImGuiID hover_id = ImGui::GetID("##hv");
    ImGui::PopID();
    int bg = storage->GetInt(bg_key, 0);
    if (clicked) {
        bg = (bg + 1) % 3;
        storage->SetInt(bg_key, bg);
    }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float r = d::ed::CardRounding();
    if (bg == 0) {
        d::MockViewport(dl, mn, mx, r);
    } else if (bg == 1) {
        d::ed::Gradient(dl, mn, mx, d::Live(IM_COL32(170, 205, 240, 255)), d::Live(IM_COL32(236, 228, 210, 255)),
                        true, r);
    } else {
        dl->AddRectFilled(mn, mx, d::Live(IM_COL32(60, 60, 64, 255)), r);
        float cell = d::ed::Max(4.0f, floorf(fs * 0.6f));
        cell = d::ed::Max(cell, sqrtf(sz.x * sz.y / 4096.0f));
        dl->PushClipRect(mn, mx, true);
        int nx = (int)(sz.x / cell) + 1, ny = (int)(sz.y / cell) + 1;
        ImU32 light = d::Live(IM_COL32(200, 200, 205, 255));
        for (int y = 0; y < ny; ++y)
            for (int x = (y & 1); x < nx; x += 2)
                dl->AddRectFilled(ImVec2(mn.x + (float)x * cell, mn.y + (float)y * cell),
                                  ImVec2(mn.x + (float)(x + 1) * cell, mn.y + (float)(y + 1) * cell), light);
        dl->PopClipRect();
        d::ed::Outline(dl, mn, mx, d::ed::BorderCol(), r, 1.0f);
    }
    float z = zoom;
    if (z <= 0.0f) {
        float extent = d::ed::Max(c.gap + c.length, c.dot_size) + (c.outline ? c.outline_thickness : 0.0f) + 1.0f;
        float fit = d::ed::Min(sz.x, sz.y) * 0.38f / (extent > 1.0f ? extent : 1.0f);
        z = d::Clamp(floorf(fit), 1.0f, 4.0f);
    }
    float kick = c.dynamic ? d::SpreadKick(d::Time()) * 5.0f : 0.0f;
    ImVec2 ctr((mn.x + mx.x) * 0.5f, (mn.y + mx.y) * 0.5f);
    dl->PushClipRect(mn, mx, true);
    DrawCrosshair(dl, ctr, c, z, kick);
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%.0fx", z);
    float tfs = fs * 0.75f;
    ImVec2 ts = d::TextSize(tfs, buf);
    float h = d::ed::Anim(hover_id, hovered, 0.15f);
    d::TextShadow(dl, tfs, ImVec2(mx.x - ts.x - 5.0f, mx.y - ts.y - 3.0f), d::Live(IM_COL32_WHITE, 0.55f + 0.45f * h),
                  buf);
    dl->PopClipRect();
    return clicked;
}

namespace detail {

inline bool CrossSlider(const char* label, const char* hook, float* v, float lo, float hi, const char* fmt) {
    bool c = ImGui::SliderFloat(label, v, lo, hi, fmt);
    IMB_GAME_ITEM_HOOK(hook);
    (void)hook;
    return c;
}

inline void ApplyCrossPreset(Crosshair& c, int which) {
    ImVec4 col = c.color, ocol = c.outline_color;
    Crosshair p;
    if (which == 1) {
        p.length = 0.0f;
        p.dot = true;
        p.dot_size = 4.0f;
    } else if (which == 2) {
        p.gap = 1.0f;
        p.length = 3.0f;
        p.thickness = 1.0f;
    } else if (which == 3) {
        p.t_style = true;
        p.gap = 3.0f;
        p.length = 8.0f;
    }
    c = p;
    c.color = col;
    c.outline_color = ocol;
}

}  // namespace detail

// preview on the left (or on top when narrow), sliders, toggles, colors and a few presets
inline bool CrosshairEditor(const char* label, Crosshair* ch, float preview_size = 0.0f) {
    if (!ch) return false;
    namespace d = detail;
    const ImGuiStyle& st = ImGui::GetStyle();
    float fs = ImGui::GetFontSize();
    const char* end = d::ed::LabelEnd(label);
    bool changed = false;
    ImGui::PushID(label);
    ImGui::BeginGroup();
    if (end > label) {
        ImGui::TextUnformatted(label, end);
    }
    float ps = preview_size > 0.0f ? preview_size : floorf(fs * 8.5f);
    float avail = ImGui::GetContentRegionAvail().x;
    bool side = avail >= ps + fs * 15.0f;
    CrosshairPreview("##preview", ch, ImVec2(ps, ps));
    IMB_GAME_ITEM_HOOK("crosshair.preview");
    if (side) ImGui::SameLine(0.0f, st.ItemSpacing.x * 1.5f);
    ImGui::BeginGroup();
    float label_w = ImGui::CalcTextSize("Thickness").x + st.ItemInnerSpacing.x;
    ImGui::PushItemWidth(-label_w);
    changed |= d::CrossSlider("Gap", "crosshair.gap", &ch->gap, -4.0f, 20.0f, "%.1f");
    changed |= d::CrossSlider("Length", "crosshair.length", &ch->length, 0.0f, 30.0f, "%.1f");
    changed |= d::CrossSlider("Thickness", "crosshair.thickness", &ch->thickness, 0.5f, 10.0f, "%.1f");
    if (ch->outline)
        changed |= d::CrossSlider("Outline##px", "crosshair.outline_thickness", &ch->outline_thickness, 0.5f, 4.0f, "%.1f");
    if (ch->dot) changed |= d::CrossSlider("Dot size", "crosshair.dot_size", &ch->dot_size, 1.0f, 10.0f, "%.1f");
    ImGui::PopItemWidth();
    changed |= ess::Toggle("Dot", &ch->dot);
    IMB_GAME_ITEM_HOOK("crosshair.dot");
    ImGui::SameLine(0.0f, st.ItemSpacing.x * 2.0f);
    changed |= ess::Toggle("Outline", &ch->outline);
    IMB_GAME_ITEM_HOOK("crosshair.outline");
    ImGui::SameLine(0.0f, st.ItemSpacing.x * 2.0f);
    changed |= ess::Toggle("T", &ch->t_style);
    IMB_GAME_ITEM_HOOK("crosshair.t_style");
    ImGui::SameLine(0.0f, st.ItemSpacing.x * 2.0f);
    changed |= ess::Toggle("Spread", &ch->dynamic);
    IMB_GAME_ITEM_HOOK("crosshair.dynamic");
    ImGuiColorEditFlags cf = ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar |
                             ImGuiColorEditFlags_AlphaPreviewHalf;
    changed |= ImGui::ColorEdit4("Color", &ch->color.x, cf);
    IMB_GAME_ITEM_HOOK("crosshair.color");
    ImGui::SameLine(0.0f, st.ItemSpacing.x * 2.0f);
    changed |= ImGui::ColorEdit4("Outline##oc", &ch->outline_color.x, cf);
    static const char* const kPresets[] = {"Classic", "Dot", "Tiny", "T"};
    for (int i = 0; i < 4; ++i) {
        if (i > 0) ImGui::SameLine(0.0f, st.ItemInnerSpacing.x);
        ImGui::PushID(i);
        if (ImGui::SmallButton(kPresets[i])) {
            d::ApplyCrossPreset(*ch, i);
            changed = true;
        }
        IMB_GAME_ITEM_HOOK(kPresets[i]);
        ImGui::PopID();
    }
    ImGui::EndGroup();
    ImGui::EndGroup();
    ImGui::PopID();
    return changed;
}

namespace detail {

inline Crosshair CrosshairFrom(const float* gap, const float* length, const float* thickness, const bool* dot,
                               const bool* outline, const bool* t_style, const ImVec4* color) {
    Crosshair c;
    if (gap) c.gap = *gap;
    if (length) c.length = *length;
    if (thickness) c.thickness = *thickness;
    if (dot) c.dot = *dot;
    if (outline) c.outline = *outline;
    if (t_style) c.t_style = *t_style;
    if (color) c.color = *color;
    return c;
}

}  // namespace detail

// one pointer per field, the way generated code binds them. null fields fall back to defaults
inline bool CrosshairPreviewFields(const char* id, const float* gap, const float* length, const float* thickness,
                                   const bool* dot, const bool* outline, const bool* t_style, const ImVec4* color,
                                   const ImVec2& size = ImVec2(0, 0)) {
    Crosshair c = detail::CrosshairFrom(gap, length, thickness, dot, outline, t_style, color);
    return CrosshairPreview(id, &c, size);
}

// same for the editor. null fields (and the ones without a pointer here, like dot size) keep
// their value in the window's ImGui storage
inline bool CrosshairEditorFields(const char* label, float* gap, float* length, float* thickness, bool* dot,
                                  bool* outline, bool* t_style, ImVec4* color) {
    namespace d = detail;
    ImGui::PushID(label);
    ImGuiStorage* s = ImGui::GetStateStorage();
    ImGuiID k = ImGui::GetID("##fields");
    ImGui::PopID();
    Crosshair c;
    c.gap = gap ? *gap : s->GetFloat(k, c.gap);
    c.length = length ? *length : s->GetFloat(k + 1, c.length);
    c.thickness = thickness ? *thickness : s->GetFloat(k + 2, c.thickness);
    c.dot = dot ? *dot : s->GetBool(k + 3, c.dot);
    c.outline = outline ? *outline : s->GetBool(k + 4, c.outline);
    c.t_style = t_style ? *t_style : s->GetBool(k + 5, c.t_style);
    if (color) c.color = *color;
    else d::LoadVec4(s, k + 6, c.color);
    c.dot_size = s->GetFloat(k + 10, c.dot_size);
    c.outline_thickness = s->GetFloat(k + 11, c.outline_thickness);
    c.dynamic = s->GetBool(k + 12, c.dynamic);
    d::LoadVec4(s, k + 13, c.outline_color);
    bool changed = CrosshairEditor(label, &c);
    if (gap) *gap = c.gap;
    else s->SetFloat(k, c.gap);
    if (length) *length = c.length;
    else s->SetFloat(k + 1, c.length);
    if (thickness) *thickness = c.thickness;
    else s->SetFloat(k + 2, c.thickness);
    if (dot) *dot = c.dot;
    else s->SetBool(k + 3, c.dot);
    if (outline) *outline = c.outline;
    else s->SetBool(k + 4, c.outline);
    if (t_style) *t_style = c.t_style;
    else s->SetBool(k + 5, c.t_style);
    if (color) *color = c.color;
    else d::StoreVec4(s, k + 6, c.color);
    s->SetFloat(k + 10, c.dot_size);
    s->SetFloat(k + 11, c.outline_thickness);
    s->SetBool(k + 12, c.dynamic);
    d::StoreVec4(s, k + 13, c.outline_color);
    return changed;
}

}  // namespace game
}  // namespace imb
