#pragma once

#include "imgui.h"

namespace imforge::builder {

struct EditorContext;
class LiveBridge;

struct LiveViewState {
    ImGuiTextFilter block_filter;
    ImGuiTextFilter log_filter;
    bool show_level[4] = {true, true, true, true};
    bool log_auto_scroll = true;
    bool only_problems = false;
    unsigned last_log_generation = 0;
};

int CountAppLogEntries(const EditorContext& ctx, double since, int level_counts[4]);
void DrawLiveBlocksTab(EditorContext& ctx, LiveBridge& bridge, LiveViewState& st);
void DrawLiveLogTab(EditorContext& ctx, LiveBridge& bridge, LiveViewState& st);
void DrawLivePanelsTab(EditorContext& ctx, LiveBridge& bridge, LiveViewState& st);
void DrawLiveTroubleshootTab(EditorContext& ctx, LiveBridge& bridge, LiveViewState& st);
void DrawLiveStatsTab(EditorContext& ctx, LiveBridge& bridge, LiveViewState& st);

}  // namespace imforge::builder
