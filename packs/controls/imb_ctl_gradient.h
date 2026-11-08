#pragma once
// GradientEditor and GradientSample
#include <cstddef>
#include "imb_ctl_core.h"
#include <vector>

namespace imb {
namespace ctl {

struct GradientStop {
    float pos;
    ImVec4 color;
};

struct GradientStyle {
    float width = 0.0f;
    float height = 0.0f;
    bool allow_add = true;
    bool allow_remove = true;
    bool show_controls = true;
};

const int kMaxGradientStops = 32;

// stops sorted by pos
inline ImVec4 GradientSample(const GradientStop* stops, int count, float t) {
    if (!stops || count <= 0) return ImVec4(0, 0, 0, 0);
    if (t <= stops[0].pos) return stops[0].color;
    for (int i = 1; i < count; ++i) {
        if (t <= stops[i].pos) {
            float span = stops[i].pos - stops[i - 1].pos;
            float k = span > 0.0f ? (t - stops[i - 1].pos) / span : 1.0f;
            const ImVec4 &a = stops[i - 1].color, &b = stops[i].color;
            return ImVec4(a.x + (b.x - a.x) * k, a.y + (b.y - a.y) * k, a.z + (b.z - a.z) * k, a.w + (b.w - a.w) * k);
        }
    }
    return stops[count - 1].color;
}

namespace detail {

// insertion sort that keeps *track pointing at the same stop
inline void SortStops(GradientStop* s, int n, int* track) {
    for (int i = 1; i < n; ++i) {
        GradientStop v = s[i];
        bool moving = track && *track == i;
        int j = i - 1;
        while (j >= 0 && s[j].pos > v.pos) {
            s[j + 1] = s[j];
            if (track && *track == j) *track = j + 1;
            --j;
        }
        if (moving) *track = j + 1;
        s[j + 1] = v;
    }
}

inline void StopHandle(ImDrawList* dl, float x, float y, float hs, const ImVec4& col, bool selected, float hot,
                       bool removing) {
    float dpi = Dpi();
    float k = 1.0f + 0.15f * hot + (selected ? 0.1f : 0.0f);
    float w = hs * k, top = y;
    float alpha = removing ? 0.35f : 1.0f;
    ImU32 frame = Final(selected ? AccentRaw() : Solid(ImGuiCol_FrameBg), alpha);
    ImU32 edge = Final(selected ? TextRaw() : BorderRaw(), alpha);
    dl->AddTriangleFilled(ImVec2(x, top), ImVec2(x - w * 0.45f, top + w * 0.5f), ImVec2(x + w * 0.45f, top + w * 0.5f), frame);
    ImVec2 a(x - w * 0.5f, top + w * 0.45f), b(x + w * 0.5f, top + w * 1.45f);
    Shadow(dl, a, b, 3.0f * dpi, 0.7f * alpha);
    dl->AddRectFilled(a, b, frame, 3.0f * dpi);
    float in = Max(2.0f, 2.0f * dpi);
    ImVec2 ia(a.x + in, a.y + in), ib(b.x - in, b.y - in);
    if (col.w < 1.0f) Checker(dl, ia, ib, (ib.x - ia.x) * 0.5f);
    dl->AddRectFilled(ia, ib, ImGui::GetColorU32(ImVec4(col.x, col.y, col.z, col.w * alpha)), 2.0f * dpi);
    Rect(dl, a, b, edge, 3.0f * dpi, selected ? Max(1.5f, 1.5f * dpi) : 1.0f);
    if (removing) Cross(dl, Mid(a, b), w * 0.25f, Final(TextRaw()), Max(1.5f, 1.5f * dpi));
}

}  // namespace detail

// click under the bar to add a stop, drag handles to move them, drag a handle down off the
// widget (or right click it, or press Delete) to remove it. at least two stops always stay
inline bool GradientEditor(const char* label, GradientStop* stops, int* count, int capacity,
                           const GradientStyle& gs = GradientStyle()) {
    if (!stops || !count || capacity <= 0) return false;
    if (capacity > kMaxGradientStops) capacity = kMaxGradientStops;
    int n = detail::ClampI(*count, 0, capacity);
    bool changed = false;
    if (n == 0) {
        stops[0].pos = 0.0f;
        stops[0].color = ImVec4(0, 0, 0, 1);
        n = 1;
        changed = true;
    }
    if (n == 1 && capacity > 1) {
        stops[1].pos = 1.0f;
        stops[1].color = ImVec4(1, 1, 1, 1);
        n = 2;
        changed = true;
    }
    const ImGuiStyle& st = ImGui::GetStyle();
    float dpi = detail::Dpi();
    float fh = ImGui::GetFrameHeight();
    float w = gs.width > 0.0f ? gs.width : ImGui::CalcItemWidth();
    float bar_h = gs.height > 0.0f ? gs.height : fh;
    float hs = detail::Max(10.0f * dpi, fh * 0.6f);
    float handle_h = hs * 1.6f;
    const char* lend = detail::LabelEnd(label);
    ImGuiID id = ImGui::GetID(label);
    ImGuiStorage* store = ImGui::GetStateStorage();
    ImGuiID sel_key = detail::Sub(id, 1), rem_key = detail::Sub(id, 2);
    int sel = detail::ClampI(store->GetInt(sel_key, 0), 0, n - 1);
    detail::SortStops(stops, n, &sel);

    ImGui::PushID(label);
    ImGui::BeginGroup();
    ImGui::BeginGroup();
    ImVec2 pos = ImGui::GetCursorScreenPos();
    float inset = hs * 0.5f;
    float x0 = pos.x + inset, x1 = pos.x + w - inset;
    ImGui::InvisibleButton("##bar", ImVec2(w, bar_h), detail::kNav);
    bool bar_hovered = ImGui::IsItemHovered();
    bool open_picker = bar_hovered && ImGui::IsMouseDoubleClicked(0);
    if (ImGui::IsItemActivated() && ImGui::IsMouseDown(0)) {
        float mt = detail::Sat((ImGui::GetMousePos().x - x0) / detail::Max(x1 - x0, 1.0f));
        float best = FLT_MAX;
        for (int i = 0; i < n; ++i)
            if (detail::Abs(stops[i].pos - mt) < best) {
                best = detail::Abs(stops[i].pos - mt);
                sel = i;
            }
    }
    ImGui::InvisibleButton("##handles", ImVec2(w, handle_h), detail::kNav);
    bool h_hovered = ImGui::IsItemHovered(), h_active = ImGui::IsItemActive();
    float hy = pos.y + bar_h;
    float mt = detail::Sat((ImGui::GetMousePos().x - x0) / detail::Max(x1 - x0, 1.0f));
    int under = -1;
    if (h_hovered || h_active) {
        float mx = ImGui::GetMousePos().x;
        float best = hs * 0.6f;
        for (int i = 0; i < n; ++i) {
            float dx = detail::Abs(detail::Lerp(x0, x1, stops[i].pos) - mx);
            if (dx <= best) {
                best = dx;
                under = i;
            }
        }
    }
    bool removing = store->GetBool(rem_key, false);
    ImGuiID grab_key = detail::Sub(id, 3);
    if (ImGui::IsItemActivated() && ImGui::IsMouseDown(0)) {
        removing = false;
        bool grabbed = true;
        if (under >= 0) {
            sel = under;
        } else if (gs.allow_add && n < capacity) {
            stops[n].pos = mt;
            stops[n].color = GradientSample(stops, n, mt);
            sel = n++;
            detail::SortStops(stops, n, &sel);
            changed = true;
        } else {
            grabbed = false;
        }
        store->SetBool(grab_key, grabbed);
    }
    if (h_active && ImGui::IsMouseDown(0) && store->GetBool(grab_key, false)) {
        if (stops[sel].pos != mt) {
            stops[sel].pos = mt;
            changed = true;
        }
        detail::SortStops(stops, n, &sel);
        removing = gs.allow_remove && n > 2 && ImGui::GetMousePos().y > hy + handle_h * 2.5f;
    }
    int remove_at = -1;
    if (ImGui::IsItemDeactivated() && removing) remove_at = sel;
    if (!h_active) removing = false;
    if (h_hovered && under >= 0 && ImGui::IsMouseClicked(1)) remove_at = under;
    if (h_hovered && under >= 0 && ImGui::IsMouseDoubleClicked(0)) open_picker = true;
    if (ImGui::IsItemFocused()) {
        if (detail::KeyPressed(ImGuiKey_Delete, false)) remove_at = sel;
        int d = detail::FocusedArrows(true, false);
        if (d) {
            stops[sel].pos = detail::Sat(stops[sel].pos + 0.01f * (float)d);
            detail::SortStops(stops, n, &sel);
            changed = true;
        }
    }
    if (remove_at >= 0 && gs.allow_remove && n > 2) {
        for (int i = remove_at; i + 1 < n; ++i) stops[i] = stops[i + 1];
        --n;
        sel = detail::ClampI(remove_at > 0 ? remove_at - 1 : 0, 0, n - 1);
        removing = false;
        changed = true;
    }
    store->SetBool(rem_key, removing);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 ba(x0, pos.y), bb(x1, pos.y + bar_h);
    bool alpha = false;
    for (int i = 0; i < n; ++i) alpha |= stops[i].color.w < 1.0f;
    float r = alpha ? 0.0f : detail::Rounding(bar_h);
    if (alpha) {
        dl->PushClipRect(ba, bb, true);
        detail::Checker(dl, ba, bb, bar_h * 0.25f);
        dl->PopClipRect();
    }
    const GradientStop* cs = stops;
    int cn = n;
    detail::GradientBar(dl, ba, bb, r, false,
                        [cs, cn](float t) { return ImGui::ColorConvertFloat4ToU32(GradientSample(cs, cn, t)); }, 64);
    detail::Rect(dl, ba, bb, detail::Final(detail::BorderRaw()), r, 1.0f);
    for (int pass = 0; pass < 2; ++pass)
        for (int i = 0; i < n; ++i) {
            if ((i == sel) != (pass == 1)) continue;
            float x = detail::Lerp(x0, x1, stops[i].pos);
            float hot = detail::AnimTo(detail::Sub(id, 200 + i), i == under ? 1.0f : 0.0f);
            float yoff = (i == sel && removing) ? handle_h * 0.4f : 0.0f;
            detail::StopHandle(dl, x, hy + 1.0f + yoff, hs, stops[i].color, i == sel, hot, i == sel && removing);
        }
    if (open_picker) ImGui::OpenPopup("##stop_picker");
    if (ImGui::BeginPopup("##stop_picker")) {
        if (ImGui::ColorPicker4("##pick", &stops[sel].color.x, ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf))
            changed = true;
        ImGui::EndPopup();
    }
    if (gs.show_controls) {
        if (ImGui::ColorEdit4("##stop_color", &stops[sel].color.x,
                              ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf))
            changed = true;
        ImGui::SameLine(0.0f, st.ItemInnerSpacing.x);
        float pct = stops[sel].pos * 100.0f;
        ImGui::SetNextItemWidth(detail::Max(fh * 3.0f, w * 0.3f));
        if (ImGui::DragFloat("##stop_pos", &pct, 0.5f, 0.0f, 100.0f, "%.0f%%")) {
            stops[sel].pos = detail::Sat(pct / 100.0f);
            detail::SortStops(stops, n, &sel);
            changed = true;
        }
        ImGui::SameLine(0.0f, st.ItemInnerSpacing.x);
        ImGui::BeginDisabled(!gs.allow_remove || n <= 2);
        if (ImGui::SmallButton("Remove")) {
            for (int i = sel; i + 1 < n; ++i) stops[i] = stops[i + 1];
            --n;
            sel = detail::ClampI(sel - 1, 0, n - 1);
            changed = true;
        }
        ImGui::EndDisabled();
    }
    ImGui::EndGroup();
    detail::TrailingLabel(label, lend, bar_h);
    ImGui::EndGroup();
    ImGui::PopID();
    store->SetInt(sel_key, sel);
    *count = n;
    return changed;
}

template <int N>
inline bool GradientEditor(const char* label, GradientStop (&stops)[N], int* count,
                           const GradientStyle& gs = GradientStyle()) {
    return GradientEditor(label, stops, count, N, gs);
}

inline bool GradientEditor(const char* label, std::vector<GradientStop>* stops, const GradientStyle& gs = GradientStyle()) {
    if (!stops) return false;
    GradientStop local[kMaxGradientStops];
    int n = (int)stops->size() < kMaxGradientStops ? (int)stops->size() : kMaxGradientStops;
    for (int i = 0; i < n; ++i) local[i] = (*stops)[(size_t)i];
    if (!GradientEditor(label, local, &n, kMaxGradientStops, gs)) return false;
    stops->assign(local, local + n);
    return true;
}

// float array of [pos, r, g, b, a] per stop; slots with pos < 0 are unused. this is the form
// bindings use (float[N] or std::vector<float>)
inline bool GradientEditorPacked(const char* label, float* packed, int float_count,
                                 const GradientStyle& gs = GradientStyle()) {
    if (!packed || float_count < 10) return false;
    int cap = float_count / 5;
    if (cap > kMaxGradientStops) cap = kMaxGradientStops;
    GradientStop local[kMaxGradientStops];
    int n = 0;
    for (int i = 0; i < cap; ++i) {
        const float* p = packed + i * 5;
        if (p[0] < 0.0f) continue;
        local[n].pos = detail::Sat(p[0]);
        local[n].color = ImVec4(p[1], p[2], p[3], p[4]);
        ++n;
    }
    if (!GradientEditor(label, local, &n, cap, gs)) return false;
    for (int i = 0; i < cap; ++i) {
        float* p = packed + i * 5;
        if (i < n) {
            p[0] = local[i].pos;
            p[1] = local[i].color.x;
            p[2] = local[i].color.y;
            p[3] = local[i].color.z;
            p[4] = local[i].color.w;
        } else {
            p[0] = -1.0f;
            p[1] = p[2] = p[3] = p[4] = 0.0f;
        }
    }
    return true;
}

inline bool GradientEditorPacked(const char*, std::nullptr_t, std::nullptr_t, const GradientStyle& = GradientStyle()) {
    return false;
}

}  // namespace ctl
}  // namespace imb
