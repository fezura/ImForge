#pragma once

#include <string>
#include <vector>

#include "imgui.h"

#include "imforge/imforge_anim_names.h"

namespace imforge::builder::ui {

using imforge::EaseFromName;
using imforge::EaseName;
using imforge::PresetFromName;
using imforge::PresetName;

inline imb::Ease EaseFromName(const std::string& name) { return imforge::EaseFromName(name.c_str()); }
inline imb::Preset PresetFromName(const std::string& name) { return imforge::PresetFromName(name.c_str()); }

struct SpringParams {
    float stiffness = 170.0f;
    float damping = 26.0f;
};

// must match imb::Spring's semi-implicit integrator (dt clamped to 1/30, (int)(dt*120)+1 substeps), stepped at 1/60
float SpringStep(float& value, float& velocity, float target, const SpringParams& spring, float dt = 1.0f / 60.0f);
// samples the 0 to 1 response and returns how many samples it wrote; stops once settled or at max_samples
int SampleSpring(const SpringParams& spring, std::vector<float>& out, int max_samples = 600);
// time to settle within 1e-3 of the target, capped at 10 s
float SpringSettleTime(const SpringParams& spring);

// y grows upward, overshoot gets clipped to the rect's vertical padding
void DrawEaseCurve(ImDrawList* dl, const ImVec2& min, const ImVec2& max, imb::Ease ease, ImU32 color,
                   float thickness = 1.5f, int samples = 64);
void DrawSpringCurve(ImDrawList* dl, const ImVec2& min, const ImVec2& max, const SpringParams& spring, ImU32 color,
                     float thickness = 1.5f);
// grid plus baseline (0) and target (1) lines for the big curve editor
void DrawCurveGrid(ImDrawList* dl, const ImVec2& min, const ImVec2& max, ImU32 line_color, ImU32 bg_color);
// t in 0..1, leaves the same overshoot padding as the curve editor
ImVec2 CurvePoint(const ImVec2& min, const ImVec2& max, float t, float value);

}  // namespace imforge::builder::ui
