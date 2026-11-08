#pragma once
// SegmentedControl, PillTabs, ToggleButtonGroup, Chips
#include <cstddef>
#include "imb_ctl_core.h"

namespace imb {
namespace ctl {

struct SegmentedStyle {
    float width = 0.0f;
    float height = 0.0f;
    float rounding = -1.0f;
    ImU32 bg = 0;
    ImU32 indicator = 0;
    ImU32 text = 0;
    ImU32 text_active = 0;
    bool show_label = true;
};

// width: 0 sizes every segment to the widest item, < 0 fills the available width
inline bool SegmentedControl(const char* label, int* index, const char* const* items, int count,
                             const SegmentedStyle& ss = SegmentedStyle()) {
    if (!items || count <= 0) return false;
    if (count > detail::kMaxItems) count = detail::kMaxItems;
    int fallback = 0;
    if (!index) index = &fallback;
    const ImGuiStyle& st = ImGui::GetStyle();
    float dpi = detail::Dpi();
    float h = ss.height > 0.0f ? ss.height : ImGui::GetFrameHeight();
    const char* lend = detail::LabelEnd(label);
    float label_w = ss.show_label ? detail::LabelWidth(label, lend) : 0.0f;
    float inset = detail::Max(2.0f, 2.0f * dpi);
    float pad = st.FramePadding.x * 1.6f, seg = h;
    for (int i = 0; i < count; ++i) seg = detail::Max(seg, ImGui::CalcTextSize(items[i] ? items[i] : "").x + pad * 2.0f);
    float w = ss.width > 0.0f ? ss.width : seg * (float)count + inset * 2.0f;
    if (ss.width < 0.0f) w = detail::Max(ImGui::GetContentRegionAvail().x + ss.width + 1.0f - label_w, h * (float)count);
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGuiID id = ImGui::GetID(label);
    bool pressed = ImGui::InvisibleButton(label, ImVec2(w + label_w, h), detail::kNav);
    bool hovered = ImGui::IsItemHovered(), held = ImGui::IsItemActive();
    float inner = w - inset * 2.0f, sw = inner / (float)count;
    int under = -1;
    ImVec2 m = ImGui::GetMousePos();
    if ((hovered || held) && m.x >= pos.x + inset && m.x < pos.x + inset + inner)
        under = detail::ClampI((int)((m.x - pos.x - inset) / sw), 0, count - 1);
    int target = -1;
    if (pressed) {
        if (under >= 0) target = under;
        else if (!ImGui::IsMouseHoveringRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax()))
            target = (*index + 1) % count;
    }
    int d = detail::FocusedArrows(true, false);
    if (d) target = detail::ClampI(*index + d, 0, count - 1);
    bool changed = false;
    if (target >= 0 && target != *index) {
        *index = target;
        changed = true;
    }
    int cur = detail::ClampI(*index, 0, count - 1);
    float fi = detail::SpringTo(id, (float)cur);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    float r = detail::Rounding(h, ss.rounding);
    ImVec2 mx(pos.x + w, pos.y + h);
    detail::Frame(dl, pos, mx, detail::Final(detail::Pick(ss.bg, ImGuiCol_FrameBg)), r);
    float ri = detail::Max(0.0f, r - inset * 0.5f);
    ImU32 text_raw = ss.text ? ss.text : detail::Mix(detail::TextRaw(), detail::DimRaw(), 0.35f);
    for (int i = 0; i < count; ++i) {
        float ht = detail::AnimTo(detail::Sub(id, 100 + i), (i == under && i != cur) ? (held ? 1.0f : 0.6f) : 0.0f);
        if (ht > 0.01f) {
            ImVec2 a(pos.x + inset + sw * (float)i, pos.y + inset), b(a.x + sw, mx.y - inset);
            dl->AddRectFilled(a, b, detail::Final(detail::StyleRaw(ImGuiCol_FrameBgHovered), ht), ri);
        }
        if (i > 0) {
            float k = detail::Min(detail::Abs((float)i - fi), detail::Abs((float)i - fi - 1.0f));
            float x = pos.x + inset + sw * (float)i;
            if (k > 0.05f)
                dl->AddLine(ImVec2(x, pos.y + h * 0.28f), ImVec2(x, mx.y - h * 0.28f),
                            detail::Final(detail::BorderRaw(), detail::Sat(k) * 0.8f), detail::Max(1.0f, dpi));
        }
    }
    ImVec2 ta(pos.x + inset + fi * sw, pos.y + inset), tb(ta.x + sw, mx.y - inset);
    ImU32 ind = detail::Pick(ss.indicator, ImGuiCol_CheckMark);
    if (ss.indicator == 0 && GetStyle().accent) ind = GetStyle().accent;
    detail::Shadow(dl, ta, tb, ri, 0.8f);
    dl->AddRectFilled(ta, tb, detail::Final(ind), ri);
    ImU32 on_raw = ss.text_active ? ss.text_active : detail::OnRaw(ind);
    for (int i = 0; i < count; ++i) {
        float k = detail::Sat(1.0f - detail::Abs((float)i - fi));
        ImVec2 a(pos.x + inset + sw * (float)i, pos.y), b(a.x + sw, mx.y);
        detail::TextIn(dl, a, b, items[i], nullptr, detail::Final(detail::Mix(text_raw, on_raw, k)), 0.5f,
                       st.FramePadding.x * 0.5f);
    }
    if (label_w > 0.0f)
        dl->AddText(ImVec2(mx.x + st.ItemInnerSpacing.x, pos.y + (h - ImGui::GetFontSize()) * 0.5f),
                    detail::Final(detail::TextRaw()), label, lend);
    return changed;
}

inline bool SegmentedControl(const char* label, int* index, const char* const* items, int count, float width) {
    SegmentedStyle ss;
    ss.width = width;
    return SegmentedControl(label, index, items, count, ss);
}

inline bool SegmentedControl(const char* label, int* index, const char* items_zero_separated,
                             const SegmentedStyle& ss = SegmentedStyle()) {
    const char* items[detail::kMaxItems];
    int n = detail::SplitZero(items_zero_separated, items, detail::kMaxItems);
    return SegmentedControl(label, index, items, n, ss);
}

struct PillTabsStyle {
    float height = 0.0f;
    float spacing = -1.0f;
    ImU32 pill = 0;
    ImU32 hover = 0;
    ImU32 text = 0;
    ImU32 text_active = 0;
    bool outline = false;
};

inline bool PillTabs(const char* id_label, int* index, const char* const* items, int count,
                     const PillTabsStyle& ps = PillTabsStyle()) {
    if (!items || count <= 0) return false;
    if (count > detail::kMaxItems) count = detail::kMaxItems;
    int fallback = 0;
    if (!index) index = &fallback;
    const ImGuiStyle& st = ImGui::GetStyle();
    float h = ps.height > 0.0f ? ps.height : ImGui::GetFrameHeight();
    float gap = ps.spacing >= 0.0f ? ps.spacing : st.ItemSpacing.x * 0.5f;
    float pad = h * 0.6f;
    float xs[detail::kMaxItems], ws[detail::kMaxItems];
    float x = 0.0f;
    for (int i = 0; i < count; ++i) {
        xs[i] = x;
        ws[i] = ImGui::CalcTextSize(items[i] ? items[i] : "").x + pad * 2.0f;
        x += ws[i] + (i + 1 < count ? gap : 0.0f);
    }
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGuiID id = ImGui::GetID(id_label);
    bool pressed = ImGui::InvisibleButton(id_label, ImVec2(detail::Max(x, 1.0f), h), detail::kNav);
    bool hovered = ImGui::IsItemHovered(), held = ImGui::IsItemActive();
    int under = -1;
    float mrel = ImGui::GetMousePos().x - pos.x;
    if (hovered || held)
        for (int i = 0; i < count; ++i)
            if (mrel >= xs[i] && mrel < xs[i] + ws[i]) under = i;
    int target = -1;
    if (pressed) {
        if (under >= 0) target = under;
        else if (!ImGui::IsMouseHoveringRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax()))
            target = (*index + 1) % count;
    }
    int d = detail::FocusedArrows(true, false);
    if (d) target = detail::ClampI(*index + d, 0, count - 1);
    bool changed = false;
    if (target >= 0 && target != *index) {
        *index = target;
        changed = true;
    }
    int cur = detail::ClampI(*index, 0, count - 1);
    float fi = detail::Clamp(detail::SpringTo(id, (float)cur), 0.0f, (float)(count - 1));
    int i0 = (int)fi, i1 = detail::ClampI(i0 + 1, 0, count - 1);
    float fr = fi - (float)i0;
    float px = detail::Lerp(xs[i0], xs[i1], fr), pw = detail::Lerp(ws[i0], ws[i1], fr);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    float r = h * 0.5f;
    ImU32 pill = ps.pill ? ps.pill : detail::AccentRaw();
    ImU32 text_raw = ps.text ? ps.text : detail::Mix(detail::TextRaw(), detail::DimRaw(), 0.4f);
    ImU32 on_raw = ps.text_active ? ps.text_active : detail::OnRaw(pill);
    ImU32 hover_raw = ps.hover ? ps.hover : detail::StyleRaw(ImGuiCol_FrameBgHovered);
    for (int i = 0; i < count; ++i) {
        ImVec2 a(pos.x + xs[i], pos.y), b(a.x + ws[i], pos.y + h);
        float ht = detail::AnimTo(detail::Sub(id, 100 + i), (i == under && i != cur) ? 1.0f : 0.0f);
        if (ht > 0.01f) dl->AddRectFilled(a, b, detail::Final(hover_raw, ht), r);
        if (ps.outline) detail::Rect(dl, a, b, detail::Final(detail::BorderRaw()), r, 1.0f);
    }
    ImVec2 pa(pos.x + px, pos.y), pb(pa.x + pw, pos.y + h);
    detail::Shadow(dl, pa, pb, r, 0.6f);
    dl->AddRectFilled(pa, pb, detail::Final(pill), r);
    for (int i = 0; i < count; ++i) {
        float k = detail::Sat(1.0f - detail::Abs((float)i - fi));
        ImVec2 a(pos.x + xs[i], pos.y), b(a.x + ws[i], pos.y + h);
        detail::TextIn(dl, a, b, items[i], nullptr, detail::Final(detail::Mix(text_raw, on_raw, k)));
    }
    return changed;
}

inline bool PillTabs(const char* id_label, int* index, const char* items_zero_separated,
                     const PillTabsStyle& ps = PillTabsStyle()) {
    const char* items[detail::kMaxItems];
    int n = detail::SplitZero(items_zero_separated, items, detail::kMaxItems);
    return PillTabs(id_label, index, items, n, ps);
}

struct ToggleGroupStyle {
    float height = 0.0f;
    float min_width = 0.0f;
    ImU32 bg = 0;
    ImU32 on = 0;
    ImU32 text = 0;
    bool show_label = true;
};

// multi select: every button flips its own bool
inline bool ToggleButtonGroup(const char* label, bool* values, const char* const* items, int count,
                              const ToggleGroupStyle& ts = ToggleGroupStyle()) {
    if (!items || !values || count <= 0) return false;
    if (count > detail::kMaxItems) count = detail::kMaxItems;
    const ImGuiStyle& st = ImGui::GetStyle();
    float h = ts.height > 0.0f ? ts.height : ImGui::GetFrameHeight();
    float r = detail::Rounding(h);
    float dpi = detail::Dpi();
    const char* lend = detail::LabelEnd(label);
    ImGuiID id = ImGui::GetID(label);
    ImGui::PushID(label);
    ImGui::BeginGroup();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImU32 on = ts.on ? ts.on : detail::AccentRaw();
    ImU32 bg = detail::Pick(ts.bg, ImGuiCol_FrameBg);
    ImU32 text = ts.text ? ts.text : detail::TextRaw();
    bool changed = false;
    ImVec2 gmin = ImGui::GetCursorScreenPos(), gmax = gmin;
    for (int i = 0; i < count; ++i) {
        const char* t = items[i] ? items[i] : "";
        float w = detail::Max(ts.min_width, ImGui::CalcTextSize(t).x + st.FramePadding.x * 2.6f);
        if (i > 0) ImGui::SameLine(0.0f, 0.0f);
        ImVec2 a = ImGui::GetCursorScreenPos(), b(a.x + w, a.y + h);
        gmax = b;
        ImGui::PushID(i);
        if (ImGui::InvisibleButton("##b", ImVec2(w, h), detail::kNav)) {
            values[i] = !values[i];
            changed = true;
        }
        bool hov = ImGui::IsItemHovered(), act = ImGui::IsItemActive();
        ImGui::PopID();
        float k = detail::Anim(detail::Sub(id, i), values[i]);
        float hk = detail::AnimTo(detail::Sub(id, 200 + i), act ? 1.0f : (hov ? 0.5f : 0.0f));
        ImDrawFlags f = i == 0 ? detail::kRoundLeft : (i == count - 1 ? detail::kRoundRight : detail::kRoundNone);
        if (count == 1) f = detail::kRoundAll;
        ImU32 fill = detail::Mix(detail::Shade(bg, 0.06f * hk), detail::Mix(on, detail::Shade(on, 0.15f), hk), k);
        dl->AddRectFilled(a, b, detail::Final(fill), r, f);
        if (i > 0 && k < 0.99f && !values[i - 1])
            dl->AddLine(ImVec2(a.x, a.y + h * 0.22f), ImVec2(a.x, b.y - h * 0.22f),
                        detail::Final(detail::BorderRaw(), 0.9f), detail::Max(1.0f, dpi));
        detail::TextIn(dl, a, b, t, nullptr, detail::Final(detail::Mix(text, detail::OnRaw(on), k)));
    }
    if (st.FrameBorderSize > 0.0f) detail::Rect(dl, gmin, gmax, detail::Final(detail::BorderRaw()), r, st.FrameBorderSize);
    if (ts.show_label) detail::TrailingLabel(label, lend, h);
    ImGui::EndGroup();
    ImGui::PopID();
    return changed;
}

// same with a bit mask: bit i set means item i is on
inline bool ToggleButtonGroup(const char* label, int* mask, const char* const* items, int count,
                              const ToggleGroupStyle& ts = ToggleGroupStyle()) {
    if (!mask || count <= 0) return false;
    if (count > 31) count = 31;
    bool v[31];
    for (int i = 0; i < count; ++i) v[i] = (*mask >> i) & 1;
    if (!ToggleButtonGroup(label, v, items, count, ts)) return false;
    unsigned m = (unsigned)*mask & ~((1u << count) - 1u);
    for (int i = 0; i < count; ++i) m |= v[i] ? (1u << i) : 0u;
    *mask = (int)m;
    return true;
}

inline bool ToggleButtonGroup(const char* label, int* mask, const char* items_zero_separated,
                              const ToggleGroupStyle& ts = ToggleGroupStyle()) {
    const char* items[detail::kMaxItems];
    int n = detail::SplitZero(items_zero_separated, items, detail::kMaxItems);
    return ToggleButtonGroup(label, mask, items, n, ts);
}

struct ChipStyle {
    float height = 0.0f;
    ImU32 bg = 0;
    ImU32 selected = 0;
    ImU32 text = 0;
    bool check_icon = true;
    bool wrap = true;
};

namespace detail {

// next item on this line if it fits, else a new line; x_max is the right edge of the flow
inline void FlowNext(bool first, float w, float x_max, float gap) {
    if (first) return;
    ImGui::SameLine(0.0f, gap);
    if (ImGui::GetCursorScreenPos().x + w > x_max) ImGui::NewLine();
}

inline bool Chip(ImGuiID anim, const char* text, bool on, float h, float x_max, bool first, const ChipStyle& cs) {
    const ImGuiStyle& st = ImGui::GetStyle();
    float dpi = Dpi();
    float k = Anim(anim, on);
    float icon_w = cs.check_icon ? h * 0.55f * k : 0.0f;
    float w = TextW(text) + h * 0.8f + icon_w;
    if (cs.wrap) FlowNext(first, w, x_max, st.ItemSpacing.x * 0.6f);
    else if (!first) ImGui::SameLine(0.0f, st.ItemSpacing.x * 0.6f);
    ImVec2 a = ImGui::GetCursorScreenPos(), b(a.x + w, a.y + h);
    bool pressed = ImGui::InvisibleButton("##chip", ImVec2(w, h), kNav);
    bool hov = ImGui::IsItemHovered(), act = ImGui::IsItemActive();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImU32 sel = cs.selected ? cs.selected : AccentRaw();
    ImU32 bg = Pick(cs.bg, ImGuiCol_FrameBg);
    ImU32 fill = Mix(Shade(bg, hov ? (act ? 0.14f : 0.07f) : 0.0f), WithAlpha(sel, 0.28f), k);
    dl->AddRectFilled(a, b, Final(fill), h * 0.5f);
    Rect(dl, a, b, Final(Mix(BorderRaw(), sel, k)), h * 0.5f, Max(1.0f, dpi));
    ImU32 tc = Final(Mix(cs.text ? cs.text : TextRaw(), Shade(sel, 0.55f), k * 0.6f));
    if (icon_w > 0.5f)
        Check(dl, ImVec2(a.x + h * 0.4f + icon_w * 0.5f, a.y + h * 0.5f), h * 0.2f, Final(Shade(sel, 0.5f)),
              Max(1.5f, 1.6f * dpi), k);
    TextIn(dl, ImVec2(a.x + icon_w, a.y), b, text, nullptr, tc);
    return pressed;
}

}  // namespace detail

// toggleable chips that wrap to the available width; bit i of *mask is chip i
inline bool Chips(const char* label, int* mask, const char* const* items, int count,
                  const ChipStyle& cs = ChipStyle()) {
    if (!mask || !items || count <= 0) return false;
    if (count > 31) count = 31;
    float h = cs.height > 0.0f ? cs.height : ImGui::GetFrameHeight();
    float x_max = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
    ImGuiID id = ImGui::GetID(label);
    ImGui::PushID(label);
    ImGui::BeginGroup();
    bool changed = false;
    for (int i = 0; i < count; ++i) {
        ImGui::PushID(i);
        bool on = (*mask >> i) & 1;
        if (detail::Chip(detail::Sub(id, i), items[i] ? items[i] : "", on, h, x_max, i == 0, cs)) {
            *mask ^= 1 << i;
            changed = true;
        }
        ImGui::PopID();
    }
    ImGui::EndGroup();
    ImGui::PopID();
    return changed;
}

inline bool Chips(const char* label, bool* values, const char* const* items, int count,
                  const ChipStyle& cs = ChipStyle()) {
    if (!values || count <= 0) return false;
    if (count > 31) count = 31;
    int m = 0;
    for (int i = 0; i < count; ++i) m |= values[i] ? (1 << i) : 0;
    if (!Chips(label, &m, items, count, cs)) return false;
    for (int i = 0; i < count; ++i) values[i] = (m >> i) & 1;
    return true;
}

inline bool Chips(const char* label, int* mask, const char* items_zero_separated, const ChipStyle& cs = ChipStyle()) {
    const char* items[detail::kMaxItems];
    int n = detail::SplitZero(items_zero_separated, items, detail::kMaxItems);
    return Chips(label, mask, items, n, cs);
}

// generated code passes a bare nullptr for an unbound slot
inline bool ToggleButtonGroup(const char* label, std::nullptr_t, const char* const* items, int count,
                              const ToggleGroupStyle& ts = ToggleGroupStyle()) {
    return ToggleButtonGroup(label, static_cast<int*>(nullptr), items, count, ts);
}

inline bool Chips(const char* label, std::nullptr_t, const char* const* items, int count, const ChipStyle& cs = ChipStyle()) {
    return Chips(label, static_cast<int*>(nullptr), items, count, cs);
}

}  // namespace ctl
}  // namespace imb
