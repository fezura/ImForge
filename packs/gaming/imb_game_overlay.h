#pragma once
// Watermark, FpsPingCounter, KillFeedEntry, NotificationBanner
#include "imb_game_core.h"

namespace imb {
namespace game {

struct WatermarkStyle {
    int corner = Corner_None;
    ImVec2 margin = ImVec2(0, 0);
    bool fps = true;
    bool clock = true;
    bool animated_bar = true;
    float bar_speed = 0.25f;
    ImU32 bar_a = 0;
    ImU32 bar_b = 0;
    ImU32 bg = 0;
};

namespace detail {

inline void FlowBar(ImDrawList* dl, const ImVec2& mn, const ImVec2& mx, ImU32 a, ImU32 b, float phase) {
    const int kSegs = 12;
    float w = (mx.x - mn.x) / (float)kSegs;
    for (int i = 0; i < kSegs; ++i) {
        float u0 = (float)i / (float)kSegs, u1 = (float)(i + 1) / (float)kSegs;
        float k0 = 0.5f + 0.5f * sinf((u0 * 0.8f - phase) * 2.0f * kPi);
        float k1 = 0.5f + 0.5f * sinf((u1 * 0.8f - phase) * 2.0f * kPi);
        ImU32 c0 = ed::LerpColor(a, b, k0), c1 = ed::LerpColor(a, b, k1);
        ImVec2 s0(mn.x + w * (float)i, mn.y), s1(i == kSegs - 1 ? mx.x : mn.x + w * (float)(i + 1), mx.y);
        dl->AddRectFilledMultiColor(s0, s1, c0, c1, c1, c0);
    }
}

}  // namespace detail

// app name | extra | 144 fps | 21:37:05 with a flowing gradient bar on top.
// Corner_None lays it out at the cursor, a corner floats it over the main viewport
inline void Watermark(const char* app_name, const char* extra = nullptr, const WatermarkStyle& st = WatermarkStyle()) {
    namespace d = detail;
    float fs = ImGui::GetFontSize();
    float fh = ImGui::GetFrameHeight();
    const char* name = app_name ? app_name : "";
    const char* name_end = d::ed::LabelEnd(name);
    const char* segs[4];
    const char* seg_end[4];
    int n = 0;
    segs[n] = name;
    seg_end[n++] = name_end;
    if (extra && extra[0]) {
        segs[n] = extra;
        seg_end[n++] = nullptr;
    }
    char fps_buf[24], clock_buf[16];
    if (st.fps) {
        std::snprintf(fps_buf, sizeof(fps_buf), "%d fps", d::SafeInt(ImGui::GetIO().Framerate + 0.5f));
        segs[n] = fps_buf;
        seg_end[n++] = nullptr;
    }
    if (st.clock) {
        segs[n] = d::ClockText(clock_buf, (int)sizeof(clock_buf));
        seg_end[n++] = nullptr;
    }
    float pad = floorf(fs * 0.6f);
    float widths[4];
    float total = pad;
    for (int i = 0; i < n; ++i) {
        float tw = ImGui::CalcTextSize(segs[i], seg_end[i]).x;
        if (st.fps && segs[i] == fps_buf) tw = d::ed::Max(tw, ImGui::CalcTextSize("000 fps").x);
        widths[i] = tw;
        total += tw + pad;
    }
    float bar_h = d::ed::Max(2.0f, floorf(2.0f * d::Dpi()));
    ImVec2 size(floorf(total), floorf(fh + bar_h));
    d::OverlayBox box = d::PlaceOverlay(name, st.corner, size, st.margin);
    ImDrawList* dl = box.dl;
    float r = d::ed::Rounding();
    ImU32 bg = st.bg ? d::Live(st.bg) : d::Live(GetStyle().overlay_bg);
    d::ed::Shadow(dl, box.mn, box.mx, r, d::Live(IM_COL32(0, 0, 0, 80)), fs * 0.35f);
    dl->AddRectFilled(box.mn, box.mx, bg, r, d::ed::kRoundBottom);
    ImU32 a = d::Live(st.bar_a ? st.bar_a : d::ed::AccentRaw());
    ImU32 b = d::Live(st.bar_b ? st.bar_b : d::Accent2());
    float phase = st.animated_bar ? d::Time() * st.bar_speed : 0.0f;
    d::FlowBar(dl, box.mn, ImVec2(box.mx.x, box.mn.y + bar_h), a, b, phase);
    float x = box.mn.x + pad;
    float ty = box.mn.y + bar_h + floorf((fh - fs) * 0.5f);
    for (int i = 0; i < n; ++i) {
        ImU32 col = i == 0 ? d::ed::TextCol() : d::ed::TextDim(1.0f);
        dl->AddText(ImVec2(x, ty), col, segs[i], seg_end[i]);
        x += widths[i] + pad;
        if (i + 1 < n) {
            float sx = floorf(x - pad * 0.5f);
            dl->AddLine(ImVec2(sx, box.mn.y + bar_h + fh * 0.25f), ImVec2(sx, box.mx.y - fh * 0.25f),
                        d::ed::BorderCol(0.8f));
        }
    }
}

inline void Watermark(const char* app_name, const char* extra, int corner, bool fps, bool clock) {
    WatermarkStyle st;
    st.corner = corner;
    st.fps = fps;
    st.clock = clock;
    Watermark(app_name, extra, st);
}

struct FpsPingStyle {
    int corner = Corner_None;
    ImVec2 margin = ImVec2(0, 0);
    float fps_good = 120.0f;
    float fps_warn = 60.0f;
    float ping_good = 40.0f;
    float ping_warn = 90.0f;
    bool show_fps = true;
    bool show_ping = true;
    bool graph = true;
    bool background = true;
};

namespace detail {

const int kFpsHistory = 24;

inline void Stat(ImDrawList* dl, float& x, float y, float fh, const char* value, const char* unit, ImU32 col,
                 float pulse) {
    float fs = ImGui::GetFontSize();
    float dr = fs * 0.22f;
    ImVec2 c(x + dr, y + fh * 0.5f);
    if (pulse > 0.0f) dl->AddCircleFilled(c, dr * (1.0f + pulse), ed::WithAlpha(col, 0.25f * (1.0f - pulse * 0.5f)), 16);
    dl->AddCircleFilled(c, dr, col, 16);
    x += dr * 2.0f + fs * 0.35f;
    float ty = y + floorf((fh - fs) * 0.5f);
    dl->AddText(ImVec2(x, ty), ed::LerpColor(col, ed::TextCol(), 0.35f), value);
    x += ImGui::CalcTextSize(value).x + fs * 0.2f;
    float ufs = fs * 0.8f;
    Text(dl, ufs, ImVec2(x, ty + (fs - ufs) * 0.7f), ed::TextDim(), unit);
    x += TextSize(ufs, unit).x;
}

}  // namespace detail

// fps < 0 reads io.Framerate, ping < 0 hides the ping part. colors follow the thresholds
inline void FpsPingCounter(const char* id, float fps = -1.0f, float ping_ms = -1.0f,
                           const FpsPingStyle& st = FpsPingStyle()) {
    namespace d = detail;
    float fs = ImGui::GetFontSize();
    float fh = ImGui::GetFrameHeight();
    float f = fps < 0.0f ? ImGui::GetIO().Framerate : d::Fin(fps);
    bool show_ping = st.show_ping && ping_ms >= 0.0f;
    ImGui::PushID(id);
    ImGuiID fps_grade_id = ImGui::GetID("##fg"), ping_grade_id = ImGui::GetID("##pg");
    ImGuiID hist_base = ImGui::GetID("##h"), hist_pos = ImGui::GetID("##hp"), hist_time = ImGui::GetID("##ht");
    ImGui::PopID();
    ImGuiStorage* storage = ImGui::GetStateStorage();
    char fv[16], pv[16];
    std::snprintf(fv, sizeof(fv), "%d", d::SafeInt(f + 0.5f));
    std::snprintf(pv, sizeof(pv), "%d", d::SafeInt(ping_ms + 0.5f));
    float graph_w = st.graph && st.show_fps ? floorf(fs * 2.4f) : 0.0f;
    float pad = floorf(fs * 0.55f);
    float dot = fs * 0.44f + fs * 0.35f;
    float w = pad;
    if (st.show_fps) w += dot + d::ed::Max(ImGui::CalcTextSize(fv).x, ImGui::CalcTextSize("000").x) + fs * 0.2f +
                         d::TextSize(fs * 0.8f, "fps").x + pad;
    if (graph_w > 0.0f) w += graph_w + pad;
    if (show_ping) w += dot + d::ed::Max(ImGui::CalcTextSize(pv).x, ImGui::CalcTextSize("000").x) + fs * 0.2f +
                       d::TextSize(fs * 0.8f, "ms").x + pad;
    d::OverlayBox box = d::PlaceOverlay(id, st.corner, ImVec2(floorf(w), fh), st.margin);
    ImDrawList* dl = box.dl;
    if (st.background) {
        float r = d::ed::Max(d::ed::Rounding(), fh * 0.3f);
        dl->AddRectFilled(box.mn, box.mx, d::Live(GetStyle().overlay_bg), r);
        d::ed::Outline(dl, box.mn, box.mx, d::ed::BorderCol(0.6f), r, 1.0f);
    }
    float t = d::Time();
    float x = box.mn.x + pad;
    if (st.show_fps) {
        float g = d::ed::AnimTo(fps_grade_id, d::Grade(f, st.fps_good, st.fps_warn, true), 0.3f);
        float pulse = g > 1.5f ? 0.5f + 0.5f * sinf(t * 8.0f) : 0.0f;
        float start = x;
        d::Stat(dl, x, box.mn.y, fh, fv, "fps", d::Live(d::GradeColor(g)), pulse);
        x = d::ed::Max(x, start + dot + ImGui::CalcTextSize("000").x + fs * 0.2f + d::TextSize(fs * 0.8f, "fps").x) + pad;
        if (graph_w > 0.0f) {
            float acc_t = storage->GetFloat(hist_time, 0.0f) + d::Dt();
            int head = storage->GetInt(hist_pos, 0);
            if (acc_t >= 0.1f) {
                acc_t = 0.0f;
                storage->SetFloat(hist_base + (ImGuiID)head, f);
                head = (head + 1) % d::kFpsHistory;
                storage->SetInt(hist_pos, head);
            }
            storage->SetFloat(hist_time, acc_t);
            float peak = 1.0f;
            for (int i = 0; i < d::kFpsHistory; ++i) peak = d::ed::Max(peak, storage->GetFloat(hist_base + (ImGuiID)i, 0.0f));
            float bw = graph_w / (float)d::kFpsHistory;
            float gy0 = box.mn.y + fh * 0.25f, gy1 = box.mx.y - fh * 0.25f;
            for (int i = 0; i < d::kFpsHistory; ++i) {
                float v = storage->GetFloat(hist_base + (ImGuiID)((head + i) % d::kFpsHistory), 0.0f);
                if (v <= 0.0f) continue;
                float hgt = (gy1 - gy0) * d::ed::Clamp01(v / peak);
                ImU32 c = d::Live(d::GradeColor(d::Grade(v, st.fps_good, st.fps_warn, true)), 0.75f);
                dl->AddRectFilled(ImVec2(x + bw * (float)i, gy1 - hgt), ImVec2(x + bw * (float)(i + 1) - 1.0f, gy1), c);
            }
            x += graph_w + pad;
        }
    }
    if (show_ping) {
        float g = d::ed::AnimTo(ping_grade_id, d::Grade(ping_ms, st.ping_good, st.ping_warn, false), 0.3f);
        float pulse = g > 1.5f ? 0.5f + 0.5f * sinf(t * 8.0f) : 0.0f;
        d::Stat(dl, x, box.mn.y, fh, pv, "ms", d::Live(d::GradeColor(g)), pulse);
    }
}

inline void FpsPingCounter(const char* id, float fps, float ping_ms, int corner, bool graph) {
    FpsPingStyle st;
    st.corner = corner;
    st.graph = graph;
    FpsPingCounter(id, fps, ping_ms, st);
}

enum KillFlags {
    KillFlag_None = 0,
    KillFlag_Headshot = 1 << 0,
    KillFlag_Wallbang = 1 << 1,
    KillFlag_Local = 1 << 2
};

struct KillFeedStyle {
    float lifetime = 6.0f;
    float fade = 0.8f;
    float slide = 0.25f;
    bool align_right = true;
};

namespace detail {

inline void HeadshotIcon(ImDrawList* dl, const ImVec2& c, float s, ImU32 col) {
    dl->AddCircle(c, s * 0.42f, col, 16, ed::Max(1.0f, s * 0.12f));
    dl->AddLine(ImVec2(c.x - s * 0.6f, c.y), ImVec2(c.x + s * 0.6f, c.y), col, ed::Max(1.0f, s * 0.1f));
    dl->AddLine(ImVec2(c.x, c.y - s * 0.6f), ImVec2(c.x, c.y + s * 0.6f), col, ed::Max(1.0f, s * 0.1f));
}

inline void WallIcon(ImDrawList* dl, const ImVec2& c, float s, ImU32 col) {
    float th = ed::Max(1.0f, s * 0.12f);
    for (int i = -1; i <= 1; ++i) {
        float x = c.x + (float)i * s * 0.3f;
        dl->AddLine(ImVec2(x, c.y - s * 0.45f), ImVec2(x, c.y + s * 0.45f), ed::WithAlpha(col, 0.6f), th);
    }
    dl->AddLine(ImVec2(c.x - s * 0.6f, c.y + s * 0.25f), ImVec2(c.x + s * 0.6f, c.y - s * 0.25f), col, th);
}

}  // namespace detail

// one feed row. `age` is seconds since the event: slides in, holds, fades and collapses.
// returns false once expired (nothing is drawn or submitted then), so you can drop the entry
inline bool KillFeedEntry(const char* id, const char* killer, const char* victim, const char* weapon, float age,
                          int killer_team = 0, int victim_team = 1, int flags = 0,
                          const KillFeedStyle& st = KillFeedStyle()) {
    namespace d = detail;
    float life = d::ed::Max(st.lifetime, 0.1f);
    if (!(age >= 0.0f)) age = 0.0f;
    if (age >= life) return false;
    float fs = ImGui::GetFontSize();
    float fh = ImGui::GetFrameHeight();
    const char* k = killer ? killer : "";
    const char* v = victim ? victim : "";
    const char* wpn = weapon ? weapon : "";
    float pad = floorf(fs * 0.5f), gap = floorf(fs * 0.4f), icon = fs * 0.9f;
    float wfs = fs * 0.85f;
    float kw = ImGui::CalcTextSize(k).x, vw = ImGui::CalcTextSize(v).x, ww = wpn[0] ? d::TextSize(wfs, wpn).x : 0.0f;
    int icons = ((flags & KillFlag_Headshot) ? 1 : 0) + ((flags & KillFlag_Wallbang) ? 1 : 0);
    float cw = pad + (kw > 0.0f ? kw + gap : 0.0f) + (ww > 0.0f ? ww + gap : 0.0f) + (float)icons * (icon + gap) + vw + pad;
    float fade = d::ed::Max(st.fade, 0.01f), slide = d::ed::Max(st.slide, 0.01f);
    float in = d::ed::Clamp01(age / slide);
    in = 1.0f - (1.0f - in) * (1.0f - in) * (1.0f - in);
    float out = d::ed::Clamp01((life - age) / fade);
    float collapse = d::ed::Clamp01((life - age) / (fade * 0.4f));
    float alpha = in * out;
    float avail = ImGui::GetContentRegionAvail().x;
    float row_h = floorf(fh * (collapse * (2.0f - collapse)));
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGui::PushID(id);
    ImGui::Dummy(ImVec2(d::ed::Max(st.align_right ? avail : cw, 1.0f), d::ed::Max(row_h, 1.0f)));
    ImGui::PopID();
    float x0 = st.align_right ? pos.x + avail - cw : pos.x;
    x0 += (1.0f - in) * cw * 0.35f * (st.align_right ? 1.0f : -1.0f);
    ImVec2 mn(floorf(x0), pos.y), mx(floorf(x0 + cw), pos.y + fh);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->PushClipRect(ImVec2(pos.x - cw, pos.y), ImVec2(pos.x + avail + cw, pos.y + d::ed::Max(row_h, 1.0f)), true);
    bool local = (flags & KillFlag_Local) != 0;
    float r = d::ed::Rounding();
    dl->AddRectFilled(mn, mx, d::Live(GetStyle().overlay_bg, alpha * (local ? 1.0f : 0.85f)), r);
    if (local) d::ed::Outline(dl, mn, mx, d::Live(GetStyle().bad, alpha), r, d::ed::Max(1.0f, 1.5f * d::Dpi()));
    float flash = 1.0f - d::ed::Clamp01(age / 0.45f);
    if (flash > 0.0f) dl->AddRectFilled(mn, mx, d::Live(IM_COL32_WHITE, 0.18f * flash * alpha), r);
    float x = mn.x + pad;
    float ty = mn.y + floorf((fh - fs) * 0.5f);
    if (kw > 0.0f) {
        dl->AddText(ImVec2(x, ty), d::Live(d::TeamColor(killer_team), alpha), k);
        x += kw + gap;
    }
    if (ww > 0.0f) {
        d::Text(dl, wfs, ImVec2(x, mn.y + floorf((fh - wfs) * 0.5f)), d::ed::TextDim(alpha), wpn);
        x += ww + gap;
    }
    ImU32 ic = d::Live(IM_COL32(235, 235, 235, 255), alpha);
    if (flags & KillFlag_Wallbang) {
        d::WallIcon(dl, ImVec2(x + icon * 0.5f, (mn.y + mx.y) * 0.5f), icon, ic);
        x += icon + gap;
    }
    if (flags & KillFlag_Headshot) {
        d::HeadshotIcon(dl, ImVec2(x + icon * 0.5f, (mn.y + mx.y) * 0.5f), icon, d::Live(GetStyle().bad, alpha));
        x += icon + gap;
    }
    dl->AddText(ImVec2(x, ty), d::Live(d::TeamColor(victim_team), alpha), v);
    dl->PopClipRect();
    return true;
}

inline bool KillFeedEntry(const char* id, const char* killer, const char* victim, const char* weapon, float age,
                          int killer_team, int victim_team, int flags, float lifetime) {
    KillFeedStyle st;
    if (lifetime > 0.0f) st.lifetime = lifetime;
    return KillFeedEntry(id, killer, victim, weapon, age, killer_team, victim_team, flags, st);
}

enum BannerKind { Banner_Info = 0, Banner_Success = 1, Banner_Warning = 2, Banner_Error = 3 };

struct BannerStyle {
    bool overlay = false;
    float timeout = 0.0f;
    bool closable = true;
    float width = 0.0f;
};

namespace detail {

inline ImU32 BannerColor(int kind) {
    const ess::Style& s = ess::GetStyle();
    switch (kind) {
        case Banner_Success: return s.toast_success;
        case Banner_Warning: return s.toast_warning;
        case Banner_Error: return s.toast_error;
        default: return s.toast_info;
    }
}

inline void BannerIcon(ImDrawList* dl, const ImVec2& c, float s, int kind, ImU32 col) {
    float th = ed::Max(1.5f, s * 0.16f);
    ImU32 ink = ed::IsBright(col) ? ed::DarkText() : ImGui::GetColorU32(IM_COL32_WHITE);
    if (kind == Banner_Warning) {
        dl->AddTriangleFilled(ImVec2(c.x, c.y - s), ImVec2(c.x + s * 1.05f, c.y + s * 0.8f),
                              ImVec2(c.x - s * 1.05f, c.y + s * 0.8f), col);
        dl->AddLine(ImVec2(c.x, c.y - s * 0.35f), ImVec2(c.x, c.y + s * 0.2f), ink, th);
        dl->AddCircleFilled(ImVec2(c.x, c.y + s * 0.5f), th * 0.6f, ink, 8);
        return;
    }
    dl->AddCircleFilled(c, s, col, 24);
    if (kind == Banner_Success) {
        ImVec2 p[3] = {ImVec2(c.x - s * 0.45f, c.y), ImVec2(c.x - s * 0.1f, c.y + s * 0.35f),
                       ImVec2(c.x + s * 0.5f, c.y - s * 0.35f)};
        Polyline(dl, p, 3, ink, th, false);
    } else if (kind == Banner_Error) {
        float q = s * 0.38f;
        dl->AddLine(ImVec2(c.x - q, c.y - q), ImVec2(c.x + q, c.y + q), ink, th);
        dl->AddLine(ImVec2(c.x + q, c.y - q), ImVec2(c.x - q, c.y + q), ink, th);
    } else {
        dl->AddCircleFilled(ImVec2(c.x, c.y - s * 0.42f), th * 0.6f, ink, 8);
        dl->AddLine(ImVec2(c.x, c.y - s * 0.12f), ImVec2(c.x, c.y + s * 0.5f), ink, th);
    }
}

// draws the banner in [mn, mx] (already slid) and submits its buttons when `interactive`.
// returns 1 for the action button, 2 for close
inline int BannerBody(ImDrawList* dl, const ImVec2& mn, const ImVec2& mx, const char* text, const char* action,
                      int kind, bool closable, bool interactive, float progress, float alpha) {
    float fs = ImGui::GetFontSize();
    float h = mx.y - mn.y;
    float r = ed::Max(ed::Rounding(), fs * 0.3f);
    ImU32 col = BannerColor(kind);
    ed::Shadow(dl, mn, mx, r, Live(IM_COL32(0, 0, 0, 90), alpha), fs * 0.4f);
    dl->AddRectFilled(mn, mx, Live(GetStyle().overlay_bg, alpha), r);
    ed::Gradient(dl, mn, ImVec2(mn.x + (mx.x - mn.x) * 0.45f, mx.y), Live(col, 0.22f * alpha), Live(col, 0.0f), false, r);
    float stripe = ed::Max(3.0f, fs * 0.22f);
    dl->AddRectFilled(mn, ImVec2(mn.x + stripe, mx.y), Live(col, alpha), r, ed::kRoundLeft);
    float pad = floorf(fs * 0.7f);
    float is = fs * 0.55f;
    BannerIcon(dl, ImVec2(mn.x + stripe + pad + is, (mn.y + mx.y) * 0.5f), is, kind, Live(col, alpha));
    float right = mx.x - pad;
    int result = 0;
    if (closable) {
        float cs = floorf(h * 0.5f);
        ImVec2 cmn(right - cs, mn.y + floorf((h - cs) * 0.5f)), cmx(right, cmn.y + cs);
        bool hot = false;
        if (interactive) {
            ImGui::SetCursorScreenPos(cmn);
            if (ImGui::InvisibleButton("##close", ImVec2(cs, cs))) result = 2;
            IMB_GAME_ITEM_HOOK("banner.close");
            hot = ImGui::IsItemHovered();
        }
        if (hot) dl->AddRectFilled(cmn, cmx, ed::Col(0, ImGuiCol_FrameBgHovered, alpha), cs * 0.5f);
        float q = cs * 0.22f;
        ImVec2 cc((cmn.x + cmx.x) * 0.5f, (cmn.y + cmx.y) * 0.5f);
        ImU32 xc = hot ? ed::TextCol(alpha) : ed::TextDim(alpha);
        dl->AddLine(ImVec2(cc.x - q, cc.y - q), ImVec2(cc.x + q, cc.y + q), xc, 1.5f);
        dl->AddLine(ImVec2(cc.x + q, cc.y - q), ImVec2(cc.x - q, cc.y + q), xc, 1.5f);
        right = cmn.x - pad * 0.5f;
    }
    if (action && action[0]) {
        ImVec2 ts = ImGui::CalcTextSize(action);
        float bh = floorf(h * 0.62f), bw = floorf(ts.x + fs * 1.4f);
        ImVec2 bmn(right - bw, mn.y + floorf((h - bh) * 0.5f)), bmx(right, bmn.y + bh);
        bool hot = false, held = false;
        if (interactive) {
            ImGui::SetCursorScreenPos(bmn);
            if (ImGui::InvisibleButton("##action", ImVec2(bw, bh))) result = 1;
            IMB_GAME_ITEM_HOOK("banner.action");
            hot = ImGui::IsItemHovered();
            held = ImGui::IsItemActive();
        }
        ImU32 bc = ed::LerpColor(Live(col, 0.8f * alpha), Live(ed::LerpColor(col, IM_COL32_WHITE, 0.2f), alpha), hot ? 1.0f : 0.0f);
        if (held) bc = Live(ed::LerpColor(col, IM_COL32(0, 0, 0, 255), 0.15f), alpha);
        dl->AddRectFilled(bmn, bmx, bc, bh * 0.5f);
        ImU32 tc = ed::IsBright(col) ? ed::DarkText() : ImGui::GetColorU32(IM_COL32_WHITE);
        ed::TextIn(dl, bmn, bmx, action, nullptr, ed::WithAlpha(tc, alpha), 0.5f);
        right = bmn.x - pad;
    }
    float tx = mn.x + stripe + pad * 2.0f + is * 2.0f;
    ed::TextIn(dl, ImVec2(tx, mn.y), ImVec2(right, mx.y), text ? text : "", nullptr, ed::TextCol(alpha), 0.0f);
    if (progress >= 0.0f) {
        float ph = ed::Max(2.0f, floorf(2.0f * Dpi()));
        dl->AddRectFilled(ImVec2(mn.x + stripe, mx.y - ph), ImVec2(mn.x + stripe + (mx.x - mn.x - stripe) * progress, mx.y),
                          Live(col, 0.7f * alpha));
    }
    return result;
}

}  // namespace detail

// slides down while *open is true and back up once it turns false. inline it pushes the content
// below it down; with overlay it floats at the top of the main viewport. the close button and the
// timeout set *open = false (open == nullptr starts shown and keeps the state in ImGui storage).
// returns true when the action button was clicked
inline bool NotificationBanner(const char* id, bool* open, const char* text, const char* action = nullptr,
                               int kind = Banner_Info, const BannerStyle& st = BannerStyle()) {
    namespace d = detail;
    float fs = ImGui::GetFontSize();
    float fh = ImGui::GetFrameHeight();
    ImGui::PushID(id);
    ImGuiID anim_id = ImGui::GetID("##slide");
    ImGuiID start_key = ImGui::GetID("##start"), was_key = ImGui::GetID("##was"), open_key = ImGui::GetID("##open");
    ImGuiStorage* storage = ImGui::GetStateStorage();
    bool local_open = storage->GetBool(open_key, true);
    bool* in_open = open;
    if (!open) open = &local_open;
    float now = d::Time();
    if (*open && !storage->GetBool(was_key, false)) storage->SetFloat(start_key, now);
    storage->SetBool(was_key, *open);
    float t = d::ed::Anim(anim_id, *open, 0.3f);
    if (t <= 0.001f && !*open) {
        if (!in_open) storage->SetBool(open_key, local_open);
        ImGui::PopID();
        return false;
    }
    float h = floorf(fh * 1.8f);
    float progress = -1.0f;
    if (st.timeout > 0.0f && *open) {
        float elapsed = now - storage->GetFloat(start_key, now);
        progress = 1.0f - d::ed::Clamp01(elapsed / st.timeout);
    }
    int result = 0;
    bool interactive = t > 0.9f && *open;
    if (st.overlay) {
        ImGuiViewport* vp = ImGui::GetMainViewport();
        float w = st.width > 0.0f ? st.width : d::ed::Min(vp->WorkSize.x - fs * 2.0f, fs * 40.0f);
        float margin = fs * 0.75f;
        ImVec2 p(floorf(vp->WorkPos.x + (vp->WorkSize.x - w) * 0.5f), floorf(vp->WorkPos.y + margin - (h + margin * 2.0f) * (1.0f - t)));
        char name[96];
        std::snprintf(name, sizeof(name), "##imb_banner_%s", id);
        ImGui::SetNextWindowPos(p);
        ImGui::SetNextWindowSize(ImVec2(w, h));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGuiWindowFlags wf = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove |
                              ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoBackground;
        ImGui::Begin(name, nullptr, wf);
        ImGui::PopStyleVar(2);
        ImVec2 wp = ImGui::GetWindowPos();
        bool hovered = ImGui::IsWindowHovered();
        result = d::BannerBody(ImGui::GetWindowDrawList(), wp, ImVec2(wp.x + w, wp.y + h), text, action, kind, st.closable,
                               interactive, progress, t);
        ImGui::End();
        if (hovered && st.timeout > 0.0f) storage->SetFloat(start_key, storage->GetFloat(start_key, now) + d::Dt());
    } else {
        ImVec2 pos = ImGui::GetCursorScreenPos();
        float w = st.width > 0.0f ? st.width : d::ed::Max(ImGui::GetContentRegionAvail().x, fs * 10.0f);
        float vis = floorf(h * t);
        ImVec2 mn(pos.x, pos.y - (h - vis)), mx(pos.x + w, pos.y + vis);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        bool hovered = ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(pos, ImVec2(pos.x + w, pos.y + vis));
        dl->PushClipRect(pos, ImVec2(pos.x + w + fs, pos.y + vis + fs * 0.5f), true);
        result = d::BannerBody(dl, mn, mx, text, action, kind, st.closable, interactive, progress, t);
        dl->PopClipRect();
        ImGui::SetCursorScreenPos(pos);
        ImGui::Dummy(ImVec2(w, d::ed::Max(vis, 1.0f)));
        if (hovered && st.timeout > 0.0f) storage->SetFloat(start_key, storage->GetFloat(start_key, now) + d::Dt());
    }
    if (result == 2) *open = false;
    if (progress == 0.0f) *open = false;
    if (!in_open) storage->SetBool(open_key, local_open);
    ImGui::PopID();
    return result == 1;
}

inline bool NotificationBanner(const char* id, bool* open, const char* text, const char* action, int kind, bool overlay,
                               float timeout) {
    BannerStyle st;
    st.overlay = overlay;
    st.timeout = timeout;
    return NotificationBanner(id, open, text, action, kind, st);
}

}  // namespace game
}  // namespace imb
