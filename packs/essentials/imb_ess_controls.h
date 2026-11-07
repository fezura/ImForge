#pragma once
// Toggle, KeybindPicker, ColorSwatchRow
#include "imb_ess_core.h"

namespace imb {
namespace ess {

struct ToggleStyle {
    ImU32 on_color = 0;
    ImU32 off_color = 0;
    ImU32 knob_color = 0;
    float width = 0.0f;
    float height = 0.0f;
    bool show_label = true;
    bool disabled = false;
    float disabled_alpha = 0.5f;
};

inline bool Toggle(const char* label, bool* v, const ToggleStyle& ts = ToggleStyle()) {
    const ImGuiStyle& st = ImGui::GetStyle();
    float h = ts.height > 0.0f ? ts.height : ImGui::GetFrameHeight() * 0.8f;
    float w = ts.width > 0.0f ? ts.width : h * 1.8f;
    const char* end = detail::LabelEnd(label);
    bool with_label = ts.show_label && end > label;
    ImVec2 label_size = with_label ? ImGui::CalcTextSize(label, end) : ImVec2(0, 0);
    float total_w = w + (label_size.x > 0.0f ? st.ItemInnerSpacing.x + label_size.x : 0.0f);
    float total_h = detail::Max(h, ImGui::GetFrameHeight());
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGuiID id = ImGui::GetID(label);
    bool pressed = false;
    bool hovered = false;
    if (ts.disabled) {
        ImGui::PushID(label);
        ImGui::Dummy(ImVec2(total_w, total_h));
        ImGui::PopID();
    } else {
        pressed = ImGui::InvisibleButton(label, ImVec2(total_w, total_h));
        hovered = ImGui::IsItemHovered();
    }
    if (pressed) *v = !*v;
    float t = detail::Anim(id, *v, GetStyle().anim_duration);
    float alpha = ts.disabled ? ts.disabled_alpha : 1.0f;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 tmin(pos.x, pos.y + (total_h - h) * 0.5f), tmax(tmin.x + w, tmin.y + h);
    ImU32 off_src = ts.off_color ? ts.off_color : GetStyle().track;
    ImU32 off = detail::Col(off_src, ImGuiCol_FrameBg, alpha);
    ImU32 on = ts.on_color ? ImGui::GetColorU32(detail::WithAlpha(ts.on_color, alpha))
                           : detail::Accent(alpha);
    ImU32 track = detail::LerpColor(off, on, t);
    if (hovered) track = detail::LerpColor(track, ImGui::GetColorU32(IM_COL32_WHITE), 0.08f);
    dl->AddRectFilled(tmin, tmax, track, h * 0.5f);
    float r = h * 0.5f - detail::Max(1.5f, h * 0.12f);
    ImVec2 knob(detail::Lerp(tmin.x + h * 0.5f, tmax.x - h * 0.5f, t), (tmin.y + tmax.y) * 0.5f);
    dl->AddCircleFilled(ImVec2(knob.x, knob.y + 1.0f), r,
                        ImGui::GetColorU32(IM_COL32(0, 0, 0, (int)(50.0f * alpha))));
    ImU32 kc = ts.knob_color ? ImGui::GetColorU32(detail::WithAlpha(ts.knob_color, alpha))
                             : detail::KnobCol(alpha);
    dl->AddCircleFilled(knob, r, kc);
    if (label_size.x > 0.0f)
        dl->AddText(ImVec2(tmax.x + st.ItemInnerSpacing.x, pos.y + (total_h - label_size.y) * 0.5f),
                    detail::TextCol(alpha), label, end);
    return pressed;
}

inline bool Toggle(const char* label, bool* v, ImU32 on_color, ImU32 off_color = 0) {
    ToggleStyle ts;
    ts.on_color = on_color;
    ts.off_color = off_color;
    return Toggle(label, v, ts);
}

inline bool Toggle(const char* label, bool* v, const ImVec4& on_color,
                   const ImVec4& off_color = ImVec4(0, 0, 0, 0)) {
    return Toggle(label, v, ImGui::ColorConvertFloat4ToU32(on_color),
                  ImGui::ColorConvertFloat4ToU32(off_color));
}

struct KeybindStyle {
    ImVec2 size = ImVec2(0, 0);
    ImU32 bg = 0;
    ImU32 bg_hover = 0;
    ImU32 bg_capture = 0;
    ImU32 text = 0;
    bool show_label = true;
    bool allow_mouse = true;
};

namespace detail {

inline ImGuiID& CapturingKeybind() {
    static ImGuiID id = 0;
    return id;
}

// KeyName() encoding, 0 if nothing was pressed
inline int PollPressedKey(bool allow_mouse, bool* escaped) {
    *escaped = false;
#if IMGUI_VERSION_NUM >= 18700
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        *escaped = true;
        return 0;
    }
    for (int k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_GamepadStart; ++k)
        if (ImGui::IsKeyPressed((ImGuiKey)k, false)) return k;
#else
    const ImGuiIO& io = ImGui::GetIO();
    int esc = ImGui::GetKeyIndex(ImGuiKey_Escape);
    if (esc >= 0 && esc < IM_ARRAYSIZE(io.KeysDown) && ImGui::IsKeyPressed(esc, false)) {
        *escaped = true;
        return 0;
    }
    for (int k = 0; k < IM_ARRAYSIZE(io.KeysDown); ++k)
        if (ImGui::IsKeyPressed(k, false)) return k;
#endif
    if (allow_mouse)
        for (int b = 0; b < 5; ++b)
            if (ImGui::IsMouseClicked(b)) return -(b + 1);
    return 0;
}

}  // namespace detail

inline bool KeybindPicker(const char* label, int* key, const KeybindStyle& ks = KeybindStyle()) {
    const ImGuiStyle& st = ImGui::GetStyle();
    const char* end = detail::LabelEnd(label);
    ImGuiID id = ImGui::GetID(label);
    ImGuiID& cap = detail::CapturingKeybind();
    bool capturing = cap == id;
    float font = ImGui::GetFontSize();
    float w = ks.size.x > 0.0f ? ks.size.x : ImGui::CalcTextSize("[press a key]").x + font;
    float h = ks.size.y > 0.0f ? ks.size.y : ImGui::GetFrameHeight();
    bool with_label = ks.show_label && end > label;
    ImVec2 label_size = with_label ? ImGui::CalcTextSize(label, end) : ImVec2(0, 0);
    float total_w = w + (label_size.x > 0.0f ? st.ItemInnerSpacing.x + label_size.x : 0.0f);
    ImVec2 pos = ImGui::GetCursorScreenPos();
    bool pressed = ImGui::InvisibleButton(label, ImVec2(total_w, h));
    bool hovered = ImGui::IsItemHovered();
    bool changed = false;
    if (capturing) {
        bool escaped = false;
        int k = detail::PollPressedKey(ks.allow_mouse, &escaped);
        if (escaped) {
            changed = *key != 0;
            *key = 0;
            cap = 0;
            capturing = false;
        } else if (k != 0) {
            changed = *key != k;
            *key = k;
            cap = 0;
            capturing = false;
        }
    } else if (pressed) {
        cap = id;
        capturing = true;
    }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 bmax(pos.x + w, pos.y + h);
    float r = detail::Rounding();
    ImU32 bg;
    if (capturing) {
        ImU32 src = ks.bg_capture ? ks.bg_capture : GetStyle().accent;
        bg = detail::Col(src, ImGuiCol_CheckMark, 0.85f);
    } else {
        bg = detail::Col(hovered ? ks.bg_hover : ks.bg,
                         hovered ? ImGuiCol_ButtonHovered : ImGuiCol_Button);
    }
    dl->AddRectFilled(pos, bmax, bg, r);
    dl->AddRect(pos, bmax, capturing ? detail::Accent() : detail::BorderCol(), r);
    ImU32 tc = detail::Col(ks.text, ImGuiCol_Text);
    if (capturing) {
        float pulse = 0.65f + 0.35f * sinf((float)ImGui::GetTime() * 6.0f);
        tc = detail::WithAlpha(tc, pulse);
    }
    const char* shown = capturing ? "[press a key]" : KeyName(*key);
    detail::TextIn(dl, pos, bmax, shown, nullptr, tc, 0.5f, st.FramePadding.x);
    if (label_size.x > 0.0f)
        dl->AddText(ImVec2(bmax.x + st.ItemInnerSpacing.x, pos.y + (h - label_size.y) * 0.5f),
                    detail::TextCol(), label, end);
    return changed;
}

inline bool IsCapturingKeybind() { return detail::CapturingKeybind() != 0; }

inline bool ColorSwatchRow(const char* label, ImVec4* colors, int count, int* selected,
                           float swatch_size = 0.0f) {
    if (!colors || count <= 0) return false;
    const ImGuiStyle& st = ImGui::GetStyle();
    ImGui::PushID(label);
    float sz = swatch_size > 0.0f ? swatch_size : ImGui::GetFrameHeight();
    float gap = st.ItemInnerSpacing.x;
    const char* end = detail::LabelEnd(label);
    ImVec2 label_size = end > label ? ImGui::CalcTextSize(label, end) : ImVec2(0, 0);
    float row_w = (float)count * sz + (float)(count - 1) * gap;
    float total_w = row_w + (label_size.x > 0.0f ? st.ItemInnerSpacing.x + label_size.x : 0.0f);
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##swatches", ImVec2(total_w, sz));
    bool hovered = ImGui::IsItemHovered();
    int hover_idx = -1;
    if (hovered) {
        float rel = ImGui::GetMousePos().x - pos.x;
        int i = (int)(rel / (sz + gap));
        if (i >= 0 && i < count && rel - (float)i * (sz + gap) <= sz) hover_idx = i;
    }
    bool changed = false;
    ImGuiStorage* storage = ImGui::GetStateStorage();
    ImGuiID popup_key = ImGui::GetID("##picker_idx");
    if (hover_idx >= 0 && ImGui::IsMouseClicked(0) && selected && *selected != hover_idx) {
        *selected = hover_idx;
        changed = true;
    }
    if (hover_idx >= 0 && ImGui::IsMouseClicked(1)) {
        storage->SetInt(popup_key, hover_idx);
        ImGui::OpenPopup("##picker");
    }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float r = detail::Max(detail::Rounding(), sz * 0.2f);
    ImGuiID anim_base = ImGui::GetID("##sw");
    const ImU32 kCheckDark = IM_COL32(60, 60, 60, 255), kCheckLight = IM_COL32(110, 110, 110, 255);
    for (int i = 0; i < count; ++i) {
        ImVec2 mn(pos.x + (float)i * (sz + gap), pos.y), mx(mn.x + sz, mn.y + sz);
        bool is_sel = selected && *selected == i;
        float t = detail::Anim(anim_base + (ImGuiID)i, is_sel, GetStyle().anim_duration);
        if (colors[i].w < 1.0f) {
            ImVec2 mid((mn.x + mx.x) * 0.5f, (mn.y + mx.y) * 0.5f);
            dl->AddRectFilled(mn, mx, kCheckDark, r);
            dl->AddRectFilled(mn, mid, kCheckLight, r, detail::kRoundTopLeft);
            dl->AddRectFilled(mid, mx, kCheckLight, r, detail::kRoundBottomRight);
        }
        dl->AddRectFilled(mn, mx, ImGui::GetColorU32(colors[i]), r);
        if (t > 0.01f) {
            float inset = -1.5f - 1.5f * t;
            detail::Outline(dl, ImVec2(mn.x + inset, mn.y + inset), ImVec2(mx.x - inset, mx.y - inset),
                            detail::Accent(t), r + 1.5f, 2.0f);
        } else if (i == hover_idx) {
            detail::Outline(dl, mn, mx, detail::TextDim(), r, 1.5f);
        } else {
            dl->AddRect(mn, mx, detail::BorderCol(0.6f), r);
        }
    }
    if (label_size.x > 0.0f)
        dl->AddText(ImVec2(pos.x + row_w + st.ItemInnerSpacing.x,
                           pos.y + (sz - label_size.y) * 0.5f),
                    detail::TextCol(), label, end);
    if (ImGui::BeginPopup("##picker")) {
        int i = storage->GetInt(popup_key, 0);
        if (i >= 0 && i < count &&
            ImGui::ColorPicker4("##p", &colors[i].x, ImGuiColorEditFlags_AlphaBar))
            changed = true;
        ImGui::EndPopup();
    }
    ImGui::PopID();
    return changed;
}

}  // namespace ess
}  // namespace imb
