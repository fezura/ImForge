#pragma once

#include <string>

namespace imforge::builder {

// Properties ("Preview" buttons) writes this, Canvas and AnimTimeline read it. `generation` bumps on
// every request, readers compare it against the last one they handled.
struct AnimPreviewState {
    enum class Kind { None, Enter, Exit, Transition };
    Kind kind = Kind::None;
    std::string panel;
    std::string node_id;
    int transition_index = -1;
    double requested_time = 0;
    unsigned generation = 0;
    bool loop = false;

    bool Pending(unsigned last_seen) const { return kind != Kind::None && generation != last_seen; }
};

inline AnimPreviewState& GetAnimPreviewState() {
    static AnimPreviewState state;
    return state;
}

inline void RequestAnimPreview(const std::string& panel, const std::string& node_id, AnimPreviewState::Kind kind,
                               int transition_index, double now) {
    AnimPreviewState& s = GetAnimPreviewState();
    s.kind = kind;
    s.panel = panel;
    s.node_id = node_id;
    s.transition_index = transition_index;
    s.requested_time = now;
    ++s.generation;
}

inline void ClearAnimPreview() {
    AnimPreviewState& s = GetAnimPreviewState();
    s.kind = AnimPreviewState::Kind::None;
    s.transition_index = -1;
    s.node_id.clear();
}

}  // namespace imforge::builder
