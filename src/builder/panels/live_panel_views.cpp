#include "live_panel_views.h"

#include <cfloat>
#include <cstdio>
#include <string>
#include <vector>

#include "../builder_fonts.h"
#include "../builder_style.h"
#include "../editor_context.h"
#include "../live_bridge.h"
#include "../ui/ui_helpers.h"
#include "live_status.h"

namespace imforge::builder {

namespace {

const char* kLevelNames[4] = {"Debug", "Info", "Warn", "Error"};

bool IsAppEntry(const LogEntry& e, double since) { return e.source == "app" && e.time > since; }

std::string ShortHash(const std::string& h) { return h.size() > 8 ? h.substr(0, 8) : h; }

void DrawRegistrationLine(const LiveBlockStatus& b) {
    std::string line = b.is_handler
                           ? "imforge::live::RegisterHandler(\"" + b.key + "\", &" + b.function_name + ");"
                           : "imforge::live::RegisterBlock(\"" + b.key + "\", &" + b.function_name + ");";
    if (ImGui::MenuItem("Copy function name")) ImGui::SetClipboardText(b.function_name.c_str());
    if (ImGui::MenuItem("Copy registration call")) ImGui::SetClipboardText(line.c_str());
}

void StatusBadge(const LiveBlockStatus& b, bool registry_known) {
    const BuilderColors& c = Colors();
    if (!registry_known) ui::Badge("offline", c.text_dim);
    else if (!b.registered) ui::Badge("not registered", c.warning);
    else if (b.stale) ui::Badge("stale: recompile", c.error);
    else if (b.is_handler || b.app_hash.empty() || b.expected_hash.empty()) ui::Badge("registered", c.success);
    else ui::Badge("up to date", c.success);
}

}  // namespace

int CountAppLogEntries(const EditorContext& ctx, double since, int level_counts[4]) {
    int total = 0;
    for (size_t i = 0; i < ctx.log.Size(); ++i) {
        const LogEntry& e = ctx.log.At(i);
        if (!IsAppEntry(e, since)) continue;
        ++total;
        ++level_counts[(int)e.level];
    }
    return total;
}

void DrawLiveBlocksTab(EditorContext& ctx, LiveBridge& bridge, LiveViewState& st) {
    const BuilderColors& c = Colors();
    const LiveStatus& s = GetLiveStatus();
    bool registry_known = s.connected && s.client_info_received;
    if (ImGui::Button("Refresh")) bridge.RefreshStatus(ctx);
    ui::Tooltip("Rebuild the table from the project and the app's last blocks message");
    ImGui::SameLine();
    ImGui::Checkbox("Only problems", &st.only_problems);
    ImGui::SameLine();
    if (s.stale_count > 0) {
        ui::Badge(("stale " + std::to_string(s.stale_count)).c_str(), c.error);
        ImGui::SameLine();
    }
    if (s.unregistered_count > 0) {
        ui::Badge(("not registered " + std::to_string(s.unregistered_count)).c_str(), c.warning);
        ImGui::SameLine();
    }
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::InputTextWithHint("##live_block_filter", "Filter blocks", st.block_filter.InputBuf, IM_ARRAYSIZE(st.block_filter.InputBuf)))
        st.block_filter.Build();
    if (s.blocks.empty()) {
        ImGui::Spacing();
        ui::TextDim(ctx.has_project ? "No locked blocks, Code nodes or handler-enabled events in the project."
                                    : "Open a project to see its blocks and handlers.");
        return;
    }
    ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY |
                            ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_Hideable;
    if (!ImGui::BeginTable("##live_blocks_table", 8, flags, ImVec2(0, 0))) return;
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Kind", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 4.5f);
    ImGui::TableSetupColumn("Panel", ImGuiTableColumnFlags_WidthStretch, 1.0f);
    ImGui::TableSetupColumn("Node", ImGuiTableColumnFlags_WidthStretch, 1.0f);
    ImGui::TableSetupColumn("Event", ImGuiTableColumnFlags_WidthStretch, 0.8f);
    ImGui::TableSetupColumn("Function", ImGuiTableColumnFlags_WidthStretch, 2.0f);
    ImGui::TableSetupColumn("Registered", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 5.0f);
    ImGui::TableSetupColumn("Hash", ImGuiTableColumnFlags_WidthStretch, 1.6f);
    ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 9.0f);
    ImGui::TableHeadersRow();
    ImFont* mono = MonoFont();
    for (size_t i = 0; i < s.blocks.size(); ++i) {
        const LiveBlockStatus& b = s.blocks[i];
        bool problem = registry_known && (!b.registered || b.stale);
        if (st.only_problems && !problem) continue;
        if (st.block_filter.IsActive() && !st.block_filter.PassFilter(b.key.c_str()) &&
            !st.block_filter.PassFilter(b.function_name.c_str()))
            continue;
        ImGui::PushID((int)i);
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        bool selected = ctx.active_kind == DocKind::Panel && ctx.active_panel == b.panel && ctx.IsSelected(b.node_id);
        if (ImGui::Selectable(b.is_handler ? "handler" : "block", selected,
                              ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap)) {
            if (ctx.active_kind != DocKind::Panel || ctx.active_panel != b.panel) ctx.SetActivePanel(b.panel);
            ctx.Select(b.node_id);
            ctx.OpenPanel("Hierarchy", false);
        }
        if (ImGui::BeginPopupContextItem("##live_block_ctx")) {
            DrawRegistrationLine(b);
            ImGui::EndPopup();
        }
        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted(b.panel.c_str());
        ImGui::TableSetColumnIndex(2);
        ImGui::TextUnformatted(b.node_id.c_str());
        ImGui::TableSetColumnIndex(3);
        if (b.event.empty()) ui::TextDim("-");
        else ImGui::TextUnformatted(b.event.c_str());
        ImGui::TableSetColumnIndex(4);
        if (mono) ImGui::PushFont(mono);
        ImGui::TextUnformatted(b.function_name.c_str());
        if (mono) ImGui::PopFont();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s\nRight-click to copy", b.key.c_str());
        ImGui::TableSetColumnIndex(5);
        if (!registry_known) ui::TextDim("?");
        else ImGui::TextColored(b.registered ? c.success : c.warning, "%s", b.registered ? "yes" : "no");
        ImGui::TableSetColumnIndex(6);
        if (b.is_handler) ui::TextDim("n/a");
        else if (b.app_hash.empty() && b.expected_hash.empty()) ui::TextDim("unknown");
        else if (b.stale) ImGui::TextColored(c.error, "app %s / builder %s", ShortHash(b.app_hash).c_str(), ShortHash(b.expected_hash).c_str());
        else if (!b.app_hash.empty()) ImGui::TextUnformatted(ShortHash(b.app_hash).c_str());
        else ui::TextDim("builder %s", ShortHash(b.expected_hash).c_str());
        if (ImGui::IsItemHovered() && !b.is_handler)
            ImGui::SetTooltip("app: %s\nbuilder (last export): %s", b.app_hash.empty() ? "-" : b.app_hash.c_str(),
                              b.expected_hash.empty() ? "- (export the code to compare)" : b.expected_hash.c_str());
        ImGui::TableSetColumnIndex(7);
        StatusBadge(b, registry_known);
        ImGui::PopID();
    }
    ImGui::EndTable();
}

void DrawLiveLogTab(EditorContext& ctx, LiveBridge& bridge, LiveViewState& st) {
    const BuilderColors& c = Colors();
    int counts[4] = {0, 0, 0, 0};
    CountAppLogEntries(ctx, bridge.AppLogClearTime(), counts);
    if (ImGui::Button("Clear")) bridge.ClearAppLog(ctx);
    ImGui::SameLine();
    ImGui::Checkbox("Auto-scroll", &st.log_auto_scroll);
    ImGui::SameLine();
    ImGui::TextUnformatted("|");
    for (int i = 0; i < 4; ++i) {
        ImGui::SameLine();
        ImVec4 col = ui::LevelColor(i);
        ImGui::PushStyleColor(ImGuiCol_Text, st.show_level[i] ? col : c.text_disabled);
        ImGui::PushStyleColor(ImGuiCol_Button, st.show_level[i] ? WithAlpha(col, 0.15f) : ImVec4(0, 0, 0, 0));
        char label[64];
        std::snprintf(label, sizeof(label), "%s %d###live_lvl%d", kLevelNames[i], counts[i], i);
        if (ImGui::SmallButton(label)) st.show_level[i] = !st.show_level[i];
        ImGui::PopStyleColor(2);
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::InputTextWithHint("##live_log_filter", "Filter app log", st.log_filter.InputBuf, IM_ARRAYSIZE(st.log_filter.InputBuf)))
        st.log_filter.Build();
    if (!ImGui::BeginChild("##live_log_scroll", ImVec2(0, 0), ImGuiChildFlags_None,
                           ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_AlwaysVerticalScrollbar)) {
        ImGui::EndChild();
        return;
    }
    std::vector<int> visible;
    for (size_t i = 0; i < ctx.log.Size(); ++i) {
        const LogEntry& e = ctx.log.At(i);
        if (!IsAppEntry(e, bridge.AppLogClearTime())) continue;
        if (!st.show_level[(int)e.level]) continue;
        if (st.log_filter.IsActive() && !st.log_filter.PassFilter(e.message.c_str())) continue;
        visible.push_back((int)i);
    }
    if (visible.empty()) {
        ui::TextDim(ctx.live.IsConnected() ? "The app has not logged anything yet (imforge::live log messages appear here)."
                                           : "Connect an app to see its log messages.");
    }
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 2));
    ImGuiListClipper clipper;
    clipper.Begin((int)visible.size());
    while (clipper.Step()) {
        for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
            const LogEntry& e = ctx.log.At((size_t)visible[(size_t)row]);
            char ts[32];
            int total_ms = (int)(e.time * 1000.0);
            std::snprintf(ts, sizeof(ts), "%02d:%02d.%03d", (total_ms / 60000) % 100, (total_ms / 1000) % 60, total_ms % 1000);
            ImGui::PushStyleColor(ImGuiCol_Text, c.text_disabled);
            ImGui::TextUnformatted(ts);
            ImGui::PopStyleColor();
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Text, ui::LevelColor((int)e.level));
            ImGui::TextUnformatted(ui::LevelIcon((int)e.level));
            ImGui::PopStyleColor();
            ImGui::SameLine();
            ImVec4 col = e.level == LogLevel::Error     ? c.error
                         : e.level == LogLevel::Warning ? c.warning
                         : e.level == LogLevel::Debug   ? c.text_dim
                                                        : c.text;
            ImGui::PushStyleColor(ImGuiCol_Text, col);
            ImGui::TextUnformatted(e.message.c_str());
            ImGui::PopStyleColor();
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) ImGui::SetClipboardText(e.message.c_str());
        }
    }
    ImGui::PopStyleVar();
    if (st.log_auto_scroll && (ctx.log.Generation() != st.last_log_generation || ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f))
        ImGui::SetScrollHereY(1.0f);
    if (ImGui::GetIO().MouseWheel != 0.0f && ImGui::IsWindowHovered() && ImGui::GetScrollY() < ImGui::GetScrollMaxY() - 1.0f)
        st.log_auto_scroll = false;
    st.last_log_generation = ctx.log.Generation();
    ImGui::EndChild();
}

void DrawLivePanelsTab(EditorContext& ctx, LiveBridge& bridge, LiveViewState&) {
    const BuilderColors& c = Colors();
    std::vector<LivePanelRow> rows = bridge.PanelRows(ctx);
    bool connected = ctx.live.IsConnected();
    if (rows.empty()) {
        ImGui::Spacing();
        ui::TextDim("No panels in the project and the app has not reported any.");
        return;
    }
    ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY |
                            ImGuiTableFlags_SizingStretchProp;
    if (!ImGui::BeginTable("##live_panels_table", 5, flags)) return;
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Panel", ImGuiTableColumnFlags_WidthStretch, 1.5f);
    ImGui::TableSetupColumn("In project", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 5.5f);
    ImGui::TableSetupColumn("Drawn by app", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 6.5f);
    ImGui::TableSetupColumn("Registered blocks", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 8.0f);
    ImGui::TableSetupColumn("Note", ImGuiTableColumnFlags_WidthStretch, 3.0f);
    ImGui::TableHeadersRow();
    for (size_t i = 0; i < rows.size(); ++i) {
        const LivePanelRow& r = rows[i];
        ImGui::PushID((int)i);
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        bool active = ctx.active_kind == DocKind::Panel && ctx.active_panel == r.name;
        if (ImGui::Selectable(r.name.c_str(), active, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap) &&
            r.in_project)
            ctx.SetActivePanel(r.name);
        if (ImGui::BeginPopupContextItem("##live_panel_ctx")) {
            if (ImGui::MenuItem("Copy name")) ImGui::SetClipboardText(r.name.c_str());
            if (r.in_project && ImGui::MenuItem("Push this panel", nullptr, false, connected)) bridge.PushPanel(ctx, r.name);
            ImGui::EndPopup();
        }
        ImGui::TableSetColumnIndex(1);
        ImGui::TextColored(r.in_project ? c.success : c.error, "%s", r.in_project ? "yes" : "no");
        ImGui::TableSetColumnIndex(2);
        if (!connected) ui::TextDim("?");
        else ImGui::TextColored(r.drawn_by_app ? c.success : c.text_dim, "%s", r.drawn_by_app ? "yes" : "no");
        ImGui::TableSetColumnIndex(3);
        if (connected) ImGui::Text("%d", r.registered_blocks);
        else ui::TextDim("-");
        ImGui::TableSetColumnIndex(4);
        if (!r.in_project) {
            ui::Badge("not in project", c.warning);
            ImGui::SameLine();
            ImGui::TextWrapped("The app calls imforge::live::Draw(\"%s\") but no panel with that name exists; it renders a placeholder.", r.name.c_str());
        } else if (connected && !r.drawn_by_app) {
            ui::TextDim("The app never draws this panel (add imforge::live::Draw(\"%s\", ...) to see it live).", r.name.c_str());
        } else if (connected) {
            ui::TextDim("live");
        } else {
            ui::TextDim("-");
        }
        ImGui::PopID();
    }
    ImGui::EndTable();
}

void DrawLiveTroubleshootTab(EditorContext& ctx, LiveBridge& bridge, LiveViewState&) {
    const BuilderColors& c = Colors();
    const LiveStatus& s = GetLiveStatus();
    std::vector<LiveIssue> issues = bridge.Troubleshoot(ctx);
    if (!ImGui::BeginChild("##live_trouble_scroll", ImVec2(0, 0), ImGuiChildFlags_None)) {
        ImGui::EndChild();
        return;
    }
    float wrap = ImGui::GetContentRegionAvail().x;
    for (size_t i = 0; i < issues.size(); ++i) {
        const LiveIssue& is = issues[i];
        int level = is.severity == LiveIssue::Severity::Error ? 3 : is.severity == LiveIssue::Severity::Warning ? 2 : 1;
        ImVec4 col = ui::LevelColor(level);
        ImGui::PushID((int)i);
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddRectFilled(p, ImVec2(p.x + 3.0f, p.y + ImGui::GetTextLineHeightWithSpacing() * 2.2f), ToU32(col));
        ImGui::Indent(10.0f);
        ImGui::PushStyleColor(ImGuiCol_Text, col);
        ImGui::Text("%s %s", ui::LevelIcon(level), is.title.c_str());
        ImGui::PopStyleColor();
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + wrap - 14.0f);
        ui::TextDim("%s", is.detail.c_str());
        ImGui::PopTextWrapPos();
        ImGui::Unindent(10.0f);
        ImGui::Spacing();
        ImGui::PopID();
    }
    ImGui::Separator();
    ui::TextDim("Detected state");
    if (ui::BeginPropertyTable("##live_state_table", ImGui::GetFontSize() * 9.0f)) {
        auto row = [](const char* label, const std::string& value) {
            ui::PropertyLabel(label);
            ImGui::TextUnformatted(value.c_str());
        };
        row("Endpoint", ctx.live.IsRunning() ? ctx.live.Url() : LiveServer::MakeUrl(ctx.project.live, ctx.prefs.live_bind_host));
        row("Bind host", ctx.prefs.live_bind_host.empty() ? "127.0.0.1" : ctx.prefs.live_bind_host);
        row("Server", ctx.live.StatusText());
        row("Client", s.connected ? (s.client_info_received ? s.app + " (pid " + std::to_string(s.pid) + ")" : "paired, no client_info yet") : "none");
        row("Link integrity", s.security_events > 0 ? std::to_string(s.security_events) + " frame(s) failed authentication; last: " + s.last_security_event
                                                    : "every message MAC'ed and sequence-checked, no violations");
        row("App ImGui", s.imgui_version > 0 ? Project::VersionString(s.imgui_version) : "-");
        row("Project ImGui", ctx.project.imgui_version + " (" + ctx.project.imgui_branch + ")");
        row("Picker", ctx.PickerMode() ? "on" : "off");
        row("Auto-push", bridge.AutoPush() ? "on" : "off");
        row("Export dir", ctx.has_project ? ctx.project.dir + "/" + ctx.project.codegen.output_dir : "-");
        ui::EndPropertyTable();
    }
    if (!ctx.live.TransportAvailable()) ImGui::TextColored(c.error, "Transport support is compiled out of this builder.");
    ImGui::EndChild();
}

void DrawLiveStatsTab(EditorContext& ctx, LiveBridge&, LiveViewState&) {
    const LiveStatus& s = GetLiveStatus();
    const LiveStats& st = ctx.live.Stats();
    const LiveClientInfo& cl = ctx.live.Client();
    if (!ImGui::BeginChild("##live_stats_scroll", ImVec2(0, 0), ImGuiChildFlags_None)) {
        ImGui::EndChild();
        return;
    }
    const std::vector<float>& h = s.fps_history;
    float lo = FLT_MAX, hi = 0, sum = 0;
    for (float v : h) {
        lo = v < lo ? v : lo;
        hi = v > hi ? v : hi;
        sum += v;
    }
    char overlay[64];
    if (h.empty()) std::snprintf(overlay, sizeof(overlay), "no stats yet");
    else std::snprintf(overlay, sizeof(overlay), "%.0f fps (avg %.0f, min %.0f, max %.0f)", s.fps, sum / (float)h.size(), lo, hi);
    ImGui::PlotLines("##live_fps", h.empty() ? nullptr : h.data(), (int)h.size(), 0, overlay, 0.0f,
                     hi > 0 ? hi * 1.2f : 120.0f, ImVec2(-FLT_MIN, ImGui::GetFontSize() * 5.0f));
    ui::TextDim("App frame rate, one sample every 0.5 s over the last 60 s");
    ImGui::Spacing();
    if (ui::BeginPropertyTable("##live_stats_table", ImGui::GetFontSize() * 9.0f)) {
        auto row = [](const char* label, const std::string& value) {
            ui::PropertyLabel(label);
            ImGui::TextUnformatted(value.c_str());
        };
        char buf[128];
        std::snprintf(buf, sizeof(buf), "%u messages, %u bytes", st.sent, (unsigned)st.bytes_sent);
        row("Sent", buf);
        std::snprintf(buf, sizeof(buf), "%u messages, %u bytes", st.received, (unsigned)st.bytes_received);
        row("Received", buf);
        if (s.last_push_time > 0) {
            std::snprintf(buf, sizeof(buf), "%.1f s ago, %u bytes", ctx.time - s.last_push_time, (unsigned)s.last_push_bytes);
            row("Last push", buf);
        } else {
            row("Last push", "-");
        }
        if (s.connected) {
            std::snprintf(buf, sizeof(buf), "%.0f s", ctx.time - cl.connected_at);
            row("Connected for", buf);
            std::snprintf(buf, sizeof(buf), "%.1f s ago", ctx.time - cl.last_message_at);
            row("Last message", buf);
        }
        std::string widgets;
        for (const std::string& w : s.registered_widgets) widgets += (widgets.empty() ? "" : ", ") + w;
        row("App widgets", widgets.empty() ? "-" : widgets);
        std::string textures;
        for (const std::string& t : s.texture_names) textures += (textures.empty() ? "" : ", ") + t;
        row("App textures", textures.empty() ? "-" : textures);
        std::string panels;
        for (const std::string& p : s.app_panels) panels += (panels.empty() ? "" : ", ") + p;
        row("App panels", panels.empty() ? "-" : panels);
        ui::EndPropertyTable();
    }
    ImGui::EndChild();
}

}  // namespace imforge::builder
