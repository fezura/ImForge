#pragma once
// SearchField, TagInput, KeyChordPicker
#include <cstddef>
#include "imb_ctl_core.h"
#include <string>
#include <vector>

namespace imb {
namespace ctl {

struct SearchStyle {
    const char* hint = "Search";
    float width = 0.0f;
    bool focus_shortcut = false;
    bool escape_clears = true;
    ImU32 bg = 0;
    ImU32 icon = 0;
};

namespace detail {
inline ImGuiID& PendingFocus() {
    static ImGuiID id = 0;
    return id;
}
}  // namespace detail

// asks the SearchField with this label (same ID stack) to take keyboard focus on its next draw
inline void FocusSearchField(const char* label) { detail::PendingFocus() = ImGui::GetID(label); }

// pill shaped text field with a magnifier and a clear button. returns true when the text
// changed (typing, the clear button or Escape)
inline bool SearchField(const char* label, char* buf, size_t buf_size, const SearchStyle& ss = SearchStyle()) {
    if (!buf || buf_size == 0) return false;
    const ImGuiStyle& st = ImGui::GetStyle();
    const ImGuiIO& io = ImGui::GetIO();
    float dpi = detail::Dpi();
    float h = ImGui::GetFrameHeight();
    const char* lend = detail::LabelEnd(label);
    float label_w = detail::LabelWidth(label, lend);
    float w = ss.width > 0.0f ? ss.width : ImGui::CalcItemWidth();
    if (ss.width < 0.0f) w = detail::Max(ImGui::GetContentRegionAvail().x + ss.width + 1.0f - label_w, h * 4.0f);
    w = detail::Max(w, h * 3.0f);
    ImGuiID id = ImGui::GetID(label);
    ImGuiStorage* store = ImGui::GetStateStorage();
    ImGuiID k_active = detail::Sub(id, 1), k_hover = detail::Sub(id, 2), k_refocus = detail::Sub(id, 3);
    bool was_active = store->GetBool(k_active, false);
    bool focus_now = store->GetBool(k_refocus, false) || detail::PendingFocus() == id;
    if (ss.focus_shortcut && io.KeyCtrl && detail::LetterPressed('F') &&
        ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
        focus_now = true;
    if (detail::PendingFocus() == id) detail::PendingFocus() = 0;
    store->SetBool(k_refocus, false);

    ImGui::PushID(label);
    ImGui::BeginGroup();
    ImVec2 pos = ImGui::GetCursorScreenPos(), b(pos.x + w, pos.y + h);
    float act = detail::Anim(detail::Sub(id, 4), was_active);
    float hov = detail::Anim(detail::Sub(id, 5), store->GetBool(k_hover, false));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float r = h * 0.5f;
    ImU32 bg = detail::Pick(ss.bg, ImGuiCol_FrameBg);
    ImU32 accent = detail::AccentRaw();
    if (act > 0.01f) imb::Glow(dl, pos, b, r, detail::Final(accent, 0.3f * act), 4.0f * dpi * act);
    dl->AddRectFilled(pos, b, detail::Final(detail::Shade(bg, 0.05f * detail::Max(hov, act))), r);
    detail::Rect(dl, pos, b, detail::Final(detail::Mix(detail::BorderRaw(), accent, act)), r,
                 detail::Max(1.0f, dpi) * (1.0f + 0.5f * act));
    ImU32 icon = ss.icon ? ss.icon : detail::Mix(detail::DimRaw(), accent, act);
    detail::Magnifier(dl, ImVec2(pos.x + h * 0.55f, pos.y + h * 0.5f), h * 0.26f, detail::Final(icon),
                      detail::Max(1.4f, 1.4f * dpi));

    bool changed = false;
    float text_x = pos.x + h * 0.95f, clear_w = h;
    ImGui::SetCursorScreenPos(ImVec2(text_x, pos.y));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(st.FramePadding.x * 0.3f, st.FramePadding.y));
    ImGui::SetNextItemWidth(detail::Max(b.x - clear_w - text_x, 1.0f));
    if (focus_now) ImGui::SetKeyboardFocusHere();
    if (ImGui::InputTextWithHint("##input", ss.hint ? ss.hint : "", buf, buf_size)) changed = true;
    bool active = ImGui::IsItemActive();
    bool hovered = ImGui::IsItemHovered();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(3);
    if (was_active && ss.escape_clears && detail::KeyPressed(ImGuiKey_Escape, false) && buf[0]) {
        buf[0] = 0;
        changed = true;
    }
    ImGui::SameLine(0.0f, 0.0f);
    ImGui::SetCursorScreenPos(ImVec2(b.x - clear_w, pos.y));
    float ct = detail::Anim(detail::Sub(id, 6), buf[0] != 0);
    if (buf[0]) {
        if (ImGui::InvisibleButton("##clear", ImVec2(clear_w, h))) {
            buf[0] = 0;
            changed = true;
            store->SetBool(k_refocus, true);
        }
        hovered |= ImGui::IsItemHovered();
        float ch = detail::Anim(detail::Sub(id, 7), ImGui::IsItemHovered());
        ImVec2 c(b.x - clear_w * 0.5f, pos.y + h * 0.5f);
        dl->AddCircleFilled(c, h * 0.3f * ct, detail::Final(detail::DimRaw(), (0.35f + 0.4f * ch) * ct), 16);
        detail::Cross(dl, c, h * 0.11f * ct, detail::Final(detail::OnRaw(detail::DimRaw()), ct),
                      detail::Max(1.4f, 1.4f * dpi));
    } else {
        ImGui::Dummy(ImVec2(clear_w, h));
    }
    store->SetBool(k_active, active);
    store->SetBool(k_hover, hovered);
    detail::TrailingLabel(label, lend, h);
    ImGui::EndGroup();
    ImGui::PopID();
    return changed;
}

inline bool SearchField(const char* label, std::string* text, const SearchStyle& ss = SearchStyle()) {
    if (!text) return false;
    char buf[256];
    snprintf(buf, sizeof(buf), "%s", text->c_str());
    if (!SearchField(label, buf, sizeof(buf), ss)) return false;
    *text = buf;
    return true;
}

inline bool SearchField(const char* label, std::string* text, const char* hint) {
    SearchStyle ss;
    ss.hint = hint;
    return SearchField(label, text, ss);
}

struct TagInputStyle {
    const char* hint = "Add tag";
    char separator = ',';
    int max_tags = 0;
    ImU32 tag_bg = 0;
    ImU32 tag_text = 0;
    bool show_label = true;
};

namespace detail {

// next tag in a separator list, spaces trimmed; returns false at the end
inline bool NextTag(const char*& p, char sep, const char** b, const char** e) {
    while (p && *p) {
        const char* s = p;
        while (*p && *p != sep) ++p;
        const char* t = p;
        if (*p) ++p;
        while (s < t && (*s == ' ' || *s == '\t')) ++s;
        while (t > s && (t[-1] == ' ' || t[-1] == '\t')) --t;
        if (t > s) {
            *b = s;
            *e = t;
            return true;
        }
    }
    return false;
}

inline bool TagExists(const char* tags, char sep, const char* b, const char* e) {
    const char *tb, *te;
    const char* p = tags;
    size_t n = (size_t)(e - b);
    while (NextTag(p, sep, &tb, &te))
        if ((size_t)(te - tb) == n && strncmp(tb, b, n) == 0) return true;
    return false;
}

inline bool AppendTag(char* tags, size_t size, char sep, const char* text) {
    const char* b = text;
    const char* e = text + strlen(text);
    while (b < e && (*b == ' ' || *b == sep)) ++b;
    while (e > b && (e[-1] == ' ' || e[-1] == sep)) --e;
    if (e <= b || TagExists(tags, sep, b, e)) return false;
    size_t len = strlen(tags), add = (size_t)(e - b) + (len ? 1 : 0);
    if (len + add + 1 > size) return false;
    if (len) tags[len++] = sep;
    memcpy(tags + len, b, (size_t)(e - b));
    tags[len + (size_t)(e - b)] = 0;
    return true;
}

// removes tags[b, e) and one neighbouring separator
inline void EraseTag(char* tags, char sep, const char* b, const char* e) {
    char* s = tags + (b - tags);
    char* t = tags + (e - tags);
    while (*t == ' ') ++t;
    if (*t == sep) ++t;
    else {
        while (s > tags && s[-1] == ' ') --s;
        if (s > tags && s[-1] == sep) --s;
    }
    memmove(s, t, strlen(t) + 1);
}

struct TagCallbackData {
    char sep;
    bool hit;
    char committed[128];
};

inline int TagCallback(ImGuiInputTextCallbackData* d) {
    TagCallbackData* u = (TagCallbackData*)d->UserData;
    if (d->EventFlag == ImGuiInputTextFlags_CallbackCharFilter) {
        if (d->EventChar == (ImWchar)u->sep) {
            u->hit = true;
            return 1;
        }
        return 0;
    }
    if (u->hit && d->Buf) {
        snprintf(u->committed, sizeof(u->committed), "%s", d->Buf);
        d->DeleteChars(0, d->BufTextLen);
    }
    return 0;
}

}  // namespace detail

inline int TagCount(const char* tags, char separator = ',') {
    int n = 0;
    const char *b, *e;
    const char* p = tags;
    while (detail::NextTag(p, separator, &b, &e)) ++n;
    return n;
}

// removable tags plus a text field: Enter or the separator adds, the x or Backspace on an
// empty field removes. tags live in one separated string ("red,green,blue")
inline bool TagInput(const char* label, char* tags, size_t tags_size, const TagInputStyle& ts = TagInputStyle()) {
    if (!tags || tags_size == 0) return false;
    const ImGuiStyle& st = ImGui::GetStyle();
    float dpi = detail::Dpi();
    float fh = ImGui::GetFrameHeight();
    float tag_h = fh - detail::Max(4.0f, 4.0f * dpi);
    float pad = detail::Max(2.0f, 2.0f * dpi), gap = st.ItemInnerSpacing.x * 0.75f;
    const char* lend = detail::LabelEnd(label);
    float w = ImGui::CalcItemWidth();
    ImGuiID id = ImGui::GetID(label);
    ImGuiStorage* store = ImGui::GetStateStorage();
    ImGuiID k_refocus = detail::Sub(id, 2), k_active = detail::Sub(id, 3);
    int pend_size = 0;
    char* pending = detail::TextBuf(detail::Sub(id, 1), &pend_size);
    float min_input = ImGui::GetFontSize() * 6.0f;
    float xw = tag_h * 0.8f;

    // layout pass so the frame can be drawn first
    float x = pad, y = pad;
    int lines = 1, n = 0;
    const char *b, *e;
    const char* p = tags;
    while (detail::NextTag(p, ts.separator, &b, &e)) {
        float tw = detail::TextW(b, e) + tag_h * 0.5f + xw;
        if (x > pad && x + tw > w - pad) {
            x = pad;
            y += tag_h + pad;
            ++lines;
        }
        x += tw + gap;
        ++n;
    }
    bool full = ts.max_tags > 0 && n >= ts.max_tags;
    if (!full && x > pad && x + min_input > w - pad) {
        y += tag_h + pad;
        ++lines;
    }
    float frame_h = detail::Max(fh, (float)lines * (tag_h + pad) + pad);

    ImGui::PushID(label);
    ImGui::BeginGroup();
    ImVec2 pos = ImGui::GetCursorScreenPos(), fb(pos.x + w, pos.y + frame_h);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float act = detail::Anim(detail::Sub(id, 4), store->GetBool(k_active, false));
    float r = detail::Rounding(fh);
    detail::Frame(dl, pos, fb, detail::Final(detail::StyleRaw(ImGuiCol_FrameBg)), r);
    if (act > 0.01f) detail::Rect(dl, pos, fb, detail::Final(detail::AccentRaw(), act), r, detail::Max(1.0f, dpi));

    bool changed = false;
    const char* remove_b = nullptr;
    const char* remove_e = nullptr;
    ImU32 tag_bg = ts.tag_bg ? ts.tag_bg : detail::WithAlpha(detail::AccentRaw(), 0.3f);
    ImU32 tag_text = ts.tag_text ? ts.tag_text : detail::TextRaw();
    x = pad;
    y = pad;
    p = tags;
    int i = 0;
    while (detail::NextTag(p, ts.separator, &b, &e)) {
        float tw = detail::TextW(b, e) + tag_h * 0.5f + xw;
        if (x > pad && x + tw > w - pad) {
            x = pad;
            y += tag_h + pad;
        }
        ImVec2 ta(pos.x + x, pos.y + y), tb(ta.x + tw, ta.y + tag_h);
        bool tag_hov = ImGui::IsMouseHoveringRect(ta, tb);
        dl->AddRectFilled(ta, tb, detail::Final(tag_hov ? detail::Shade(tag_bg, 0.08f) : tag_bg), tag_h * 0.5f);
        detail::TextIn(dl, ta, ImVec2(tb.x - xw, tb.y), b, e, detail::Final(tag_text), 0.5f, tag_h * 0.25f);
        ImGui::SetCursorScreenPos(ImVec2(tb.x - xw, ta.y));
        ImGui::PushID(i);
        if (ImGui::InvisibleButton("##x", ImVec2(xw, tag_h), detail::kNav)) {
            remove_b = b;
            remove_e = e;
        }
        float xh = detail::Anim(detail::Sub(id, 100 + i), ImGui::IsItemHovered());
        ImGui::PopID();
        ImVec2 xc(tb.x - xw * 0.55f, ta.y + tag_h * 0.5f);
        if (xh > 0.01f) dl->AddCircleFilled(xc, tag_h * 0.32f, detail::Final(detail::TextRaw(), 0.18f * xh), 12);
        detail::Cross(dl, xc, tag_h * 0.13f, detail::Final(tag_text, 0.6f + 0.4f * xh), detail::Max(1.2f, 1.2f * dpi));
        x += tw + gap;
        ++i;
    }
    bool active = false;
    if (!full) {
        if (x > pad && x + min_input > w - pad) {
            x = pad;
            y += tag_h + pad;
        }
        ImGui::SetCursorScreenPos(ImVec2(pos.x + x, pos.y + y + (tag_h - fh) * 0.5f));
        ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, IM_COL32(0, 0, 0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
        ImGui::SetNextItemWidth(detail::Max(w - pad - x, 1.0f));
        if (store->GetBool(k_refocus, false)) {
            ImGui::SetKeyboardFocusHere();
            store->SetBool(k_refocus, false);
        }
        bool was_empty = pending[0] == 0;
        detail::TagCallbackData cb;
        cb.sep = ts.separator;
        cb.hit = false;
        cb.committed[0] = 0;
        bool enter = ImGui::InputTextWithHint("##add", ts.hint ? ts.hint : "", pending, (size_t)pend_size,
                                              ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackCharFilter |
                                                  ImGuiInputTextFlags_CallbackAlways,
                                              detail::TagCallback, &cb);
        active = ImGui::IsItemActive();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(3);
        if (cb.committed[0]) {
            if (detail::AppendTag(tags, tags_size, ts.separator, cb.committed)) changed = true;
            pending[0] = 0;
        } else if (enter) {
            if (pending[0] && detail::AppendTag(tags, tags_size, ts.separator, pending)) changed = true;
            pending[0] = 0;
            store->SetBool(k_refocus, true);
        }
        if (active && was_empty && !remove_b && detail::KeyPressed(ImGuiKey_Backspace, false)) {
            const char *lb = nullptr, *le = nullptr;
            const char* q = tags;
            while (detail::NextTag(q, ts.separator, &b, &e)) {
                lb = b;
                le = e;
            }
            if (lb) {
                remove_b = lb;
                remove_e = le;
            }
        }
    }
    if (remove_b) {
        detail::EraseTag(tags, ts.separator, remove_b, remove_e);
        changed = true;
    }
    store->SetBool(k_active, active);
    ImGui::SetCursorScreenPos(pos);
    ImGui::Dummy(ImVec2(w, frame_h));
    if (ts.show_label && lend > label) {
        ImGui::SameLine(0.0f, st.ItemInnerSpacing.x);
        ImVec2 lp = ImGui::GetCursorScreenPos();
        ImGui::Dummy(ImVec2(detail::TextW(label, lend), fh));
        dl->AddText(ImVec2(lp.x, lp.y + (fh - ImGui::GetFontSize()) * 0.5f), detail::Final(detail::TextRaw()), label, lend);
    }
    ImGui::EndGroup();
    ImGui::PopID();
    return changed;
}

inline bool TagInput(const char* label, std::string* tags, const TagInputStyle& ts = TagInputStyle()) {
    if (!tags) return false;
    char buf[1024];
    snprintf(buf, sizeof(buf), "%s", tags->c_str());
    if (!TagInput(label, buf, sizeof(buf), ts)) return false;
    *tags = buf;
    return true;
}

inline bool TagInput(const char* label, std::vector<std::string>* tags, const TagInputStyle& ts = TagInputStyle()) {
    if (!tags) return false;
    char buf[1024];
    size_t len = 0;
    buf[0] = 0;
    for (size_t i = 0; i < tags->size(); ++i) {
        const std::string& t = (*tags)[i];
        if (len + t.size() + 2 >= sizeof(buf)) break;
        if (len) buf[len++] = ts.separator;
        memcpy(buf + len, t.data(), t.size());
        len += t.size();
        buf[len] = 0;
    }
    if (!TagInput(label, buf, sizeof(buf), ts)) return false;
    tags->clear();
    const char *b, *e;
    const char* p = buf;
    while (detail::NextTag(p, ts.separator, &b, &e)) tags->push_back(std::string(b, e));
    return true;
}

struct ChordStyle {
    ImVec2 size = ImVec2(0, 0);
    bool allow_mouse = true;
    bool show_label = true;
    ImU32 bg = 0;
    ImU32 keycap = 0;
};

inline int CurrentMods() {
    const ImGuiIO& io = ImGui::GetIO();
    return (io.KeyCtrl ? kModCtrl : 0) | (io.KeyShift ? kModShift : 0) | (io.KeyAlt ? kModAlt : 0) |
           (io.KeySuper ? kModSuper : 0);
}

// "Ctrl+Shift+F", static buffer
inline const char* ChordName(int chord) {
    static char buf[96];
    static const char* const kNames[4] = {"Ctrl", "Shift", "Alt", "Super"};
    static const int kBits[4] = {kModCtrl, kModShift, kModAlt, kModSuper};
    int key = chord & ~kModMask;
    size_t n = 0;
    buf[0] = 0;
    for (int i = 0; i < 4; ++i)
        if (chord & kBits[i]) n += (size_t)snprintf(buf + n, sizeof(buf) - n, "%s%s", n ? "+" : "", kNames[i]);
    if (key) snprintf(buf + n, sizeof(buf) - n, "%s%s", n ? "+" : "", KeyName(key));
    else if (n == 0) snprintf(buf, sizeof(buf), "None");
    return buf;
}

// modifiers must match exactly
inline bool IsChordPressed(int chord, bool repeat = false) {
    int key = chord & ~kModMask;
    if (key <= 0 || (chord & kModMask) != CurrentMods()) return false;
    if (key >= kMouseKey && key < kMouseKey + 5) return ImGui::IsMouseClicked(key - kMouseKey, repeat);
#if IMGUI_VERSION_NUM >= 18700
    if (key < ImGuiKey_NamedKey_BEGIN || key >= ImGuiKey_NamedKey_END) return false;
    return ImGui::IsKeyPressed((ImGuiKey)key, repeat);
#else
    if (key >= IM_ARRAYSIZE(ImGui::GetIO().KeysDown)) return false;
    return ImGui::IsKeyPressed(key, repeat);
#endif
}

namespace detail {

inline ImGuiID& CapturingChord() {
    static ImGuiID id = 0;
    return id;
}

// frame the capturing picker was last drawn, so a picker that disappears mid capture lets go
inline int& CapturingFrame() {
    static int frame = -1;
    return frame;
}

// first non-modifier key pressed this frame, 0 if none. *cancel on Escape or a left click
// outside the widget, *clear on Backspace
inline int PollChordKey(bool allow_mouse, bool hovered, bool* cancel, bool* clear) {
    *cancel = *clear = false;
    if (KeyPressed(ImGuiKey_Escape, false) || (ImGui::IsMouseClicked(0) && !hovered)) {
        *cancel = true;
        return 0;
    }
    if (KeyPressed(ImGuiKey_Backspace, false)) {
        *clear = true;
        return 0;
    }
#if IMGUI_VERSION_NUM >= 18700
    for (int k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_GamepadStart; ++k) {
        if (k >= ImGuiKey_LeftCtrl && k <= ImGuiKey_RightSuper) continue;
        if (ImGui::IsKeyPressed((ImGuiKey)k, false)) return k;
    }
#else
    // no key enum before 1.87: skip presses on the frame a modifier went down, that press is
    // the modifier itself
    static int prev_mods = 0;
    int mods = CurrentMods();
    bool mod_went_down = (mods & ~prev_mods) != 0;
    prev_mods = mods;
    if (!mod_went_down) {
        const ImGuiIO& io = ImGui::GetIO();
        for (int k = 0; k < IM_ARRAYSIZE(io.KeysDown); ++k)
            if (ImGui::IsKeyPressed(k, false)) return k;
    }
#endif
    if (allow_mouse)
        for (int b = 0; b < 5; ++b)
            if (ImGui::IsMouseClicked(b)) return kMouseKey + b;
    return 0;
}

// keycaps for "Ctrl", "Shift", ..., returns the width; draws only when dl is set
inline float Keycaps(ImDrawList* dl, ImVec2 p, float h, int chord, bool trailing_plus, float alpha) {
    const char* parts[5];
    int n = 0;
    if (chord & kModCtrl) parts[n++] = "Ctrl";
    if (chord & kModShift) parts[n++] = "Shift";
    if (chord & kModAlt) parts[n++] = "Alt";
    if (chord & kModSuper) parts[n++] = "Super";
    int key = chord & ~kModMask;
    if (key) parts[n++] = KeyName(key);
    float dpi = Dpi(), pad = 5.0f * dpi, plus_w = TextW("+") + 4.0f * dpi;
    float x = 0.0f, cap_h = h - 6.0f * dpi, lip = Max(1.5f, 1.5f * dpi);
    for (int i = 0; i < n; ++i) {
        float tw = TextW(parts[i]) + pad * 2.0f;
        if (dl) {
            ImVec2 a(p.x + x, p.y + (h - cap_h) * 0.5f), b(a.x + tw, a.y + cap_h);
            float r = 3.0f * dpi;
            dl->AddRectFilled(a, b, Final(BorderRaw(), alpha), r);
            dl->AddRectFilled(a, ImVec2(b.x, b.y - lip), Final(GetStyle().thumb ? GetStyle().thumb : Solid(ImGuiCol_Button), alpha), r);
            TextIn(dl, a, ImVec2(b.x, b.y - lip), parts[i], nullptr, Final(TextRaw(), alpha));
        }
        x += tw;
        if (i + 1 < n || trailing_plus) {
            if (dl) TextIn(dl, ImVec2(p.x + x, p.y), ImVec2(p.x + x + plus_w, p.y + h), "+", nullptr, Final(DimRaw(), alpha));
            x += plus_w;
        }
    }
    return x;
}

}  // namespace detail

inline bool IsCapturingChord() {
    ImGuiID& cap = detail::CapturingChord();
    if (cap != 0 && ImGui::GetFrameCount() - detail::CapturingFrame() > 1) cap = 0;
    return cap != 0;
}

// click, then press a key with any modifiers. Escape cancels, Backspace clears
inline bool KeyChordPicker(const char* label, int* chord, const ChordStyle& cs = ChordStyle()) {
    int tmp = 0;
    if (!chord) chord = &tmp;
    const ImGuiStyle& st = ImGui::GetStyle();
    float dpi = detail::Dpi();
    float h = cs.size.y > 0.0f ? cs.size.y : ImGui::GetFrameHeight();
    const char* lend = detail::LabelEnd(label);
    float label_w = cs.show_label ? detail::LabelWidth(label, lend) : 0.0f;
    ImGuiID id = ImGui::GetID(label);
    ImGuiID& cap = detail::CapturingChord();
    if (cap != 0 && ImGui::GetFrameCount() - detail::CapturingFrame() > 1) cap = 0;
    bool capturing = cap == id;
    const char* prompt = "Press keys...";
    int shown = capturing ? CurrentMods() : *chord;
    float content = (shown == 0 && !capturing) ? detail::TextW("None")
                                                : detail::Keycaps(nullptr, ImVec2(0, 0), h, shown, capturing && shown, 1.0f);
    if (capturing) content += detail::TextW(prompt);
    float w = cs.size.x > 0.0f ? cs.size.x : detail::Max(content, detail::TextW(prompt)) + st.FramePadding.x * 2.0f;
    ImVec2 pos = ImGui::GetCursorScreenPos();
    bool pressed = ImGui::InvisibleButton(label, ImVec2(w + label_w, h), detail::kNav);
    bool hovered = ImGui::IsItemHovered();
    bool changed = false;
    if (capturing) {
        bool cancel = false, clear = false;
        int key = detail::PollChordKey(cs.allow_mouse, hovered, &cancel, &clear);
        if (cancel) {
            cap = 0;
        } else if (clear) {
            changed = *chord != 0;
            *chord = 0;
            cap = 0;
        } else if (key) {
            int nc = key | CurrentMods();
            changed = nc != *chord;
            *chord = nc;
            cap = 0;
        }
        capturing = cap == id;
    } else if (pressed) {
        cap = id;
        capturing = true;
    }
    if (capturing) detail::CapturingFrame() = ImGui::GetFrameCount();
    float ct = detail::Anim(id, capturing);
    float hk = detail::Anim(detail::Sub(id, 1), hovered);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 b(pos.x + w, pos.y + h);
    float r = detail::Rounding(h);
    ImU32 bg = detail::Pick(cs.bg, ImGuiCol_FrameBg);
    ImU32 accent = detail::AccentRaw();
    detail::Frame(dl, pos, b, detail::Final(detail::Mix(detail::Shade(bg, 0.06f * hk), detail::WithAlpha(accent, 0.22f), ct)), r);
    if (ct > 0.01f) detail::Rect(dl, pos, b, detail::Final(accent, ct), r, detail::Max(1.0f, dpi) * 1.5f);
    float x = pos.x + st.FramePadding.x;
    if (capturing) {
        int mods = CurrentMods();
        float kw = detail::Keycaps(dl, ImVec2(x, pos.y), h, mods, mods != 0, 1.0f);
        float pulse = 0.55f + 0.45f * std::sin((float)ImGui::GetTime() * 6.0f);
        dl->AddText(ImVec2(x + kw, pos.y + (h - ImGui::GetFontSize()) * 0.5f), detail::Final(detail::TextRaw(), pulse), prompt);
    } else if (*chord == 0) {
        detail::TextIn(dl, pos, b, "None", nullptr, detail::Final(detail::DimRaw()));
    } else {
        float kw = detail::Keycaps(nullptr, ImVec2(0, 0), h, *chord, false, 1.0f);
        detail::Keycaps(dl, ImVec2(pos.x + (w - kw) * 0.5f, pos.y), h, *chord, false, 1.0f);
    }
    if (label_w > 0.0f)
        dl->AddText(ImVec2(b.x + st.ItemInnerSpacing.x, pos.y + (h - ImGui::GetFontSize()) * 0.5f),
                    detail::Final(detail::TextRaw()), label, lend);
    return changed;
}

inline bool TagInput(const char* label, std::nullptr_t, const TagInputStyle& ts = TagInputStyle()) {
    return TagInput(label, static_cast<std::string*>(nullptr), ts);
}

}  // namespace ctl
}  // namespace imb
