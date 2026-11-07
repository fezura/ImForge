#include "ease_curves.h"

#include <cmath>

namespace imforge::builder::ui {

namespace {
const float kOvershootPad = 0.25f;
const float kSettleEps = 1e-3f;
const float kMaxSettleSeconds = 10.0f;
}  // namespace

float SpringStep(float& value, float& velocity, float target, const SpringParams& spring, float dt) {
    if (dt > 1.0f / 30.0f) dt = 1.0f / 30.0f;
    if (dt < 0.0f) dt = 0.0f;
    int steps = (int)(dt * 120.0f) + 1;
    float h = dt / (float)steps;
    for (int i = 0; i < steps; i++) {
        float a = spring.stiffness * (target - value) - spring.damping * velocity;
        velocity += a * h;
        value += velocity * h;
    }
    if (std::fabs(target - value) < 1e-4f && std::fabs(velocity) < 1e-4f) {
        value = target;
        velocity = 0.0f;
    }
    return value;
}

int SampleSpring(const SpringParams& spring, std::vector<float>& out, int max_samples) {
    out.clear();
    if (max_samples < 2) max_samples = 2;
    float v = 0.0f, vel = 0.0f;
    out.push_back(0.0f);
    for (int i = 1; i < max_samples; ++i) {
        SpringStep(v, vel, 1.0f, spring);
        out.push_back(v);
        if (std::fabs(1.0f - v) < kSettleEps && std::fabs(vel) < kSettleEps) break;
    }
    return (int)out.size();
}

float SpringSettleTime(const SpringParams& spring) {
    float v = 0.0f, vel = 0.0f;
    const int max_steps = (int)(kMaxSettleSeconds * 60.0f);
    for (int i = 1; i <= max_steps; ++i) {
        SpringStep(v, vel, 1.0f, spring);
        if (std::fabs(1.0f - v) < kSettleEps && std::fabs(vel) < kSettleEps) return (float)i / 60.0f;
    }
    return kMaxSettleSeconds;
}

ImVec2 CurvePoint(const ImVec2& min, const ImVec2& max, float t, float value) {
    float h = max.y - min.y;
    float pad = h * kOvershootPad;
    float y0 = max.y - pad, y1 = min.y + pad;
    float y = y0 + (y1 - y0) * value;
    if (y < min.y) y = min.y;
    if (y > max.y) y = max.y;
    return ImVec2(min.x + (max.x - min.x) * t, y);
}

void DrawCurveGrid(ImDrawList* dl, const ImVec2& min, const ImVec2& max, ImU32 line_color, ImU32 bg_color) {
    dl->AddRectFilled(min, max, bg_color, 4.0f);
    for (int i = 1; i < 4; ++i) {
        float x = min.x + (max.x - min.x) * (float)i / 4.0f;
        dl->AddLine(ImVec2(x, min.y), ImVec2(x, max.y), line_color);
    }
    ImVec2 base = CurvePoint(min, max, 0.0f, 0.0f), top = CurvePoint(min, max, 0.0f, 1.0f);
    dl->AddLine(ImVec2(min.x, base.y), ImVec2(max.x, base.y), line_color, 1.0f);
    dl->AddLine(ImVec2(min.x, top.y), ImVec2(max.x, top.y), line_color, 1.0f);
}

void DrawEaseCurve(ImDrawList* dl, const ImVec2& min, const ImVec2& max, imb::Ease ease, ImU32 color, float thickness,
                   int samples) {
    if (max.x <= min.x || max.y <= min.y) return;
    if (samples < 2) samples = 2;
    if (samples > 512) samples = 512;
    ImVec2 pts[512];
    for (int i = 0; i < samples; ++i) {
        float t = (float)i / (float)(samples - 1);
        pts[i] = CurvePoint(min, max, t, imb::EaseApply(ease, t));
    }
    dl->AddPolyline(pts, samples, color, ImDrawFlags_None, thickness);
}

void DrawSpringCurve(ImDrawList* dl, const ImVec2& min, const ImVec2& max, const SpringParams& spring, ImU32 color,
                     float thickness) {
    if (max.x <= min.x || max.y <= min.y) return;
    std::vector<float> values;
    int n = SampleSpring(spring, values);
    if (n < 2) return;
    std::vector<ImVec2> pts((size_t)n);
    for (int i = 0; i < n; ++i) pts[(size_t)i] = CurvePoint(min, max, (float)i / (float)(n - 1), values[(size_t)i]);
    dl->AddPolyline(pts.data(), n, color, ImDrawFlags_None, thickness);
}

}  // namespace imforge::builder::ui
