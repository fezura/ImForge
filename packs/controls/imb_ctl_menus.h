#pragma once
// SplitButton, IconDropdown, CommandPalette, FuzzyScore
#include "imb_ctl_core.h"

namespace imb {
namespace ctl {

namespace detail {

// popup under an anchor rect that fades and slides in; pair a true return with EndMenuPopup()
inline bool BeginMenuPopup(const char* str_id, ImGuiID anim, const ImVec2& a, const ImVec2& b, float min_w) {
    float dpi = Dpi();
    float t = Anim(anim, ImGui::IsPopupOpen(str_id), 0.12f);
    ImGui::SetNextWindowPos(ImVec2(a.x, b.y + (2.0f + 4.0f * (1.0f - t)) * dpi));
    ImGui::SetNextWindowSizeConstraints(ImVec2(min_w, 0.0f), ImVec2(FLT_MAX, FLT_MAX));
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * Max(t, 0.05f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4.0f * dpi, 4.0f * dpi));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, 1.0f * dpi));
    if (ImGui::BeginPopup(str_id)) return true;
    ImGui::PopStyleVar(3);
    return false;
}

inline void EndMenuPopup() {
    ImGui::EndPopup();
    ImGui::PopStyleVar(3);
}

// a list row: hover pill, optional icon column, check for the selected one, dim hint on the right
inline bool MenuRow(ImGuiID anim, const char* text, const char* icon, float icon_w, bool selected, float w,
                    const char* hint = nullptr, bool highlighted = false) {
    float dpi = Dpi();
    float h = ImGui::GetFrameHeight();
    ImVec2 a = ImGui::GetCursorScreenPos(), b(a.x + w, a.y + h);
    bool pressed = ImGui::InvisibleButton("##row", ImVec2(w, h), kNav);
    bool hot = ImGui::IsItemHovered() || ImGui::IsItemFocused() || highlighted;
    float k = Anim(anim, hot, 0.08f);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float r = Rounding(h);
    if (k > 0.01f) dl->AddRectFilled(a, b, Final(StyleRaw(ImGuiCol_HeaderHovered), k), r);
    if (selected) dl->AddRectFilled(ImVec2(a.x, a.y + h * 0.25f), ImVec2(a.x + 2.5f * dpi, b.y - h * 0.25f), Final(AccentRaw()), 1.5f * dpi);
    float pad = ImGui::GetStyle().FramePadding.x;
    float x = a.x + pad;
    if (icon_w > 0.0f) {
        if (icon && *icon) TextIn(dl, ImVec2(x, a.y), ImVec2(x + icon_w, b.y), icon, nullptr, Final(selected ? AccentRaw() : TextRaw()));
        x += icon_w + pad;
    }
    float right = b.x - pad;
    if (selected) {
        Check(dl, ImVec2(right - h * 0.25f, a.y + h * 0.5f), h * 0.2f, Final(AccentRaw()), Max(1.5f, 1.5f * dpi));
        right -= h * 0.6f;
    }
    if (hint && *hint) {
        float hw = TextW(hint);
        dl->AddText(ImVec2(right - hw, a.y + (h - ImGui::GetFontSize()) * 0.5f), Final(DimRaw()), hint);
        right -= hw + pad;
    }
    TextIn(dl, ImVec2(x, a.y), ImVec2(right, b.y), text, nullptr, Final(TextRaw()), 0.0f);
    return pressed;
}

}  // namespace detail

struct SplitButtonStyle {
    ImVec2 size = ImVec2(0, 0);
    ImU32 color = 0;
    ImU32 text = 0;
};

// main part runs the current action, the arrow opens the list. picking an item makes it the
// current one and also counts as a click. the main part shows the label, or the current item
// when the visible label is empty
inline bool SplitButton(const char* label, int* selected, const char* const* items, int count,
                        const SplitButtonStyle& bs = SplitButtonStyle()) {
    int tmp = 0;
    if (!selected) selected = &tmp;
    if (count > 0) *selected = detail::ClampI(*selected, 0, count - 1);
    const ImGuiStyle& st = ImGui::GetStyle();
    float dpi = detail::Dpi();
    const char* lend = detail::LabelEnd(label);
    const char* text = lend > label ? label : (count > 0 && items && items[*selected] ? items[*selected] : "");
    const char* tend = lend > label ? lend : nullptr;
    float h = bs.size.y > 0.0f ? bs.size.y : ImGui::GetFrameHeight();
    float arrow_w = h;
    float main_w = bs.size.x > 0.0f ? detail::Max(bs.size.x - arrow_w, h) : detail::TextW(text, tend) + st.FramePadding.x * 2.0f;
    ImGuiID id = ImGui::GetID(label);
    ImGui::PushID(label);
    ImGui::BeginGroup();
    ImVec2 pos = ImGui::GetCursorScreenPos();
    bool clicked = ImGui::InvisibleButton("##main", ImVec2(main_w, h), detail::kNav);
    float mh = detail::AnimTo(detail::Sub(id, 1), ImGui::IsItemActive() ? 1.0f : (ImGui::IsItemHovered() ? 0.5f : 0.0f));
    ImGui::SameLine(0.0f, 0.0f);
    bool open = ImGui::InvisibleButton("##arrow", ImVec2(arrow_w, h), detail::kNav);
    float ah = detail::AnimTo(detail::Sub(id, 2), ImGui::IsItemActive() ? 1.0f : (ImGui::IsItemHovered() ? 0.5f : 0.0f));
    ImVec2 b(pos.x + main_w + arrow_w, pos.y + h);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float r = detail::Rounding(h);
    ImU32 base = bs.color ? bs.color : detail::AccentRaw();
    ImU32 tc = bs.text ? bs.text : detail::OnRaw(base);
    detail::Shadow(dl, pos, b, r, 0.6f);
    dl->AddRectFilled(pos, ImVec2(pos.x + main_w, b.y), detail::Final(detail::Shade(base, 0.12f * mh)), r, detail::kRoundLeft);
    dl->AddRectFilled(ImVec2(pos.x + main_w, pos.y), b, detail::Final(detail::Shade(base, 0.12f * ah)), r, detail::kRoundRight);
    dl->AddLine(ImVec2(pos.x + main_w, pos.y + h * 0.2f), ImVec2(pos.x + main_w, b.y - h * 0.2f),
                detail::Final(tc, 0.35f), detail::Max(1.0f, dpi));
    detail::TextIn(dl, pos, ImVec2(pos.x + main_w, b.y), text, tend, detail::Final(tc));
    bool popup_open = ImGui::IsPopupOpen("##menu");
    float ot = detail::Anim(detail::Sub(id, 3), popup_open);
    detail::Chevron(dl, ImVec2(pos.x + main_w + arrow_w * 0.5f, pos.y + h * 0.5f), h * 0.13f, detail::kPi * ot,
                    detail::Final(tc), detail::Max(1.3f, 1.4f * dpi));
    ImGui::EndGroup();
    if (open) ImGui::OpenPopup("##menu");
    if (count > 0 && items && detail::BeginMenuPopup("##menu", detail::Sub(id, 4), pos, b, b.x - pos.x)) {
        float w = 0.0f;
        for (int i = 0; i < count; ++i) w = detail::Max(w, detail::TextW(items[i]));
        w = detail::Max(w + st.FramePadding.x * 2.0f + h * 0.6f, b.x - pos.x - 8.0f * dpi);
        for (int i = 0; i < count; ++i) {
            ImGui::PushID(i);
            if (detail::MenuRow(detail::Sub(id, 100 + i), items[i] ? items[i] : "", nullptr, 0.0f, i == *selected, w)) {
                *selected = i;
                clicked = true;
                ImGui::CloseCurrentPopup();
            }
            if (i == *selected) ImGui::SetItemDefaultFocus();
            ImGui::PopID();
        }
        detail::EndMenuPopup();
    }
    ImGui::PopID();
    return clicked;
}

inline bool SplitButton(const char* label, int* selected, const char* items_zero_separated,
                        const SplitButtonStyle& bs = SplitButtonStyle()) {
    const char* items[detail::kMaxItems];
    int n = detail::SplitZero(items_zero_separated, items, detail::kMaxItems);
    return SplitButton(label, selected, items, n, bs);
}

struct DropdownStyle {
    float width = 0.0f;
    const char* placeholder = "Select...";
    bool show_label = true;
    ImU32 bg = 0;
};

// a combo whose rows (and the closed box) show an icon string next to each item
inline bool IconDropdown(const char* label, int* current, const char* const* items, int count,
                         const char* const* icons = nullptr, int icon_count = -1,
                         const DropdownStyle& ds = DropdownStyle()) {
    int tmp = -1;
    if (!current) current = &tmp;
    if (icon_count < 0) icon_count = icons ? count : 0;
    const ImGuiStyle& st = ImGui::GetStyle();
    float dpi = detail::Dpi();
    float h = ImGui::GetFrameHeight();
    const char* lend = detail::LabelEnd(label);
    float w = ds.width > 0.0f ? ds.width : ImGui::CalcItemWidth();
    float icon_w = 0.0f;
    for (int i = 0; i < icon_count && i < count; ++i)
        if (icons[i] && *icons[i]) icon_w = detail::Max(icon_w, detail::TextW(icons[i]));
    ImGuiID id = ImGui::GetID(label);
    ImGui::PushID(label);
    ImGui::BeginGroup();
    ImVec2 pos = ImGui::GetCursorScreenPos(), b(pos.x + w, pos.y + h);
    bool open = ImGui::InvisibleButton("##box", ImVec2(w, h), detail::kNav);
    bool hov = ImGui::IsItemHovered();
    bool popup_open = ImGui::IsPopupOpen("##menu");
    float hk = detail::Anim(detail::Sub(id, 1), hov || popup_open);
    float ot = detail::Anim(detail::Sub(id, 2), popup_open);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float r = detail::Rounding(h);
    ImU32 bg = detail::Pick(ds.bg, ImGuiCol_FrameBg);
    detail::Frame(dl, pos, b, detail::Final(detail::Shade(bg, 0.06f * hk)), r);
    if (ot > 0.01f) detail::Rect(dl, pos, b, detail::Final(detail::AccentRaw(), ot), r, detail::Max(1.0f, dpi));
    int cur = *current;
    bool valid = items && cur >= 0 && cur < count;
    float x = pos.x + st.FramePadding.x;
    if (valid && icon_w > 0.0f) {
        const char* ic = cur < icon_count ? icons[cur] : nullptr;
        if (ic && *ic) detail::TextIn(dl, ImVec2(x, pos.y), ImVec2(x + icon_w, b.y), ic, nullptr, detail::Final(detail::AccentRaw()));
        x += icon_w + st.FramePadding.x;
    }
    float chev = h * 0.8f;
    detail::TextIn(dl, ImVec2(x, pos.y), ImVec2(b.x - chev, b.y), valid ? items[cur] : ds.placeholder, nullptr,
                   detail::Final(valid ? detail::TextRaw() : detail::DimRaw()), 0.0f);
    detail::Chevron(dl, ImVec2(b.x - h * 0.45f, pos.y + h * 0.5f), h * 0.13f, detail::kPi * ot, detail::Final(detail::DimRaw()),
                    detail::Max(1.3f, 1.4f * dpi));
    if (ds.show_label) detail::TrailingLabel(label, lend, h);
    ImGui::EndGroup();
    if (open) ImGui::OpenPopup("##menu");
    bool changed = false;
    if (items && count > 0 && detail::BeginMenuPopup("##menu", detail::Sub(id, 3), pos, b, w)) {
        float rw = w - 8.0f * dpi;
        for (int i = 0; i < count; ++i) {
            ImGui::PushID(i);
            const char* ic = (icons && i < icon_count) ? icons[i] : nullptr;
            if (detail::MenuRow(detail::Sub(id, 100 + i), items[i] ? items[i] : "", ic, icon_w, i == cur, rw)) {
                if (*current != i) changed = true;
                *current = i;
                ImGui::CloseCurrentPopup();
            }
            if (i == cur) ImGui::SetItemDefaultFocus();
            ImGui::PopID();
        }
        detail::EndMenuPopup();
    }
    ImGui::PopID();
    return changed;
}

inline bool IconDropdown(const char* label, int* current, const char* items_zero_separated,
                         const char* icons_zero_separated = nullptr, const DropdownStyle& ds = DropdownStyle()) {
    const char* items[detail::kMaxItems];
    const char* icons[detail::kMaxItems];
    int n = detail::SplitZero(items_zero_separated, items, detail::kMaxItems);
    int ni = detail::SplitZero(icons_zero_separated, icons, detail::kMaxItems);
    return IconDropdown(label, current, items, n, icons, ni, ds);
}

namespace detail {
inline char Lower(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c; }
inline bool IsWordSep(char c) { return c == ' ' || c == '_' || c == '-' || c == '.' || c == '/' || c == ':'; }
}  // namespace detail

// subsequence match, case insensitive. -1 when not all pattern characters appear in order,
// higher is better: word starts and runs of consecutive characters score most. spaces in the
// pattern are ignored. positions (optional) gets the matched character offsets
inline int FuzzyScore(const char* pattern, const char* text, int* positions = nullptr, int max_positions = 0) {
    if (!text) return -1;
    if (!pattern) return 0;
    int score = 0, prev = -1, np = 0;
    const char* p = pattern;
    while (*p == ' ') ++p;
    if (!*p) return 0;
    for (int i = 0; text[i] && *p; ++i) {
        if (detail::Lower(text[i]) != detail::Lower(*p)) continue;
        int s = 1;
        bool word = i == 0 || detail::IsWordSep(text[i - 1]) ||
                    (text[i - 1] >= 'a' && text[i - 1] <= 'z' && text[i] >= 'A' && text[i] <= 'Z');
        if (word) s += 8;
        int gap = prev < 0 ? i : i - prev - 1;
        if (prev >= 0 && gap == 0) s += 6;
        else s -= gap < 3 ? gap : 3;
        score += s;
        if (positions && np < max_positions) positions[np++] = i;
        prev = i;
        ++p;
        while (*p == ' ') ++p;
    }
    if (*p) return -1;
    return score < 0 ? 0 : score;
}

struct CommandPaletteStyle {
    float width = 0.0f;
    int max_rows = 8;
    char shortcut = 'P';
    const char* hint = "Type a command";
    ImU32 match = 0;
};

namespace detail {

struct PaletteState {
    ImGuiID id = 0;
    char filter[128] = {0};
    char last[128] = {0};
    int last_count = -1;
    int sel = 0;
    int move = 0;
    bool scroll = false;
    ImVector<int> results;
    ImVector<int> scores;
};

inline PaletteState& Palette() {
    static PaletteState s;
    return s;
}

inline ImGuiID& PendingPalette() {
    static ImGuiID id = 0;
    return id;
}

inline int PaletteCallback(ImGuiInputTextCallbackData* d) {
    PaletteState* s = (PaletteState*)d->UserData;
    if (d->EventFlag == ImGuiInputTextFlags_CallbackHistory) {
        if (d->EventKey == ImGuiKey_UpArrow) s->move = -1;
        else if (d->EventKey == ImGuiKey_DownArrow) s->move = 1;
    }
    return 0;
}

inline void FilterPalette(PaletteState& s, const char* const* items, int count) {
    s.results.resize(0);
    s.scores.resize(0);
    for (int i = 0; i < count; ++i) {
        int sc = FuzzyScore(s.filter, items[i] ? items[i] : "");
        if (sc < 0) continue;
        int at = s.results.Size;
        s.results.push_back(i);
        s.scores.push_back(sc);
        while (at > 0 && s.scores[at - 1] < sc) {
            s.results[at] = s.results[at - 1];
            s.scores[at] = s.scores[at - 1];
            --at;
        }
        s.results[at] = i;
        s.scores[at] = sc;
    }
    snprintf(s.last, sizeof(s.last), "%s", s.filter);
    s.last_count = count;
    s.sel = 0;
    s.scroll = true;
}

// item text with the matched characters in the accent color
inline void MatchedText(ImDrawList* dl, ImVec2 p, const char* text, const char* pattern, ImU32 normal, ImU32 hi) {
    int pos[64];
    for (int i = 0; i < 64; ++i) pos[i] = -1;
    int n = 0;
    if (FuzzyScore(pattern, text, pos, 64) >= 0)
        while (n < 64 && pos[n] >= 0) ++n;
    int k = 0;
    const char* run = text;
    bool run_m = n > 0 && pos[0] == 0;
    for (int i = 0;; ++i) {
        bool end = text[i] == 0;
        bool m = !end && k < n && pos[k] == i;
        if (end || m != run_m) {
            if (text + i > run) {
                dl->AddText(p, run_m ? hi : normal, run, text + i);
                p.x += TextW(run, text + i);
            }
            if (end) break;
            run = text + i;
            run_m = m;
        }
        if (m) ++k;
    }
}

}  // namespace detail

// opens the palette with this id on its next draw (same ID stack)
inline void OpenCommandPalette(const char* id) { detail::PendingPalette() = ImGui::GetID(id); }

// Ctrl+P style popup: type to fuzzy filter, Up/Down to move, Enter or click to run, Escape to
// close. returns true on the frame an item was picked, *selected gets its index. *open is
// optional, without it the palette keeps its own state (Ctrl+P / OpenCommandPalette)
inline bool CommandPalette(const char* id, bool* open, const char* const* items, int count, int* selected,
                           const char* const* hints = nullptr, int hint_count = -1,
                           const CommandPaletteStyle& cs = CommandPaletteStyle()) {
    int tmp_sel = 0;
    if (!selected) selected = &tmp_sel;
    if (hint_count < 0) hint_count = hints ? count : 0;
    const ImGuiIO& io = ImGui::GetIO();
    float dpi = detail::Dpi();
    float fh = ImGui::GetFrameHeight();
    ImGuiID gid = ImGui::GetID(id);
    ImGui::PushID(id);
    ImGuiStorage* store = ImGui::GetStateStorage();
    ImGuiID k_open = detail::Sub(gid, 1), k_was = detail::Sub(gid, 2);
    bool local_open = store->GetBool(k_open, false);
    bool* o = open ? open : &local_open;
    if (detail::PendingPalette() == gid) {
        *o = true;
        detail::PendingPalette() = 0;
    }
    bool letter = cs.shortcut >= 'A' && cs.shortcut <= 'Z';
    if (letter && io.KeyCtrl && !io.KeyShift && !io.KeyAlt && detail::LetterPressed(cs.shortcut)) *o = !*o;
    bool was_open = store->GetBool(k_was, false);
    bool is_open = ImGui::IsPopupOpen("##palette");
    if (was_open && !is_open) *o = false;
    detail::PaletteState& ps = detail::Palette();
    if (*o && !is_open && !was_open) {
        ImGui::OpenPopup("##palette");
        ps.id = gid;
        ps.filter[0] = 0;
        ps.last_count = -1;
        ps.sel = 0;
    }
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    float w = cs.width > 0.0f ? cs.width : detail::Min(ImGui::GetFontSize() * 34.0f, vp->Size.x * 0.9f);
    float t = detail::Anim(detail::Sub(gid, 3), is_open, 0.14f);
    ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + vp->Size.x * 0.5f, vp->Pos.y + vp->Size.y * 0.12f - (1.0f - t) * 8.0f * dpi),
                            ImGuiCond_Always, ImVec2(0.5f, 0.0f));
    ImGui::SetNextWindowSize(ImVec2(w, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * detail::Max(t, 0.05f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6.0f * dpi, 6.0f * dpi));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, detail::Max(ImGui::GetStyle().PopupRounding, 6.0f * dpi));
    bool picked = false;
    if (ImGui::BeginPopup("##palette", ImGuiWindowFlags_NoMove)) {
        if (!*o || ps.id != gid) ImGui::CloseCurrentPopup();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 ip = ImGui::GetCursorScreenPos();
        float iw = ImGui::GetContentRegionAvail().x;
        detail::Magnifier(dl, ImVec2(ip.x + fh * 0.5f, ip.y + fh * 0.5f), fh * 0.26f, detail::Final(detail::AccentRaw()),
                          detail::Max(1.4f, 1.4f * dpi));
        ImGui::SetCursorScreenPos(ImVec2(ip.x + fh, ip.y));
        ImGui::SetNextItemWidth(detail::Max(iw - fh, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0, 0, 0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
        if (!store->GetBool(detail::Sub(gid, 4), false) && !ImGui::IsMouseDown(0)) ImGui::SetKeyboardFocusHere();
        ps.move = 0;
        bool enter = ImGui::InputTextWithHint("##filter", cs.hint ? cs.hint : "", ps.filter, sizeof(ps.filter),
                                              ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackHistory,
                                              detail::PaletteCallback, &ps);
        bool input_active = ImGui::IsItemActive();
        store->SetBool(detail::Sub(gid, 4), input_active);
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
        if (!input_active && was_open) {
            if (detail::KeyPressed(ImGuiKey_UpArrow)) ps.move = -1;
            if (detail::KeyPressed(ImGuiKey_DownArrow)) ps.move = 1;
            if (detail::KeyPressed(ImGuiKey_Enter, false)) enter = true;
        }
        if (ps.last_count != count || strcmp(ps.last, ps.filter) != 0) detail::FilterPalette(ps, items, count);
        if (ps.move && ps.results.Size > 0) {
            ps.sel = (ps.sel + ps.move + ps.results.Size) % ps.results.Size;
            ps.scroll = true;
        }
        ImVec2 sp = ImGui::GetCursorScreenPos();
        dl->AddLine(ImVec2(sp.x - 6.0f * dpi, sp.y), ImVec2(sp.x + iw + 6.0f * dpi, sp.y), detail::Final(detail::BorderRaw(), 0.7f));
        ImGui::Dummy(ImVec2(1.0f, 2.0f * dpi));
        int rows = detail::ClampI(ps.results.Size, 1, cs.max_rows > 0 ? cs.max_rows : 8);
        float row_h = fh + ImGui::GetStyle().ItemSpacing.y;
        if (ps.results.Size == 0) {
            ImGui::TextDisabled("No matching commands");
        } else {
            ImGui::BeginChild("##results", ImVec2(0.0f, row_h * (float)rows), false, ImGuiWindowFlags_NoNav);
            ImDrawList* cdl = ImGui::GetWindowDrawList();
            float rw = ImGui::GetContentRegionAvail().x;
            ImU32 match = cs.match ? cs.match : detail::AccentRaw();
            for (int r = 0; r < ps.results.Size; ++r) {
                int idx = ps.results[r];
                const char* text = items[idx] ? items[idx] : "";
                ImGui::PushID(idx);
                ImVec2 a = ImGui::GetCursorScreenPos(), b(a.x + rw, a.y + fh);
                bool vis = ImGui::IsRectVisible(a, b);
                bool click = ImGui::InvisibleButton("##row", ImVec2(rw, fh));
                if (ImGui::IsItemHovered() && (io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f)) ps.sel = r;
                if (click) {
                    ps.sel = r;
                    enter = true;
                }
                if (r == ps.sel && ps.scroll) {
                    ImGui::SetScrollHereY(0.5f);
                    ps.scroll = false;
                }
                if (vis) {
                    float k = detail::AnimTo(detail::Sub(gid, 1000 + r), r == ps.sel ? 1.0f : 0.0f, 0.08f);
                    float rr = detail::Rounding(fh);
                    if (k > 0.01f) {
                        cdl->AddRectFilled(a, b, detail::Final(detail::StyleRaw(ImGuiCol_HeaderHovered), k), rr);
                        cdl->AddRectFilled(ImVec2(a.x, a.y + fh * 0.22f), ImVec2(a.x + 2.5f * dpi, b.y - fh * 0.22f),
                                           detail::Final(match, k), 1.5f * dpi);
                    }
                    float tx = a.x + ImGui::GetStyle().FramePadding.x + 2.0f * dpi * k;
                    detail::MatchedText(cdl, ImVec2(tx, a.y + (fh - ImGui::GetFontSize()) * 0.5f), text, ps.filter,
                                        detail::Final(detail::TextRaw()), detail::Final(match));
                    const char* hint = (hints && idx < hint_count) ? hints[idx] : nullptr;
                    if (hint && *hint) {
                        float hw = detail::TextW(hint);
                        cdl->AddText(ImVec2(b.x - hw - ImGui::GetStyle().FramePadding.x, a.y + (fh - ImGui::GetFontSize()) * 0.5f),
                                     detail::Final(detail::DimRaw()), hint);
                    }
                }
                ImGui::PopID();
            }
            ImGui::EndChild();
        }
        char foot[48];
        snprintf(foot, sizeof(foot), "%d of %d", ps.results.Size, count);
        ImGui::TextDisabled("%s", foot);
        if (enter && ps.results.Size > 0) {
            *selected = ps.results[detail::ClampI(ps.sel, 0, ps.results.Size - 1)];
            picked = true;
        }
        if (picked || detail::KeyPressed(ImGuiKey_Escape, false)) {
            *o = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar(3);
    store->SetBool(k_was, ImGui::IsPopupOpen("##palette"));
    if (!open) store->SetBool(k_open, *o);
    ImGui::PopID();
    return picked;
}

inline bool CommandPalette(const char* id, bool* open, const char* items_zero_separated, int* selected,
                           const char* hints_zero_separated = nullptr, const CommandPaletteStyle& cs = CommandPaletteStyle()) {
    const char* items[256];
    const char* hints[256];
    int n = detail::SplitZero(items_zero_separated, items, 256);
    int nh = detail::SplitZero(hints_zero_separated, hints, 256);
    return CommandPalette(id, open, items, n, selected, hints, nh, cs);
}

}  // namespace ctl
}  // namespace imb
