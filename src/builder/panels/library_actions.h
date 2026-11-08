#pragma once

#include <string>
#include <vector>

#include "imgui.h"

namespace imforge::builder {

struct EditorContext;

// drag payload: a preset name, or "custom:<Name>" for a project animation
constexpr const char* kPayloadMotion = "IMFORGE_MOTION";
constexpr const char* kCustomMotionPrefix = "custom:";

std::string MotionPayloadFor(const std::string& name, bool custom);
std::string DefaultMotionTrigger(EditorContext& ctx, const std::string& payload);

// one undo step; nodes that already have the same motion on the same trigger are skipped
int AddMotionToNodes(EditorContext& ctx, const std::vector<std::string>& ids, const std::string& payload,
                     const std::string& trigger);

// Call between BeginDragDropTarget() and EndDragDropTarget(). Returns true while a motion payload is
// being dragged (the caller can skip its own handling), adds it to node_id on delivery.
bool AcceptMotionDrop(EditorContext& ctx, const std::string& node_id);

// same, for a custom target over [min, max] (the canvas viewport)
bool AcceptMotionDropRect(EditorContext& ctx, const std::string& node_id, const ImVec2& min, const ImVec2& max);

// "<tab> [<verb> <arg>]": opens the Library on a tab (animations, designs, widgets, templates; empty keeps the
// current one) and optionally runs a tab command such as "designs select NeonCyber" or "animations add hover"
bool ShowLibraryTab(EditorContext& ctx, const std::string& tab);

// scrolls the visible card grid to y pixels from the top on the next frame (automation)
void ScrollLibrary(float y);

}  // namespace imforge::builder
