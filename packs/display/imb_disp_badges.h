#pragma once
// Badge, Avatar, StatusDot, Kbd, Divider
#include "imb_disp_core.h"

namespace imb {
namespace disp {

enum class BadgeKind { Pill, Count, Dot };

struct BadgeStyle {
    ImU32 color = 0;       // pills use the accent, counts and dots the danger color
    ImU32 text = 0;        // 0 = white or near-black, whichever reads better
    int max_count = 99;    // shows "99+" above this
    bool show_zero = false;
    bool subtle = false;   // tinted background with colored text
    bool attach = false;   // sits on the top-right corner of the previous item and takes no space
    float pop = 0.45f;     // seconds of the bump when the value changes
};

namespace detail {

inline float PopScale(ImGuiID id, int value, float duration) {
    float t0 = ChangeTime(id, value);
    if (t0 < 0.0f || duration <= 0.0f) return 1.0f;
    float x = (Now() - t0) / duration;
    if (x < 0.0f || x >= 1.0f) return 1.0f;
    return 1.0f + 0.42f * sinf(x * kPi) * (1.0f - x);
}

inline void DrawBadge(const char* tb, const char* te, BadgeKind kind, ImU32 col, int pop_value, ImGuiID id,
                      const BadgeStyle& bs) {
    float font = ImGui::GetFontSize();
    float sm = font * 0.85f;
    float h = sm + font * 0.3f;
    bool dot = kind == BadgeKind::Dot;
    ImVec2 ts = dot ? ImVec2(0, 0) : TextSize(sm, tb, te);
    float w = dot ? font * 0.62f : Max(h, ts.x + h * 0.75f);
    if (dot) h = w;
    float scale = PopScale(id, pop_value, bs.pop);
    ImVec2 c;
    if (bs.attach) {
        ImVec2 imn = ImGui::GetItemRectMin(), imx = ImGui::GetItemRectMax();
        c = ImVec2(imx.x - Min(w * 0.3f, font * 0.35f), imn.y + Min(h * 0.3f, font * 0.3f));
    } else {
        float fh = ImGui::GetFrameHeight();
        ImVec2 pos = ImGui::GetCursorScreenPos();
        ImGui::Dummy(ImVec2(w, Max(fh, h)));
        if (!ImGui::IsItemVisible()) return;
        c = ImVec2(pos.x + w * 0.5f, pos.y + Max(fh, h) * 0.5f);
    }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float sw = w * scale, sh = h * scale;
    ImVec2 mn(c.x - sw * 0.5f, c.y - sh * 0.5f), mx(c.x + sw * 0.5f, c.y + sh * 0.5f);
    ImU32 bg = ImGui::GetColorU32(col);
    if (bs.attach) dl->AddRectFilled(ImVec2(mn.x - 1.5f, mn.y - 1.5f), ImVec2(mx.x + 1.5f, mx.y + 1.5f), WindowBg(), sh * 0.5f + 1.5f);
    if (bs.subtle) {
        dl->AddRectFilled(mn, mx, WithAlpha(bg, 0.18f), sh * 0.5f);
        Outline(dl, mn, mx, WithAlpha(bg, 0.55f), sh * 0.5f, 1.0f);
    } else {
        dl->AddRectFilled(mn, mx, bg, sh * 0.5f);
    }
    if (dot || te <= tb) return;
    ImU32 tc = bs.text ? ImGui::GetColorU32(bs.text) : (bs.subtle ? Lighten(bg, 0.25f) : OnColor(col));
    float fs = sm * scale;
    ImVec2 tsz = TextSize(fs, tb, te);
    Text(dl, fs, ImVec2(c.x - tsz.x * 0.5f, c.y - tsz.y * 0.5f), tc, tb, te);
}

}  // namespace detail

inline void Badge(const char* text, const BadgeStyle& bs = BadgeStyle()) {
    ImGuiID id = ImGui::GetID(text);
    ImU32 col = bs.color ? bs.color : detail::AccentRaw();
    const char* end = detail::LabelEnd(text);
    unsigned h = 0;
    for (const char* p = text; p < end; ++p) h = h * 31u + (unsigned char)*p;
    detail::DrawBadge(text, end, BadgeKind::Pill, col, (int)h, id, bs);
}

inline void Badge(const char* text, ImU32 color) {
    BadgeStyle bs;
    bs.color = color;
    Badge(text, bs);
}

// bumps when count changes, hidden at 0 unless show_zero
inline void BadgeCount(const char* id_label, int count, const BadgeStyle& bs = BadgeStyle()) {
    if (count <= 0 && !bs.show_zero) {
        detail::ChangeTime(ImGui::GetID(id_label), count);
        return;
    }
    char buf[16];
    int cap = bs.max_count > 0 ? bs.max_count : 99;
    if (count > cap) snprintf(buf, sizeof(buf), "%d+", cap);
    else snprintf(buf, sizeof(buf), "%d", count);
    ImU32 col = bs.color ? bs.color : GetStyle().danger;
    detail::DrawBadge(buf, buf + strlen(buf), BadgeKind::Count, col, count, ImGui::GetID(id_label), bs);
}

inline void BadgeDot(const char* id_label, const BadgeStyle& bs = BadgeStyle()) {
    ImU32 col = bs.color ? bs.color : GetStyle().danger;
    detail::DrawBadge(nullptr, nullptr, BadgeKind::Dot, col, 0, ImGui::GetID(id_label), bs);
}

inline void Badge(const char* text_or_id, BadgeKind kind, int count, ImU32 color = 0, bool attach = false) {
    BadgeStyle bs;
    bs.color = color;
    bs.attach = attach;
    if (kind == BadgeKind::Count) BadgeCount(text_or_id, count, bs);
    else if (kind == BadgeKind::Dot) BadgeDot(text_or_id, bs);
    else Badge(text_or_id, bs);
}

struct AvatarStyle {
    float size = 0.0f;  // diameter, 0 = 1.6 x frame height
    ImTextureID texture = ImTextureID();
    ImU32 color = 0;    // 0 = picked from the name
    Status status = Status::None;
    bool ring = false;  // colored ring in the status color (accent without a status)
    bool square = false;
};

// up to two letters: first letters of the first and last word ("Ada Lovelace" gives "AL")
inline int Initials(const char* name, char* out, int cap) {
    if (!out || cap < 1) return 0;
    out[0] = 0;
    if (!name || cap < 2) return 0;
    const char* end = detail::LabelEnd(name);
    const char* words[16];
    int nw = 0;
    bool in_word = false;
    for (const char* p = name; p < end; ++p) {
        bool sep = *p == ' ' || *p == '\t' || *p == '.' || *p == '_' || *p == '-';
        if (!sep && !in_word && nw < 16) words[nw++] = p;
        in_word = !sep;
    }
    int n = 0;
    for (int w = 0; w < nw && w < 2; ++w) {
        const char* p = words[w == 0 ? 0 : nw - 1];
        int len = 1;
        unsigned char c0 = (unsigned char)*p;
        if (c0 >= 0xF0) len = 4;
        else if (c0 >= 0xE0) len = 3;
        else if (c0 >= 0xC0) len = 2;
        if (p + len > end || n + len >= cap) break;
        for (int k = 0; k < len; ++k) {
            char ch = p[k];
            if (len == 1 && ch >= 'a' && ch <= 'z') ch = (char)(ch - 32);
            out[n++] = ch;
        }
        if (nw == 1) break;
    }
    out[n] = 0;
    return n;
}

inline ImU32 AvatarColor(const char* name) {
    const char* end = detail::LabelEnd(name);
    return detail::HashColor(name, end);
}

inline void Avatar(const char* name, const AvatarStyle& as = AvatarStyle()) {
    using namespace detail;
    float d = as.size > 0.0f ? as.size : ImGui::GetFrameHeight() * 1.6f;
    ImVec2 mn = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(d, d));
    if (!ImGui::IsItemVisible()) return;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 mx(mn.x + d, mn.y + d), c(mn.x + d * 0.5f, mn.y + d * 0.5f);
    float rounding = as.square ? d * 0.28f : d * 0.5f;
    ImU32 base = as.color ? as.color : (name ? AvatarColor(name) : AccentRaw());
    ImU32 status_col = StatusColor(as.status);
    float inset = 0.0f;
    if (as.ring) {
        ImU32 rc = ImGui::GetColorU32(status_col ? status_col : AccentRaw());
        float rth = Max(1.5f, d * 0.05f);
        if (as.square) Outline(dl, mn, mx, rc, rounding, rth);
        else dl->AddCircle(c, d * 0.5f - rth * 0.5f, rc, 0, rth);
        inset = rth * 2.0f;
    }
    ImVec2 a(mn.x + inset, mn.y + inset), b(mx.x - inset, mx.y - inset);
    float ir = Max(rounding - inset, 0.0f);
    if (as.texture != ImTextureID()) {
        dl->AddImageRounded(as.texture, a, b, ImVec2(0, 0), ImVec2(1, 1), ImGui::GetColorU32(IM_COL32_WHITE), ir);
    } else {
        ImU32 top = ImGui::GetColorU32(Lighten(base, 0.12f)), bottom = ImGui::GetColorU32(Mix(base, IM_COL32(0, 0, 0, 255), 0.12f));
        imb::GradientRect(dl, a, b, top, bottom, true, ir);
        char ini[12];
        if (Initials(name, ini, sizeof(ini)) > 0) {
            float fs = (b.x - a.x) * (ini[1] ? 0.38f : 0.45f);
            TextCentered(dl, fs, c, OnColor(base), ini);
        }
    }
    if (status_col && !as.ring) {
        float sr = Max(3.0f, d * 0.15f);
        ImVec2 sc(mx.x - sr * (as.square ? 0.6f : 1.05f), mx.y - sr * (as.square ? 0.6f : 1.05f));
        dl->AddCircleFilled(sc, sr + Max(1.5f, d * 0.04f), WindowBg());
        dl->AddCircleFilled(sc, sr, ImGui::GetColorU32(status_col));
    }
}

inline void Avatar(const char* name, float size, Status status, ImTextureID texture) {
    AvatarStyle as;
    as.size = size;
    as.status = status;
    as.texture = texture;
    Avatar(name, as);
}

inline void Avatar(const char* name, float size, Status status, std::nullptr_t) {
    Avatar(name, size, status, ImTextureID());
}

struct StatusDotStyle {
    float radius = 0.0f;  // 0 = 0.3 x font size
    bool pulse = true;    // ripple for online and busy
    bool show_label = true;
    ImU32 color = 0;      // overrides the status color
};

// label null or empty shows the status name, "##id" shows nothing
inline void StatusDot(Status s, const char* label = nullptr, const StatusDotStyle& ds = StatusDotStyle()) {
    using namespace detail;
    float font = ImGui::GetFontSize();
    float r = ds.radius > 0.0f ? ds.radius : font * 0.3f;
    const char* text = (label && *label) ? label : StatusName(s);
    const char* end = (label && *label) ? LabelEnd(label) : text + strlen(text);
    bool with_text = ds.show_label && end > text;
    ImVec2 ts = with_text ? ImGui::CalcTextSize(text, end) : ImVec2(0, 0);
    float spacing = ImGui::GetStyle().ItemInnerSpacing.x;
    float fh = ImGui::GetFrameHeight();
    float dot_w = r * 2.0f + (ds.pulse ? r * 1.2f : 0.0f);
    // unlabeled dots are keyed by their spot in the window, so several of them don't share state
    ImVec2 lp = ImGui::GetCursorPos();
    unsigned spot = ((unsigned)(int)lp.x * 73856093u) ^ ((unsigned)(int)lp.y * 19349663u);
    ImGuiID id = label && *label ? ImGui::GetID(label) : ImGui::GetID((const void*)(uintptr_t)spot);
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(dot_w + (with_text ? spacing + ts.x : 0.0f), fh));
    ImGuiStorage* st = Store();
    ImGuiID kcur = id ^ 0x68e31da4u, kprev = id ^ 0xb5297a4du, kt = id ^ 0x1b56c4e9u;
    int cur = st->GetInt(kcur, -1);
    if (cur < 0) {
        st->SetInt(kcur, (int)s);
        st->SetInt(kprev, (int)s);
        st->SetFloat(kt, -100.0f);
    } else if (cur != (int)s) {
        st->SetInt(kprev, cur);
        st->SetInt(kcur, (int)s);
        st->SetFloat(kt, Now());
    }
    if (!ImGui::IsItemVisible()) return;
    float k = Clamp01((Now() - st->GetFloat(kt, -100.0f)) / 0.3f);
    ImU32 dim = Raw(ImGuiCol_TextDisabled);
    ImU32 from = StatusColor((Status)st->GetInt(kprev, (int)s)), to = StatusColor(s);
    if (!from) from = dim;
    if (!to) to = dim;
    ImU32 col = ds.color ? ImGui::GetColorU32(ds.color) : ImGui::GetColorU32(Mix(from, to, k));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 c(pos.x + dot_w * 0.5f, pos.y + fh * 0.5f);
    if (ds.pulse && (s == Status::Online || s == Status::Busy)) {
        float ph = fmodf(Now() * 0.75f, 1.0f);
        float e = 1.0f - (1.0f - ph) * (1.0f - ph);
        dl->AddCircle(c, r * (1.0f + 0.65f * e), WithAlpha(col, 0.55f * (1.0f - ph)), 0, Max(1.0f, r * 0.35f));
    }
    switch (s) {
        case Status::Offline:
        case Status::None:
            dl->AddCircle(c, r - 0.75f, col, 0, Max(1.5f, r * 0.42f));
            break;
        case Status::Away:
            dl->AddCircleFilled(c, r, col);
            dl->AddCircleFilled(ImVec2(c.x - r * 0.45f, c.y - r * 0.45f), r * 0.62f, WindowBg());
            break;
        case Status::Busy:
            dl->AddCircleFilled(c, r, col);
            dl->AddRectFilled(ImVec2(c.x - r * 0.55f, c.y - r * 0.16f), ImVec2(c.x + r * 0.55f, c.y + r * 0.16f),
                              ImGui::GetColorU32(IM_COL32_WHITE), r * 0.16f);
            break;
        default:
            dl->AddCircleFilled(c, r, col);
            break;
    }
    if (with_text) Text(dl, font, ImVec2(pos.x + dot_w + spacing, pos.y + (fh - ts.y) * 0.5f), TextCol(), text, end);
}

struct KbdStyle {
    ImU32 bg = 0;
    ImU32 text = 0;
    ImU32 border = 0;
    bool pressed = false;
};

namespace detail {

inline void KeyCap(ImDrawList* dl, const ImVec2& mn, const ImVec2& mx, const char* b, const char* e, float press,
                   const KbdStyle& ks, float font_size, ImU32 active = 0) {
    float r = Max(Rounding(), (mx.y - mn.y) * 0.18f);
    float lip = Max(2.0f, (mx.y - mn.y) * 0.12f) * (1.0f - press * 0.7f);
    float drop = (mx.y - mn.y) * 0.08f * press;
    ImU32 on = active ? ImGui::GetColorU32(active) : Accent();
    ImU32 border = ks.border ? ImGui::GetColorU32(ks.border) : BorderCol();
    ImU32 face = ks.bg ? ImGui::GetColorU32(ks.bg) : CardBg(0.12f);
    face = Mix(face, on, press * 0.85f);
    dl->AddRectFilled(ImVec2(mn.x, mn.y + drop), mx, Mix(border, on, press * 0.6f), r);
    ImVec2 fmx(mx.x, mx.y - lip);
    dl->AddRectFilled(ImVec2(mn.x, mn.y + drop), fmx, face, r);
    Outline(dl, ImVec2(mn.x, mn.y + drop), mx, WithAlpha(border, 0.7f), r, 1.0f);
    ImU32 tc = ks.text ? ImGui::GetColorU32(ks.text) : (press > 0.5f ? OnColor(active ? active : AccentRaw()) : TextCol());
    ImVec2 ts = TextSize(font_size, b, e);
    Text(dl, font_size, ImVec2((mn.x + mx.x - ts.x) * 0.5f, mn.y + drop + (fmx.y - mn.y - drop - ts.y) * 0.5f), tc, b, e);
}

// "Ctrl+Shift+P" into tokens, a '+' with nothing before it is a key of its own ("Ctrl++")
inline int SplitKeys(const char* s, const char** b, const char** e, int cap) {
    int n = 0;
    const char* tok = s;
    for (const char* p = s; s && n < cap; ++p) {
        if (*p == 0 || (*p == '+' && p > tok)) {
            if (p > tok) {
                b[n] = tok;
                e[n] = p;
                ++n;
            }
            if (*p == 0) break;
            tok = p + 1;
        }
    }
    return n;
}

}  // namespace detail

inline void Kbd(const char* keys, const KbdStyle& ks = KbdStyle()) {
    using namespace detail;
    const char* b[8];
    const char* e[8];
    int n = SplitKeys(keys ? keys : "", b, e, 8);
    float font = ImGui::GetFontSize();
    float fs = font * 0.85f;
    float h = ImGui::GetFrameHeight();
    float pad = font * 0.45f;
    float plus_w = TextSize(fs, "+").x + font * 0.3f;
    float w = 0.0f;
    for (int i = 0; i < n; ++i) w += Max(TextSize(fs, b[i], e[i]).x + pad * 2.0f, h * 0.9f) + (i > 0 ? plus_w : 0.0f);
    if (w <= 0.0f) w = h;
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(w, h));
    if (!ImGui::IsItemVisible()) return;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float x = pos.x;
    float press = ks.pressed ? 1.0f : 0.0f;
    for (int i = 0; i < n; ++i) {
        if (i > 0) {
            TextCentered(dl, fs, ImVec2(x + plus_w * 0.5f, pos.y + h * 0.5f), TextDim(), "+");
            x += plus_w;
        }
        float kw = Max(TextSize(fs, b[i], e[i]).x + pad * 2.0f, h * 0.9f);
        KeyCap(dl, ImVec2(x, pos.y), ImVec2(x + kw, pos.y + h), b[i], e[i], press, ks, fs);
        x += kw;
    }
}

enum class DividerLook { Line, Fade, Dashed, Dotted };
enum class DividerAlign { Left, Center, Right };

struct DividerStyle {
    DividerLook look = DividerLook::Line;
    DividerAlign align = DividerAlign::Center;
    ImU32 color = 0;
    ImU32 text = 0;
    float thickness = 0.0f;
    float spacing = 0.0f;  // extra space above and below
};

inline void Divider(const char* label = nullptr, const DividerStyle& ds = DividerStyle()) {
    using namespace detail;
    float font = ImGui::GetFontSize();
    float w = Avail();
    const char* end = label ? LabelEnd(label) : nullptr;
    bool has = label && end > label;
    float sp = ds.spacing > 0.0f ? ds.spacing : font * 0.25f;
    float h = (has ? ImGui::GetTextLineHeight() : Max(1.0f, ds.thickness)) + sp * 2.0f;
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(w, h));
    if (!ImGui::IsItemVisible()) return;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float th = ds.thickness > 0.0f ? ds.thickness : 1.0f;
    float y = std::floor(pos.y + h * 0.5f) + (((int)th) % 2 ? 0.5f : 0.0f);
    ImU32 col = ds.color ? ImGui::GetColorU32(ds.color) : Col(GetStyle().border, ImGuiCol_Separator);
    float x0 = pos.x, x1 = pos.x + w;
    float gap = font * 0.6f;
    float tl = 0.0f, tr = 0.0f;
    if (has) {
        ImVec2 ts = ImGui::CalcTextSize(label, end);
        float tx;
        if (ds.align == DividerAlign::Left) tx = x0 + font * 1.2f;
        else if (ds.align == DividerAlign::Right) tx = x1 - font * 1.2f - ts.x;
        else tx = x0 + (w - ts.x) * 0.5f;
        tx = Max(x0, tx);
        tl = tx - gap;
        tr = tx + ts.x + gap;
        ImU32 tc = ds.text ? ImGui::GetColorU32(ds.text) : TextDim();
        Text(dl, font, ImVec2(tx, pos.y + (h - ts.y) * 0.5f), tc, label, end);
    }
    auto seg = [&](float a, float b, bool fade_left, bool fade_right) {
        if (b - a < 1.0f) return;
        switch (ds.look) {
            case DividerLook::Fade: {
                ImU32 l = fade_left ? WithAlpha(col, 0.0f) : col, r = fade_right ? WithAlpha(col, 0.0f) : col;
                dl->AddRectFilledMultiColor(ImVec2(a, y - th * 0.5f), ImVec2(b, y + th * 0.5f), l, r, r, l);
                break;
            }
            case DividerLook::Dashed:
                DashedLine(dl, ImVec2(a, y), ImVec2(b, y), col, th, font * 0.5f, font * 0.3f);
                break;
            case DividerLook::Dotted: {
                float step = Max(th * 3.0f, font * 0.4f);
                for (float x = a + step * 0.5f; x < b; x += step) dl->AddCircleFilled(ImVec2(x, y), Max(1.0f, th * 0.8f), col, 8);
                break;
            }
            default:
                dl->AddLine(ImVec2(a, y), ImVec2(b, y), col, th);
                break;
        }
    };
    if (has) {
        seg(x0, tl, true, false);
        seg(tr, x1, false, true);
    } else if (ds.look == DividerLook::Fade) {
        float mid = (x0 + x1) * 0.5f;
        seg(x0, mid, true, false);
        seg(mid, x1, false, true);
    } else {
        seg(x0, x1, false, false);
    }
}

inline void Divider(const char* label, DividerLook look, DividerAlign align = DividerAlign::Center) {
    DividerStyle ds;
    ds.look = look;
    ds.align = align;
    Divider(label, ds);
}

}  // namespace disp
}  // namespace imb
