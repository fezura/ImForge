#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>

#include "imgui.h"

#include "imforge/imb_motion.h"
#include "imforge/imforge_motion_json.h"

#include "../builder_style.h"
#include "../editor_context.h"
#include "../ui/ui_helpers.h"
#include "anim_keyframe_editor.h"

#ifdef ICON_FA_PLUS
#define KF_ICON_ADD ICON_FA_PLUS
#define KF_ICON_REMOVE ICON_FA_XMARK
#else
#define KF_ICON_ADD "+"
#define KF_ICON_REMOVE "x"
#endif

namespace imforge::builder::kfui {

namespace {

ImVec4 TrackColor(int prop_index) {
    static const ImVec4 kPalette[] = {
        ImVec4(0.42f, 0.67f, 1.00f, 1.0f), ImVec4(0.98f, 0.62f, 0.32f, 1.0f), ImVec4(0.45f, 0.85f, 0.55f, 1.0f),
        ImVec4(0.86f, 0.52f, 0.95f, 1.0f), ImVec4(0.95f, 0.80f, 0.35f, 1.0f), ImVec4(0.40f, 0.85f, 0.88f, 1.0f),
        ImVec4(0.96f, 0.47f, 0.58f, 1.0f),
    };
    return kPalette[(prop_index < 0 ? 0 : prop_index) % 7];
}

const int kSegmentSamples = 24;

// one number per key for the curve: the value, or the average channel for tint
float Scalar(const kf::PropInfo* info, const kf::Key& k) {
    if (info && info->color) return (k.v[0] + k.v[1] + k.v[2] + k.v[3]) * 0.25f;
    return k.v[0];
}

int PropIndex(const std::string& key) {
    for (int i = 0; i < kf::PropCount(); ++i)
        if (key == kf::PropAt(i).key) return i;
    return -1;
}

void FormatValue(const kf::TrackEdit& tr, const kf::Key& k, char* buf, size_t n) {
    const kf::PropInfo* p = kf::FindProp(tr.prop);
    if (p && p->color) {
        std::snprintf(buf, n, "rgba %.2f %.2f %.2f %.2f", (double)k.v[0], (double)k.v[1], (double)k.v[2], (double)k.v[3]);
        return;
    }
    std::snprintf(buf, n, p ? p->format : "%.2f", (double)k.v[0]);
}

void Ruler(ImDrawList* dl, float x0, float x1, float y0, float y1, float duration) {
    const BuilderColors& c = Colors();
    ImU32 line = ToU32(c.separator), text = ToU32(c.text_dim);
    dl->AddLine(ImVec2(x0, y1 - 1.0f), ImVec2(x1, y1 - 1.0f), line);
    for (int i = 0; i <= 20; ++i) {
        float x = x0 + (x1 - x0) * (float)i / 20.0f;
        bool major = i % 5 == 0;
        dl->AddLine(ImVec2(x, y1 - (major ? 7.0f : 3.0f)), ImVec2(x, y1 - 1.0f), line);
        if (!major) continue;
        char buf[24];
        std::snprintf(buf, sizeof(buf), "%.2fs", (double)(duration * (float)i / 20.0f));
        float w = ImGui::CalcTextSize(buf).x;
        float tx = i == 20 ? x - w - 2.0f : (i == 0 ? x + 2.0f : x - w * 0.5f);
        dl->AddText(ImVec2(tx, y0), text, buf);
    }
}

void Diamond(ImDrawList* dl, const ImVec2& c, float r, ImU32 fill, ImU32 border, bool pinned) {
    if (pinned) {
        dl->AddRectFilled(ImVec2(c.x - r * 0.75f, c.y - r * 0.75f), ImVec2(c.x + r * 0.75f, c.y + r * 0.75f), fill, 2.0f);
        dl->AddRect(ImVec2(c.x - r * 0.75f, c.y - r * 0.75f), ImVec2(c.x + r * 0.75f, c.y + r * 0.75f), border, 2.0f);
        return;
    }
    ImVec2 pts[4] = {ImVec2(c.x, c.y - r), ImVec2(c.x + r, c.y), ImVec2(c.x, c.y + r), ImVec2(c.x - r, c.y)};
    dl->AddConvexPolyFilled(pts, 4, fill);
    dl->AddPolyline(pts, 4, border, ImDrawFlags_Closed, 1.0f);
}

void AddTrackCombo(EditorContext& ctx, float width) {
    EditorState& s = State();
    ImGui::SetNextItemWidth(width);
    if (!ImGui::BeginCombo("##addtrack", KF_ICON_ADD " Add track", ImGuiComboFlags_HeightLarge)) return;
    for (int i = 0; i < kf::PropCount(); ++i) {
        const kf::PropInfo& p = kf::PropAt(i);
        bool used = false;
        for (const kf::TrackEdit& tr : s.edit.tracks) used = used || tr.prop == p.key;
        if (used) continue;
        if (ImGui::Selectable(p.label)) {
            s.edit.tracks.push_back(kf::MakeTrack(p.key));
            s.track = (int)s.edit.tracks.size() - 1;
            s.key = 0;
            Commit(ctx, "Add track");
        }
    }
    ImGui::EndCombo();
}

}  // namespace

void DrawTimeline(EditorContext& ctx, float height) {
    EditorState& s = State();
    const BuilderColors& c = Colors();
    const ImGuiStyle& st = ImGui::GetStyle();
    const float fs = ImGui::GetFontSize();
    if (!ImGui::BeginChild("##kf_timeline", ImVec2(0, height), ImGuiChildFlags_None)) {
        ImGui::EndChild();
        return;
    }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 origin = ImGui::GetCursorScreenPos();
    float avail = ImGui::GetContentRegionAvail().x;
    const float label_w = std::floor(fs * 7.5f);
    const float ruler_h = std::floor(fs * 1.35f);
    const float row_h = std::max(22.0f, std::floor(fs * 1.75f));
    const float pad = std::floor(fs * 0.6f);
    const float x0 = origin.x + label_w + pad, x1 = std::max(x0 + 60.0f, origin.x + avail - pad);
    const float span = x1 - x0;
    const float dur = s.edit.duration > 0 ? s.edit.duration : 1.0f;
    const bool free = ImGui::GetIO().KeyShift;
    const int track_count = (int)s.edit.tracks.size();

    Ruler(dl, x0, x1, origin.y, origin.y + ruler_h, dur);
    ImGui::SetCursorScreenPos(ImVec2(x0 - pad, origin.y));
    ImGui::InvisibleButton("##ruler", ImVec2(span + pad * 2.0f, ruler_h));
    if (ImGui::IsItemActive()) {
        float p = (ImGui::GetMousePos().x - x0) / span;
        s.progress = std::max(0.0f, std::min(1.0f, free ? p : kf::SnapTime(p, false)));
        s.time = s.edit.delay + s.progress * dur;
        s.playing = false;
    }
    ui::Tooltip("Drag to scrub (snaps to 0.05, hold Shift for free)");

    int delete_track = -1;
    for (int ti = 0; ti < track_count; ++ti) {
        kf::TrackEdit& tr = s.edit.tracks[(size_t)ti];
        const kf::PropInfo* info = kf::FindProp(tr.prop);
        ImVec4 tc = TrackColor(PropIndex(tr.prop));
        const float y0 = origin.y + ruler_h + row_h * (float)ti, y1 = y0 + row_h, ym = (y0 + y1) * 0.5f;
        bool track_sel = ti == s.track;
        ImGui::PushID(ti);
        if (track_sel) dl->AddRectFilled(ImVec2(origin.x, y0), ImVec2(origin.x + avail, y1), ToU32(c.selection, 0.18f));
        else if (ti % 2) dl->AddRectFilled(ImVec2(origin.x, y0), ImVec2(origin.x + avail, y1), ToU32(c.bg_hover, 0.25f));
        dl->AddLine(ImVec2(origin.x, y1), ImVec2(origin.x + avail, y1), ToU32(c.separator, 0.45f));

        // label cell
        const float bs = row_h - 6.0f;
        bool label_hov = ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(ImVec2(origin.x, y0), ImVec2(origin.x + label_w, y1));
        bool show_rm = label_hov || track_sel;
        ImGui::SetCursorScreenPos(ImVec2(origin.x, y0));
        ImGui::InvisibleButton("##label", ImVec2(show_rm ? label_w - bs - 4.0f : label_w, row_h));
        if (ImGui::IsItemClicked()) {
            s.track = ti;
            s.key = std::min(s.key, (int)tr.keys.size() - 1);
        }
        dl->AddRectFilled(ImVec2(origin.x + 4.0f, ym - 4.0f), ImVec2(origin.x + 8.0f, ym + 4.0f), ToU32(tc), 1.0f);
        dl->PushClipRect(ImVec2(origin.x, y0), ImVec2(origin.x + label_w - (show_rm ? bs + 4.0f : 0.0f), y1), true);
        dl->AddText(ImVec2(origin.x + 14.0f, ym - ImGui::GetTextLineHeight() * 0.5f), ToU32(track_sel ? c.text : c.text_dim),
                    info ? info->label : tr.prop.c_str());
        dl->PopClipRect();
        if (show_rm) {
            ImGui::SetCursorScreenPos(ImVec2(origin.x + label_w - bs - 2.0f, y0 + 3.0f));
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
            if (ImGui::Button(KF_ICON_REMOVE "##rmtrack", ImVec2(bs, bs))) delete_track = ti;
            ImGui::PopStyleVar();
            ImGui::PopStyleColor();
            ui::Tooltip("Remove this track");
        }

        // the value curve, scaled to the track's own range
        float vmin = FLT_MAX, vmax = -FLT_MAX;
        for (const kf::Key& k : tr.keys) {
            float v = Scalar(info, k);
            vmin = std::min(vmin, v);
            vmax = std::max(vmax, v);
        }
        const float cy0 = y0 + 5.0f, cy1 = y1 - 5.0f;
        auto value_y = [&](float v) {
            if (vmax - vmin < 1e-6f) return ym;
            float y = cy1 - (v - vmin) / (vmax - vmin) * (cy1 - cy0);
            return std::max(y0 + 1.0f, std::min(y1 - 1.0f, y));
        };
        for (size_t k = 0; k + 1 < tr.keys.size(); ++k) {
            const kf::Key& a = tr.keys[k];
            const kf::Key& b = tr.keys[k + 1];
            float sx0 = x0 + a.t * span, sx1 = x0 + b.t * span;
            imb::EaseSpec ease;
            if (!ParseEaseSpec(a.ease.empty() ? s.edit.ease : a.ease, ease)) ease = imb::EaseSpec();
            bool seg_sel = track_sel && (int)k == s.key;
            if (seg_sel) dl->AddRectFilled(ImVec2(sx0, y0 + 2.0f), ImVec2(sx1, y1 - 2.0f), ToU32(tc, 0.14f), 3.0f);
            ImVec2 pts[kSegmentSamples];
            float va = Scalar(info, a), vb = Scalar(info, b);
            for (int i = 0; i < kSegmentSamples; ++i) {
                float u = (float)i / (float)(kSegmentSamples - 1);
                pts[i] = ImVec2(sx0 + (sx1 - sx0) * u, value_y(va + (vb - va) * imb::SampleEase(ease, u)));
            }
            dl->AddPolyline(pts, kSegmentSamples, ToU32(tc, seg_sel ? 0.95f : 0.5f), ImDrawFlags_None, seg_sel ? 1.75f : 1.0f);
        }

        // keys first so they win hover over the row button below
        const float r = std::max(5.0f, std::floor(row_h * 0.3f));
        const int nkeys = (int)tr.keys.size();
        for (int k = 0; k < nkeys; ++k) {
            bool pinned = k == 0 || k == nkeys - 1;
            const float ky = value_y(Scalar(info, tr.keys[(size_t)k]));
            ImVec2 kc(x0 + tr.keys[(size_t)k].t * span, ky);
            ImGui::PushID(k);
            ImGui::SetCursorScreenPos(ImVec2(kc.x - r - 2.0f, ky - r - 2.0f));
            ImGui::InvisibleButton("##key", ImVec2(r * 2.0f + 4.0f, r * 2.0f + 4.0f));
            bool hov = ImGui::IsItemHovered(), act = ImGui::IsItemActive();
            if (ImGui::IsItemActivated()) {
                s.track = ti;
                s.key = k;
            }
            if (act && !pinned && ImGui::IsMouseDragging(0, 1.0f)) {
                float nt = (ImGui::GetMousePos().x - x0) / span;
                float before = tr.keys[(size_t)k].t;
                kf::RetimeKey(tr, k, nt, free);
                if (tr.keys[(size_t)k].t != before) Commit(ctx, "Move keyframe");
                s.dragging = true;
            }
            if (ImGui::IsItemDeactivated()) s.dragging = false;
            if ((hov || act) && !pinned) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
            if (hov && !act) {
                char val[64];
                FormatValue(tr, tr.keys[(size_t)k], val, sizeof(val));
                ImGui::SetTooltip("t %.3f (%.2f s)\n%s%s", (double)tr.keys[(size_t)k].t, (double)(tr.keys[(size_t)k].t * dur), val,
                                  pinned ? "\nfirst and last keys stay at 0 and 1" : "\ndrag to move, Shift for free, Del removes");
            }
            bool sel = track_sel && k == s.key;
            ImU32 fill = ToU32(sel ? c.accent : (hov ? c.text : tc));
            Diamond(dl, ImVec2(x0 + tr.keys[(size_t)k].t * span, ky), sel ? r + 1.0f : r, fill,
                    ToU32(sel ? ImVec4(1, 1, 1, 1) : c.bg_window), pinned);
            ImGui::PopID();
        }

        // row background: click selects the segment, double click adds a key
        ImGui::SetCursorScreenPos(ImVec2(x0 - pad, y0));
        ImGui::InvisibleButton("##row", ImVec2(span + pad * 2.0f, row_h));
        if (ImGui::IsItemClicked()) {
            float t = (ImGui::GetMousePos().x - x0) / span;
            s.track = ti;
            int seg = 0;
            for (int k = 0; k + 1 < nkeys; ++k)
                if (t >= tr.keys[(size_t)k].t) seg = k;
            s.key = seg;
        }
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
            float t = (ImGui::GetMousePos().x - x0) / span;
            t = kf::SnapTime(t, free);
            int idx = kf::InsertKey(tr, t, nullptr, s.edit.ease, SampleEaseString);
            if (idx >= 0) {
                s.track = ti;
                s.key = idx;
                Commit(ctx, "Add keyframe");
            }
        }
        if (ImGui::IsItemHovered() && !ImGui::IsMouseDown(0)) ui::Tooltip("Double-click to add a keyframe");
        ImGui::PopID();
    }

    if (delete_track >= 0) {
        s.edit.tracks.erase(s.edit.tracks.begin() + delete_track);
        s.track = std::max(0, std::min(s.track, (int)s.edit.tracks.size() - 1));
        s.key = 0;
        Commit(ctx, "Remove track");
    }

    float rows_bottom = origin.y + ruler_h + row_h * (float)track_count;
    if (track_count == 0) {
        dl->AddText(ImVec2(x0, rows_bottom + 6.0f), ToU32(c.text_dim), "No tracks yet. Add one to animate a property.");
        rows_bottom += row_h;
    }
    float ph = x0 + s.progress * span;
    ImU32 phc = ToU32(c.accent);
    dl->AddLine(ImVec2(ph, origin.y), ImVec2(ph, rows_bottom), phc, 1.5f);
    dl->AddTriangleFilled(ImVec2(ph - 5.0f, origin.y), ImVec2(ph + 5.0f, origin.y), ImVec2(ph, origin.y + 6.0f), phc);

    ImGui::SetCursorScreenPos(ImVec2(origin.x, rows_bottom + st.ItemSpacing.y));
    AddTrackCombo(ctx, label_w);
    ImGui::SameLine();
    ui::TextDim("Double-click a row to add a key, drag keys to retime, Del removes the selected one.");

    if (ImGui::IsWindowFocused() && !ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Delete, false)) {
        if (kf::TrackEdit* tr = SelectedTrack()) {
            if (kf::DeleteKey(*tr, s.key)) {
                s.key = std::max(0, s.key - 1);
                Commit(ctx, "Delete keyframe");
            }
        }
    }
    ImGui::EndChild();
}

void DrawKeyInspector(EditorContext& ctx) {
    EditorState& s = State();
    const ImGuiStyle& st = ImGui::GetStyle();
    const float fs = ImGui::GetFontSize();
    kf::TrackEdit* tr = SelectedTrack();
    if (!tr || tr->keys.empty()) {
        ui::TextDim("Select a keyframe to edit its value and the easing of the segment after it.");
        return;
    }
    s.key = std::max(0, std::min(s.key, (int)tr->keys.size() - 1));
    kf::Key& key = tr->keys[(size_t)s.key];
    const kf::PropInfo* info = kf::FindProp(tr->prop);
    const int n = (int)tr->keys.size();
    const bool pinned = s.key == 0 || s.key == n - 1;
    const bool last = s.key == n - 1;
    const float dur = s.edit.duration > 0 ? s.edit.duration : 1.0f;

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(info ? info->label : tr->prop.c_str());
    ImGui::SameLine();
    ui::TextDim("key %d of %d", s.key + 1, n);
    ImGui::SameLine(0, fs);

    ImGui::AlignTextToFramePadding();
    ui::TextDim("t");
    ImGui::SameLine();
    float t = key.t;
    ImGui::SetNextItemWidth(fs * 4.5f);
    if (pinned) ImGui::BeginDisabled();
    if (ImGui::DragFloat("##t", &t, 0.002f, 0.0f, 1.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp)) {
        kf::RetimeKey(*tr, s.key, t, true);
        Commit(ctx, "Move keyframe");
    }
    if (pinned) ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayShort))
        ImGui::SetTooltip(pinned ? "The first and last keyframes stay at t = 0 and t = 1" : "%.3f s into the animation",
                          (double)(key.t * dur));
    ImGui::SameLine(0, fs);

    ImGui::AlignTextToFramePadding();
    ui::TextDim("value");
    ImGui::SameLine();
    if (info && info->color) {
        ImGui::SetNextItemWidth(fs * 9.0f);
        if (ImGui::ColorEdit4("##v", key.v, ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf | ImGuiColorEditFlags_Uint8))
            Commit(ctx, "Edit keyframe");
    } else {
        float lo = info ? info->min : -10000.0f, hi = info ? info->max : 10000.0f;
        ImGui::SetNextItemWidth(fs * 6.5f);
        if (ImGui::DragFloat("##v", &key.v[0], info ? info->speed : 0.01f, lo, hi, info ? info->format : "%.2f",
                             ImGuiSliderFlags_AlwaysClamp))
            Commit(ctx, "Edit keyframe");
        if (info) {
            ImGui::SameLine(0, st.ItemInnerSpacing.x);
            if (ImGui::SmallButton("identity")) {
                key.v[0] = info->identity;
                Commit(ctx, "Edit keyframe");
            }
            ui::Tooltip("Reset to the value that leaves the widget untouched");
        }
    }
    ImGui::SameLine(0, fs);

    ImGui::AlignTextToFramePadding();
    ui::TextDim("ease");
    ImGui::SameLine();
    float buttons_w = ui::ButtonWidth(KF_ICON_ADD " key") + ui::ButtonWidth("Delete") + st.ItemSpacing.x + fs;
    float ease_w = std::max(fs * 5.0f, std::min(fs * 10.0f, ImGui::GetContentRegionAvail().x - buttons_w));
    if (last) {
        ImGui::BeginDisabled();
        ImGui::Button("no segment", ImVec2(ease_w, 0));
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("The last keyframe has no segment after it");
    } else if (EaseField("seg_ease", key.ease, s.edit.ease.c_str(), ease_w)) {
        Commit(ctx, "Edit easing");
    }
    ImGui::SameLine(0, fs);

    if (ImGui::Button(KF_ICON_ADD " key")) {
        int idx = kf::InsertKey(*tr, s.progress, nullptr, s.edit.ease, SampleEaseString);
        if (idx >= 0) {
            s.key = idx;
            Commit(ctx, "Add keyframe");
        }
    }
    ui::Tooltip("Adds a keyframe on this track where the playhead is, keeping the current curve");
    ImGui::SameLine();
    bool can_delete = kf::CanDeleteKey(*tr, s.key);
    if (!can_delete) ImGui::BeginDisabled();
    if (ImGui::Button("Delete")) {
        kf::DeleteKey(*tr, s.key);
        s.key = std::max(0, s.key - 1);
        Commit(ctx, "Delete keyframe");
    }
    if (!can_delete) ImGui::EndDisabled();
}

}  // namespace imforge::builder::kfui
