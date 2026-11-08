#include "motion_preview.h"

#include <cfloat>
#include <cmath>
#include <cstring>

#include "imforge/imb_runtime.h"

#include "../builder_fonts.h"
#include "../builder_style.h"

namespace imforge::builder {

namespace {

const ImGuiID kCycleSalt = 0x6d6f7469u;

ImU32 AccentOr(ImU32 c) { return c ? c : ToU32(Colors().accent); }

ImU32 Lighter(ImU32 c, float t) {
    ImVec4 v = ImGui::ColorConvertU32ToFloat4(c);
    v.x += (1.0f - v.x) * t;
    v.y += (1.0f - v.y) * t;
    v.z += (1.0f - v.z) * t;
    return ImGui::ColorConvertFloat4ToU32(v);
}

ImU32 Alpha(ImU32 c, float a) {
    ImVec4 v = ImGui::ColorConvertU32ToFloat4(c);
    v.w *= a;
    return ImGui::ColorConvertFloat4ToU32(v);
}

const char* SampleIcon(const MotionPreviewStyle& st) {
    if (st.icon) return st.icon;
#if IMFORGE_HAS_ICON_HEADER
    return GetFonts().has_icons ? ICON_FA_STAR : nullptr;
#else
    return nullptr;
#endif
}

void DrawCard(ImDrawList* dl, const ImVec2& mn, const ImVec2& mx, const MotionPreviewStyle& st) {
    const float h = mx.y - mn.y, w = mx.x - mn.x;
    const float r = std::floor(h * 0.24f);
    const ImU32 accent = AccentOr(st.accent);
    const ImU32 accent2 = st.accent2 ? st.accent2 : Lighter(accent, 0.28f);
    imb::Shadow(dl, mn, mx, r, IM_COL32(0, 0, 0, 110), std::floor(h * 0.32f), ImVec2(0, std::floor(h * 0.08f)));
    dl->AddRectFilled(mn, mx, accent, r);
    dl->AddRectFilledMultiColor(ImVec2(mn.x + r * 0.5f, mn.y + 1.0f), ImVec2(mx.x - r * 0.5f, mn.y + h * 0.5f),
                                Alpha(accent2, 0.0f), Alpha(accent2, 0.45f), Alpha(accent2, 0.0f), Alpha(accent2, 0.0f));
    dl->AddRect(mn, mx, IM_COL32(255, 255, 255, 40), r, 0, 1.0f);
    ImFont* font = ImGui::GetFont();
    const float fs = std::floor(h * 0.4f);
    const ImU32 text = st.text ? st.text : IM_COL32(255, 255, 255, 255);
    const char* label = st.label ? st.label : "Aa";
    const char* icon = SampleIcon(st);
    const float disc = std::floor(h * 0.3f);
    const float lw = font->CalcTextSizeA(fs, FLT_MAX, 0.0f, label).x;
    const float gap = icon ? std::floor(h * 0.16f) : 0.0f;
    const float total = (icon ? disc * 2.0f + gap : 0.0f) + lw;
    float x = std::floor(mn.x + (w - total) * 0.5f);
    const float cy = mn.y + h * 0.5f;
    if (icon) {
        const ImVec2 c(x + disc, cy);
        dl->AddCircleFilled(c, disc, IM_COL32(255, 255, 255, 46), 24);
        const float is = std::floor(disc * 1.05f);
        const ImVec2 gs = font->CalcTextSizeA(is, FLT_MAX, 0.0f, icon);
        dl->AddText(font, is, ImVec2(std::floor(c.x - gs.x * 0.5f), std::floor(c.y - gs.y * 0.5f)), text, icon);
        x += disc * 2.0f + gap;
    }
    dl->AddText(font, fs, ImVec2(x, std::floor(cy - fs * 0.5f)), text, label);
}

void DrawText(ImDrawList* dl, const ImVec2& mn, const ImVec2& mx, const MotionPreviewStyle& st, float reveal) {
    ImFont* font = ImGui::GetFont();
    const char* label = st.label ? st.label : "Hello";
    const size_t len = std::strlen(label);
    const float h = mx.y - mn.y;
    float fs = std::floor(h * 0.72f);
    float tw = font->CalcTextSizeA(fs, FLT_MAX, 0.0f, label).x;
    if (tw > mx.x - mn.x && tw > 0.0f) {
        fs = std::floor(fs * (mx.x - mn.x) / tw);
        tw = font->CalcTextSizeA(fs, FLT_MAX, 0.0f, label).x;
    }
    reveal = reveal < 0.0f ? 0.0f : (reveal > 1.0f ? 1.0f : reveal);
    size_t n = (size_t)std::floor((float)len * reveal + 0.5f);
    const float x = std::floor((mn.x + mx.x - tw) * 0.5f), y = std::floor(mn.y + (h - fs) * 0.42f);
    const ImU32 text = st.text ? st.text : ToU32(Colors().text);
    if (n > 0) dl->AddText(font, fs, ImVec2(x, y), text, label, label + n);
    const float bar_h = std::fmax(2.0f, std::floor(h * 0.06f));
    dl->AddRectFilled(ImVec2(x, mx.y - bar_h), ImVec2(x + tw * reveal, mx.y), AccentOr(st.accent), bar_h * 0.5f);
}

void DrawRow(ImDrawList* dl, const ImVec2& mn, const ImVec2& mx, const MotionPreviewStyle& st, int i) {
    const float h = mx.y - mn.y, r = std::floor(h * 0.3f);
    const ImU32 accent = AccentOr(st.accent);
    dl->AddRectFilled(mn, mx, Alpha(Lighter(accent, 0.15f), 0.22f + 0.06f * (float)i), r);
    dl->AddRect(mn, mx, Alpha(accent, 0.55f), r, 0, 1.0f);
    const float d = std::floor(h * 0.22f);
    dl->AddCircleFilled(ImVec2(mn.x + h * 0.5f, mn.y + h * 0.5f), d, accent, 16);
    const float bx = mn.x + h, bw = (mx.x - bx - h * 0.4f) * (i == 1 ? 0.55f : 0.8f), bh = std::fmax(2.0f, std::floor(h * 0.16f));
    const float by = std::floor(mn.y + (h - bh) * 0.5f);
    dl->AddRectFilled(ImVec2(bx, by), ImVec2(bx + bw, by + bh), IM_COL32(255, 255, 255, 150), bh * 0.5f);
}

float TotalTime(const imb::AnimDef& def) {
    const int iters = def.iterations > 0 ? def.iterations : 1;
    return def.delay + (def.duration > 0.0f ? def.duration : 0.0f) * (float)iters;
}

struct Clock {
    double t = 0.0;
    int frame = -1;
};

double PreviewClock() {
    static Clock c;
    const int f = ImGui::GetFrameCount();
    if (c.frame != f) {
        c.frame = f;
        float dt = ImGui::GetIO().DeltaTime;
        c.t += dt > 0.25f ? 0.25f : dt;
    }
    return c.t;
}

}  // namespace

void MotionSampleRect(const ImVec2& min, const ImVec2& max, const MotionPreviewStyle& st, MotionPreviewKind kind,
                      ImVec2& out_min, ImVec2& out_max) {
    const float aw = max.x - min.x, ah = max.y - min.y;
    float w, h;
    switch (kind) {
    case MotionPreviewKind::Text:
        w = aw * 0.8f;
        h = std::fmin(ah * 0.42f, w * 0.34f);
        break;
    case MotionPreviewKind::Stagger:
        w = aw * 0.64f;
        h = ah * 0.66f;
        break;
    default:
        w = std::fmin(aw * st.size, ah * 1.15f);
        h = w * 0.56f;
        break;
    }
    out_min = ImVec2(std::floor(min.x + (aw - w) * 0.5f), std::floor(min.y + (ah - h) * 0.5f));
    out_max = ImVec2(std::floor(out_min.x + w), std::floor(out_min.y + h));
}

void DrawMotionSample(ImDrawList* dl, const ImVec2& min, const ImVec2& max, const MotionPreviewStyle& st,
                      MotionPreviewKind kind, float text_reveal) {
    ImVec2 mn, mx;
    MotionSampleRect(min, max, st, kind, mn, mx);
    if (kind == MotionPreviewKind::Text) {
        DrawText(dl, mn, mx, st, text_reveal);
    } else if (kind == MotionPreviewKind::Stagger) {
        const float gap = std::floor((mx.y - mn.y) * 0.08f), rh = std::floor((mx.y - mn.y - gap * 2.0f) / 3.0f);
        for (int i = 0; i < 3; ++i) {
            const float y = mn.y + (rh + gap) * (float)i;
            DrawRow(dl, ImVec2(mn.x, y), ImVec2(mx.x, y + rh), st, i);
        }
    } else {
        DrawCard(dl, mn, mx, st);
    }
}

#if IMFORGE_HAS_MOTION

MotionPreviewKind MotionPreviewKindOf(const imb::AnimDef& def) {
    const char* cat = def.category ? def.category : "";
    if (std::strcmp(cat, "stagger") == 0) return MotionPreviewKind::Stagger;
    if (std::strcmp(cat, "text") == 0) return MotionPreviewKind::Text;
    switch (def.default_trigger) {
    case imb::Trigger::Loop: return MotionPreviewKind::Loop;
    case imb::Trigger::Hover:
    case imb::Trigger::Focus: return MotionPreviewKind::Hover;
    case imb::Trigger::Press: return MotionPreviewKind::Press;
    case imb::Trigger::Exit: return MotionPreviewKind::Exit;
    default: break;
    }
    if (std::strcmp(cat, "loop") == 0) return MotionPreviewKind::Loop;
    if (std::strcmp(cat, "exit") == 0) return MotionPreviewKind::Exit;
    if (std::strcmp(cat, "hover") == 0) return MotionPreviewKind::Hover;
    if (std::strcmp(cat, "press") == 0) return MotionPreviewKind::Press;
    return MotionPreviewKind::OneShot;
}

void DrawMotionPreview(ImGuiID id, const imb::AnimDef& def, const ImVec2& min, const ImVec2& max,
                       const MotionPreviewStyle& st) {
    const MotionPreviewKind kind = MotionPreviewKindOf(def);
    const double clock = PreviewClock() + (double)st.phase + 1000.0;
    // the canvas may run the shared imb clock paused or slowed, previews always run in real time
    imb::detail::Runtime& rt = imb::detail::State();
    const float saved_dt = rt.dt_override;
    const int saved_frame = rt.dt_frame;
    rt.dt_override = ImGui::GetIO().DeltaTime;
    rt.dt_frame = ImGui::GetFrameCount();

    imb::MotionBinding b;
    b.def = &def;
    imb::MotionOptions opt;
    opt.glow_color = AccentOr(st.accent);
    switch (kind) {
    case MotionPreviewKind::Loop: b.trigger = imb::Trigger::Loop; break;
    case MotionPreviewKind::Hover:
        b.trigger = imb::Trigger::Hover;
        opt.force_hover = st.hovered;
        break;
    case MotionPreviewKind::Press:
        b.trigger = imb::Trigger::Press;
        opt.force_press = st.pressed || st.hovered;
        break;
    case MotionPreviewKind::Exit: b.trigger = imb::Trigger::Exit; break;
    default: b.trigger = imb::Trigger::Enter; break;
    }
    const bool infinite = def.iterations == 0;
    const float period = std::fmax(st.period, (infinite ? def.duration : TotalTime(def)) + (kind == MotionPreviewKind::Exit ? 1.15f : 0.6f));
    const int cycle = (int)(clock / period);
    const float phase = (float)(clock - (double)cycle * period);
    if (kind == MotionPreviewKind::Exit) opt.visible = phase < 0.8f;
    const bool replay = kind == MotionPreviewKind::OneShot || kind == MotionPreviewKind::Text || kind == MotionPreviewKind::Stagger;

    const ImVec2 cursor = ImGui::GetCursorScreenPos();
    ImGui::PushClipRect(min, max, true);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 emn, emx;
    MotionSampleRect(min, max, st, kind, emn, emx);
    ImGuiStorage* storage = ImGui::GetStateStorage();
    if (kind == MotionPreviewKind::Stagger) {
        b.stagger = 0.09f;
        const float gap = std::floor((emx.y - emn.y) * 0.08f), rh = std::floor((emx.y - emn.y - gap * 2.0f) / 3.0f);
        const bool restart = storage->GetInt(id ^ kCycleSalt, -1) != cycle;
        storage->SetInt(id ^ kCycleSalt, cycle);
        for (int i = 0; i < 3; ++i) {
            const ImGuiID rid = id + (ImGuiID)i * 0x9e3779b9u;
            if (restart) imb::TriggerMotion(rid, imb::Trigger::Enter);
            opt.stagger_index = i;
            const float y = emn.y + (rh + gap) * (float)i;
            ImGui::SetCursorScreenPos(ImVec2(emn.x, y));
            if (imb::BeginMotion(rid, &b, 1, opt)) {
                DrawRow(dl, ImVec2(emn.x, y), ImVec2(emx.x, y + rh), st, i);
                ImGui::Dummy(ImVec2(emx.x - emn.x, rh));
                imb::EndMotion();
            }
        }
    } else {
        if (replay) {
            const int last = storage->GetInt(id ^ kCycleSalt, -1);
            if (last != cycle) {
                if (last != -1) imb::TriggerMotion(id, imb::Trigger::Enter);
                storage->SetInt(id ^ kCycleSalt, cycle);
            }
        }
        ImGui::SetCursorScreenPos(emn);
        if (imb::BeginMotion(id, &b, 1, opt)) {
            DrawMotionSample(dl, min, max, st, kind, imb::MotionTextReveal(id));
            ImGui::Dummy(ImVec2(emx.x - emn.x, emx.y - emn.y));
            imb::EndMotion();
        }
    }
    ImGui::PopClipRect();
    ImGui::SetCursorScreenPos(cursor);
    rt.dt_override = saved_dt;
    rt.dt_frame = saved_frame;
}

#endif

}  // namespace imforge::builder
