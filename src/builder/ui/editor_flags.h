#pragma once

#include <set>
#include <string>

namespace imforge::builder {

inline std::set<std::string>& HiddenInEditor() {
    static std::set<std::string> hidden_in_editor;
    return hidden_in_editor;
}

inline bool IsHiddenInEditor(const std::string& id) { return HiddenInEditor().count(id) != 0; }

inline void SetHiddenInEditor(const std::string& id, bool hidden) {
    if (hidden) HiddenInEditor().insert(id);
    else HiddenInEditor().erase(id);
}

inline void ToggleHiddenInEditor(const std::string& id) { SetHiddenInEditor(id, !IsHiddenInEditor(id)); }

}  // namespace imforge::builder
