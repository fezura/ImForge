#pragma once

namespace imforge {
namespace builder {

inline bool& FontsDirtyFlag() {
    static bool dirty = false;
    return dirty;
}

}  // namespace builder
}  // namespace imforge
