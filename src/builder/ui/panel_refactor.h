#pragma once

#include <string>
#include <vector>

#include "imforge/imforge_core.h"

namespace imforge::builder {

// A binding root (`player` in `player.health`) or, with state=true, a state var (`open` in
// `state.open`). Matching goes through the expression lexer, so `myplayer.x`, `player2.y`,
// `cfg.player`, enum literals and string literals are left alone.
struct RenameTarget {
    bool state = false;
    std::string from, to;
};

struct PathRef {
    std::string node_id, where, root, member;
};

std::string RenameInExpr(const std::string& src, const RenameTarget& target, int* count = nullptr);
std::string RenameInInterpolation(const std::string& src, const RenameTarget& target, int* count = nullptr);

// Renames the param/state entry and every use in the tree: bindings, visibleIf/enabledIf,
// {"expr":...} values in props/fx/anim/style/actions, interpolated strings and tooltips, set/toggle
// targets, set/copyClipboard values and component instance props. Pass `project` to get exact prop
// types from the widget catalog and component defs. Returns how many references changed.
int RenameRootInPanel(Panel& panel, const std::string& old_name, const std::string& new_name,
                      const Project* project = nullptr);
int RenameStateVarInPanel(Panel& panel, const std::string& old_name, const std::string& new_name,
                          const Project* project = nullptr);

std::vector<PathRef> CollectPathRefs(const Panel& panel, const Project* project = nullptr);

std::string ApplyNameCase(const std::string& ident, const std::string& style);
std::string PanelSignaturePreview(const Panel& panel, const CodegenSettings& settings);

}  // namespace imforge::builder
