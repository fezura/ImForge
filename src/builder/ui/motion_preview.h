#pragma once

#include "imgui.h"

#if __has_include("imforge/imb_motion.h")
#include "imforge/imb_motion.h"
#define IMFORGE_HAS_MOTION 1
#else
#define IMFORGE_HAS_MOTION 0
#endif

namespace imforge::builder {

struct MotionPreviewStyle {
    ImU32 accent = 0, accent2 = 0, text = 0;  // 0 picks the builder accent / white text
    const char* label = nullptr;              // nullptr: "Aa", or "Hello" for text motions
    const char* icon = nullptr;               // nullptr: a star glyph when the icon font is loaded
    bool hovered = false, pressed = false;    // drive hover and press motions
    float period = 1.5f;                      // one-shot motions replay at least this often
    float size = 0.56f;                       // element width as a fraction of the rect
    float phase = 0.0f;                       // seconds added to the shared replay clock
};

enum class MotionPreviewKind { OneShot, Exit, Loop, Hover, Press, Text, Stagger };

#if IMFORGE_HAS_MOTION
MotionPreviewKind MotionPreviewKindOf(const imb::AnimDef& def);

// Draws the sample element inside [min, max] wrapped in imb::BeginMotion/EndMotion. Loops always run,
// one-shot motions replay on a shared clock, exits show the element and then play, hover/press motions
// follow style.hovered / style.pressed. Runs on io.DeltaTime even while the canvas clock is paused.
// Leaves the cursor where it was. `id` has to be unique per preview slot.
void DrawMotionPreview(ImGuiID id, const imb::AnimDef& def, const ImVec2& min, const ImVec2& max,
                       const MotionPreviewStyle& style = MotionPreviewStyle());
#endif

// the static element, also used when previews are capped or the engine isn't built in
void DrawMotionSample(ImDrawList* dl, const ImVec2& min, const ImVec2& max, const MotionPreviewStyle& style,
                      MotionPreviewKind kind, float text_reveal = 1.0f);
void MotionSampleRect(const ImVec2& min, const ImVec2& max, const MotionPreviewStyle& style, MotionPreviewKind kind,
                      ImVec2& out_min, ImVec2& out_max);

}  // namespace imforge::builder
