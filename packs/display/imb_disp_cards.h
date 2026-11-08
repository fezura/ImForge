#pragma once
// StatCard, EmptyState, TooltipCard
#include "imb_disp_charts.h"

namespace imb {
namespace disp {

struct StatCardStyle {
    ImVec2 size = ImVec2(0, 0);  // 0 width fills, 0 height fits the content
    const char* format = "%.0f";
    bool show_delta = false;
    float delta = 0.0f;
    const char* delta_format = "%.1f%%";  // gets the absolute value, the arrow shows the sign
    bool higher_is_better = true;
    const float* spark = nullptr;
    int spark_count = 0;
    ImU32 accent = 0;
    const char* icon = nullptr;  // short text or an icon font glyph, top right
    float value_scale = 1.9f;
};

// the value counts up when it first shows and glides on changes. Returns the value drawn.
inline float StatCard(const char* title, float value, const StatCardStyle& cs = StatCardStyle()) {
    using namespace detail;
    value = Fin(value);
    float font = ImGui::GetFontSize();
    float p = font * 0.8f;
    float sm = font * 0.88f;
    float big = font * Clamp(cs.value_scale, 1.0f, 4.0f);
    bool spark = cs.spark && cs.spark_count > 0;
    float spark_h = font * 2.1f;
    float h_auto = p + sm + font * 0.3f + big + (spark ? font * 0.5f + spark_h : 0.0f) + p;
    ImVec2 sz(ResolveW(cs.size.x, font * 9.0f), cs.size.y > 0.0f ? cs.size.y : h_auto);
    ImGuiID id = ImGui::GetID(title);
    ImVec2 mn = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton(title, sz);
    bool hovered = ImGui::IsItemHovered();
    float shown = Tween(id, value, 0.0f, GetStyle().anim_duration * 2.0f);
    float hov = imb::Transition(id ^ 0x4cf5ad43u, hovered, 0.15f);
    if (!ImGui::IsItemVisible()) return shown;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 mx = Add(mn, sz);
    float r = CardRounding();
    ImU32 accent = cs.accent ? ImGui::GetColorU32(cs.accent) : Accent();
    if (hov > 0.01f) imb::Shadow(dl, mn, mx, r, IM_COL32(0, 0, 0, (int)(70.0f * hov)), font * 0.7f);
    dl->AddRectFilled(mn, mx, CardBg(), r);
    Outline(dl, mn, mx, Mix(BorderCol(0.8f), WithAlpha(accent, 0.7f), hov), r, 1.0f);
    const char* tend = LabelEnd(title);
    Text(dl, sm, ImVec2(mn.x + p, mn.y + p), TextDim(), title, tend);
    if (cs.icon && *cs.icon) {
        float ir = font * 0.85f;
        ImVec2 ic(mx.x - p - ir, mn.y + p + ir * 0.6f);
        dl->AddCircleFilled(ic, ir, WithAlpha(accent, 0.16f));
        TextCentered(dl, font, ic, accent, cs.icon);
    }
    char vbuf[48];
    Format(vbuf, sizeof(vbuf), cs.format, shown);
    float vy = mn.y + p + sm + font * 0.3f;
    ImVec2 vs = TextSize(big, vbuf);
    Text(dl, big, ImVec2(mn.x + p, vy), TextCol(), vbuf);
    if (cs.show_delta) {
        const Style& st = GetStyle();
        float delta = Fin(cs.delta);
        bool up = delta >= 0.0f;
        bool good = up == cs.higher_is_better;
        ImU32 dc = ImGui::GetColorU32(delta == 0.0f ? Raw(ImGuiCol_TextDisabled) : (good ? st.success : st.danger));
        char dbuf[32];
        Format(dbuf, sizeof(dbuf), cs.delta_format, fabsf(delta));
        float fs = font * 0.8f;
        ImVec2 ds = TextSize(fs, dbuf);
        float tri = fs * 0.36f;
        float pw = ds.x + tri * 2.0f + font * 0.75f, ph = fs + font * 0.3f;
        ImVec2 pmn(mn.x + p + vs.x + font * 0.6f, vy + (vs.y - ph) * 0.5f);
        if (pmn.x + pw > mx.x - p) pmn = ImVec2(mn.x + p, vy + vs.y);
        ImVec2 pmx(pmn.x + pw, pmn.y + ph);
        dl->AddRectFilled(pmn, pmx, WithAlpha(dc, 0.15f), ph * 0.5f);
        ImVec2 tc(pmn.x + font * 0.35f + tri, (pmn.y + pmx.y) * 0.5f);
        if (delta == 0.0f) {
            dl->AddRectFilled(ImVec2(tc.x - tri, tc.y - 1.0f), ImVec2(tc.x + tri, tc.y + 1.0f), dc);
        } else if (up) {
            dl->AddTriangleFilled(ImVec2(tc.x, tc.y - tri), ImVec2(tc.x + tri, tc.y + tri * 0.7f),
                                  ImVec2(tc.x - tri, tc.y + tri * 0.7f), dc);
        } else {
            dl->AddTriangleFilled(ImVec2(tc.x - tri, tc.y - tri * 0.7f), ImVec2(tc.x + tri, tc.y - tri * 0.7f),
                                  ImVec2(tc.x, tc.y + tri), dc);
        }
        Text(dl, fs, ImVec2(tc.x + tri + font * 0.25f, pmn.y + (ph - ds.y) * 0.5f), dc, dbuf);
    }
    if (spark) {
        ImVec2 smn(mn.x + p, mx.y - p - spark_h), smx(mx.x - p, mx.y - p);
        SparklineStyle ss;
        ss.color = cs.accent;
        ss.format = cs.format;
        bool over = hovered && ImGui::IsMouseHoveringRect(smn, smx);
        int idx = DrawSpark(dl, id ^ 0x7a3c9e15u, smn, smx, cs.spark, cs.spark_count, ss, over);
        if (idx >= 0) {
            char buf[48];
            Format(buf, sizeof(buf), cs.format, cs.spark[idx]);
            ImGui::BeginTooltip();
            ImGui::TextUnformatted(buf);
            ImGui::EndTooltip();
        }
    }
    return shown;
}

inline float StatCard(const char* title, float value, const char* format, bool show_delta, float delta,
                      const float* spark, int spark_count) {
    StatCardStyle cs;
    cs.format = format;
    cs.show_delta = show_delta;
    cs.delta = delta;
    cs.spark = spark;
    cs.spark_count = spark_count;
    return StatCard(title, value, cs);
}

inline float StatCard(const char* title, float value, const char* format, bool show_delta, float delta,
                      std::nullptr_t, std::nullptr_t) {
    return StatCard(title, value, format, show_delta, delta, (const float*)nullptr, 0);
}

enum class EmptyIcon { None, Inbox, Search, Folder, Alert, Check };

struct EmptyStateStyle {
    EmptyIcon icon = EmptyIcon::Inbox;
    const char* icon_text = nullptr;  // icon font glyph instead of the drawn icon
    float icon_size = 0.0f;
    ImU32 icon_color = 0;
    float width = 0.0f;               // 0 = available width
    float max_text_width = 0.0f;      // 0 = 22 x font size
};

namespace detail {

inline void EmptyIconShape(ImDrawList* dl, EmptyIcon icon, const ImVec2& c, float s, ImU32 col) {
    float th = Max(1.5f, s * 0.09f);
    switch (icon) {
        case EmptyIcon::Inbox: {
            float w = s, h = s * 0.72f;
            ImVec2 a(c.x - w * 0.5f, c.y - h * 0.5f), b(c.x + w * 0.5f, c.y + h * 0.5f);
            float tray = a.y + h * 0.55f;
            dl->PathLineTo(ImVec2(a.x + w * 0.18f, a.y));
            dl->PathLineTo(ImVec2(b.x - w * 0.18f, a.y));
            dl->PathLineTo(ImVec2(b.x, tray));
            dl->PathLineTo(ImVec2(b.x, b.y));
            dl->PathLineTo(ImVec2(a.x, b.y));
            dl->PathLineTo(ImVec2(a.x, tray));
            Stroke(dl, col, th, true);
            dl->PathLineTo(ImVec2(a.x, tray));
            dl->PathLineTo(ImVec2(a.x + w * 0.3f, tray));
            dl->PathLineTo(ImVec2(a.x + w * 0.38f, tray + h * 0.18f));
            dl->PathLineTo(ImVec2(b.x - w * 0.38f, tray + h * 0.18f));
            dl->PathLineTo(ImVec2(b.x - w * 0.3f, tray));
            dl->PathLineTo(ImVec2(b.x, tray));
            Stroke(dl, col, th);
            break;
        }
        case EmptyIcon::Search: {
            float r = s * 0.32f;
            ImVec2 lc(c.x - s * 0.1f, c.y - s * 0.1f);
            dl->AddCircle(lc, r, col, 0, th);
            dl->AddLine(Polar(lc, r + th * 0.4f, kPi * 0.25f), Polar(lc, r + s * 0.32f, kPi * 0.25f), col, th * 1.4f);
            break;
        }
        case EmptyIcon::Folder: {
            float w = s, h = s * 0.74f;
            ImVec2 a(c.x - w * 0.5f, c.y - h * 0.5f), b(c.x + w * 0.5f, c.y + h * 0.5f);
            dl->PathLineTo(ImVec2(a.x, b.y));
            dl->PathLineTo(ImVec2(a.x, a.y));
            dl->PathLineTo(ImVec2(a.x + w * 0.36f, a.y));
            dl->PathLineTo(ImVec2(a.x + w * 0.46f, a.y + h * 0.16f));
            dl->PathLineTo(ImVec2(b.x, a.y + h * 0.16f));
            dl->PathLineTo(ImVec2(b.x, b.y));
            Stroke(dl, col, th, true);
            dl->AddLine(ImVec2(a.x, a.y + h * 0.32f), ImVec2(b.x, a.y + h * 0.32f), col, th);
            break;
        }
        case EmptyIcon::Alert: {
            float h = s * 0.86f;
            ImVec2 top(c.x, c.y - h * 0.55f), l(c.x - s * 0.5f, c.y + h * 0.45f), r(c.x + s * 0.5f, c.y + h * 0.45f);
            dl->PathLineTo(top);
            dl->PathLineTo(r);
            dl->PathLineTo(l);
            Stroke(dl, col, th, true);
            dl->AddLine(ImVec2(c.x, c.y - h * 0.18f), ImVec2(c.x, c.y + h * 0.14f), col, th);
            dl->AddCircleFilled(ImVec2(c.x, c.y + h * 0.3f), th * 0.75f, col);
            break;
        }
        case EmptyIcon::Check:
            dl->AddCircle(c, s * 0.48f, col, 0, th);
            CheckMark(dl, c, s * 0.42f, col, th);
            break;
        default: break;
    }
}

// greedy word wrap into at most `cap` lines
inline int WrapLines(const char* b, const char* e, float size, float width, const char** lb, const char** le, int cap) {
    int n = 0;
    const char* line = b;
    while (line < e && n < cap) {
        while (line < e && *line == ' ') ++line;
        if (line >= e) break;
        const char* best = nullptr;
        const char* p = line;
        while (p <= e) {
            const char* we = p;
            while (we < e && *we != ' ' && *we != '\n') ++we;
            if (TextSize(size, line, we).x > width && best) break;
            best = we;
            if (we >= e || *we == '\n') break;
            p = we + 1;
        }
        if (!best) best = e;
        lb[n] = line;
        le[n] = best;
        ++n;
        line = best < e ? best + 1 : e;
    }
    return n;
}

}  // namespace detail

// centered icon, title, hint and an optional button. Returns true when the button is clicked.
inline bool EmptyState(const char* title, const char* hint = nullptr, const char* button = nullptr,
                       const EmptyStateStyle& es = EmptyStateStyle()) {
    using namespace detail;
    float font = ImGui::GetFontSize();
    float w = es.width > 0.0f ? es.width : Avail();
    float tw = Min(es.max_text_width > 0.0f ? es.max_text_width : font * 22.0f, w);
    float d = es.icon_size > 0.0f ? es.icon_size : font * 3.6f;
    bool icon = es.icon != EmptyIcon::None || (es.icon_text && *es.icon_text);
    float title_fs = font * 1.15f;
    const char* tend = title ? LabelEnd(title) : nullptr;
    const char* lb[8];
    const char* le[8];
    int lines = hint && *hint ? WrapLines(hint, hint + strlen(hint), font, tw, lb, le, 8) : 0;
    float line_h = font * 1.3f;
    bool has_button = button && *button && LabelEnd(button) > button;
    const char* bend = has_button ? LabelEnd(button) : nullptr;
    ImVec2 bts = has_button ? ImGui::CalcTextSize(button, bend) : ImVec2(0, 0);
    ImVec2 bsz(bts.x + font * 2.2f, ImGui::GetFrameHeight() + font * 0.2f);
    float top_h = font * 0.8f + (icon ? d + font * 0.8f : 0.0f) + (tend > title ? title_fs : 0.0f) +
                  (lines > 0 ? font * 0.4f + line_h * (float)lines : 0.0f);
    ImGui::PushID(title ? title : "##empty");
    ImGui::BeginGroup();
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(w, top_h));
    bool pressed = false;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float cx = pos.x + w * 0.5f;
    float y = pos.y + font * 0.8f;
    if (icon) {
        ImVec2 c(cx, y + d * 0.5f);
        ImU32 ic = es.icon_color ? ImGui::GetColorU32(es.icon_color) : Accent();
        dl->AddCircleFilled(c, d * 0.5f, WithAlpha(ic, 0.12f));
        dl->AddCircle(c, d * 0.5f, WithAlpha(ic, 0.22f), 0, 1.0f);
        if (es.icon_text && *es.icon_text) TextCentered(dl, d * 0.42f, c, ic, es.icon_text);
        else EmptyIconShape(dl, es.icon, c, d * 0.42f, WithAlpha(ic, 0.9f));
        y += d + font * 0.8f;
    }
    if (tend > title) {
        ImVec2 ts = TextSize(title_fs, title, tend);
        Text(dl, title_fs, ImVec2(cx - ts.x * 0.5f, y), TextCol(), title, tend);
        y += title_fs;
    }
    if (lines > 0) {
        y += font * 0.4f;
        for (int i = 0; i < lines; ++i) {
            ImVec2 ts = TextSize(font, lb[i], le[i]);
            Text(dl, font, ImVec2(cx - ts.x * 0.5f, y + (line_h - font) * 0.5f), TextDim(), lb[i], le[i]);
            y += line_h;
        }
    }
    if (has_button) {
        ImGui::Dummy(ImVec2(w, font * 0.5f));
        ImVec2 bp(cx - bsz.x * 0.5f, ImGui::GetCursorScreenPos().y);
        ImGui::SetCursorScreenPos(bp);
        ImGuiID bid = ImGui::GetID(button);
        pressed = ImGui::InvisibleButton(button, bsz);
        bool hov = ImGui::IsItemHovered(), held = ImGui::IsItemActive();
        float t = imb::Transition(bid, hov, 0.12f);
        ImU32 bg = Accent();
        bg = held ? Mix(bg, IM_COL32(0, 0, 0, 255), 0.12f) : Lighten(bg, 0.12f * t);
        ImVec2 bmx = Add(bp, bsz);
        if (t > 0.01f) imb::Glow(dl, bp, bmx, bsz.y * 0.5f, WithAlpha(AccentRaw(), 0.4f * t), font * 0.5f);
        dl->AddRectFilled(bp, bmx, bg, bsz.y * 0.5f);
        Text(dl, font, ImVec2(cx - bts.x * 0.5f, bp.y + (bsz.y - bts.y) * 0.5f), OnColor(AccentRaw()), button, bend);
    }
    ImGui::Dummy(ImVec2(w, font * 0.6f));
    ImGui::EndGroup();
    ImGui::PopID();
    return pressed;
}

inline bool EmptyState(const char* title, const char* hint, const char* button, EmptyIcon icon) {
    EmptyStateStyle es;
    es.icon = icon;
    return EmptyState(title, hint, button, es);
}

struct TooltipCardStyle {
    ImU32 header = 0;                    // colored band behind the title, 0 = plain title
    ImTextureID image = ImTextureID();
    ImVec2 image_size = ImVec2(0, 0);    // 0 = card width at 16:9
    float width = 0.0f;                  // 0 = 20 x font size
    const char* footer = nullptr;        // small dim line at the bottom, a shortcut for example
    bool fade = true;
    float delay = 0.2f;                  // seconds of hovering before it shows
};

namespace detail {

struct CardLayout {
    float w, h, p, band, image_h, image_w, text_y, footer_y;
    int lines;
    const char* lb[16];
    const char* le[16];
};

inline void LayoutCard(CardLayout& L, const char* text, const TooltipCardStyle& ts) {
    float font = ImGui::GetFontSize();
    L.w = ts.width > 0.0f ? ts.width : font * 20.0f;
    L.p = font * 0.75f;
    float inner = L.w - L.p * 2.0f;
    L.band = font + L.p * 1.6f;
    float y = L.band + L.p * 0.7f;
    L.image_w = L.image_h = 0.0f;
    if (ts.image != ImTextureID()) {
        L.image_w = ts.image_size.x > 0.0f ? Min(ts.image_size.x, inner) : inner;
        L.image_h = ts.image_size.y > 0.0f ? ts.image_size.y : L.image_w * 9.0f / 16.0f;
        y += L.image_h + L.p * 0.7f;
    }
    L.text_y = y;
    L.lines = text && *text ? WrapLines(text, text + strlen(text), font, inner, L.lb, L.le, 16) : 0;
    y += (float)L.lines * font * 1.25f;
    L.footer_y = y + (L.lines ? L.p * 0.5f : 0.0f);
    if (ts.footer && *ts.footer) y = L.footer_y + font * 0.85f;
    L.h = y + L.p * 0.9f;
}

inline void DrawCard(ImDrawList* dl, const ImVec2& mn, const CardLayout& L, const char* title, const TooltipCardStyle& ts) {
    float font = ImGui::GetFontSize();
    float r = CardRounding();
    ImVec2 mx(mn.x + L.w, mn.y + L.h);
    imb::Shadow(dl, mn, mx, r, ImGui::GetColorU32(IM_COL32(0, 0, 0, 90)), font * 0.6f);
    dl->AddRectFilled(mn, mx, Col(0, ImGuiCol_PopupBg, 1.0f), r);
    const char* tend = title ? LabelEnd(title) : nullptr;
    if (ts.header) {
        ImU32 hc = ImGui::GetColorU32(ts.header);
        ImU32 hc2 = ImGui::GetColorU32(Mix(ts.header, IM_COL32(0, 0, 0, 255), 0.2f));
        // rounded on top, square where it meets the body
        imb::GradientRect(dl, mn, ImVec2(mx.x, mn.y + L.band), hc, hc2, false, r);
        imb::GradientRect(dl, ImVec2(mn.x, mn.y + L.band * 0.5f), ImVec2(mx.x, mn.y + L.band), hc, hc2, false, 0.0f);
        if (tend > title) Text(dl, font, ImVec2(mn.x + L.p, mn.y + (L.band - font) * 0.5f), OnColor(ts.header), title, tend);
    } else {
        if (tend > title) Text(dl, font, ImVec2(mn.x + L.p, mn.y + (L.band - font) * 0.5f), TextCol(), title, tend);
        dl->AddLine(ImVec2(mn.x + L.p, mn.y + L.band - 0.5f), ImVec2(mx.x - L.p, mn.y + L.band - 0.5f), BorderCol(0.6f), 1.0f);
    }
    if (L.image_h > 0.0f) {
        ImVec2 a(mn.x + L.p, mn.y + L.band + L.p * 0.7f);
        dl->AddImageRounded(ts.image, a, ImVec2(a.x + L.image_w, a.y + L.image_h), ImVec2(0, 0), ImVec2(1, 1),
                            ImGui::GetColorU32(IM_COL32_WHITE), Rounding());
    }
    float y = mn.y + L.text_y;
    for (int i = 0; i < L.lines; ++i) {
        Text(dl, font, ImVec2(mn.x + L.p, y), TextCol(0.85f), L.lb[i], L.le[i]);
        y += font * 1.25f;
    }
    if (ts.footer && *ts.footer) Text(dl, font * 0.85f, ImVec2(mn.x + L.p, mn.y + L.footer_y), TextDim(), ts.footer);
    Outline(dl, mn, mx, BorderCol(0.7f), r, 1.0f);
}

inline int& TooltipCardDepth() {
    static int d = 0;
    return d;
}

}  // namespace detail

// draws the card in the layout, for previews and docs
inline void TooltipCardInline(const char* title, const char* text, const TooltipCardStyle& ts = TooltipCardStyle()) {
    detail::CardLayout L;
    detail::LayoutCard(L, text, ts);
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(L.w, L.h));
    if (ImGui::IsItemVisible()) detail::DrawCard(ImGui::GetWindowDrawList(), pos, L, title, ts);
}

// call right after the item it describes; shows while that item is hovered
inline bool TooltipCard(const char* title, const char* text, const TooltipCardStyle& ts = TooltipCardStyle()) {
    using namespace detail;
    bool hovered = ImGui::IsItemHovered();
#if IMGUI_VERSION_NUM >= 18900
    ImGuiID item = ImGui::GetItemID();
#else
    ImGuiID item = 0;
#endif
    ImGuiID id = ImGui::GetID(title ? title : "##tooltipcard") ^ item ^ 0x5f3759dfu;
    ImGuiStorage* st = Store();
    float since = st->GetFloat(id, -1.0f);
    if (hovered && since < 0.0f) st->SetFloat(id, since = Now());
    else if (!hovered && since >= 0.0f) st->SetFloat(id, since = -1.0f);
    bool show = hovered && Now() - since >= ts.delay;
    float a = ts.fade ? imb::Transition(id, show, 0.14f) : (show ? 1.0f : 0.0f);
    if (!show || a <= 0.0f) return false;
    CardLayout L;
    LayoutCard(L, text, ts);
    float m = ImGui::GetFontSize() * 0.6f;
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * a);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_PopupBg, IM_COL32(0, 0, 0, 0));
#if IMGUI_VERSION_NUM >= 18940
    bool open = ImGui::BeginTooltip();
#else
    ImGui::BeginTooltip();
    bool open = true;
#endif
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
    if (open) {
        ImVec2 pos = ImGui::GetCursorScreenPos();
        ImGui::Dummy(ImVec2(L.w + m * 2.0f, L.h + m * 2.0f));
        DrawCard(ImGui::GetWindowDrawList(), ImVec2(pos.x + m, pos.y + m), L, title, ts);
        ImGui::EndTooltip();
    }
    ImGui::PopStyleVar();
    return open;
}

inline bool TooltipCard(const char* title, const char* text, ImU32 header, const char* footer = nullptr) {
    TooltipCardStyle ts;
    ts.header = header;
    ts.footer = footer;
    return TooltipCard(title, text, ts);
}

// free-form content inside the card frame; call EndTooltipCard() only when this returns true
inline bool BeginTooltipCard(const char* title, const TooltipCardStyle& ts = TooltipCardStyle()) {
    using namespace detail;
    float font = ImGui::GetFontSize();
    float p = font * 0.75f;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(p, p));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, CardRounding());
#if IMGUI_VERSION_NUM >= 18940
    bool open = ImGui::BeginTooltip();
#else
    ImGui::BeginTooltip();
    bool open = true;
#endif
    ImGui::PopStyleVar(2);
    if (!open) return false;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 wp = ImGui::GetWindowPos(), ws = ImGui::GetWindowSize();
    const char* tend = title ? LabelEnd(title) : nullptr;
    float band = font + p * 1.6f;
    if (ts.header) {
        dl->AddRectFilled(wp, ImVec2(wp.x + ws.x, wp.y + band), ImGui::GetColorU32(ts.header), CardRounding(), kRoundTop);
        if (tend > title) {
            ImGui::PushStyleColor(ImGuiCol_Text, OnColor(ts.header));
            ImGui::TextUnformatted(title, tend);
            ImGui::PopStyleColor();
        }
        ImGui::Dummy(ImVec2(0, p * 0.6f));
    } else if (tend > title) {
        ImGui::TextUnformatted(title, tend);
        ImGui::Separator();
    }
    float w = ts.width > 0.0f ? ts.width : font * 20.0f;
    ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + w - p * 2.0f);
    ++TooltipCardDepth();
    return true;
}

inline void EndTooltipCard() {
    int& d = detail::TooltipCardDepth();
    IM_ASSERT(d > 0 && "EndTooltipCard without a successful BeginTooltipCard");
    --d;
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
}

}  // namespace disp
}  // namespace imb
