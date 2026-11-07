#pragma once

#include <string>

#include "imforge/imforge_core.h"

#include "anim_preview_state.h"

namespace imforge::builder {

// Play/pause/scrub and forced triggers for the AnimTimeline panel. Pending AnimPreviewState requests from
// Properties get picked up in AnimPreviewDeltaTime() and turned into a replay plus an override.
//
// Canvas, once per frame before drawing the panel:
//   float dt = AnimPreviewDeltaTime(io.DeltaTime);   // 0 while paused, scaled while playing; may imb::Reset()
//   imb::NewFrame(dt);
//   opts.animations = AnimPreviewAnimationsEnabled();
// Interpreter (design mode), when working out hover/active/open/visible/focus/disabled:
//   int forced = AnimPreviewForcedState(node, trigger);   // -1 = not forced, 0 = force off, 1 = force on
//   if (forced >= 0) state = forced != 0;

enum AnimPreviewTriggerBit {
    kAnimTriggerHover = 1 << 0,
    kAnimTriggerActive = 1 << 1,
    kAnimTriggerOpen = 1 << 2,
    kAnimTriggerVisible = 1 << 3,
    kAnimTriggerFocus = 1 << 4,
    kAnimTriggerDisabled = 1 << 5,
};

struct AnimPreviewDriver {
    bool playing = true;
    bool loop = false;
    float time = 0.0f;
    float time_scale = 1.0f;
    float duration = 1.0f;
    bool replay_requested = false;
    bool preview_animations = true;
    std::string node_id;
    int override_mask = 0;
    int trigger_mask = 0;
    int transition_index = -1;
    float scrub_to = -1.0f;
    float pending_jump = 0.0f;
    int hold_frames = 0;
    int hold_state = 0;
    float hold_dt = 0.0f;
    unsigned last_request_generation = 0;
    unsigned replay_generation = 0;
    int reset_frame = -1;
};

AnimPreviewDriver& GetAnimPreviewDriver();

// eats Properties requests, applies replay/seek/loop, returns the dt to pass to imb::NewFrame()
float AnimPreviewDeltaTime(float real_dt);
bool AnimPreviewAnimationsEnabled();

int AnimPreviewTriggerBit(const char* trigger);
// -1 if not forced, else 0/1. The Node overload also picks up a pending "preview transition N"
// request from Properties by matching transitions[N].on.
int AnimPreviewForcedState(const std::string& node_id, const char* trigger);
int AnimPreviewForcedState(const Node& node, const char* trigger);
bool AnimPreviewForceState(const std::string& node_id, const char* trigger);

// state -1 clears, 0 forces off, 1 forces on. switching to another node drops the old overrides
void AnimPreviewSetTrigger(const std::string& node_id, const char* trigger, int state);
void AnimPreviewClearTriggers();

void AnimPreviewPlay(bool play);
void AnimPreviewStop();
void AnimPreviewReplay();
void AnimPreviewSeek(float seconds);
// turns a pending transition request into a real trigger override so the panel can show it
void AnimPreviewResolveRequest(const Node& node);

}  // namespace imforge::builder
