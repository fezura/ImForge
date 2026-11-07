#include "anim_preview_driver.h"

#include <cstring>

#include "imgui.h"

#include "imforge/imb_runtime.h"
#include "imforge/imforge_anim_names.h"

namespace imforge::builder {

namespace {

const int kReplayHoldFrames = 2;
const float kMinTimeScale = 0.1f;
const float kMaxTimeScale = 2.0f;
const float kMaxJumpSeconds = 60.0f;
const float kSettleSeconds = 5.0f;

void ConsumeRequest(AnimPreviewDriver& d) {
    const AnimPreviewState& s = GetAnimPreviewState();
    if (!s.Pending(d.last_request_generation)) return;
    d.last_request_generation = s.generation;
    d.node_id = s.node_id;
    d.loop = s.loop;
    d.override_mask = 0;
    d.trigger_mask = 0;
    d.transition_index = -1;
    switch (s.kind) {
    case AnimPreviewState::Kind::Enter:
        d.override_mask = kAnimTriggerVisible;
        d.trigger_mask = kAnimTriggerVisible;
        break;
    case AnimPreviewState::Kind::Exit:
        d.override_mask = kAnimTriggerVisible;
        d.trigger_mask = 0;
        break;
    case AnimPreviewState::Kind::Transition: d.transition_index = s.transition_index; break;
    case AnimPreviewState::Kind::None: break;
    }
    d.playing = true;
    d.replay_requested = true;
}

int MaskedState(const AnimPreviewDriver& d, int bit) {
    if (bit == 0 || !(d.override_mask & bit)) return -1;
    if (d.hold_frames > 0) return d.hold_state;
    return (d.trigger_mask & bit) ? 1 : 0;
}

// exit previews have to start from a fully entered node. During the hold the node reads as visible and a
// settle jump finishes its enter, then the forced "off" plays the exit from t = 1
bool ReplaysExit(const AnimPreviewDriver& d) {
    return (d.override_mask & kAnimTriggerVisible) && !(d.trigger_mask & kAnimTriggerVisible);
}

}  // namespace

AnimPreviewDriver& GetAnimPreviewDriver() {
    static AnimPreviewDriver d;
    return d;
}

bool AnimPreviewAnimationsEnabled() { return GetAnimPreviewDriver().preview_animations; }

int AnimPreviewTriggerBit(const char* trigger) {
    int idx = TransitionTriggerIndex(trigger);
    return idx < 0 ? 0 : (1 << idx);
}

float AnimPreviewDeltaTime(float real_dt) {
    AnimPreviewDriver& d = GetAnimPreviewDriver();
    ConsumeRequest(d);
    if (d.time_scale < kMinTimeScale) d.time_scale = kMinTimeScale;
    if (d.time_scale > kMaxTimeScale) d.time_scale = kMaxTimeScale;
    if (d.duration < 1.0f) d.duration = 1.0f;
    if (real_dt < 0.0f) real_dt = 0.0f;

    if (d.scrub_to >= 0.0f) {
        if (d.scrub_to >= d.time) {
            d.pending_jump = d.scrub_to - d.time;
        } else {
            d.replay_requested = true;
            d.pending_jump = d.scrub_to;
        }
        d.scrub_to = -1.0f;
    }

    if (d.replay_requested) {
        d.replay_requested = false;
        imb::Reset();
        d.time = 0.0f;
        d.hold_frames = kReplayHoldFrames;
        bool exit = ReplaysExit(d);
        d.hold_state = exit ? 1 : 0;
        d.hold_dt = exit ? kSettleSeconds : 0.0f;
        ++d.replay_generation;
        d.reset_frame = ImGui::GetFrameCount();
        return 0.0f;
    }

    float dt = 0.0f;
    if (d.hold_frames > 0) {
        --d.hold_frames;
        dt = d.hold_dt;
        d.hold_dt = 0.0f;
        return dt;
    } else {
        if (d.playing) dt = real_dt * d.time_scale;
        if (d.pending_jump > 0.0f) {
            if (d.pending_jump > kMaxJumpSeconds) d.pending_jump = kMaxJumpSeconds;
            dt += d.pending_jump;
            d.pending_jump = 0.0f;
        }
    }
    d.time += dt;
    if (d.loop && d.playing && d.time >= d.duration) d.replay_requested = true;
    return dt;
}

int AnimPreviewForcedState(const std::string& node_id, const char* trigger) {
    const AnimPreviewDriver& d = GetAnimPreviewDriver();
    if (d.node_id.empty() || d.node_id != node_id) return -1;
    return MaskedState(d, AnimPreviewTriggerBit(trigger));
}

int AnimPreviewForcedState(const Node& node, const char* trigger) {
    const AnimPreviewDriver& d = GetAnimPreviewDriver();
    if (d.node_id.empty() || d.node_id != node.id) return -1;
    int bit = AnimPreviewTriggerBit(trigger);
    int masked = MaskedState(d, bit);
    if (masked >= 0) return masked;
    if (d.transition_index >= 0 && d.transition_index < (int)node.anim.transitions.size() && trigger &&
        node.anim.transitions[(size_t)d.transition_index].on == trigger)
        return d.hold_frames > 0 ? d.hold_state : 1;
    return -1;
}

bool AnimPreviewForceState(const std::string& node_id, const char* trigger) {
    return AnimPreviewForcedState(node_id, trigger) == 1;
}

void AnimPreviewSetTrigger(const std::string& node_id, const char* trigger, int state) {
    AnimPreviewDriver& d = GetAnimPreviewDriver();
    int bit = AnimPreviewTriggerBit(trigger);
    if (bit == 0) return;
    if (d.node_id != node_id) {
        d.node_id = node_id;
        d.override_mask = 0;
        d.trigger_mask = 0;
        d.transition_index = -1;
    }
    if (state < 0) {
        d.override_mask &= ~bit;
        d.trigger_mask &= ~bit;
        return;
    }
    d.override_mask |= bit;
    if (state > 0) d.trigger_mask |= bit;
    else d.trigger_mask &= ~bit;
}

void AnimPreviewClearTriggers() {
    AnimPreviewDriver& d = GetAnimPreviewDriver();
    d.override_mask = 0;
    d.trigger_mask = 0;
    d.transition_index = -1;
}

void AnimPreviewPlay(bool play) { GetAnimPreviewDriver().playing = play; }

void AnimPreviewStop() {
    AnimPreviewDriver& d = GetAnimPreviewDriver();
    d.playing = false;
    d.replay_requested = true;
    d.pending_jump = 0.0f;
    d.scrub_to = -1.0f;
}

void AnimPreviewReplay() {
    AnimPreviewDriver& d = GetAnimPreviewDriver();
    d.playing = true;
    d.replay_requested = true;
    d.pending_jump = 0.0f;
    d.scrub_to = -1.0f;
}

void AnimPreviewSeek(float seconds) {
    AnimPreviewDriver& d = GetAnimPreviewDriver();
    if (seconds < 0.0f) seconds = 0.0f;
    d.scrub_to = seconds;
}

void AnimPreviewResolveRequest(const Node& node) {
    AnimPreviewDriver& d = GetAnimPreviewDriver();
    if (d.transition_index < 0 || d.node_id != node.id) return;
    if (d.transition_index < (int)node.anim.transitions.size()) {
        const std::string& on = node.anim.transitions[(size_t)d.transition_index].on;
        int bit = AnimPreviewTriggerBit(on.c_str());
        d.override_mask |= bit;
        d.trigger_mask |= bit;
    }
    d.transition_index = -1;
}

}  // namespace imforge::builder
