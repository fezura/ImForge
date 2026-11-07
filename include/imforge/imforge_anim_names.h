#pragma once
// imb::Ease / imb::Preset names, shared by builder, interpreter and codegen so they can't drift
#include "imb_runtime.h"

#include <cstring>

namespace imforge {

const int kEaseCount = 12;
const int kPresetCount = 10;
const int kTransitionTriggerCount = 6;

inline const char* EaseName(imb::Ease e) {
    switch (e) {
    case imb::Ease::Linear: return "Linear";
    case imb::Ease::InQuad: return "InQuad";
    case imb::Ease::OutQuad: return "OutQuad";
    case imb::Ease::InOutQuad: return "InOutQuad";
    case imb::Ease::InCubic: return "InCubic";
    case imb::Ease::OutCubic: return "OutCubic";
    case imb::Ease::InOutCubic: return "InOutCubic";
    case imb::Ease::OutBack: return "OutBack";
    case imb::Ease::InBack: return "InBack";
    case imb::Ease::OutElastic: return "OutElastic";
    case imb::Ease::OutBounce: return "OutBounce";
    case imb::Ease::OutExpo: return "OutExpo";
    }
    return "OutCubic";
}

inline const char* PresetName(imb::Preset p) {
    switch (p) {
    case imb::Preset::None: return "None";
    case imb::Preset::Fade: return "Fade";
    case imb::Preset::SlideUp: return "SlideUp";
    case imb::Preset::SlideDown: return "SlideDown";
    case imb::Preset::SlideLeft: return "SlideLeft";
    case imb::Preset::SlideRight: return "SlideRight";
    case imb::Preset::FadeSlideUp: return "FadeSlideUp";
    case imb::Preset::FadeSlideDown: return "FadeSlideDown";
    case imb::Preset::ScalePop: return "ScalePop";
    case imb::Preset::Stagger: return "Stagger";
    }
    return "None";
}

// *out is left alone on failure
inline bool TryEaseFromName(const char* name, imb::Ease* out) {
    if (!name || !out) return false;
    for (int i = 0; i < kEaseCount; ++i) {
        imb::Ease e = static_cast<imb::Ease>(i);
        if (std::strcmp(EaseName(e), name) == 0) {
            *out = e;
            return true;
        }
    }
    return false;
}

inline bool TryPresetFromName(const char* name, imb::Preset* out) {
    if (!name || !out) return false;
    for (int i = 0; i < kPresetCount; ++i) {
        imb::Preset p = static_cast<imb::Preset>(i);
        if (std::strcmp(PresetName(p), name) == 0) {
            *out = p;
            return true;
        }
    }
    return false;
}

// unknown names fall back to OutCubic / None
inline imb::Ease EaseFromName(const char* name) {
    imb::Ease e = imb::Ease::OutCubic;
    TryEaseFromName(name, &e);
    return e;
}

inline imb::Preset PresetFromName(const char* name) {
    imb::Preset p = imb::Preset::None;
    TryPresetFromName(name, &p);
    return p;
}

// "Spring" is a valid ease in the schema but means imb::Spring, not a curve
inline bool IsSpringEaseName(const char* name) { return name && std::strcmp(name, "Spring") == 0; }

// schema TransitionTrigger, keep the index order stable
inline const char* TransitionTriggerName(int index) {
    switch (index) {
    case 0: return "hover";
    case 1: return "active";
    case 2: return "open";
    case 3: return "visible";
    case 4: return "focus";
    case 5: return "disabled";
    }
    return "hover";
}

inline int TransitionTriggerIndex(const char* name) {
    if (!name) return -1;
    for (int i = 0; i < kTransitionTriggerCount; ++i)
        if (std::strcmp(TransitionTriggerName(i), name) == 0) return i;
    return -1;
}

}  // namespace imforge
