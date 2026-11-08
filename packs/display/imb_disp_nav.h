#pragma once
// ProgressSteps, TimelineScrubber, Breadcrumbs, KeyOverlay
#include "imb_disp_badges.h"

namespace imb {
namespace disp {

struct StepsStyle {
    float radius = 0.0f;  // 0 = 0.55 x frame height
    float line_thickness = 0.0f;
    ImU32 done = 0;
    ImU32 current = 0;
    ImU32 todo = 0;
    bool numbers = true;
    bool labels = true;
    bool allow_back = true;  // clicking a finished step goes back to it
};

// *current is the active step. Returns true when a click changed it.
inline bool ProgressSteps(const char* id_label, const char* const* labels, int count, int* current,
                          const StepsStyle& ps = StepsStyle()) {
    using namespace detail;
    if (count <= 0) return false;
    float font = ImGui::GetFontSize();
    float r = ps.radius > 0.0f ? ps.radius : ImGui::GetFrameHeight() * 0.55f;
    float m = r * 0.45f;
    float sm = font * 0.85f;
    bool with_labels = ps.labels && labels;
    float w = Avail();
    float h = m * 2.0f + r * 2.0f + (with_labels ? font * 0.35f + sm : 0.0f);
    ImGuiID id = ImGui::GetID(id_label);
    ImVec2 mn = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton(id_label, ImVec2(w, h));
    bool hovered = ImGui::IsItemHovered();
    bool clicked = ImGui::IsItemClicked(0);
    int cur = current ? *current : 0;
    if (cur < 0) cur = 0;
    if (cur > count) cur = count;
    float p = Tween(id, (float)cur);
    float lw0 = with_labels && labels[0] ? TextSize(sm, labels[0]).x : 0.0f;
    float lwn = with_labels && labels[count - 1] ? TextSize(sm, labels[count - 1]).x : 0.0f;
    float pad_l = Max(r + m, lw0 * 0.5f), pad_r = Max(r + m, lwn * 0.5f);
    float span = Max(w - pad_l - pad_r, 1.0f);
    float cy = mn.y + m + r;
    auto cx = [&](int i) { return count > 1 ? mn.x + pad_l + span * (float)i / (float)(count - 1) : mn.x + w * 0.5f; };
    int hover_idx = -1;
    if (hovered) {
        float mxp = ImGui::GetMousePos().x;
        float best = FLT_MAX;
        for (int i = 0; i < count; ++i) {
            float d = fabsf(mxp - cx(i));
            if (d < best && d <= Max(r * 1.4f, span / (float)(count > 1 ? count - 1 : 1) * 0.5f)) {
                best = d;
                hover_idx = i;
            }
        }
    }
    bool can_click = hover_idx >= 0 && current && ps.allow_back && hover_idx < cur;
    if (can_click) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    bool changed = false;
    if (clicked && can_click) {
        *current = hover_idx;
        changed = true;
    }
    if (!ImGui::IsItemVisible()) return changed;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImU32 done_c = ps.done ? ImGui::GetColorU32(ps.done) : Accent();
    ImU32 cur_c = ps.current ? ImGui::GetColorU32(ps.current) : Accent();
    ImU32 todo_c = ps.todo ? ImGui::GetColorU32(ps.todo) : TrackCol();
    float lt = ps.line_thickness > 0.0f ? ps.line_thickness : Max(2.0f, font * 0.15f);
    float ring = Max(1.5f, r * 0.14f);
    for (int i = 0; i + 1 < count; ++i) {
        float a = cx(i) + r + m * 0.6f, b = cx(i + 1) - r - m * 0.6f;
        if (b <= a) continue;
        dl->AddLine(ImVec2(a, cy), ImVec2(b, cy), todo_c, lt);
        float k = Clamp01(p - (float)i);
        if (k > 0.0f) dl->AddLine(ImVec2(a, cy), ImVec2(Lerp(a, b, k), cy), done_c, lt);
    }
    float pulse = fmodf(Now() * 0.8f, 1.0f);
    for (int i = 0; i < count; ++i) {
        ImVec2 c(cx(i), cy);
        float reach = Clamp01(p - (float)i + 1.0f);
        float done = Clamp01(p - (float)i);
        ImU32 accent = Mix(cur_c, done_c, done);
        if (reach > 0.99f && done < 0.01f)
            dl->AddCircle(c, r + m * pulse, WithAlpha(cur_c, 0.45f * (1.0f - pulse)), 0, ring);
        if (i == hover_idx) dl->AddCircleFilled(c, r + m * 0.5f, TextCol(0.08f));
        dl->AddCircleFilled(c, r, Mix(CardBg(), accent, Lerp(0.0f, 0.18f, reach) + 0.82f * done));
        dl->AddCircle(c, r - ring * 0.5f, Mix(todo_c, accent, reach), 0, ring);
        if (done > 0.5f) {
            CheckMark(dl, c, r * 0.62f, WithAlpha(OnColor(accent), (done - 0.5f) * 2.0f), Max(1.5f, r * 0.17f));
        } else if (ps.numbers) {
            char nb[12];
            snprintf(nb, sizeof(nb), "%d", i + 1);
            TextCentered(dl, sm, c, WithAlpha(Mix(TextDim(), accent, reach), 1.0f - done * 2.0f), nb);
        }
        if (with_labels && labels[i]) {
            ImVec2 ts = TextSize(sm, labels[i]);
            float lx = Clamp(c.x - ts.x * 0.5f, mn.x, mn.x + w - ts.x);
            ImU32 lc = i == cur ? TextCol() : (i < cur ? TextCol(0.75f) : TextDim());
            Text(dl, sm, ImVec2(lx, cy + r + m + font * 0.35f), lc, labels[i]);
        }
    }
    return changed;
}

inline bool ProgressSteps(const char* id_label, const char* labels_zero_separated, int* current,
                          const StepsStyle& ps = StepsStyle()) {
    const char* items[64];
    int n = detail::SplitZero(labels_zero_separated, items, 64);
    return ProgressSteps(id_label, items, n, current, ps);
}

struct ScrubberStyle {
    float height = 0.0f;  // 0 = 1.2 x frame height
    ImU32 track = 0;
    ImU32 fill = 0;
    ImU32 playhead = 0;
    ImU32 marker = 0;
    const ImU32* marker_colors = nullptr;  // one per marker, optional
    bool show_time = true;
    float snap_px = 0.0f;  // 0 = 0.6 x font size; markers pull the playhead in while dragging
};

// *time in seconds, 0..duration. Returns true while dragging changes it.
inline bool TimelineScrubber(const char* label, float* time, float duration, const float* markers = nullptr,
                             int marker_count = 0, const ScrubberStyle& ss = ScrubberStyle()) {
    using namespace detail;
    float font = ImGui::GetFontSize();
    float w = Avail();
    float h = ss.height > 0.0f ? ss.height : ImGui::GetFrameHeight() * 1.2f;
    duration = Fin(duration);
    if (duration <= 0.0f) duration = 1.0f;
    if (duration > 1.0e8f) duration = 1.0e8f;
    float t = time ? Clamp(Fin(*time), 0.0f, duration) : 0.0f;
    char tmpl[16];
    FormatTime(tmpl, sizeof(tmpl), duration);
    for (char* c = tmpl; *c; ++c)
        if (*c >= '0' && *c <= '9') *c = '0';
    float lab_w = ss.show_time ? TextSize(font * 0.85f, tmpl).x : 0.0f;
    float gap = ss.show_time ? font * 0.6f : 0.0f;
    ImGuiID id = ImGui::GetID(label);
    ImVec2 mn = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton(label, ImVec2(w, h));
    bool hovered = ImGui::IsItemHovered(), active = ImGui::IsItemActive();
    float x0 = mn.x + lab_w + gap, x1 = mn.x + w - lab_w - gap;
    if (x1 - x0 < 8.0f) {
        x0 = mn.x;
        x1 = mn.x + w;
    }
    float snap = ss.snap_px > 0.0f ? ss.snap_px : font * 0.6f;
    float mouse_x = ImGui::GetMousePos().x;
    int near_marker = -1;
    if ((hovered || active) && markers) {
        float best = snap;
        for (int i = 0; i < marker_count; ++i) {
            float mxp = x0 + (x1 - x0) * Fin01(markers[i] / duration);
            float d = fabsf(mxp - mouse_x);
            if (d <= best) {
                best = d;
                near_marker = i;
            }
        }
    }
    bool changed = false;
    if (active && time) {
        float nt = Clamp01((mouse_x - x0) / Max(x1 - x0, 1.0f)) * duration;
        if (near_marker >= 0) nt = Clamp(Fin(markers[near_marker]), 0.0f, duration);
        if (nt != *time) {
            *time = nt;
            t = nt;
            changed = true;
        }
    }
    float grow = imb::Transition(id ^ 0x3f84d5b5u, hovered || active, 0.12f);
    if (!ImGui::IsItemVisible()) return changed;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float cy = mn.y + h * 0.5f;
    float th = Max(3.0f, font * 0.24f) * (1.0f + 0.35f * grow);
    ImU32 track = ss.track ? ImGui::GetColorU32(ss.track) : TrackCol();
    ImU32 fill = ss.fill ? ImGui::GetColorU32(ss.fill) : Accent();
    ImU32 head = ss.playhead ? ImGui::GetColorU32(ss.playhead) : TextCol();
    static const float kSteps[] = {0.1f, 0.25f, 0.5f, 1, 2, 5, 10, 15, 30, 60, 120, 300, 600, 900, 1800, 3600};
    float px_per_s = (x1 - x0) / duration;
    float step = kSteps[IM_ARRAYSIZE(kSteps) - 1];
    for (int i = 0; i < IM_ARRAYSIZE(kSteps); ++i)
        if (kSteps[i] * px_per_s >= font * 3.5f) {
            step = kSteps[i];
            break;
        }
    float tick_f = duration / step;
    int nticks = tick_f > 512.0f ? 512 : (int)tick_f;
    for (int i = 0; i <= nticks && i <= 512; ++i) {
        float x = x0 + (float)i * step * px_per_s;
        dl->AddLine(ImVec2(x, cy + th * 0.5f + font * 0.18f), ImVec2(x, cy + th * 0.5f + font * 0.4f), TextDim(0.5f), 1.0f);
    }
    dl->AddRectFilled(ImVec2(x0, cy - th * 0.5f), ImVec2(x1, cy + th * 0.5f), track, th * 0.5f);
    float hx = x0 + (x1 - x0) * Clamp01(t / duration);
    if (hx - x0 > 0.5f) dl->AddRectFilled(ImVec2(x0, cy - th * 0.5f), ImVec2(hx, cy + th * 0.5f), fill, th * 0.5f);
    for (int i = 0; markers && i < marker_count; ++i) {
        float mxp = x0 + (x1 - x0) * Fin01(markers[i] / duration);
        float s = font * 0.28f * (i == near_marker ? 1.4f : 1.0f);
        ImU32 mc = ss.marker_colors ? ImGui::GetColorU32(ss.marker_colors[i]) : Pick(ss.marker, GetStyle().warning);
        dl->AddQuadFilled(ImVec2(mxp, cy - s * 1.3f), ImVec2(mxp + s, cy), ImVec2(mxp, cy + s * 1.3f), ImVec2(mxp - s, cy), mc);
    }
    if (hovered && !active) {
        float gx = Clamp(mouse_x, x0, x1);
        dl->AddLine(ImVec2(gx, mn.y + 2.0f), ImVec2(gx, mn.y + h - 2.0f), TextDim(0.6f), 1.0f);
        char gb[24];
        FormatTime(gb, sizeof(gb), near_marker >= 0 ? markers[near_marker] : Clamp01((gx - x0) / Max(x1 - x0, 1.0f)) * duration);
        ImGui::BeginTooltip();
        ImGui::TextUnformatted(gb);
        ImGui::EndTooltip();
    }
    float kr = th * (1.15f + 0.35f * grow);
    dl->AddLine(ImVec2(hx, mn.y + 1.0f), ImVec2(hx, mn.y + h - 1.0f), WithAlpha(head, 0.85f), Max(1.5f, font * 0.1f));
    if (grow > 0.01f) dl->AddCircleFilled(ImVec2(hx, cy), kr * 1.9f, WithAlpha(fill, 0.25f * grow));
    dl->AddCircleFilled(ImVec2(hx, cy), kr, head);
    dl->AddCircleFilled(ImVec2(hx, cy), kr * 0.45f, fill);
    if (ss.show_time) {
        char a[16], b[16];
        FormatTime(a, sizeof(a), t);
        FormatTime(b, sizeof(b), duration);
        float fs = font * 0.85f;
        ImVec2 as = TextSize(fs, a), bs = TextSize(fs, b);
        Text(dl, fs, ImVec2(mn.x + lab_w - as.x, cy - as.y * 0.5f), active ? TextCol() : TextCol(0.85f), a);
        Text(dl, fs, ImVec2(mn.x + w - lab_w, cy - bs.y * 0.5f), TextDim(), b);
    }
    return changed;
}

inline bool TimelineScrubber(const char* label, float* time, float duration, std::nullptr_t, std::nullptr_t) {
    return TimelineScrubber(label, time, duration, (const float*)nullptr, 0);
}

struct BreadcrumbStyle {
    ImU32 text = 0;
    ImU32 current = 0;
    ImU32 hover = 0;
    ImU32 separator = 0;
    bool collapse = true;  // too wide: keep the first and last items, hide the middle behind "..."
};

// returns the clicked item, or -1. The last item is the current location and isn't clickable.
inline int Breadcrumbs(const char* id_label, const char* const* items, int count,
                       const BreadcrumbStyle& bs = BreadcrumbStyle()) {
    using namespace detail;
    if (count <= 0 || !items) return -1;
    if (count > 64) count = 64;
    float font = ImGui::GetFontSize();
    float pad = font * 0.45f, sep_w = font * 1.1f, h = ImGui::GetFrameHeight();
    float widths[64];
    float total = 0.0f;
    for (int i = 0; i < count; ++i) {
        widths[i] = ImGui::CalcTextSize(items[i] ? items[i] : "").x + pad * 2.0f;
        total += widths[i] + (i > 0 ? sep_w : 0.0f);
    }
    float avail = Avail();
    float more_w = ImGui::CalcTextSize("...").x + pad * 2.0f;
    int hide_from = 1, hide_to = 1;  // hidden range [hide_from, hide_to)
    if (bs.collapse && total > avail && count > 2) {
        float used = widths[0] + sep_w + more_w;
        int k = count;
        while (k > 1) {
            float add = sep_w + widths[k - 1];
            if (k < count && used + add > avail) break;
            used += add;
            --k;
        }
        if (k > 1) hide_to = k;
    }
    int clicked = -1;
    ImGui::PushID(id_label);
    ImGui::BeginGroup();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImU32 sep = bs.separator ? ImGui::GetColorU32(bs.separator) : TextDim(0.7f);
    bool first = true;
    for (int i = 0; i < count; ++i) {
        bool more = i == hide_from && hide_to > hide_from;
        if (i > hide_from && i < hide_to) continue;
        if (!first) {
            ImGui::SameLine(0.0f, 0.0f);
            ImVec2 sp = ImGui::GetCursorScreenPos();
            ImGui::Dummy(ImVec2(sep_w, h));
            Chevron(dl, ImVec2(sp.x + sep_w * 0.5f, sp.y + h * 0.5f), font * 0.22f, sep, Max(1.2f, font * 0.09f));
            ImGui::SameLine(0.0f, 0.0f);
        }
        first = false;
        const char* text = more ? "..." : (items[i] ? items[i] : "");
        float iw = more ? more_w : widths[i];
        ImVec2 pos = ImGui::GetCursorScreenPos();
        bool last = i == count - 1;
        bool hov = false;
        ImGui::PushID(more ? -1 : i);
        ImGuiID bid = ImGui::GetID("##crumb");
        if (last) {
            ImGui::Dummy(ImVec2(iw, h));
        } else if (ImGui::InvisibleButton("##crumb", ImVec2(iw, h))) {
            if (more) ImGui::OpenPopup("##crumbs_more");
            else clicked = i;
        }
        if (!last) hov = ImGui::IsItemHovered();
        if (more && ImGui::BeginPopup("##crumbs_more")) {
            for (int j = hide_from; j < hide_to; ++j) {
                ImGui::PushID(j);
                if (ImGui::Selectable(items[j] ? items[j] : "")) clicked = j;
                ImGui::PopID();
            }
            ImGui::EndPopup();
        }
        ImGui::PopID();
        float t = last ? 0.0f : imb::Transition(bid, hov, 0.12f);
        if (t > 0.01f) dl->AddRectFilled(pos, ImVec2(pos.x + iw, pos.y + h), TextCol(0.08f * t), Rounding());
        ImU32 tc;
        if (last) tc = bs.current ? ImGui::GetColorU32(bs.current) : TextCol();
        else tc = Mix(bs.text ? ImGui::GetColorU32(bs.text) : TextDim(), bs.hover ? ImGui::GetColorU32(bs.hover) : TextCol(), t);
        ImVec2 ts = ImGui::CalcTextSize(text);
        Text(dl, font, ImVec2(pos.x + pad, pos.y + (h - ts.y) * 0.5f), tc, text);
    }
    ImGui::EndGroup();
    ImGui::PopID();
    return clicked;
}

inline bool Breadcrumbs(const char* id_label, const char* items_zero_separated, int* clicked,
                        const BreadcrumbStyle& bs = BreadcrumbStyle()) {
    const char* items[64];
    int n = detail::SplitZero(items_zero_separated, items, 64);
    int c = Breadcrumbs(id_label, items, n, bs);
    if (c >= 0 && clicked) *clicked = c;
    return c >= 0;
}

struct KeyOverlayStyle {
    float key_size = 0.0f;  // 0 = 1.9 x font size
    float gap = 0.0f;
    const char* keys = "WASD";  // top key, then the three below it
    bool space = true;
    bool mouse = true;
    ImU32 active = 0;
    const bool* state = nullptr;  // 7 flags in the order KeyOverlay returns, instead of live input
};

namespace detail {

inline bool LetterDown(char c) {
    if (c >= 'a' && c <= 'z') c = (char)(c - 32);
#if IMGUI_VERSION_NUM >= 18700
    if (c >= 'A' && c <= 'Z') return ImGui::IsKeyDown((ImGuiKey)(ImGuiKey_A + (c - 'A')));
    if (c >= '0' && c <= '9') return ImGui::IsKeyDown((ImGuiKey)(ImGuiKey_0 + (c - '0')));
    return false;
#else
    // before 1.87 io.KeysDown takes the backend's native codes, Win32 and GLFW use ASCII for letters
    const ImGuiIO& io = ImGui::GetIO();
    int k = (unsigned char)c;
    return k < IM_ARRAYSIZE(io.KeysDown) && io.KeysDown[k];
#endif
}

inline bool SpaceDown() {
#if IMGUI_VERSION_NUM >= 18700
    return ImGui::IsKeyDown(ImGuiKey_Space);
#else
    int k = ImGui::GetKeyIndex(ImGuiKey_Space);
    return k >= 0 && ImGui::IsKeyDown(k);
#endif
}

}  // namespace detail

// bit per key, in order: the four letters, space, left mouse, right mouse
inline int KeyOverlay(const char* id_label, const KeyOverlayStyle& ko = KeyOverlayStyle()) {
    using namespace detail;
    float font = ImGui::GetFontSize();
    float k = ko.key_size > 0.0f ? ko.key_size : font * 1.9f;
    float g = ko.gap > 0.0f ? ko.gap : Max(2.0f, k * 0.12f);
    float space_h = k * 0.6f;
    float mouse_w = k * 1.25f;
    float w = k * 3.0f + g * 2.0f + (ko.mouse ? g * 3.0f + mouse_w : 0.0f);
    float h = k * 2.0f + g + (ko.space ? g + space_h : 0.0f);
    ImGuiID id = ImGui::GetID(id_label);
    ImVec2 mn = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(w, h));
    char letters[4] = {'W', 'A', 'S', 'D'};
    if (ko.keys)
        for (int i = 0; i < 4 && ko.keys[i]; ++i) letters[i] = ko.keys[i];
    bool down[7];
    if (ko.state) {
        for (int i = 0; i < 7; ++i) down[i] = ko.state[i];
    } else {
        for (int i = 0; i < 4; ++i) down[i] = LetterDown(letters[i]);
        down[4] = SpaceDown();
        down[5] = ImGui::IsMouseDown(0);
        down[6] = ImGui::IsMouseDown(1);
    }
    int mask = 0;
    float press[7];
    for (int i = 0; i < 7; ++i) {
        if (down[i]) mask |= 1 << i;
        press[i] = imb::Transition(id + 1u + (ImGuiID)i, down[i], 0.07f);
    }
    if (!ImGui::IsItemVisible()) return mask;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    KbdStyle ks;
    ImU32 on = ko.active ? ImGui::GetColorU32(ko.active) : Accent();
    ImVec2 cell[4] = {ImVec2(mn.x + k + g, mn.y), ImVec2(mn.x, mn.y + k + g), ImVec2(mn.x + k + g, mn.y + k + g),
                      ImVec2(mn.x + (k + g) * 2.0f, mn.y + k + g)};
    for (int i = 0; i < 4; ++i) {
        ImVec2 b = Add(cell[i], ImVec2(k, k));
        if (press[i] > 0.01f) imb::Glow(dl, cell[i], b, Rounding(), WithAlpha(on, 0.45f * press[i]), k * 0.25f);
        KeyCap(dl, cell[i], b, &letters[i], &letters[i] + 1, press[i], ks, font, ko.active);
    }
    if (ko.space) {
        ImVec2 a(mn.x, mn.y + k * 2.0f + g * 2.0f), b(mn.x + k * 3.0f + g * 2.0f, a.y + space_h);
        if (press[4] > 0.01f) imb::Glow(dl, a, b, Rounding(), WithAlpha(on, 0.45f * press[4]), k * 0.25f);
        KeyCap(dl, a, b, "Space", nullptr, press[4], ks, font * 0.8f, ko.active);
    }
    if (ko.mouse) {
        ImVec2 a(mn.x + k * 3.0f + g * 5.0f, mn.y), b(a.x + mouse_w, mn.y + k * 2.0f + g);
        float rr = mouse_w * 0.5f;
        float split = a.y + (b.y - a.y) * 0.45f, mid = (a.x + b.x) * 0.5f;
        dl->AddRectFilled(a, b, CardBg(0.12f), rr);
        ImU32 face = CardBg(0.12f);
        if (press[5] > 0.01f) dl->AddRectFilled(a, ImVec2(mid, split), Mix(face, on, press[5] * 0.85f), rr, kRoundTopLeft);
        if (press[6] > 0.01f) dl->AddRectFilled(ImVec2(mid, a.y), ImVec2(b.x, split), Mix(face, on, press[6] * 0.85f), rr, kRoundTopRight);
        dl->AddLine(ImVec2(a.x + 1.0f, split), ImVec2(b.x - 1.0f, split), BorderCol(), 1.0f);
        dl->AddLine(ImVec2(mid, a.y + 1.0f), ImVec2(mid, split), BorderCol(), 1.0f);
        float wh = (split - a.y) * 0.42f, ww = mouse_w * 0.12f;
        dl->AddRectFilled(ImVec2(mid - ww, a.y + (split - a.y - wh) * 0.5f), ImVec2(mid + ww, a.y + (split - a.y + wh) * 0.5f),
                          TextDim(0.8f), ww);
        Outline(dl, a, b, BorderCol(), rr, 1.0f);
    }
    return mask;
}

inline int KeyOverlay(const char* id_label, float key_size, bool mouse = true) {
    KeyOverlayStyle ko;
    ko.key_size = key_size;
    ko.mouse = mouse;
    return KeyOverlay(id_label, ko);
}

}  // namespace disp
}  // namespace imb
