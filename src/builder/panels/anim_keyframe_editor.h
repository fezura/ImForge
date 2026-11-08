#pragma once

#include <string>

#include "imgui.h"

#include "imforge/imforge_json.h"

#include "anim_keyframe_edit.h"

namespace imb {
struct AnimDef;
}

namespace imforge::builder {

struct EditorContext;

namespace kfui {

// the "Keyframes" tab of the Anim Timeline panel
void DrawKeyframesTab(EditorContext& ctx);

// shared by anim_keyframe_*.cpp
struct EditorState {
    std::string name;
    Json source;
    kf::AnimEdit edit;
    std::string load_error;
    int track = 0, key = 0;
    float progress = 0.0f;   // playhead in keyframe space, 0..1
    float time = 0.0f;       // playhead in seconds when full timing is on
    float hold = 0.0f;
    bool playing = true, loop = true, full_timing = false;
    bool dragging = false;
    std::string rename_buf, rename_error, preset_search;
};

EditorState& State();
// one undo step per label, edits with the same label inside 0.3 s merge (drags)
void Commit(EditorContext& ctx, const char* label);
kf::TrackEdit* SelectedTrack();

void DrawTimeline(EditorContext& ctx, float height);
void DrawKeyInspector(EditorContext& ctx);

// ease picker: curve thumbnail + name, opens a popup with named eases, a bezier editor, steps and spring.
// `ease` empty means the animation's default when default_ease is set
bool EaseField(const char* id, std::string& ease, const char* default_ease, float width);
void DrawEaseThumb(ImDrawList* dl, const ImVec2& min, const ImVec2& max, const std::string& ease, ImU32 col,
                   float thickness = 1.0f);
float SampleEaseString(const std::string& ease, float t);

// keyframe-space preview of def at `progress`, drawn with the motion_preview sample element
void DrawScrubPreview(const imb::AnimDef& def, float progress, const ImVec2& min, const ImVec2& max);

}  // namespace kfui

}  // namespace imforge::builder
