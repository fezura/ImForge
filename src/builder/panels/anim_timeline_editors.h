#pragma once

#include <string>
#include <vector>

#include "imgui.h"

#include "../editor_context.h"

namespace imforge::builder::anim_timeline {

enum class RowKind { Transition, Enter, Exit, Child };

struct Row {
    RowKind kind = RowKind::Transition;
    int index = -1;
    float start = 0.0f;
    float duration = 0.0f;
    bool spring = false;
    std::string label, badge;
};

const int kMaxChildRows = 12;

bool IsSpring(const Transition& tr);
float TransitionLength(const Transition& tr);
void BuildRows(const Node& node, std::vector<Row>& out);
float NodeTimelineLength(const Node& node);
bool HasAnimOrFx(const Node& node);
ImVec4 TriggerColor(const std::string& on);
std::string FxSummary(const FxSpec& fx);

// selected_transition is -1 while the enter/exit presets are being edited
void DrawCurveEditor(EditorContext& ctx, Node& node, int& selected_transition);
void DrawPresetEditor(EditorContext& ctx, Node& node);
void DrawQuickAdd(EditorContext& ctx, Node& node);
void DrawFxPreview(EditorContext& ctx, Node& node);

}  // namespace imforge::builder::anim_timeline
