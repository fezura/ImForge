#pragma once
// ProfileSlots, HotkeyList, TeamColorPicker
#include "imb_game_core.h"
#include "imb_ess_controls.h"
#include <string>

namespace imb {
namespace game {

enum ProfileAction {
    ProfileAction_None = 0,
    ProfileAction_Select = 1,
    ProfileAction_Load = 2,
    ProfileAction_Save = 3,
    ProfileAction_Delete = 4
};

struct ProfileSlotsStyle {
    float width = 0.0f;
    bool buttons = true;
    bool confirm_delete = true;
};

namespace detail {

inline bool ActionButton(const char* id, const char* text, float w, float h, ImU32 color, bool enabled,
                         float pulse = 0.0f) {
    ImVec2 mn = ImGui::GetCursorScreenPos(), mx(mn.x + w, mn.y + h);
    ImGuiID anim_id = ImGui::GetID(id);
    if (!enabled) ImGui::BeginDisabled();
    bool pressed = ImGui::InvisibleButton(id, ImVec2(w, h));
    bool hovered = ImGui::IsItemHovered();
    bool held = ImGui::IsItemActive();
    if (!enabled) ImGui::EndDisabled();
    float t = ed::Anim(anim_id, hovered || held, 0.12f);
    float a = enabled ? 1.0f : 0.35f;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float r = ed::Rounding();
    ImU32 bg = ed::LerpColor(Live(color, 0.22f * a), Live(color, 0.45f * a), held ? 1.0f : t);
    if (pulse > 0.0f) bg = ed::LerpColor(bg, Live(color, 0.8f), pulse);
    dl->AddRectFilled(mn, mx, bg, r);
    ed::Outline(dl, mn, mx, Live(color, (0.5f + 0.4f * t) * a), r, 1.0f);
    ed::TextIn(dl, mn, mx, text, nullptr, ed::TextCol(a), 0.5f);
    return pressed && enabled;
}

inline bool IsBlank(const char* s) {
    if (!s) return true;
    for (; *s; ++s)
        if (*s != ' ' && *s != '	') return false;
    return true;
}

inline int SplitNames(const char* s, const char** out, int cap) {
    int n = 0;
    while (s && *s && n < cap) {
        out[n++] = s;
        s += strlen(s) + 1;
    }
    return n;
}

}  // namespace detail

// names: one per slot, null or blank is an empty slot. *selected is the slot the buttons act on
// (null keeps it in ImGui storage),
// `active` gets the loaded marker. returns the ProfileAction of this frame (also written to *action).
// double click loads a slot. nothing here touches files, act on the returned action yourself
inline int ProfileSlots(const char* id, const char* const* names, int count, int* selected, int active = -1,
                        int* action = nullptr, const ProfileSlotsStyle& st = ProfileSlotsStyle()) {
    namespace d = detail;
    if (action) *action = ProfileAction_None;
    if (!names || count <= 0) return ProfileAction_None;
    const ImGuiStyle& ist = ImGui::GetStyle();
    float fs = ImGui::GetFontSize();
    float fh = ImGui::GetFrameHeight();
    float w = st.width > 0.0f ? st.width : d::ed::Max(ImGui::GetContentRegionAvail().x, fs * 8.0f);
    float rh = floorf(fh * 1.35f);
    float gap = floorf(3.0f * d::Dpi());
    int result = ProfileAction_None;
    ImGui::PushID(id);
    ImGuiStorage* storage = ImGui::GetStateStorage();
    ImGuiID confirm_key = ImGui::GetID("##confirm"), local_key = ImGui::GetID("##sel");
    int local_sel = storage->GetInt(local_key, -1);
    int* sel = selected ? selected : &local_sel;
    ImGui::BeginGroup();
    ImVec2 pos = ImGui::GetCursorScreenPos();
    float list_h = rh * (float)count + gap * (float)(count - 1);
    bool pressed = ImGui::InvisibleButton("##slots", ImVec2(w, list_h));
    IMB_GAME_ITEM_HOOK("profiles.list");
    bool hovered = ImGui::IsItemHovered();
    int hot = -1;
    if (hovered) {
        int i = (int)((ImGui::GetMousePos().y - pos.y) / (rh + gap));
        if (i >= 0 && i < count) hot = i;
    }
    if (pressed && hot >= 0 && hot != *sel) {
        *sel = hot;
        result = ProfileAction_Select;
        storage->SetFloat(confirm_key, 0.0f);
    }
    int cur = *sel;
    bool cur_used = cur >= 0 && cur < count && !d::IsBlank(names[cur]);
    if (hovered && hot >= 0 && hot == cur && cur_used && ImGui::IsMouseDoubleClicked(0)) result = ProfileAction_Load;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    float r = d::ed::Rounding();
    float dur = ess::GetStyle().anim_duration;
    ImU32 accent = d::ed::AccentRaw();
    ImGuiID base = ImGui::GetID("##row");
    float iy = d::ed::AnimTo(ImGui::GetID("##iy"), pos.y + (rh + gap) * (float)(cur < 0 ? 0 : cur), dur);
    for (int i = 0; i < count; ++i) {
        ImVec2 a(pos.x, pos.y + (rh + gap) * (float)i), b(pos.x + w, a.y + rh);
        bool used = !d::IsBlank(names[i]);
        float th = d::ed::Anim(base + (ImGuiID)i, i == hot, dur);
        float ts = d::ed::Anim(base + (ImGuiID)(count + i), i == cur, dur);
        ImU32 bg = d::ed::Col(0, ImGuiCol_FrameBg, 0.7f + 0.3f * th);
        bg = d::ed::LerpColor(bg, d::Live(accent, 0.22f), ts);
        dl->AddRectFilled(a, b, bg, r);
        ImU32 num_col = used ? d::Live(accent, 0.35f + 0.65f * ts) : d::ed::TextDim(0.5f);
        float cr = rh * 0.28f;
        ImVec2 cc(a.x + ist.FramePadding.x + cr, (a.y + b.y) * 0.5f);
        dl->AddCircleFilled(cc, cr, d::ed::WithAlpha(num_col, 0.3f), 20);
        dl->AddCircle(cc, cr, num_col, 20, 1.0f);
        char num[16];
        std::snprintf(num, sizeof(num), "%d", i + 1);
        float nfs = fs * 0.8f;
        ImVec2 ns = d::TextSize(nfs, num);
        d::Text(dl, nfs, ImVec2(cc.x - ns.x * 0.5f, cc.y - ns.y * 0.5f), d::ed::TextCol(), num);
        float tx = cc.x + cr + ist.ItemInnerSpacing.x * 1.5f;
        float right = b.x - ist.FramePadding.x;
        if (i == active && used) {
            const char* tag = "ACTIVE";
            float tfs = fs * 0.7f;
            ImVec2 tsz = d::TextSize(tfs, tag);
            ImVec2 pm(right - tsz.x - fs * 0.8f, (a.y + b.y - tsz.y) * 0.5f - 2.0f);
            ImVec2 px(right, pm.y + tsz.y + 4.0f);
            float glow = 0.5f + 0.5f * sinf(d::Time() * 3.0f);
            dl->AddRectFilled(pm, px, d::Live(GetStyle().good, 0.18f + 0.1f * glow), (px.y - pm.y) * 0.5f);
            d::Text(dl, tfs, ImVec2(pm.x + fs * 0.4f, pm.y + 2.0f), d::Live(GetStyle().good), tag);
            right = pm.x - ist.ItemInnerSpacing.x;
        }
        if (used) d::ed::TextIn(dl, ImVec2(tx, a.y), ImVec2(right, b.y), names[i], nullptr, d::ed::TextCol(), 0.0f);
        else d::ed::TextIn(dl, ImVec2(tx, a.y), ImVec2(right, b.y), "Empty slot", nullptr, d::ed::TextDim(0.8f), 0.0f);
    }
    if (cur >= 0 && cur < count) {
        float bar = d::ed::Max(3.0f, fs * 0.2f);
        dl->AddRectFilled(ImVec2(pos.x, iy + rh * 0.2f), ImVec2(pos.x + bar, iy + rh * 0.8f), d::Live(accent), bar * 0.5f);
    }
    if (st.buttons) {
        float bw = floorf((w - ist.ItemInnerSpacing.x * 2.0f) / 3.0f);
        float now = d::Time();
        float until = storage->GetFloat(confirm_key, 0.0f);
        bool confirming = st.confirm_delete && until > now;
        bool has_sel = cur >= 0 && cur < count;
        if (d::ActionButton("##load", "Load", bw, fh, accent, cur_used)) result = ProfileAction_Load;
        IMB_GAME_ITEM_HOOK("profiles.load");
        ImGui::SameLine(0.0f, ist.ItemInnerSpacing.x);
        if (d::ActionButton("##save", "Save", bw, fh, GetStyle().good, has_sel)) result = ProfileAction_Save;
        IMB_GAME_ITEM_HOOK("profiles.save");
        ImGui::SameLine(0.0f, ist.ItemInnerSpacing.x);
        float pulse = confirming ? 0.25f + 0.25f * sinf(now * 10.0f) : 0.0f;
        if (d::ActionButton("##delete", confirming ? "Sure?" : "Delete", w - bw * 2.0f - ist.ItemInnerSpacing.x * 2.0f,
                            fh, GetStyle().bad, cur_used, pulse)) {
            if (!st.confirm_delete || confirming) {
                result = ProfileAction_Delete;
                storage->SetFloat(confirm_key, 0.0f);
            } else {
                storage->SetFloat(confirm_key, now + 2.5f);
            }
        }
        IMB_GAME_ITEM_HOOK("profiles.delete");
    }
    ImGui::EndGroup();
    if (!selected) storage->SetInt(local_key, local_sel);
    ImGui::PopID();
    if (action) *action = result;
    return result;
}

inline int ProfileSlots(const char* id, const char* names_zero_separated, int* selected, int active = -1,
                        int* action = nullptr, const ProfileSlotsStyle& st = ProfileSlotsStyle()) {
    const char* items[32] = {};
    int n = detail::SplitNames(names_zero_separated, items, 32);
    return ProfileSlots(id, items, n, selected, active, action, st);
}

// names from a std::string array (a bound std::vector's data()) when given, else the zero
// separated list. this is the shape generated code calls
inline int ProfileSlotsList(const char* id, const std::string* names, int count, const char* fallback_zero_separated,
                            int* selected, int active = -1, int* action = nullptr) {
    if (!names) return ProfileSlots(id, fallback_zero_separated ? fallback_zero_separated : "", selected, active, action);
    const char* items[32] = {};
    int n = count < 32 ? count : 32;
    for (int i = 0; i < n; ++i) items[i] = names[i].c_str();
    return ProfileSlots(id, items, n, selected, active, action);
}

struct HotkeyListStyle {
    float width = 0.0f;
    float badge_width = 0.0f;
    bool zebra = true;
    bool show_conflicts = true;
};

// action name + keybind badge per row, click a badge to rebind (Escape clears).
// keys use the imb::ess::KeyName encoding. returns true when a key changed, the row goes to *changed_index
inline bool HotkeyList(const char* id, const char* const* actions, int* keys, int count, int* changed_index = nullptr,
                       const HotkeyListStyle& st = HotkeyListStyle()) {
    namespace d = detail;
    if (!actions || !keys || count <= 0) return false;
    const ImGuiStyle& ist = ImGui::GetStyle();
    float fs = ImGui::GetFontSize();
    float fh = ImGui::GetFrameHeight();
    float w = st.width > 0.0f ? st.width : d::ed::Max(ImGui::GetContentRegionAvail().x, fs * 10.0f);
    float bw = st.badge_width > 0.0f ? st.badge_width : ImGui::CalcTextSize("[press a key]").x + fs;
    float rh = floorf(fh * 1.3f);
    bool changed = false;
    ImGui::PushID(id);
    ImGui::BeginGroup();
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ist.ItemSpacing.x, floorf(2.0f * d::Dpi())));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float r = d::ed::Rounding();
    ImGuiID base = ImGui::GetID("##hk");
    bool win_hovered = ImGui::IsWindowHovered();
    for (int i = 0; i < count; ++i) {
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImVec2 a = p, b(p.x + w, p.y + rh);
        bool hot = win_hovered && ImGui::IsMouseHoveringRect(a, b);
        float th = d::ed::Anim(base + (ImGuiID)i, hot, 0.12f);
        ImU32 bg = d::ed::Col(0, ImGuiCol_FrameBg, (st.zebra && (i & 1) ? 0.35f : 0.6f) + 0.35f * th);
        dl->AddRectFilled(a, b, bg, r);
        bool conflict = false;
        if (st.show_conflicts && keys[i] != 0)
            for (int j = 0; j < count && !conflict; ++j)
                if (j != i && keys[j] == keys[i]) conflict = true;
        ess::KeybindStyle ks;
        ks.size = ImVec2(bw, fh);
        ks.show_label = false;
        if (conflict) ks.bg = d::ed::WithAlpha(GetStyle().warn, 0.4f);
        ImVec2 bp(b.x - ist.FramePadding.x * 0.5f - bw, p.y + floorf((rh - fh) * 0.5f));
        ImGui::SetCursorScreenPos(bp);
        ImGui::PushID(i);
        if (ess::KeybindPicker("##key", &keys[i], ks)) {
            changed = true;
            if (changed_index) *changed_index = i;
        }
        IMB_GAME_ITEM_HOOK("hotkeys.badge");
        ImGui::PopID();
        float tx = a.x + ist.FramePadding.x;
        if (conflict) {
            float cr = fs * 0.3f;
            ImVec2 cc(bp.x - cr - ist.ItemInnerSpacing.x, (a.y + b.y) * 0.5f);
            dl->AddCircleFilled(cc, cr, d::Live(GetStyle().warn), 16);
            ImVec2 ts = d::TextSize(fs * 0.7f, "!");
            d::Text(dl, fs * 0.7f, ImVec2(cc.x - ts.x * 0.5f, cc.y - ts.y * 0.5f), d::ed::DarkText(), "!");
        }
        float text_right = bp.x - ist.ItemInnerSpacing.x - (conflict ? fs : 0.0f);
        d::ed::TextIn(dl, ImVec2(tx, a.y), ImVec2(text_right, b.y), actions[i] ? actions[i] : "", nullptr,
                      d::ed::TextCol(0.8f + 0.2f * th), 0.0f);
        ImGui::SetCursorScreenPos(p);
        ImGui::Dummy(ImVec2(w, rh));
    }
    ImGui::PopStyleVar();
    ImGui::EndGroup();
    ImGui::PopID();
    return changed;
}

// zero separated action names. count <= 0 takes one row per name; keys == nullptr keeps the
// keys in ImGui storage
inline bool HotkeyList(const char* id, const char* actions_zero_separated, int* keys, int count,
                       int* changed_index = nullptr, const HotkeyListStyle& st = HotkeyListStyle()) {
    const char* items[64] = {};
    int n = detail::SplitNames(actions_zero_separated, items, 64);
    if (count > 0 && count < n) n = count;
    if (keys) return HotkeyList(id, items, keys, n, changed_index, st);
    int local[64];
    ImGui::PushID(id);
    ImGuiID base = ImGui::GetID("##keys");
    ImGui::PopID();
    ImGuiStorage* s = ImGui::GetStateStorage();
    for (int i = 0; i < n; ++i) local[i] = s->GetInt(base + (ImGuiID)i, 0);
    bool changed = HotkeyList(id, items, local, n, changed_index, st);
    for (int i = 0; i < n; ++i) s->SetInt(base + (ImGuiID)i, local[i]);
    return changed;
}

struct TeamPreset {
    const char* name;
    ImU32 a, b;
};

inline const TeamPreset* TeamPresets(int* count) {
    static const TeamPreset kPresets[] = {
        {"Classic", IM_COL32(74, 163, 255, 255), IM_COL32(255, 86, 86, 255)},
        {"Colorblind", IM_COL32(0, 114, 178, 255), IM_COL32(230, 159, 0, 255)},
        {"Toxic", IM_COL32(124, 252, 120, 255), IM_COL32(190, 90, 255, 255)},
        {"Ice", IM_COL32(120, 230, 255, 255), IM_COL32(255, 150, 60, 255)},
        {"Mono", IM_COL32(235, 235, 235, 255), IM_COL32(255, 210, 60, 255)},
    };
    if (count) *count = IM_ARRAYSIZE(kPresets);
    return kPresets;
}

namespace detail {

inline void Checker(ImDrawList* dl, const ImVec2& mn, const ImVec2& mx, float r) {
    ImVec2 mid((mn.x + mx.x) * 0.5f, (mn.y + mx.y) * 0.5f);
    dl->AddRectFilled(mn, mx, Live(IM_COL32(60, 60, 60, 255)), r);
    dl->AddRectFilled(mn, mid, Live(IM_COL32(110, 110, 110, 255)), r, ed::kRoundTopLeft);
    dl->AddRectFilled(mid, mx, Live(IM_COL32(110, 110, 110, 255)), r, ed::kRoundBottomRight);
}

inline void TeamSwatch(ImDrawList* dl, const ImVec2& mn, const ImVec2& mx, const ImVec4& c, const char* name,
                       float hover) {
    float r = ed::Max(ed::Rounding(), 4.0f);
    float lift = hover * 2.0f * Dpi();
    ImVec2 a(mn.x, mn.y - lift), b(mx.x, mx.y - lift);
    ImU32 col = ImGui::GetColorU32(c);
    if (hover > 0.01f) ed::Glow(dl, a, b, r, ed::WithAlpha(col, 0.5f * hover), ed::GlowSize() * hover);
    if (c.w < 1.0f) Checker(dl, a, b, r);
    dl->AddRectFilled(a, b, col, r);
    ed::Gradient(dl, a, ImVec2(b.x, (a.y + b.y) * 0.5f), Live(IM_COL32(255, 255, 255, 40)),
                 Live(IM_COL32(255, 255, 255, 0)), true, 0.0f);
    ed::Outline(dl, a, b, Live(IM_COL32(255, 255, 255, (int)(40.0f + 80.0f * hover))), r, 1.0f);
    ImU32 tc = ed::IsBright(U32(c)) ? ed::DarkText() : ImGui::GetColorU32(IM_COL32_WHITE);
    float fs = ImGui::GetFontSize();
    char hex[32];
    std::snprintf(hex, sizeof(hex), "#%02X%02X%02X", SafeInt(Fin01(c.x) * 255.0f + 0.5f), SafeInt(Fin01(c.y) * 255.0f + 0.5f),
                  SafeInt(Fin01(c.z) * 255.0f + 0.5f));
    float pad = fs * 0.5f;
    ed::TextIn(dl, ImVec2(a.x + pad, a.y), ImVec2(b.x - pad, (a.y + b.y) * 0.5f + fs * 0.3f), name, nullptr, tc, 0.0f);
    ImVec2 hs = TextSize(fs * 0.75f, hex);
    Text(dl, fs * 0.75f, ImVec2(a.x + pad, b.y - hs.y - fs * 0.25f), ed::WithAlpha(tc, 0.75f), hex);
}

inline void SwapIcon(ImDrawList* dl, const ImVec2& c, float s, ImU32 col) {
    float th = ed::Max(1.5f, s * 0.14f);
    dl->AddLine(ImVec2(c.x - s, c.y - s * 0.35f), ImVec2(c.x + s * 0.7f, c.y - s * 0.35f), col, th);
    dl->AddTriangleFilled(ImVec2(c.x + s, c.y - s * 0.35f), ImVec2(c.x + s * 0.5f, c.y - s * 0.75f),
                          ImVec2(c.x + s * 0.5f, c.y + s * 0.05f), col);
    dl->AddLine(ImVec2(c.x + s, c.y + s * 0.35f), ImVec2(c.x - s * 0.7f, c.y + s * 0.35f), col, th);
    dl->AddTriangleFilled(ImVec2(c.x - s, c.y + s * 0.35f), ImVec2(c.x - s * 0.5f, c.y - s * 0.05f),
                          ImVec2(c.x - s * 0.5f, c.y + s * 0.75f), col);
}

inline bool SameColor(const ImVec4& c, ImU32 u) {
    return (ImGui::ColorConvertFloat4ToU32(c) | IM_COL32_A_MASK) == (u | IM_COL32_A_MASK);
}

}  // namespace detail

// two team swatches (click one to pick), a swap button and preset pairs. null colors are kept
// in ImGui storage. returns true on change
inline bool TeamColorPicker(const char* label, ImVec4* team_a, ImVec4* team_b, const char* name_a = "Team",
                            const char* name_b = "Enemy", bool presets = true) {
    namespace d = detail;
    const ImGuiStyle& ist = ImGui::GetStyle();
    float fs = ImGui::GetFontSize();
    float fh = ImGui::GetFrameHeight();
    const char* end = d::ed::LabelEnd(label);
    bool changed = false;
    ImGui::PushID(label);
    ImGuiStorage* storage = ImGui::GetStateStorage();
    ImGuiID local_key = ImGui::GetID("##local");
    ImVec4 local_a = ImGui::ColorConvertU32ToFloat4(GetStyle().team[0]);
    ImVec4 local_b = ImGui::ColorConvertU32ToFloat4(GetStyle().team[1]);
    if (!team_a) d::LoadVec4(storage, local_key, local_a);
    if (!team_b) d::LoadVec4(storage, local_key + 4, local_b);
    ImVec4* in_a = team_a;
    ImVec4* in_b = team_b;
    if (!team_a) team_a = &local_a;
    if (!team_b) team_b = &local_b;
    ImGui::BeginGroup();
    if (end > label) ImGui::TextUnformatted(label, end);
    float avail = d::ed::Max(ImGui::GetContentRegionAvail().x, fs * 12.0f);
    float swap_w = floorf(fh * 1.2f);
    float sw = d::ed::Min(floorf((avail - swap_w - ist.ItemInnerSpacing.x * 2.0f) * 0.5f), fs * 10.0f);
    float sh = floorf(fh * 1.9f);
    ImGuiID swap_key = ImGui::GetID("##swap_t");
    float now = d::Time();
    float since = now - storage->GetFloat(swap_key, -10.0f);
    float e = since >= 0.0f ? d::ed::Clamp01(since / 0.28f) : 1.0f;
    e = 1.0f - (1.0f - e) * (1.0f - e) * (1.0f - e);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 pa = ImGui::GetCursorScreenPos();
    ImVec2 pb(pa.x + sw + swap_w + ist.ItemInnerSpacing.x * 2.0f, pa.y);
    float travel = pb.x - pa.x;

    bool click_a = ImGui::InvisibleButton("##a", ImVec2(sw, sh));
    IMB_GAME_ITEM_HOOK("team.a");
    float ha = d::ed::Anim(ImGui::GetID("##a"), ImGui::IsItemHovered(), 0.15f);
    ImGui::SameLine(0.0f, ist.ItemInnerSpacing.x);
    ImVec2 sp = ImGui::GetCursorScreenPos();
    sp.y += floorf((sh - swap_w) * 0.5f);
    ImGui::SetCursorScreenPos(sp);
    bool swap = ImGui::InvisibleButton("##swap", ImVec2(swap_w, swap_w));
    IMB_GAME_ITEM_HOOK("team.swap");
    float hs = d::ed::Anim(ImGui::GetID("##swap"), ImGui::IsItemHovered(), 0.15f);
    ImGui::SameLine(0.0f, ist.ItemInnerSpacing.x);
    ImGui::SetCursorScreenPos(pb);
    bool click_b = ImGui::InvisibleButton("##b", ImVec2(sw, sh));
    IMB_GAME_ITEM_HOOK("team.b");
    float hb = d::ed::Anim(ImGui::GetID("##b"), ImGui::IsItemHovered(), 0.15f);

    if (swap) {
        ImVec4 tmp = *team_a;
        *team_a = *team_b;
        *team_b = tmp;
        storage->SetFloat(swap_key, now);
        changed = true;
        e = 0.0f;
    }
    float off = (1.0f - e) * travel;
    d::TeamSwatch(dl, ImVec2(pa.x + off, pa.y), ImVec2(pa.x + off + sw, pa.y + sh), *team_a, name_a ? name_a : "", ha);
    d::TeamSwatch(dl, ImVec2(pb.x - off, pb.y), ImVec2(pb.x - off + sw, pb.y + sh), *team_b, name_b ? name_b : "", hb);
    ImVec2 smn = sp, smx(sp.x + swap_w, sp.y + swap_w);
    dl->AddRectFilled(smn, smx, d::ed::Col(0, ImGuiCol_FrameBg, 0.7f + 0.3f * hs), swap_w * 0.5f);
    d::SwapIcon(dl, ImVec2((smn.x + smx.x) * 0.5f, (smn.y + smx.y) * 0.5f), swap_w * 0.28f,
                d::ed::LerpColor(d::ed::TextDim(), d::ed::Accent(), hs));

    if (click_a) ImGui::OpenPopup("##pick_a");
    if (click_b) ImGui::OpenPopup("##pick_b");
    ImGuiColorEditFlags pf = ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_NoSidePreview;
    if (ImGui::BeginPopup("##pick_a")) {
        changed |= ImGui::ColorPicker4("##pa", &team_a->x, pf);
        ImGui::EndPopup();
    }
    if (ImGui::BeginPopup("##pick_b")) {
        changed |= ImGui::ColorPicker4("##pb", &team_b->x, pf);
        ImGui::EndPopup();
    }

    float dr = team_a->x - team_b->x, dg = team_a->y - team_b->y, db = team_a->z - team_b->z;
    if (dr * dr + dg * dg + db * db < 0.03f) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(GetStyle().warn));
        ImGui::TextUnformatted("These two are hard to tell apart");
        ImGui::PopStyleColor();
    }
    if (presets) {
        int n = 0;
        const TeamPreset* list = TeamPresets(&n);
        float ps = floorf(fh * 1.1f);
        for (int i = 0; i < n; ++i) {
            if (i > 0) ImGui::SameLine(0.0f, ist.ItemInnerSpacing.x);
            ImVec2 mn = ImGui::GetCursorScreenPos(), mx(mn.x + ps * 1.6f, mn.y + ps);
            ImGui::PushID(i);
            bool pressed = ImGui::InvisibleButton("##preset", ImVec2(ps * 1.6f, ps));
            IMB_GAME_ITEM_HOOK("team.preset");
            bool hovered = ImGui::IsItemHovered();
            float t = d::ed::Anim(ImGui::GetID("##preset"), hovered, 0.12f);
            ImGui::PopID();
            if (hovered) ImGui::SetTooltip("%s", list[i].name);
            if (pressed) {
                *team_a = ImGui::ColorConvertU32ToFloat4(list[i].a);
                *team_b = ImGui::ColorConvertU32ToFloat4(list[i].b);
                storage->SetFloat(swap_key, -10.0f);
                changed = true;
            }
            float rr = d::ed::Max(d::ed::Rounding(), 3.0f);
            float mid = (mn.x + mx.x) * 0.5f;
            float lift = t * 1.5f;
            dl->AddRectFilled(ImVec2(mn.x, mn.y - lift), ImVec2(mid, mx.y - lift), d::Live(list[i].a), rr, d::ed::kRoundLeft);
            dl->AddRectFilled(ImVec2(mid, mn.y - lift), ImVec2(mx.x, mx.y - lift), d::Live(list[i].b), rr,
                              d::kRoundRight);
            bool on = d::SameColor(*team_a, list[i].a) && d::SameColor(*team_b, list[i].b);
            if (on) d::ed::Outline(dl, ImVec2(mn.x - 2, mn.y - 2 - lift), ImVec2(mx.x + 2, mx.y + 2 - lift), d::ed::Accent(), rr + 2.0f, 2.0f);
            else d::ed::Outline(dl, ImVec2(mn.x, mn.y - lift), ImVec2(mx.x, mx.y - lift), d::ed::BorderCol(0.4f + 0.6f * t), rr, 1.0f);
        }
    }
    ImGui::EndGroup();
    if (!in_a) d::StoreVec4(storage, local_key, local_a);
    if (!in_b) d::StoreVec4(storage, local_key + 4, local_b);
    ImGui::PopID();
    return changed;
}

}  // namespace game
}  // namespace imb
