#pragma once
// PlayerListRow, HealthArmorBar
#include "imb_game_core.h"

namespace imb {
namespace game {

enum PlayerFlags {
    PlayerFlag_None = 0,
    PlayerFlag_Dead = 1 << 0,
    PlayerFlag_Friend = 1 << 1,
    PlayerFlag_Muted = 1 << 2,
    PlayerFlag_Speaking = 1 << 3,
    PlayerFlag_Bot = 1 << 4,
    PlayerFlag_Local = 1 << 5
};

enum PlayerRowAction {
    PlayerRow_None = 0,
    PlayerRow_Click = 1,
    PlayerRow_Profile = 2,
    PlayerRow_Friend = 3,
    PlayerRow_Mute = 4
};

struct PlayerInfo {
    const char* name = "";
    int team = 0;
    float health = 1.0f;
    int flags = 0;
    int ping = -1;
    int score = -1;
};

namespace detail {

inline void SpeakerIcon(ImDrawList* dl, const ImVec2& c, float s, ImU32 col, bool muted, bool speaking, float t) {
    ImVec2 body0(c.x - s * 0.55f, c.y - s * 0.2f), body1(c.x - s * 0.25f, c.y + s * 0.2f);
    dl->AddRectFilled(body0, body1, col);
    ImVec2 cone[4] = {ImVec2(body1.x, body0.y), ImVec2(c.x + s * 0.1f, c.y - s * 0.5f),
                      ImVec2(c.x + s * 0.1f, c.y + s * 0.5f), ImVec2(body1.x, body1.y)};
    dl->AddConvexPolyFilled(cone, 4, col);
    float th = ed::Max(1.0f, s * 0.12f);
    if (muted) {
        dl->AddLine(ImVec2(c.x - s * 0.6f, c.y - s * 0.6f), ImVec2(c.x + s * 0.6f, c.y + s * 0.6f), Live(GetStyle().bad),
                    th * 1.4f);
        return;
    }
    if (!speaking) return;
    for (int i = 0; i < 2; ++i) {
        float a = 0.5f + 0.5f * sinf(t * 9.0f - (float)i * 1.2f);
        dl->PathClear();
        dl->PathArcTo(ImVec2(c.x + s * 0.1f, c.y), s * (0.35f + 0.25f * (float)i), -0.9f, 0.9f, 10);
        Stroke(dl, ed::WithAlpha(col, 0.35f + 0.65f * a), th, false);
    }
}

inline void StarIcon(ImDrawList* dl, const ImVec2& c, float s, ImU32 col) {
    ImVec2 outer[5], inner[5];
    for (int i = 0; i < 5; ++i) {
        float a = -kPi * 0.5f + (float)i * kPi * 0.4f;
        float b = a + kPi * 0.2f;
        outer[i] = ImVec2(c.x + cosf(a) * s, c.y + sinf(a) * s);
        inner[i] = ImVec2(c.x + cosf(b) * s * 0.45f, c.y + sinf(b) * s * 0.45f);
    }
    dl->AddConvexPolyFilled(inner, 5, col);
    for (int i = 0; i < 5; ++i) dl->AddTriangleFilled(outer[i], inner[i], inner[(i + 4) % 5], col);
}

inline void ProfileIcon(ImDrawList* dl, const ImVec2& c, float s, ImU32 col) {
    dl->AddCircleFilled(ImVec2(c.x, c.y - s * 0.3f), s * 0.3f, col, 16);
    dl->PathClear();
    dl->PathArcTo(ImVec2(c.x, c.y + s * 0.65f), s * 0.6f, kPi, 2.0f * kPi, 14);
    dl->PathFillConvex(col);
}

inline void Pill(ImDrawList* dl, const ImVec2& p, const char* text, ImU32 bg, ImU32 fg, float* out_w) {
    float fs = ImGui::GetFontSize() * 0.68f;
    ImVec2 ts = TextSize(fs, text);
    ImVec2 mx(p.x + ts.x + fs * 0.8f, p.y + ts.y + 2.0f);
    dl->AddRectFilled(p, mx, bg, (mx.y - p.y) * 0.5f);
    Text(dl, fs, ImVec2(p.x + fs * 0.4f, p.y + 1.0f), fg, text);
    if (out_w) *out_w = mx.x - p.x;
}

}  // namespace detail

// one row of a scoreboard / player list. hovering slides in profile, friend and mute buttons.
// right click is a profile action too. returns a PlayerRowAction, act on it yourself
inline int PlayerListRow(const char* id, const PlayerInfo& p, bool selected = false, float width = 0.0f) {
    namespace d = detail;
    const ImGuiStyle& ist = ImGui::GetStyle();
    float fs = ImGui::GetFontSize();
    float fh = ImGui::GetFrameHeight();
    float w = width > 0.0f ? width : d::ed::Max(ImGui::GetContentRegionAvail().x, fs * 12.0f);
    float h = floorf(fh * 1.4f);
    ImVec2 mn = ImGui::GetCursorScreenPos(), mx(mn.x + w, mn.y + h);
    ImGui::PushID(id);
    bool pressed = ImGui::InvisibleButton("##row", ImVec2(w, h));
    bool hovered = ImGui::IsItemHovered();
    bool held = ImGui::IsItemActive();
    bool right_click = hovered && ImGui::IsMouseClicked(1);
    ImGuiID hover_id = ImGui::GetID("##hv"), sel_id = ImGui::GetID("##sel"), btn_id = ImGui::GetID("##btn");
    ImGui::PopID();
    float dur = ess::GetStyle().anim_duration;
    float th = d::ed::Anim(hover_id, hovered, dur);
    float ts = d::ed::Anim(sel_id, selected, dur);
    bool dead = (p.flags & PlayerFlag_Dead) != 0;
    float alpha = dead ? 0.55f : 1.0f;
    ImU32 accent = d::ed::AccentRaw();
    ImU32 team = d::TeamColor(p.team);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float r = d::ed::Rounding();

    ImU32 bg = d::ed::Col(0, ImGuiCol_FrameBg, 0.55f + 0.35f * th);
    bg = d::ed::LerpColor(bg, d::Live(accent, 0.2f), ts);
    if (held) bg = d::ed::LerpColor(bg, d::Live(accent, 0.35f), 0.5f);
    dl->AddRectFilled(mn, mx, bg, r);
    d::ed::Gradient(dl, mn, ImVec2(mn.x + w * 0.35f, mx.y), d::Live(team, 0.16f * alpha), d::Live(team, 0.0f), false, r);
    if (ts > 0.01f) d::ed::Outline(dl, mn, mx, d::Live(accent, 0.7f * ts), r, 1.0f);
    float strip = d::ed::Max(3.0f, floorf(fs * 0.22f));
    dl->AddRectFilled(ImVec2(mn.x, mn.y + h * 0.15f), ImVec2(mn.x + strip, mx.y - h * 0.15f), d::Live(team, alpha), strip * 0.5f);

    float x = mn.x + strip + ist.FramePadding.x;
    float ty = mn.y + floorf((h - fs) * 0.5f);
    const char* name = p.name ? p.name : "";
    ImVec2 ns = ImGui::CalcTextSize(name);
    float name_max = w * 0.45f;
    ImVec4 clip(x, mn.y, x + name_max, mx.y);
    dl->AddText(ImGui::GetFont(), fs, ImVec2(x, ty), d::ed::TextCol(alpha), name, nullptr, 0.0f, &clip);
    float nw = d::ed::Min(ns.x, name_max);
    if (dead) dl->AddLine(ImVec2(x, ty + fs * 0.55f), ImVec2(x + nw, ty + fs * 0.55f), d::ed::TextDim(), 1.0f);
    x += nw + ist.ItemInnerSpacing.x * 1.5f;
    float pill_y = mn.y + h * 0.5f - fs * 0.42f;
    float pw = 0.0f;
    if (p.flags & PlayerFlag_Local) {
        d::Pill(dl, ImVec2(x, pill_y), "YOU", d::Live(accent, 0.3f), d::Live(accent), &pw);
        x += pw + ist.ItemInnerSpacing.x;
    }
    if (p.flags & PlayerFlag_Bot) {
        d::Pill(dl, ImVec2(x, pill_y), "BOT", d::ed::Col(0, ImGuiCol_FrameBgActive), d::ed::TextDim(), &pw);
        x += pw + ist.ItemInnerSpacing.x;
    }
    float is = fs * 0.5f;
    float t = d::Time();
    float cy = (mn.y + mx.y) * 0.5f;
    if (p.flags & (PlayerFlag_Speaking | PlayerFlag_Muted)) {
        bool muted = (p.flags & PlayerFlag_Muted) != 0;
        d::SpeakerIcon(dl, ImVec2(x + is, cy), is, d::ed::TextCol(muted ? 0.6f : 1.0f), muted,
                       (p.flags & PlayerFlag_Speaking) != 0, t);
        x += is * 2.4f;
    }
    if (p.flags & PlayerFlag_Friend) {
        d::StarIcon(dl, ImVec2(x + is, cy), is, d::Live(GetStyle().warn));
        x += is * 2.4f;
    }

    float right = mx.x - ist.FramePadding.x;
    float bar_w = floorf(fs * 4.0f), bar_h = d::ed::Max(3.0f, floorf(fs * 0.3f));
    ImVec2 bmn(right - bar_w, cy - bar_h * 0.5f), bmx(right, cy + bar_h * 0.5f);
    if (dead) {
        d::ed::TextIn(dl, ImVec2(bmn.x, mn.y), ImVec2(bmx.x, mx.y), "DEAD", nullptr, d::ed::TextDim(), 1.0f);
    } else {
        float hp = d::Fin01(p.health);
        dl->AddRectFilled(bmn, bmx, d::Live(IM_COL32(0, 0, 0, 120)), bar_h * 0.5f);
        if (hp > 0.0f) dl->AddRectFilled(bmn, ImVec2(bmn.x + bar_w * hp, bmx.y), d::Live(d::HealthColor(hp)), bar_h * 0.5f);
    }
    right = bmn.x - ist.ItemInnerSpacing.x * 2.0f;
    char buf[16];
    if (p.ping >= 0) {
        std::snprintf(buf, sizeof(buf), "%dms", p.ping);
        ImU32 pc = d::Live(d::GradeColor(d::Grade((float)p.ping, 40.0f, 90.0f, false)), alpha);
        float pwid = ImGui::CalcTextSize("000ms").x;
        d::ed::TextIn(dl, ImVec2(right - pwid, mn.y), ImVec2(right, mx.y), buf, nullptr, pc, 1.0f);
        right -= pwid + ist.ItemInnerSpacing.x * 2.0f;
    }
    if (p.score >= 0) {
        std::snprintf(buf, sizeof(buf), "%d", p.score);
        float swid = ImGui::CalcTextSize("0000").x;
        d::ed::TextIn(dl, ImVec2(right - swid, mn.y), ImVec2(right, mx.y), buf, nullptr, d::ed::TextCol(alpha), 1.0f);
    }

    // hover buttons: profile, friend, mute (left to right)
    float bs = floorf(h * 0.7f);
    float bgap = floorf(fs * 0.2f);
    float area = bs * 3.0f + bgap * 4.0f;
    int hot_btn = -1;
    if (th > 0.01f) {
        float slide = (1.0f - th) * area;
        ImVec2 amn(mx.x - area + slide, mn.y), amx(mx.x, mx.y);
        dl->PushClipRect(mn, mx, true);
        d::ed::Gradient(dl, ImVec2(amn.x - fs * 1.5f, mn.y), ImVec2(amn.x, mx.y), d::ed::Col(0, ImGuiCol_FrameBg, 0.0f),
                        d::ed::Col(0, ImGuiCol_WindowBg, th), false, 0.0f);
        dl->AddRectFilled(amn, amx, d::ed::Col(0, ImGuiCol_WindowBg, th), r, d::kRoundRight);
        for (int i = 0; i < 3; ++i) {
            ImVec2 b0(amn.x + bgap + (float)i * (bs + bgap), mn.y + floorf((h - bs) * 0.5f)), b1(b0.x + bs, b0.y + bs);
            bool over = hovered && d::MouseIn(b0, b1);
            if (over) hot_btn = i;
            float tb = d::ed::Anim(btn_id + (ImGuiID)i, over, 0.1f);
            dl->AddRectFilled(b0, b1, d::ed::LerpColor(d::ed::Col(0, ImGuiCol_Button, 0.6f * th), d::Live(accent, 0.8f * th), tb),
                              bs * 0.3f);
            ImVec2 bc((b0.x + b1.x) * 0.5f, (b0.y + b1.y) * 0.5f);
            ImU32 ic = d::ed::TextCol(th);
            if (i == 0) d::ProfileIcon(dl, bc, bs * 0.32f, ic);
            else if (i == 1) d::StarIcon(dl, bc, bs * 0.3f, (p.flags & PlayerFlag_Friend) ? d::Live(GetStyle().warn, th) : ic);
            else d::SpeakerIcon(dl, bc, bs * 0.32f, ic, (p.flags & PlayerFlag_Muted) == 0, false, t);
        }
        dl->PopClipRect();
    }
    if (right_click) return PlayerRow_Profile;
    if (!pressed) return PlayerRow_None;
    if (hot_btn == 0) return PlayerRow_Profile;
    if (hot_btn == 1) return PlayerRow_Friend;
    if (hot_btn == 2) return PlayerRow_Mute;
    return PlayerRow_Click;
}

inline int PlayerListRow(const char* id, const char* name, int team, float health, int flags = 0, int ping = -1,
                         bool selected = false, float width = 0.0f) {
    PlayerInfo p;
    p.name = name;
    p.team = team;
    p.health = health;
    p.flags = flags;
    p.ping = ping;
    return PlayerListRow(id, p, selected, width);
}

struct HealthBarStyle {
    bool text = true;
    bool ticks = true;
    float height = 0.0f;
    bool health_gradient = true;
    ImU32 health_color = 0;
    ImU32 armor_color = 0;
    float trail_delay = 0.35f;
    float trail_speed = 0.8f;
};

namespace detail {

struct Trail {
    float shown, trail, hold, last, flash, flash_from;
};

inline Trail StepTrail(ImGuiStorage* s, ImGuiID base, float v, float delay, float speed) {
    Trail t;
    bool fresh = !s->GetBool(base + 6, false);
    t.shown = fresh ? v : s->GetFloat(base, v);
    t.trail = fresh ? v : s->GetFloat(base + 1, v);
    t.hold = s->GetFloat(base + 2, 0.0f);
    t.last = fresh ? v : s->GetFloat(base + 3, v);
    t.flash = s->GetFloat(base + 4, 0.0f);
    t.flash_from = s->GetFloat(base + 5, v);
    float dt = Dt();
    if (v < t.last) t.hold = delay;
    if (v > t.last + 1e-4f) {
        t.flash = 0.5f;
        t.flash_from = ed::Min(t.shown, t.last);
    }
    t.last = v;
    float k = ed::Min(1.0f, dt * 14.0f);
    t.shown += (v - t.shown) * k;
    if (fabsf(v - t.shown) < 1e-4f) t.shown = v;
    if (t.trail < v) {
        t.trail = v;
    } else if (t.trail > v) {
        t.hold -= dt;
        if (t.hold <= 0.0f) t.trail = ed::Max(v, t.trail - speed * dt);
    }
    t.flash = ed::Max(0.0f, t.flash - dt);
    s->SetFloat(base, t.shown);
    s->SetFloat(base + 1, t.trail);
    s->SetFloat(base + 2, t.hold);
    s->SetFloat(base + 3, t.last);
    s->SetFloat(base + 4, t.flash);
    s->SetFloat(base + 5, t.flash_from);
    s->SetBool(base + 6, true);
    return t;
}

inline void TrailLayer(ImDrawList* dl, const ImVec2& mn, const ImVec2& mx, const Trail& t, ImU32 fill, float r,
                       ImDrawFlags corners) {
    float w = mx.x - mn.x;
    ImU32 trail_col = Live(GetStyle().trail, 0.85f);
    if (t.trail > t.shown + 1e-3f)
        dl->AddRectFilled(mn, ImVec2(mn.x + w * t.trail, mx.y), trail_col, r, corners);
    if (t.shown > 0.0f) dl->AddRectFilled(mn, ImVec2(mn.x + w * t.shown, mx.y), fill, r, corners);
    if (t.flash > 0.0f && t.shown > t.flash_from) {
        float a = t.flash / 0.5f;
        dl->AddRectFilled(ImVec2(mn.x + w * t.flash_from, mn.y), ImVec2(mn.x + w * t.shown, mx.y),
                          Live(IM_COL32_WHITE, 0.55f * a), r, corners);
    }
}

}  // namespace detail

// health on top, armor below (skipped when max_armor <= 0). drops leave a light trail that
// drains after a short delay, heals flash. width 0 fills
inline void HealthArmorBar(const char* id, float health, float armor = 0.0f, float max_health = 100.0f,
                           float max_armor = 100.0f, const HealthBarStyle& st = HealthBarStyle(), float width = 0.0f) {
    namespace d = detail;
    float fs = ImGui::GetFontSize();
    float fh = ImGui::GetFrameHeight();
    float w = width > 0.0f ? width : d::ed::Max(ImGui::GetContentRegionAvail().x, fs * 8.0f);
    bool has_armor = max_armor > 0.0f;
    float h = st.height > 0.0f ? st.height : floorf(fh * (has_armor ? 1.0f : 0.8f));
    ImVec2 mn = ImGui::GetCursorScreenPos(), mx(mn.x + w, mn.y + h);
    ImGui::PushID(id);
    ImGui::Dummy(ImVec2(w, h));
    ImGuiID hb = ImGui::GetID("##h"), ab = ImGui::GetID("##a");
    ImGui::PopID();
    ImGuiStorage* s = ImGui::GetStateStorage();
    float hv = max_health > 0.0f ? d::Fin01(health / max_health) : 0.0f;
    float av = has_armor ? d::Fin01(armor / max_armor) : 0.0f;
    d::Trail ht = d::StepTrail(s, hb, hv, st.trail_delay, st.trail_speed);
    d::Trail at = d::StepTrail(s, ab, av, st.trail_delay, st.trail_speed);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float r = d::ed::Max(d::ed::Rounding(), 2.0f);
    float gap = has_armor ? d::ed::Max(1.0f, floorf(d::Dpi())) : 0.0f;
    float split = has_armor ? floorf(h * 0.68f) : h;
    dl->AddRectFilled(mn, mx, d::Live(IM_COL32(0, 0, 0, 140)), r);
    ImU32 hc = st.health_gradient ? d::HealthColor(ht.shown) : (st.health_color ? st.health_color : GetStyle().good);
    ImVec2 hmx(mx.x, mn.y + split - gap);
    d::TrailLayer(dl, mn, hmx, ht, d::Live(hc), r, has_armor ? d::ed::kRoundTop : d::ed::kRoundAll);
    d::ed::Gradient(dl, mn, ImVec2(mn.x + w * ht.shown, mn.y + split * 0.45f), d::Live(IM_COL32(255, 255, 255, 45)),
                    d::Live(IM_COL32(255, 255, 255, 0)), true, 0.0f);
    if (has_armor) {
        ImU32 ac = st.armor_color ? st.armor_color : GetStyle().armor;
        d::TrailLayer(dl, ImVec2(mn.x, mn.y + split), mx, at, d::Live(ac), r, d::ed::kRoundBottom);
    }
    if (st.ticks) {
        ImU32 tick = d::Live(IM_COL32(0, 0, 0, 70));
        for (int i = 1; i < 10; ++i) {
            float x = floorf(mn.x + w * (float)i / 10.0f);
            dl->AddLine(ImVec2(x, mn.y + 1.0f), ImVec2(x, hmx.y - 1.0f), tick);
        }
    }
    if (hv < 0.25f) {
        float pulse = 0.5f + 0.5f * sinf(d::Time() * 7.0f);
        d::ed::Outline(dl, mn, mx, d::Live(GetStyle().bad, 0.35f + 0.5f * pulse), r, 1.5f);
    } else {
        d::ed::Outline(dl, mn, mx, d::Live(IM_COL32(0, 0, 0, 180)), r, 1.0f);
    }
    if (st.text) {
        char buf[32];
        float tfs = d::ed::Min(fs * 0.85f, split);
        std::snprintf(buf, sizeof(buf), "%d", d::SafeInt(health + 0.5f));
        ImVec2 tsz = d::TextSize(tfs, buf);
        float ty = mn.y + floorf((split - tsz.y) * 0.5f);
        d::TextShadow(dl, tfs, ImVec2(mn.x + fs * 0.4f, ty), d::Live(IM_COL32_WHITE), buf);
        if (has_armor && armor > 0.0f) {
            std::snprintf(buf, sizeof(buf), "%d", d::SafeInt(armor + 0.5f));
            tsz = d::TextSize(tfs, buf);
            d::TextShadow(dl, tfs, ImVec2(mx.x - fs * 0.4f - tsz.x, ty), d::Live(IM_COL32(220, 235, 255, 255)), buf);
        }
    }
}

inline void HealthArmorBar(const char* id, float health, float armor, float max_health, float max_armor, bool text,
                           bool ticks, float width) {
    HealthBarStyle st;
    st.text = text;
    st.ticks = ticks;
    HealthArmorBar(id, health, armor, max_health, max_armor, st, width);
}

}  // namespace game
}  // namespace imb
