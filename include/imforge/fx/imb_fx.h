#pragma once
// imb::fx: shader effects (blur behind windows). Backend-agnostic API.
// Include exactly one backend header (imb_fx_dx11.h, imb_fx_gl3.h, ...), it pulls this in.
//
// Each frame:
//   1. render your scene
//   2. imb::fx::PrepareFrame(fd);   // grabs the back buffer, builds the blurred mips
//   3. ImGui::Render(); ImGui_ImplXXX_RenderDrawData(...);
//
// Call Blur() between NewFrame() and Render(), it adds a draw callback + quad to `dl`.
#include "imgui.h"

namespace imb {
namespace fx {

struct FrameDesc;   // backend specific

struct BlurParams {
    float radius = 8.0f;          // pixels, 0..64
    float rounding = 0.0f;
    ImU32 tint = 0xFFFFFFFF;      // multiplied over the blurred result
    float saturation = 1.0f;      // 1 = unchanged
    float brightness = 1.0f;      // 1 = unchanged
    float noise = 0.0f;           // 0..1 dither, helps with banding
};

// implemented by the backend header
bool IsAvailable();
void PrepareFrame(const FrameDesc& fd);
void Shutdown();
void Blur(ImDrawList* dl, const ImVec2& min, const ImVec2& max, const BlurParams& params);

inline void Blur(ImDrawList* dl, const ImVec2& min, const ImVec2& max, float radius,
                 float rounding = 0.0f, ImU32 tint = 0xFFFFFFFF) {
    BlurParams p; p.radius = radius; p.rounding = rounding; p.tint = tint;
    Blur(dl, min, max, p);
}

// call right after ImGui::Begin()
inline void BlurCurrentWindow(float radius, float rounding = -1.0f) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 pos = ImGui::GetWindowPos();
    ImVec2 size = ImGui::GetWindowSize();
    if (rounding < 0.0f) rounding = ImGui::GetStyle().WindowRounding;
    Blur(dl, pos, ImVec2(pos.x + size.x, pos.y + size.y), radius, rounding);
}

}  // namespace fx
}  // namespace imb
