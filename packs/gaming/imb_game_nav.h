#pragma once
// IconRailNav, CategoryCard
#include "imb_game_core.h"

namespace imb {
namespace game {

struct RailStyle {
    float width = 0.0f;
    float item_height = 0.0f;
    float spacing = 0.0f;
    bool tooltips = true;
    bool indicator_bar = true;
    bool background = true;
    ImU32 bg = 0;
    ImU32 indicator = 0;
    ImU32 icon = 0;
    ImU32 icon_active = 0;
};

// vertical icon-only nav. icons are any text (icon font glyphs, letters). index == nullptr keeps
// the selection in ImGui storage. returns true on change
inline bool IconRailNav(const char* id, const char* const* icons, const char* const* tooltips, int count, int* index,
                        const RailStyle& st = RailStyle()) {
    namespace d = detail;
    if (!icons || count <= 0) return false;
    float fs = ImGui::GetFontSize();
    float fh = ImGui::GetFrameHeight();
    float w = st.width > 0.0f ? st.width : floorf(fh * 2.0f);
    float ih = st.item_height > 0.0f ? st.item_height : w;
    float gap = st.spacing > 0.0f ? st.spacing : floorf(4.0f * d::Dpi());
    float pad = gap;
    float total_h = pad * 2.0f + ih * (float)count + gap * (float)(count - 1);
    ImGui::PushID(id);
    ImVec2 pos = ImGui::GetCursorScreenPos();
    bool pressed = ImGui::InvisibleButton("##rail", ImVec2(w, total_h));
    bool hovered = ImGui::IsItemHovered();
    ImGuiStorage* storage = ImGui::GetStateStorage();
    ImGuiID tip_idx_key = ImGui::GetID("##tip_i"), tip_time_key = ImGui::GetID("##tip_t");
    ImGuiID y_id = ImGui::GetID("##y"), row_id = ImGui::GetID("##row"), tip_anim = ImGui::GetID("##tip");
    ImGuiID local_key = ImGui::GetID("##index");
    ImGui::PopID();
    int local = storage->GetInt(local_key, 0);
    int* in_index = index;
    if (!index) index = &local;

    int hot = -1;
    if (hovered) {
        float rel = ImGui::GetMousePos().y - pos.y - pad;
        int i = (int)(rel / (ih + gap));
        if (rel >= 0.0f && i >= 0 && i < count && rel - (float)i * (ih + gap) <= ih) hot = i;
    }
    int cur = *index < 0 ? 0 : (*index >= count ? count - 1 : *index);
    bool changed = false;
    if (pressed && hot >= 0 && hot != cur) {
        *index = cur = hot;
        changed = true;
    }
    if (!in_index) storage->SetInt(local_key, local);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 rail_max(pos.x + w, pos.y + total_h);
    float r = d::ed::CardRounding();
    if (st.background) {
        ImU32 bg = st.bg ? d::Live(st.bg) : d::ed::CardBg();
        dl->AddRectFilled(pos, rail_max, bg, r > 0.0f ? r : w * 0.25f);
    }
    float target_y = pos.y + pad + (ih + gap) * (float)cur;
    float y = d::SpringTo(y_id, target_y);
    ImU32 accent = st.indicator ? st.indicator : d::ed::AccentRaw();
    float inset = floorf(w * 0.14f);
    float pr = d::ed::Max(d::ed::Rounding(), ih * 0.22f);
    ImVec2 pmn(pos.x + inset, y), pmx(pos.x + w - inset, y + ih);
    dl->AddRectFilled(pmn, pmx, d::Live(accent, 0.2f), pr);
    d::ed::Outline(dl, pmn, pmx, d::Live(accent, 0.45f), pr, 1.0f);
    if (st.indicator_bar) {
        float stretch = d::ed::Min(fabsf(target_y - y) * 0.35f, ih * 0.6f);
        float bh = ih * 0.45f + stretch;
        float bw = d::ed::Max(3.0f, fs * 0.22f);
        float cy = y + ih * 0.5f;
        dl->AddRectFilled(ImVec2(pos.x, cy - bh * 0.5f), ImVec2(pos.x + bw, cy + bh * 0.5f), d::Live(accent), bw * 0.5f);
    }
    ImU32 idle = st.icon ? d::Live(st.icon) : d::ed::TextDim();
    ImU32 on = st.icon_active ? d::Live(st.icon_active) : d::ed::TextCol();
    for (int i = 0; i < count; ++i) {
        ImVec2 mn(pos.x, pos.y + pad + (ih + gap) * (float)i), mx(pos.x + w, mn.y + ih);
        float ta = d::ed::Anim(row_id + (ImGuiID)i, i == cur, 0.15f);
        float th = d::ed::Anim(row_id + (ImGuiID)(count + i), i == hot, 0.12f);
        if (th > 0.01f && i != cur)
            dl->AddRectFilled(ImVec2(mn.x + inset, mn.y), ImVec2(mx.x - inset, mx.y), d::ed::Col(0, ImGuiCol_FrameBgHovered, 0.5f * th), pr);
        const char* ic = icons[i] ? icons[i] : "";
        float isz = fs * (1.15f + 0.12f * th);
        ImVec2 ts = d::TextSize(isz, ic);
        ImU32 col = d::ed::LerpColor(d::ed::LerpColor(idle, on, th * 0.6f), on, ta);
        d::Text(dl, isz, ImVec2(floorf((mn.x + mx.x - ts.x) * 0.5f), floorf((mn.y + mx.y - ts.y) * 0.5f)), col, ic);
    }

    int tip = storage->GetInt(tip_idx_key, -1);
    float tip_time = storage->GetFloat(tip_time_key, 0.0f);
    if (hot != tip) {
        storage->SetInt(tip_idx_key, hot);
        tip_time = 0.0f;
        tip = hot;
    } else if (hot >= 0) {
        tip_time += d::Dt();
    }
    storage->SetFloat(tip_time_key, tip_time);
    bool show = st.tooltips && tooltips && tip >= 0 && tooltips[tip] && tooltips[tip][0] && tip_time > 0.3f;
    float ta = d::ed::Anim(tip_anim, show, 0.12f);
    if (show && ta > 0.01f) {
        const char* text = tooltips[tip];
        ImVec2 ts = ImGui::CalcTextSize(text);
        float px = fs * 0.6f, py = fs * 0.35f;
        float cy = pos.y + pad + (ih + gap) * (float)tip + ih * 0.5f;
        float x0 = rail_max.x + fs * 0.4f + (1.0f - ta) * fs * 0.6f;
        ImVec2 tmn(x0, cy - ts.y * 0.5f - py), tmx(x0 + ts.x + px * 2.0f, cy + ts.y * 0.5f + py);
        ImDrawList* fg = ImGui::GetForegroundDrawList();
        fg->AddRectFilled(tmn, tmx, d::Live(GetStyle().overlay_bg, ta), d::ed::Rounding() + 2.0f);
        d::ed::Outline(fg, tmn, tmx, d::Live(accent, 0.5f * ta), d::ed::Rounding() + 2.0f, 1.0f);
        fg->AddTriangleFilled(ImVec2(tmn.x - fs * 0.3f, cy), ImVec2(tmn.x, cy - fs * 0.3f), ImVec2(tmn.x, cy + fs * 0.3f),
                              d::Live(GetStyle().overlay_bg, ta));
        fg->AddText(ImVec2(tmn.x + px, tmn.y + py), d::ed::TextCol(ta), text);
    }
    return changed;
}

inline bool IconRailNav(const char* id, const char* icons_zero_separated, const char* tooltips_zero_separated,
                        int* index, const RailStyle& st = RailStyle()) {
    const char* icons[32] = {};
    const char* tips[32] = {};
    int n = detail::ed::SplitZero(icons_zero_separated, icons, 32);
    int nt = detail::ed::SplitZero(tooltips_zero_separated, tips, 32);
    return IconRailNav(id, icons, nt > 0 ? tips : nullptr, n, index, st);
}

// icon + title + description + switch. clicking anywhere on the card flips *enabled; with
// enabled == nullptr it behaves like a button. returns true when toggled/clicked
inline bool CategoryCard(const char* title, const char* description, const char* icon, bool* enabled,
                         const ImVec2& size = ImVec2(0, 0), ImU32 accent = 0) {
    namespace d = detail;
    const ImGuiStyle& ist = ImGui::GetStyle();
    float fs = ImGui::GetFontSize();
    float fh = ImGui::GetFrameHeight();
    ImVec2 avail = ImGui::GetContentRegionAvail();
    ImVec2 sz(d::SizeAxis(size.x, avail.x, d::ed::Max(avail.x, fs * 14.0f)), d::SizeAxis(size.y, avail.y, floorf(fh * 2.8f)));
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGuiID id = ImGui::GetID(title);
    bool pressed = ImGui::InvisibleButton(title, sz);
    bool hovered = ImGui::IsItemHovered();
    bool held = ImGui::IsItemActive();
    if (pressed && enabled) *enabled = !*enabled;
    bool on = enabled ? *enabled : false;
    float dur = ess::GetStyle().anim_duration;
    float th = d::ed::Anim(id, hovered || held, dur);
    float te = d::ed::Anim(id + 1, on, dur);
    ImU32 acc = accent ? accent : d::ed::AccentRaw();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float r = d::ed::Max(d::ed::CardRounding(), fs * 0.35f);
    float lift = (held ? 0.5f : th * 2.0f) * d::Dpi();
    ImVec2 mn(pos.x, pos.y - lift), mx(pos.x + sz.x, pos.y + sz.y - lift);
    d::ed::Shadow(dl, mn, mx, r, d::Live(IM_COL32(0, 0, 0, 90), 0.4f + 0.6f * th), fs * (0.25f + 0.35f * th));
    dl->AddRectFilled(mn, mx, d::ed::CardBg(), r);
    if (te > 0.01f)
        d::ed::Gradient(dl, mn, ImVec2(mn.x + sz.x * 0.6f, mx.y), d::Live(acc, 0.14f * te), d::Live(acc, 0.0f), false, r);
    d::ed::Outline(dl, mn, mx, d::ed::LerpColor(d::ed::BorderCol(0.8f + 0.2f * th), d::Live(acc, 0.7f), te * (0.6f + 0.4f * th)), r, 1.0f);

    float pad = ist.FramePadding.x + fs * 0.25f;
    float box = floorf(d::ed::Min(sz.y - pad * 1.2f, fh * 1.6f));
    ImVec2 bmn(mn.x + pad, mn.y + floorf((sz.y - box) * 0.5f)), bmx(bmn.x + box, bmn.y + box);
    ImU32 box_bg = d::ed::LerpColor(d::ed::Col(0, ImGuiCol_FrameBg), d::Live(acc, 0.85f), te);
    dl->AddRectFilled(bmn, bmx, box_bg, box * 0.28f);
    if (icon && icon[0]) {
        float isz = fs * 1.2f;
        ImVec2 ts = d::TextSize(isz, icon);
        ImU32 ic = te > 0.5f && d::ed::IsBright(acc) ? d::ed::DarkText() : d::ed::TextCol();
        d::Text(dl, isz, ImVec2(floorf((bmn.x + bmx.x - ts.x) * 0.5f), floorf((bmn.y + bmx.y - ts.y) * 0.5f)), ic, icon);
    }
    float sw_h = floorf(fh * 0.72f);
    float sw_w = sw_h * 1.8f;
    float right = mx.x - pad;
    if (enabled) {
        ImVec2 smn(right - sw_w, mn.y + floorf((sz.y - sw_h) * 0.5f));
        d::DrawSwitch(dl, smn, sw_h, te, acc, 1.0f);
        right = smn.x - ist.ItemInnerSpacing.x * 2.0f;
    }
    float tx = bmx.x + pad * 0.8f;
    const char* end = d::ed::LabelEnd(title);
    bool has_desc = description && description[0];
    float title_y = has_desc ? mn.y + sz.y * 0.5f - fs - fs * 0.1f : mn.y + (sz.y - fs) * 0.5f;
    ImVec4 clip(tx, mn.y, right, mx.y);
    dl->AddText(ImGui::GetFont(), fs, ImVec2(tx, title_y), d::ed::TextCol(), title, end, 0.0f, &clip);
    if (has_desc) {
        float dfs = fs * 0.85f;
        ImVec4 dclip(tx, mn.y, right, d::ed::Min(mx.y - fs * 0.2f, title_y + fs * 1.15f + dfs * 2.2f));
        dl->AddText(ImGui::GetFont(), dfs, ImVec2(tx, title_y + fs * 1.15f), d::ed::TextDim(), description, nullptr,
                    d::ed::Max(right - tx, 1.0f), &dclip);
    }
    return pressed;
}

}  // namespace game
}  // namespace imb
