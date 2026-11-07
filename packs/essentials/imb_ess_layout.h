#pragma once
// AnimatedTabBar, AnimatedSideTabs, SectionCard
#include "imb_ess_core.h"

namespace imb {
namespace ess {

struct TabStyle {
    float height = 0.0f;
    float width = 0.0f;
    float padding = 0.0f;
    float indicator_thickness = 0.0f;
    float rounding = -1.0f;
    bool pill = false;
    bool stretch = true;
    ImU32 bg = 0;
    ImU32 indicator = 0;
    ImU32 text = 0;
    ImU32 text_active = 0;
    ImU32 hover = 0;
};

namespace detail {

inline float TabWidth(const TabStyle& ts, const char* label, float bar_w, int count, float pad) {
    return ts.stretch ? bar_w / (float)count : ImGui::CalcTextSize(label).x + pad * 2.0f;
}

inline ImU32 ActiveTabText(const TabStyle& ts, ImU32 indicator) {
    if (ts.pill && IsBright(indicator)) return DarkText();
    return Col(ts.text_active, ImGuiCol_Text);
}

}  // namespace detail

inline bool AnimatedTabBar(const char* id, const char* const* labels, int count, int* index,
                           const TabStyle& ts = TabStyle()) {
    if (!labels || count <= 0 || !index) return false;
    ImGui::PushID(id);
    float font = ImGui::GetFontSize();
    float h = ts.height > 0.0f ? ts.height : ImGui::GetFrameHeight() * 1.25f;
    float pad = ts.padding > 0.0f ? ts.padding : font;
    float w = ts.width > 0.0f ? ts.width : detail::Max(ImGui::GetContentRegionAvail().x, 1.0f);
    ImVec2 pos = ImGui::GetCursorScreenPos();
    bool pressed = ImGui::InvisibleButton("##bar", ImVec2(w, h));
    bool hovered = ImGui::IsItemHovered();
    float mouse_x = ImGui::GetMousePos().x;
    int cur = *index < 0 ? 0 : (*index >= count ? count - 1 : *index);
    int hovered_tab = -1;
    float x = pos.x;
    for (int i = 0; i < count; ++i) {
        float tw = detail::TabWidth(ts, labels[i], w, count, pad);
        if (hovered && mouse_x >= x && mouse_x < x + tw) hovered_tab = i;
        x += tw;
    }
    bool changed = false;
    if (pressed && hovered_tab >= 0 && hovered_tab != cur) {
        *index = cur = hovered_tab;
        changed = true;
    }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float r = ts.rounding >= 0.0f ? ts.rounding : detail::Rounding();
    float dur = GetStyle().anim_duration;
    ImVec2 bar_max(pos.x + w, pos.y + h);
    if (ts.bg) dl->AddRectFilled(pos, bar_max, ImGui::GetColorU32(ts.bg), r);
    float active_x = pos.x, active_w = 0.0f;
    x = pos.x;
    for (int i = 0; i < count; ++i) {
        float tw = detail::TabWidth(ts, labels[i], w, count, pad);
        if (i == cur) {
            active_x = x;
            active_w = tw;
        }
        x += tw;
    }
    float ix = detail::AnimTo(ImGui::GetID("##ix"), active_x, dur);
    float iw = detail::AnimTo(ImGui::GetID("##iw"), active_w, dur);
    ImU32 ind = detail::Col(ts.indicator, ImGuiCol_CheckMark);
    if (ts.pill) {
        float inset = detail::Max(2.0f, font * 0.15f);
        dl->AddRectFilled(ImVec2(ix + inset, pos.y + inset),
                          ImVec2(ix + iw - inset, bar_max.y - inset), ind, r);
    } else {
        float th = ts.indicator_thickness;
        if (th <= 0.0f) th = detail::Max(2.0f, font * 0.15f);
        dl->AddLine(ImVec2(pos.x, bar_max.y - 0.5f), ImVec2(bar_max.x, bar_max.y - 0.5f),
                    detail::BorderCol());
        dl->AddRectFilled(ImVec2(ix, bar_max.y - th), ImVec2(ix + iw, bar_max.y), ind, th * 0.5f);
    }
    ImGuiID tab_anim = ImGui::GetID("##tab");
    ImU32 idle_text = detail::Col(ts.text, ImGuiCol_TextDisabled);
    ImU32 active_text = detail::ActiveTabText(ts, ind);
    x = pos.x;
    for (int i = 0; i < count; ++i) {
        float tw = detail::TabWidth(ts, labels[i], w, count, pad);
        ImVec2 mn(x, pos.y), mx(x + tw, bar_max.y);
        float ta = detail::Anim(tab_anim + (ImGuiID)i, i == cur, dur);
        float th = detail::Anim(tab_anim + (ImGuiID)(count + i), i == hovered_tab && i != cur, dur);
        if (th > 0.01f && !ts.pill)
            dl->AddRectFilled(mn, mx, detail::Col(ts.hover, ImGuiCol_FrameBgHovered, 0.5f * th), r);
        ImU32 tc = detail::LerpColor(idle_text, active_text, ta);
        detail::TextIn(dl, mn, mx, labels[i], nullptr, tc, 0.5f, pad * 0.5f);
        x += tw;
    }
    ImGui::PopID();
    return changed;
}

inline bool AnimatedTabBar(const char* id, const char* labels_zero_separated, int* index,
                           const TabStyle& ts = TabStyle()) {
    const char* items[64] = {};
    int n = detail::SplitZero(labels_zero_separated, items, 64);
    return AnimatedTabBar(id, items, n, index, ts);
}

inline bool AnimatedSideTabs(const char* id, const char* const* labels, const char* const* icons,
                             int count, int* index, const TabStyle& ts = TabStyle()) {
    if (!labels || count <= 0 || !index) return false;
    ImGui::PushID(id);
    float font = ImGui::GetFontSize();
    float row_h = ts.height > 0.0f ? ts.height : ImGui::GetFrameHeight() * 1.5f;
    float pad = ts.padding > 0.0f ? ts.padding : font * 0.75f;
    float w = ts.width > 0.0f ? ts.width : detail::Max(ImGui::GetContentRegionAvail().x, 1.0f);
    ImVec2 pos = ImGui::GetCursorScreenPos();
    bool pressed = ImGui::InvisibleButton("##side", ImVec2(w, row_h * (float)count));
    bool hovered = ImGui::IsItemHovered();
    int cur = *index < 0 ? 0 : (*index >= count ? count - 1 : *index);
    int hovered_tab = -1;
    if (hovered) {
        int i = (int)((ImGui::GetMousePos().y - pos.y) / row_h);
        if (i >= 0 && i < count) hovered_tab = i;
    }
    bool changed = false;
    if (pressed && hovered_tab >= 0 && hovered_tab != cur) {
        *index = cur = hovered_tab;
        changed = true;
    }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float r = ts.rounding >= 0.0f ? ts.rounding : detail::Rounding();
    float dur = GetStyle().anim_duration;
    ImVec2 all_max(pos.x + w, pos.y + row_h * (float)count);
    if (ts.bg) dl->AddRectFilled(pos, all_max, ImGui::GetColorU32(ts.bg), r);
    float iy = detail::AnimTo(ImGui::GetID("##iy"), pos.y + row_h * (float)cur, dur);
    ImU32 ind = detail::Col(ts.indicator, ImGuiCol_CheckMark);
    float inset = detail::Max(2.0f, font * 0.15f);
    ImVec2 pill_min(pos.x + inset, iy + inset), pill_max(pos.x + w - inset, iy + row_h - inset);
    if (ts.pill) {
        dl->AddRectFilled(pill_min, pill_max, ind, r);
    } else {
        float th = ts.indicator_thickness;
        if (th <= 0.0f) th = detail::Max(3.0f, font * 0.2f);
        ImU32 row_bg = detail::Col(ts.hover, ImGuiCol_FrameBgHovered, 0.6f);
        dl->AddRectFilled(pill_min, pill_max, row_bg, r);
        dl->AddRectFilled(ImVec2(pos.x, iy + inset), ImVec2(pos.x + th, iy + row_h - inset), ind,
                          th * 0.5f);
    }
    ImGuiID tab_anim = ImGui::GetID("##row");
    float icon_w = 0.0f;
    if (icons)
        for (int i = 0; i < count; ++i)
            if (icons[i]) icon_w = detail::Max(icon_w, ImGui::CalcTextSize(icons[i]).x);
    ImU32 idle_text = detail::Col(ts.text, ImGuiCol_TextDisabled);
    ImU32 active_text = detail::ActiveTabText(ts, ind);
    for (int i = 0; i < count; ++i) {
        ImVec2 mn(pos.x, pos.y + row_h * (float)i), mx(pos.x + w, mn.y + row_h);
        float ta = detail::Anim(tab_anim + (ImGuiID)i, i == cur, dur);
        float th = detail::Anim(tab_anim + (ImGuiID)(count + i), i == hovered_tab && i != cur, dur);
        if (th > 0.01f)
            dl->AddRectFilled(ImVec2(mn.x + inset, mn.y + inset),
                              ImVec2(mx.x - inset, mx.y - inset),
                              detail::Col(ts.hover, ImGuiCol_FrameBgHovered, 0.35f * th), r);
        ImU32 tc = detail::LerpColor(idle_text, active_text, ta);
        float tx = mn.x + pad;
        if (icon_w > 0.0f) {
            if (icons[i])
                detail::TextIn(dl, ImVec2(tx, mn.y), ImVec2(tx + icon_w, mx.y), icons[i], nullptr,
                               tc, 0.5f);
            tx += icon_w + pad * 0.75f;
        }
        detail::TextIn(dl, ImVec2(tx, mn.y), ImVec2(mx.x - pad, mx.y), labels[i], nullptr, tc,
                       0.0f);
    }
    ImGui::PopID();
    return changed;
}

inline bool AnimatedSideTabs(const char* id, const char* labels_zero_separated,
                             const char* icons_zero_separated, int* index,
                             const TabStyle& ts = TabStyle()) {
    const char* items[64] = {};
    const char* icon_items[64] = {};
    int n = detail::SplitZero(labels_zero_separated, items, 64);
    int ni = detail::SplitZero(icons_zero_separated, icon_items, 64);
    return AnimatedSideTabs(id, items, ni > 0 ? icon_items : nullptr, n, index, ts);
}

// `size` works like BeginChild. height 0 auto-sizes on 1.90+ and fills on older versions.
// Always call EndSectionCard(), even when collapsed.
inline void SectionCard(const char* title, const ImVec2& size = ImVec2(0, 0),
                        bool* open = nullptr) {
    const ImGuiStyle& st = ImGui::GetStyle();
    float font = ImGui::GetFontSize();
    float header_h = floorf(ImGui::GetFrameHeight() * 1.2f + 0.5f);
    bool collapsed = open && !*open;
    ImVec2 sz = size;
    if (collapsed) sz.y = header_h;
    float r = detail::CardRounding();
    ImGuiID id = ImGui::GetID(title);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, detail::CardBg());
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, r);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 0.0f);
#if IMGUI_VERSION_NUM >= 19000
    ImGuiChildFlags cf = ImGuiChildFlags_AlwaysUseWindowPadding;
    if (sz.y == 0.0f && !collapsed) cf |= ImGuiChildFlags_AutoResizeY;
    ImGui::BeginChild(title, sz, cf, ImGuiWindowFlags_None);
#else
    ImGui::BeginChild(title, sz, false, ImGuiWindowFlags_AlwaysUseWindowPadding);
#endif
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
    ImVec2 wpos = ImGui::GetWindowPos();
    float ww = ImGui::GetWindowWidth();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 hmin = wpos, hmax(wpos.x + ww, wpos.y + header_h);
    ImGui::SetCursorScreenPos(hmin);
    bool hovered = false;
    if (open) {
        if (ImGui::InvisibleButton("##hdr", ImVec2(detail::Max(ww, 1.0f), header_h)))
            *open = !*open;
        hovered = ImGui::IsItemHovered();
        collapsed = !*open;
    } else {
        ImGui::Dummy(ImVec2(detail::Max(ww, 1.0f), header_h));
    }
    float t = detail::Anim(id, !collapsed, GetStyle().anim_duration);
    ImU32 header_col = detail::Col(GetStyle().card_header, ImGuiCol_Header, hovered ? 0.9f : 0.65f);
    dl->AddRectFilled(hmin, hmax, header_col, r,
                      collapsed ? detail::kRoundAll : detail::kRoundTop);
    detail::Gradient(dl, ImVec2(hmin.x, hmax.y - 1.0f), ImVec2(hmax.x, hmax.y),
                     detail::Accent(0.9f), detail::Accent(0.0f), false, 0.0f);
    float tx = hmin.x + st.FramePadding.x + font * 0.25f;
    if (open) {
        float cx = tx + font * 0.3f, cy = (hmin.y + hmax.y) * 0.5f, s = font * 0.3f;
        float ang = detail::Lerp(0.0f, 1.5707963f, t);
        float c = cosf(ang), sn = sinf(ang);
        ImVec2 p[3] = {ImVec2(-s * 0.6f, -s), ImVec2(s * 0.8f, 0.0f), ImVec2(-s * 0.6f, s)};
        for (int i = 0; i < 3; ++i)
            p[i] = ImVec2(cx + p[i].x * c - p[i].y * sn, cy + p[i].x * sn + p[i].y * c);
        dl->AddTriangleFilled(p[0], p[1], p[2], detail::TextCol());
        tx += font * 1.1f;
    }
    detail::TextIn(dl, ImVec2(tx, hmin.y), ImVec2(hmax.x - st.FramePadding.x, hmax.y), title,
                   detail::LabelEnd(title), detail::TextCol(), 0.0f);
    ImGui::SetCursorScreenPos(ImVec2(hmin.x + st.WindowPadding.x, hmax.y + st.WindowPadding.y));
}

inline void EndSectionCard() {
    ImGui::EndChild();
    ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
    float r = detail::CardRounding();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    detail::Shadow(dl, mn, mx, r, IM_COL32(0, 0, 0, 70), ImGui::GetFontSize() * 0.4f);
    dl->AddRect(mn, mx, detail::BorderCol(), r);
}

}  // namespace ess
}  // namespace imb
