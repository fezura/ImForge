#pragma once

#include <string>
#include <vector>

#include "imgui.h"

#include "../editor_context.h"

namespace imforge::builder::tree_actions {

constexpr const char* kPayloadNode = "IMFORGE_NODE";
constexpr const char* kPayloadNew = "IMFORGE_NEW";
constexpr const char* kComponentPayloadPrefix = "Component:";

bool ParseNewPayload(const ImGuiPayload* payload, std::string& type_out, std::string& component_out);
std::string NewPayloadFor(const std::string& type, const std::string& component);

std::vector<std::string> DraggedIds(EditorContext& ctx, const std::string& dragged_id);
bool CanDropNodes(EditorContext& ctx, const std::vector<std::string>& ids, const std::string& parent_id,
                  const std::string& anchor_id);
bool CanDropNew(EditorContext& ctx, const std::string& type, const std::string& parent_id);

Node* AddNode(EditorContext& ctx, const std::string& type, const std::string& component,
              const std::string& parent_id, int index, const char* undo_label = "Add");
Node* AddAtSelection(EditorContext& ctx, const std::string& type, const std::string& component);
bool DropNodes(EditorContext& ctx, const std::vector<std::string>& ids, const std::string& parent_id,
               const std::string& anchor_id, bool after);
bool DropNew(EditorContext& ctx, const std::string& type, const std::string& component,
             const std::string& parent_id, const std::string& anchor_id, bool after);

bool WrapSelection(EditorContext& ctx, const std::string& container_type);
bool UnwrapNode(EditorContext& ctx, const std::string& id);
bool SetLocked(EditorContext& ctx, const std::vector<std::string>& ids, bool locked);
bool MoveSelection(EditorContext& ctx, int delta);
bool IsLockedOrInherited(const Node& root, const std::string& id);

bool ConvertToComponent(EditorContext& ctx, const std::vector<std::string>& ids, const std::string& name,
                        std::string* err);
std::string SuggestComponentName(EditorContext& ctx, const std::vector<std::string>& ids);

void DrawAddMenu(EditorContext& ctx, const std::string& parent_id, int index);

const char* WidgetIcon(EditorContext& ctx, const Node& node);
const char* Icon(const char* name, const char* fallback);

}  // namespace imforge::builder::tree_actions
