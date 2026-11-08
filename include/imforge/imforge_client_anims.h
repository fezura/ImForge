#pragma once

// custom animations pushed by the builder, part of imforge_client_detail.h

#include "imforge_client_detail.h"

namespace imforge {
namespace live {
namespace client_detail {

// valid entries are registered and kept in project.animations (by name) so panels can find them
inline void ApplyAnimationList(Client& c, const std::vector<Json>& list) {
    std::vector<Json>& stored = c.project.animations;
    size_t full = 0;
    int registered = 0;
    for (const Json& a : list) {
        const std::string& name = a["name"].AsString();
        Json* slot = nullptr;
        for (Json& e : stored)
            if (e["name"].IsString() && e["name"].AsString() == name) slot = &e;
        if (!slot && stored.size() >= kProtocolMaxAnimations) {
            ++full;
            continue;
        }
        std::vector<std::string> errors;
        if (RegisterCustomAnimations(std::vector<Json>(1, a), &errors) != 1) {
            c.last_error = errors.empty() ? "bad animation" : errors[0];
            Log(c, 2, "imforge: bad animation: " + ClipForLog(c.last_error));
            continue;
        }
        if (slot) *slot = a;
        else stored.push_back(a);
        ++registered;
    }
    if (full) Log(c, 1, "imforge: " + std::to_string(full) + " animations ignored, the app already holds 2048");
    Log(c, 0, "imforge: " + std::to_string(registered) + " custom animations registered");
}

inline void ApplyAnimations(Client& c, const Json& m) {
    std::vector<Json> list;
    size_t dropped = 0;
    if (!ParseAnimations(m, list, &dropped)) {
        c.last_error = "animations message refused";
        Log(c, 2, "imforge: animations message refused (not a list, or more than 2048 entries)");
        return;
    }
    if (dropped)
        Log(c, 1, "imforge: " + std::to_string(dropped) + " animations dropped (no name, or more than 64 keyframes in a track)");
    ApplyAnimationList(c, list);
}

}  // namespace client_detail
}  // namespace live
}  // namespace imforge
