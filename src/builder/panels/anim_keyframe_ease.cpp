#include <algorithm>
#include <cmath>
#include <cstdio>

#include "imgui.h"

#include "imforge/imb_motion.h"
#include "imforge/imforge_anim_names.h"
#include "imforge/imforge_motion_json.h"

#include "../builder_style.h"
#include "../ui/ease_curves.h"
#include "../ui/ui_helpers.h"
#include "anim_keyframe_editor.h"

namespace imforge::builder::kfui {

namespace {

const int kThumbSamples = 40;
const char* const kModeLabels[] = {"Default", "Named", "Bezier", "Steps", "Spring"};

struct BezierPreset {
    const char* label;
    float p[4];
};
const BezierPreset kBezierPresets[] = {
    {"ease", {0.25f, 0.1f, 0.25f, 1.0f}},       {"ease-in", {0.42f, 0.0f, 1.0f, 1.0f}},
    {"ease-out", {0.0f, 0.0f, 0.58f, 1.0f}},    {"ease-in-out", {0.42f, 0.0f, 0.58f, 1.0f}},
    {"overshoot", {0.34f, 1.56f, 0.64f, 1.0f}}, {"anticipate", {0.36f, -0.4f, 0.6f, 1.0f}},
};

bool ToSpec(const std::string& ease, imb::EaseSpec& out) { return !ease.empty() && ParseEaseSpec(ease, out); }

// visible value range of the big bezier editor
const float kBezLo = -0.6f, kBezHi = 1.6f;

ImVec2 BezPoint(const ImVec2& mn, const ImVec2& mx, float x, float y) {
    float v = (y - kBezLo) / (kBezHi - kBezLo);
    return ImVec2(mn.x + (mx.x - mn.x) * x, mx.y - (mx.y - mn.y) * v);
}

bool BezierEditor(kf::EaseParsed& e, float size) {
    const BuilderColors& c = Colors();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 mn = ImGui::GetCursorScreenPos(), mx(mn.x + size, mn.y + size);
    ImGui::Dummy(ImVec2(size, size));
    dl->AddRectFilled(mn, mx, ToU32(c.bg_window), 4.0f);
    for (int i = 1; i < 4; ++i) {
        float x = mn.x + size * (float)i / 4.0f;
        dl->AddLine(ImVec2(x, mn.y), ImVec2(x, mx.y), ToU32(c.separator, 0.5f));
    }
    ImVec2 p0 = BezPoint(mn, mx, 0, 0), p3 = BezPoint(mn, mx, 1, 1);
    dl->AddLine(ImVec2(mn.x, p0.y), ImVec2(mx.x, p0.y), ToU32(c.separator));
    dl->AddLine(ImVec2(mn.x, p3.y), ImVec2(mx.x, p3.y), ToU32(c.separator));
    dl->AddLine(p0, p3, ToU32(c.text_disabled, 0.4f));

    bool changed = false;
    float r = std::max(5.0f, ImGui::GetFontSize() * 0.36f);
    for (int h = 0; h < 2; ++h) {
        float* px = &e.p[h * 2];
        float* py = &e.p[h * 2 + 1];
        ImVec2 hp = BezPoint(mn, mx, *px, std::max(kBezLo, std::min(kBezHi, *py)));
        ImGui::SetCursorScreenPos(ImVec2(hp.x - r * 1.6f, hp.y - r * 1.6f));
        ImGui::PushID(h);
        ImGui::InvisibleButton("##handle", ImVec2(r * 3.2f, r * 3.2f));
        bool hovered = ImGui::IsItemHovered(), active = ImGui::IsItemActive();
        if (hovered || active) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        if (active && ImGui::IsMouseDragging(0, 0.0f)) {
            ImVec2 m = ImGui::GetMousePos();
            float nx = (m.x - mn.x) / size;
            float ny = kBezLo + (mx.y - m.y) / size * (kBezHi - kBezLo);
            nx = std::max(0.0f, std::min(1.0f, nx));
            ny = std::max(kBezLo, std::min(kBezHi, ny));
            nx = std::round(nx * 100.0f) / 100.0f;
            ny = std::round(ny * 100.0f) / 100.0f;
            if (nx != *px || ny != *py) {
                *px = nx;
                *py = ny;
                changed = true;
            }
        }
        if (hovered && !active) ImGui::SetTooltip("%.2f, %.2f", (double)*px, (double)*py);
        ImGui::PopID();
        ImVec2 anchor = h == 0 ? p0 : p3;
        ImU32 hc = ToU32(active ? c.accent_active : (hovered ? c.accent_hover : c.accent));
        dl->AddLine(anchor, hp, hc, 1.5f);
        dl->AddCircleFilled(hp, r, hc, 16);
        dl->AddCircle(hp, r, ToU32(c.text), 16, 1.0f);
    }
    ImVec2 pts[kThumbSamples * 2];
    int n = kThumbSamples * 2;
    for (int i = 0; i < n; ++i) {
        float t = (float)i / (float)(n - 1);
        float y = imb::detail::SampleCubicBezier(e.p[0], e.p[1], e.p[2], e.p[3], t);
        pts[i] = BezPoint(mn, mx, t, std::max(kBezLo, std::min(kBezHi, y)));
    }
    dl->AddPolyline(pts, n, ToU32(c.text), ImDrawFlags_None, 2.0f);
    ImGui::SetCursorScreenPos(ImVec2(mn.x, mx.y + ImGui::GetStyle().ItemSpacing.y));
    return changed;
}

void CurveBox(const std::string& ease, float w, float h) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 mn = ImGui::GetCursorScreenPos(), mx(mn.x + w, mn.y + h);
    ImGui::Dummy(ImVec2(w, h));
    ui::DrawCurveGrid(dl, mn, mx, ToU32(Colors().separator, 0.6f), ToU32(Colors().bg_window));
    DrawEaseThumb(dl, mn, mx, ease, ToU32(Colors().accent), 2.0f);
}

int ModeOf(const std::string& ease) {
    if (ease.empty()) return 0;
    kf::EaseParsed e;
    if (!kf::ParseEase(ease, e)) return 1;
    return 1 + (int)e.type;
}

bool NamedGrid(std::string& ease) {
    const BuilderColors& c = Colors();
    const ImGuiStyle& st = ImGui::GetStyle();
    float fs = ImGui::GetFontSize();
    float tw = std::floor(fs * 5.2f), th = std::floor(fs * 3.4f);
    int cols = std::max(1, (int)((ImGui::GetContentRegionAvail().x + st.ItemSpacing.x) / (tw + st.ItemSpacing.x)));
    bool changed = false;
    for (int i = 0; i < kEaseCount; ++i) {
        const char* name = EaseName(static_cast<imb::Ease>(i));
        if (i % cols) ImGui::SameLine();
        ImGui::PushID(i);
        ImVec2 p = ImGui::GetCursorScreenPos();
        bool sel = ease == name;
        if (ImGui::InvisibleButton("##tile", ImVec2(tw, th)) && !sel) {
            ease = name;
            changed = true;
        }
        bool hov = ImGui::IsItemHovered();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(p, ImVec2(p.x + tw, p.y + th), ToU32(sel ? c.accent_dim : (hov ? c.bg_hover : c.bg_frame)), 4.0f);
        if (sel) dl->AddRect(p, ImVec2(p.x + tw, p.y + th), ToU32(c.accent), 4.0f);
        float lh = ImGui::GetTextLineHeight();
        DrawEaseThumb(dl, ImVec2(p.x + 6.0f, p.y + 4.0f), ImVec2(p.x + tw - 6.0f, p.y + th - lh - 4.0f), name,
                      ToU32(sel ? ImVec4(1, 1, 1, 1) : c.text), 1.5f);
        ImVec2 ts = ImGui::CalcTextSize(name);
        dl->PushClipRect(p, ImVec2(p.x + tw, p.y + th), true);
        dl->AddText(ImVec2(p.x + std::max(2.0f, (tw - ts.x) * 0.5f), p.y + th - lh - 2.0f), ToU32(sel ? ImVec4(1, 1, 1, 1) : c.text_dim),
                    name);
        dl->PopClipRect();
        ImGui::PopID();
    }
    return changed;
}

bool EasePopupBody(std::string& ease, const char* default_ease) {
    const BuilderColors& c = Colors();
    float fs = ImGui::GetFontSize();
    bool changed = false;
    int mode = ModeOf(ease);
    int first = default_ease ? 0 : 1;
    for (int i = first; i < 5; ++i) {
        if (i > first) ImGui::SameLine(0, ImGui::GetStyle().ItemInnerSpacing.x);
        bool on = mode == i;
        ImGui::PushStyleColor(ImGuiCol_Button, on ? c.accent : c.bg_frame);
        ImGui::PushStyleColor(ImGuiCol_Text, on ? ImVec4(1, 1, 1, 1) : c.text_dim);
        if (ImGui::SmallButton(kModeLabels[i]) && !on) {
            kf::EaseParsed cur;
            bool parsed = kf::ParseEase(ease, cur);
            switch (i) {
            case 0: ease.clear(); break;
            case 1: ease = parsed && cur.type == kf::EaseType::Named ? ease : std::string("OutCubic"); break;
            case 2: ease = "cubic-bezier(0.25,0.1,0.25,1)"; break;
            case 3: ease = "steps(4)"; break;
            case 4: ease = "spring(170,26)"; break;
            }
            changed = true;
        }
        ImGui::PopStyleColor(2);
    }
    mode = ModeOf(ease);
    ImGui::Separator();
    kf::EaseParsed e;
    kf::ParseEase(ease, e);
    switch (mode) {
    case 0: {
        ui::TextDim("Uses the animation's ease (%s).", default_ease ? default_ease : "");
        CurveBox(default_ease ? default_ease : "Linear", fs * 16.0f, fs * 8.0f);
        break;
    }
    case 1:
        if (ImGui::BeginChild("##named", ImVec2(fs * 17.5f, fs * 17.0f))) changed |= NamedGrid(ease);
        ImGui::EndChild();
        break;
    case 2: {
        if (BezierEditor(e, fs * 13.0f)) {
            ease = kf::FormatEase(e);
            changed = true;
        }
        ImGui::SetNextItemWidth(fs * 17.0f);
        if (ImGui::DragFloat4("##bez", e.p, 0.005f, -5.0f, 5.0f, "%.2f")) {
            e.p[0] = std::max(0.0f, std::min(1.0f, e.p[0]));
            e.p[2] = std::max(0.0f, std::min(1.0f, e.p[2]));
            ease = kf::FormatEase(e);
            changed = true;
        }
        for (size_t i = 0; i < sizeof(kBezierPresets) / sizeof(kBezierPresets[0]); ++i) {
            if (i % 3) ImGui::SameLine();
            if (ImGui::Button(kBezierPresets[i].label, ImVec2(fs * 5.5f, 0))) {
                for (int k = 0; k < 4; ++k) e.p[k] = kBezierPresets[i].p[k];
                ease = kf::FormatEase(e);
                changed = true;
            }
        }
        break;
    }
    case 3: {
        int n = (int)std::lround(e.p[0]);
        ImGui::SetNextItemWidth(fs * 16.0f);
        if (ImGui::SliderInt("##steps", &n, 1, 100, "%d steps", ImGuiSliderFlags_AlwaysClamp)) {
            e.p[0] = (float)n;
            ease = kf::FormatEase(e);
            changed = true;
        }
        CurveBox(ease, fs * 16.0f, fs * 8.0f);
        break;
    }
    case 4: {
        ImGui::SetNextItemWidth(fs * 16.0f);
        if (ImGui::DragFloat("##stiff", &e.p[0], 1.0f, 1.0f, 10000.0f, "stiffness %.0f", ImGuiSliderFlags_AlwaysClamp)) changed = true;
        ImGui::SetNextItemWidth(fs * 16.0f);
        if (ImGui::DragFloat("##damp", &e.p[1], 0.25f, 0.5f, 1000.0f, "damping %.1f", ImGuiSliderFlags_AlwaysClamp)) changed = true;
        if (changed) ease = kf::FormatEase(e);
        CurveBox(ease, fs * 16.0f, fs * 8.0f);
        ui::TextDim("Low damping wobbles, high damping settles slowly.");
        break;
    }
    }
    return changed;
}

}  // namespace

float SampleEaseString(const std::string& ease, float t) {
    imb::EaseSpec e;
    if (!ToSpec(ease, e)) return t;
    return imb::SampleEase(e, t);
}

void DrawEaseThumb(ImDrawList* dl, const ImVec2& min, const ImVec2& max, const std::string& ease, ImU32 col, float thickness) {
    if (max.x - min.x < 2.0f || max.y - min.y < 2.0f) return;
    imb::EaseSpec e;
    if (!ToSpec(ease, e)) e = imb::EaseSpec();
    ImVec2 pts[kThumbSamples];
    for (int i = 0; i < kThumbSamples; ++i) {
        float t = (float)i / (float)(kThumbSamples - 1);
        pts[i] = ui::CurvePoint(min, max, t, imb::SampleEase(e, t));
    }
    dl->AddPolyline(pts, kThumbSamples, col, ImDrawFlags_None, thickness);
}

bool EaseField(const char* id, std::string& ease, const char* default_ease, float width) {
    const BuilderColors& c = Colors();
    const ImGuiStyle& st = ImGui::GetStyle();
    ImGui::PushID(id);
    float h = ImGui::GetFrameHeight();
    if (width <= 0.0f) width = ImGui::CalcItemWidth();
    ImVec2 p = ImGui::GetCursorScreenPos();
    bool open = ImGui::InvisibleButton("##field", ImVec2(width, h));
    bool hov = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, ImVec2(p.x + width, p.y + h), ToU32(hov ? c.bg_hover : c.bg_frame), st.FrameRounding);
    float tw = std::floor(h * 1.5f);
    const std::string& shown_ease = ease.empty() && default_ease ? std::string(default_ease) : ease;
    DrawEaseThumb(dl, ImVec2(p.x + 4.0f, p.y + 3.0f), ImVec2(p.x + tw, p.y + h - 3.0f), shown_ease,
                  ToU32(ease.empty() ? c.text_dim : c.accent), 1.5f);
    std::string label = ease.empty() ? shown_ease : ease;
    dl->PushClipRect(ImVec2(p.x + tw + 4.0f, p.y), ImVec2(p.x + width - 4.0f, p.y + h), true);
    dl->AddText(ImVec2(p.x + tw + 6.0f, p.y + st.FramePadding.y), ToU32(ease.empty() ? c.text_dim : c.text), label.c_str());
    dl->PopClipRect();
    if (hov) {
        if (ease.empty()) ImGui::SetTooltip("%s, the animation's ease\nClick to give this segment its own", label.c_str());
        else ImGui::SetTooltip("%s\nClick to change the easing", label.c_str());
    }
    if (open) ImGui::OpenPopup("##ease_popup");
    {
        const ImGuiViewport* vp = ImGui::GetMainViewport();
        float fs = ImGui::GetFontSize();
        ImVec2 guess(fs * 18.5f, fs * 21.0f);
        float x = std::max(vp->WorkPos.x + 8.0f, std::min(p.x, vp->WorkPos.x + vp->WorkSize.x - guess.x - 8.0f));
        bool above = p.y + h + guess.y > vp->WorkPos.y + vp->WorkSize.y && p.y - guess.y > vp->WorkPos.y;
        ImGui::SetNextWindowPos(ImVec2(x, above ? p.y : p.y + h), ImGuiCond_Appearing, ImVec2(0.0f, above ? 1.0f : 0.0f));
    }
    bool changed = false;
    if (ImGui::BeginPopup("##ease_popup")) {
        changed = EasePopupBody(ease, default_ease);
        ImGui::EndPopup();
    }
    ImGui::PopID();
    return changed;
}

}  // namespace imforge::builder::kfui
