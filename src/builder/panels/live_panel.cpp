#include <cstdio>
#include <string>

#include "imgui.h"

#include "../builder_fonts.h"
#include "../builder_style.h"
#include "../editor_context.h"
#include "../live_bridge.h"
#include "../ui/ui_helpers.h"
#include "editor_panel.h"
#include "live_panel_views.h"
#include "live_status.h"

namespace imforge::builder {

namespace {

std::string FormatBytes(size_t bytes) {
    char buf[32];
    if (bytes < 1024) std::snprintf(buf, sizeof(buf), "%u B", (unsigned)bytes);
    else if (bytes < 1024 * 1024) std::snprintf(buf, sizeof(buf), "%.1f KB", (double)bytes / 1024.0);
    else std::snprintf(buf, sizeof(buf), "%.2f MB", (double)bytes / (1024.0 * 1024.0));
    return buf;
}

class LivePanel final : public EditorPanel {
public:
    const char* Name() const override { return "Live"; }
    const char* Icon() const override {
#ifdef ICON_FA_PLUG
        return ICON_FA_PLUG;
#else
        return nullptr;
#endif
    }

    void OnProjectChanged(EditorContext& ctx) override {
        editing_port_ = false;
        editing_name_ = false;
        bridge_.RefreshStatus(ctx);
    }

    void Draw(EditorContext& ctx) override {
        std::string pick_panel, pick_id;
        if (bridge_.ConsumePick(pick_panel, pick_id)) {
            last_pick_ = pick_panel.empty() ? pick_id : pick_panel + "." + pick_id;
            last_pick_time_ = ctx.time;
        }
        DrawStatusLine(ctx);
        DrawPairingRow(ctx);
        DrawTransportRow(ctx);
        DrawSyncRow(ctx);
        ImGui::Separator();
        DrawTabs(ctx);
    }

private:
    void DrawStatusLine(EditorContext& ctx) {
        const BuilderColors& c = Colors();
        const LiveServer& live = ctx.live;
        const LiveClientInfo& cl = live.Client();
        bool hello = live.IsConnected() && (!cl.app.empty() || cl.pid != 0 || cl.imgui_version != 0);
        ImVec4 dot = !live.TransportAvailable() ? c.error
                     : !live.IsRunning()        ? c.text_disabled
                     : live.IsConnected()       ? c.success
                                                : c.warning;
        ImGui::AlignTextToFramePadding();
        ui::StatusDot(dot);
        ImGui::SameLine();
        char buf[512];
        if (!live.TransportAvailable())
            std::snprintf(buf, sizeof(buf), "Transport unavailable (imforge_transport.h was missing when the builder was built)");
        else if (!live.IsRunning()) std::snprintf(buf, sizeof(buf), "Not listening");
        else if (!live.IsConnected() && live.TransportConnected())
            std::snprintf(buf, sizeof(buf), "App connected on %s, pairing", live.Url().c_str());
        else if (!live.IsConnected()) std::snprintf(buf, sizeof(buf), "Listening on %s", live.Url().c_str());
        else if (hello)
            std::snprintf(buf, sizeof(buf), "Client connected: %s pid %d, ImGui %s, %s/%s", cl.app.c_str(), cl.pid,
                          Project::VersionString(cl.imgui_version).c_str(), cl.backend.c_str(), cl.platform.c_str());
        else std::snprintf(buf, sizeof(buf), "Client paired on %s (waiting for its client_info)", live.Url().c_str());
        ImGui::PushFont(HeadingFont() ? HeadingFont() : ImGui::GetFont());
        ImGui::TextUnformatted(buf);
        ImGui::PopFont();
        ImGui::SameLine();
        if (!live.IsRunning()) {
            if (!live.TransportAvailable()) ImGui::BeginDisabled();
            if (ui::AccentButton("Start")) bridge_.Start(ctx, nullptr);
            if (!live.TransportAvailable()) ImGui::EndDisabled();
            ui::Tooltip("Listen for a live client with the transport settings below");
        } else {
            if (ImGui::Button("Stop")) bridge_.Stop(ctx);
            ui::Tooltip("Close the endpoint; the app keeps retrying every second");
        }
        if (bridge_.NeedsRestart(ctx)) {
            ImGui::SameLine();
            if (ImGui::Button("Restart to apply")) bridge_.Restart(ctx, nullptr);
            ui::Tooltip("The transport settings changed since the server started");
        }
        if (!live.IsRunning() && !bridge_.LastError().empty()) {
            ImGui::SameLine();
            ImGui::TextColored(c.error, "%s", bridge_.LastError().c_str());
        }
        if (!last_pick_.empty() && ctx.time - last_pick_time_ < 6.0) {
            ImGui::SameLine();
            ui::TextDim("picked %s", last_pick_.c_str());
        }
    }

    void DrawPairingRow(EditorContext& ctx) {
        const BuilderColors& c = Colors();
        const LivePairingInfo pi = ctx.live.Pairing();
        if (!pi.available) return;
        const bool require = LiveRequirePairingPref(ctx.prefs);
        ImGui::AlignTextToFramePadding();
        if (!require) ui::Badge("UNAUTHENTICATED MODE", c.error);
        else if (pi.paired) ui::Badge("PAIRED", c.success);
        else if (pi.refusing) ui::Badge("LOCKED OUT", c.error);
        else if (pi.handshake || pi.transport_connected) ui::Badge("PAIRING", c.warning);
        else ui::Badge(pi.running ? "ONE-TIME CODE READY" : "PAIRING IDLE", c.text_dim);
        ImGui::SameLine();
        if (pi.paired) ImGui::TextColored(c.success, "%s", LivePairingStateText(pi).c_str());
        else ImGui::TextUnformatted(LivePairingStateText(pi).c_str());
        if (pi.running && !pi.refusing) {
            int secs = (int)pi.expires_in;
            ImGui::SameLine();
            ui::TextDim("| code #%d, single use, rotates in %d:%02d", pi.generation, secs / 60, secs % 60);
        }
        if (pi.refusing) {
            ImGui::SameLine();
            ImGui::TextColored(c.error, "(%.1f s)", pi.refusing_for);
        }
        if (pi.total_failures > 0) {
            ImGui::SameLine();
            ImGui::TextColored(c.warning, "| %d failed attempt(s), %d left for this code", pi.total_failures, pi.attempts_left);
            ui::Tooltip(("Last failure: " + pi.last_failure).c_str());
        }
        if (pi.security_events > 0) {
            ImGui::SameLine();
            ImGui::TextColored(c.error, "| %d message(s) failed authentication", pi.security_events);
            ui::Tooltip(("Last security event: " + pi.last_security_event +
                         "\nThe link was dropped and nothing from the frame was applied; the app re-pairs automatically.")
                            .c_str());
        }

        ImGui::AlignTextToFramePadding();
        ui::TextDim("Pairing file");
        ImGui::SameLine();
        if (!pi.file.empty()) {
            ImFont* mono = MonoFont();
            if (mono) ImGui::PushFont(mono);
            ImGui::TextUnformatted(pi.file.c_str());
            if (mono) ImGui::PopFont();
            ui::Tooltip("Readable only by your user account. Apps started by you on this machine read it automatically before every connect; it is rewritten after each pairing and deleted when the server stops.");
            ImGui::SameLine();
            if (ImGui::SmallButton("Copy path")) ImGui::SetClipboardText(pi.file.c_str());
        } else {
            ImGui::TextColored(pi.file_error.empty() ? c.text_dim : c.error, "%s",
                               pi.file_error.empty() ? "(written when the server starts)" : pi.file_error.c_str());
        }
        ImGui::SameLine();
        if (!pi.running) ImGui::BeginDisabled();
        if (ImGui::SmallButton("Rotate now") && ctx.live.RotatePairingCode()) ctx.SetStatus("New one-time pairing code issued");
        ui::Tooltip("Issue a new one-time code now. The old code stops working immediately; a paired app stays connected.");
        ImGui::SameLine();
        if (ImGui::SmallButton("Copy code") && !pi.code.empty()) {
            ImGui::SetClipboardText(pi.code.c_str());
            ctx.SetStatus("One-time pairing code copied (advanced fallback)");
        }
        ui::Tooltip("Advanced fallback for apps that cannot read the pairing file (VM, other machine, sandbox, other user): pass the code with the IMFORGE_PAIRING_CODE environment variable or imforge::live::SetPairingCode(). It works once.");
        if (!pi.running) ImGui::EndDisabled();
        ImGui::SameLine();
        bool req = require;
        if (ImGui::Checkbox("Require pairing", &req)) {
            SetLiveRequirePairingPref(ctx.prefs, req);
            SavePrefs(ctx.prefs);
            ctx.live.SetRequirePairing(req);
            ctx.LogSource(req ? LogLevel::Info : LogLevel::Warning, "pairing", "%s",
                          req ? "pairing required for live clients" : "pairing DISABLED: unauthenticated live clients are accepted");
        }
        ui::Tooltip("Builder preference (not stored in the project). When off, apps that skip pairing are accepted too: any local process could drive the app link.");
        if (!require) {
            ImGui::SameLine();
            ImGui::TextColored(c.error, "any local process can connect and push to the app");
        }
    }

    void DrawTransportRow(EditorContext& ctx) {
        LiveSettings& ls = ctx.project.live;
        static const char* kNames[3] = {"tcp", "pipe", "shm"};
        int cur = ls.transport == "pipe" ? 1 : ls.transport == "shm" ? 2 : 0;
        float unit = ImGui::GetFontSize();
        ImGui::AlignTextToFramePadding();
        ui::TextDim("Transport");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(unit * 5.0f);
        if (ImGui::Combo("##live_transport", &cur, "tcp\0pipe\0shm\0")) {
            std::string chosen = kNames[cur];
            ctx.EditProject("Live transport", [&] { ls.transport = chosen; });
        }
        ImGui::SameLine();
        if (cur == 0) {
            ui::TextDim("Port");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(unit * 5.0f);
            if (!editing_port_) port_buf_ = ls.tcp_port;
            ImGui::InputInt("##live_port", &port_buf_, 0, 0);
            if (ImGui::IsItemActivated()) editing_port_ = true;
            if (ImGui::IsItemDeactivatedAfterEdit()) {
                editing_port_ = false;
                int v = port_buf_ < 1 ? 1 : port_buf_ > 65535 ? 65535 : port_buf_;
                if (v != ls.tcp_port) ctx.EditProject("Live port", [&] { ls.tcp_port = v; });
            } else if (ImGui::IsItemDeactivated()) {
                editing_port_ = false;
            }
        } else {
            std::string& target = cur == 1 ? ls.pipe_name : ls.shm_name;
            ui::TextDim("Name");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(unit * 9.0f);
            if (!editing_name_) name_buf_ = target;
            ui::InputTextStd("##live_name", name_buf_);
            if (ImGui::IsItemActivated()) editing_name_ = true;
            if (ImGui::IsItemDeactivatedAfterEdit()) {
                editing_name_ = false;
                if (!name_buf_.empty() && name_buf_ != target)
                    ctx.EditProject(cur == 1 ? "Live pipe name" : "Live shm name", [&] { target = name_buf_; });
            } else if (ImGui::IsItemDeactivated()) {
                editing_name_ = false;
            }
        }
        ImGui::SameLine();
        bool enabled = ls.enabled;
        if (ImGui::Checkbox("Auto-start", &enabled)) ctx.EditProject("Live auto-start", [&] { ls.enabled = enabled; });
        ui::Tooltip("Start listening when the project is opened");
        ImGui::SameLine();
        ui::TextDim("%s", LiveServer::MakeUrl(ls, ctx.prefs.live_bind_host).c_str());
        ImGui::SameLine();
        if (ImGui::Button("Copy client snippet")) {
            std::string snippet = bridge_.ClientSnippet(ctx);
            ImGui::SetClipboardText(snippet.c_str());
            ctx.SetStatus("Client snippet copied to the clipboard");
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal) && ImGui::BeginTooltip()) {
            ImFont* mono = MonoFont();
            if (mono) ImGui::PushFont(mono);
            ImGui::TextUnformatted(bridge_.ClientSnippet(ctx).c_str());
            if (mono) ImGui::PopFont();
            ImGui::EndTooltip();
        }
    }

    void DrawSyncRow(EditorContext& ctx) {
        const BuilderColors& c = Colors();
        const LiveStatus& s = GetLiveStatus();
        const LiveStats& st = ctx.live.Stats();
        bool connected = ctx.live.IsConnected();
        if (!connected) ImGui::BeginDisabled();
        if (ui::AccentButton("Push all")) bridge_.PushAll(ctx);
        ui::Tooltip("Send the project, components, widget manifests, theme and every panel (Shift+F5)");
        ImGui::SameLine();
        bool has_active = ctx.active_kind == DocKind::Panel && !ctx.active_panel.empty();
        if (!has_active) ImGui::BeginDisabled();
        if (ImGui::Button("Push panel")) bridge_.PushPanel(ctx, ctx.active_panel);
        if (!has_active) ImGui::EndDisabled();
        ui::Tooltip("Send only the active panel (F5)");
        if (!connected) ImGui::EndDisabled();
        ImGui::SameLine();
        bool auto_push = bridge_.AutoPush();
        if (ui::ToggleButton("##live_autopush", &auto_push)) bridge_.SetAutoPush(auto_push);
        ImGui::SameLine(0, 4);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Auto-push");
        ui::Tooltip("Push changed panels 150 ms after the last edit; project or theme changes push everything");
        ImGui::SameLine();
        bool picker = ctx.PickerMode();
        if (picker) {
            ImGui::PushStyleColor(ImGuiCol_Button, c.accent);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, c.accent_hover);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, c.accent_active);
        }
        if (!connected && !picker) ImGui::BeginDisabled();
        if (ImGui::Button(picker ? "Picking: click a widget in the app" : "Pick in app")) bridge_.SetPicker(ctx, !picker);
        if (!connected && !picker) ImGui::EndDisabled();
        if (picker) ImGui::PopStyleColor(3);
        ui::Tooltip("Ctrl+Shift+P. Click a widget inside the running app to select it here");
        if (!s.hover_id.empty()) {
            ImGui::SameLine();
            ui::TextDim("hover %s.%s", s.hover_panel.c_str(), s.hover_id.c_str());
        }
        ImGui::SameLine();
        ImGui::TextUnformatted("|");
        ImGui::SameLine();
        if (s.last_push_time > 0)
            ui::TextDim("last push %.1f s ago (%s)", ctx.time - s.last_push_time, FormatBytes(s.last_push_bytes).c_str());
        else ui::TextDim("no push yet");
        ImGui::SameLine();
        ui::TextDim("sent %u msg / %s, received %u msg / %s", st.sent, FormatBytes(st.bytes_sent).c_str(), st.received,
                    FormatBytes(st.bytes_received).c_str());
        if (connected) {
            float plot_w = ImGui::GetFontSize() * 8.0f;
            float avail = ImGui::GetContentRegionAvail().x;
            float plot_h = ImGui::GetFrameHeight();
            if (avail > plot_w + 20.0f) {
                ImGui::SameLine(ImGui::GetCursorPosX() + avail - plot_w);
                char overlay[32];
                std::snprintf(overlay, sizeof(overlay), "%.0f fps", s.fps);
                const std::vector<float>& h = s.fps_history;
                ImGui::PlotLines("##live_fps_mini", h.empty() ? nullptr : h.data(), (int)h.size(), 0, overlay, 0.0f,
                                 FLT_MAX, ImVec2(plot_w, plot_h));
            }
        }
    }

    void DrawTabs(EditorContext& ctx) {
        const LiveStatus& s = GetLiveStatus();
        if (!ImGui::BeginTabBar("##live_tabs", ImGuiTabBarFlags_NoCloseWithMiddleMouseButton)) return;
        char label[96];
        unsigned problems = s.stale_count + s.unregistered_count;
        if (problems > 0) std::snprintf(label, sizeof(label), "Blocks & handlers (%d, %u issues)###live_blocks", (int)s.blocks.size(), problems);
        else std::snprintf(label, sizeof(label), "Blocks & handlers (%d)###live_blocks", (int)s.blocks.size());
        if (ImGui::BeginTabItem(label)) {
            DrawLiveBlocksTab(ctx, bridge_, views_);
            ImGui::EndTabItem();
        }
        int counts[4] = {0, 0, 0, 0};
        int log_total = CountAppLogEntries(ctx, bridge_.AppLogClearTime(), counts);
        std::snprintf(label, sizeof(label), "App log (%d)###live_log", log_total);
        if (ImGui::BeginTabItem(label)) {
            DrawLiveLogTab(ctx, bridge_, views_);
            ImGui::EndTabItem();
        }
        if (!s.missing_panels.empty())
            std::snprintf(label, sizeof(label), "Panels (%d missing)###live_panels", (int)s.missing_panels.size());
        else std::snprintf(label, sizeof(label), "Panels (%d)###live_panels", (int)ctx.project.panels.size());
        if (ImGui::BeginTabItem(label)) {
            DrawLivePanelsTab(ctx, bridge_, views_);
            ImGui::EndTabItem();
        }
        int warnings = 0;
        for (const LiveIssue& i : bridge_.Troubleshoot(ctx))
            if (i.severity != LiveIssue::Severity::Info) ++warnings;
        if (warnings > 0) std::snprintf(label, sizeof(label), "Troubleshooting (%d)###live_trouble", warnings);
        else std::snprintf(label, sizeof(label), "Troubleshooting###live_trouble");
        if (ImGui::BeginTabItem(label)) {
            DrawLiveTroubleshootTab(ctx, bridge_, views_);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Stats###live_stats")) {
            DrawLiveStatsTab(ctx, bridge_, views_);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    LiveBridge& bridge_ = GlobalLiveBridge();
    LiveViewState views_;
    bool editing_port_ = false;
    bool editing_name_ = false;
    int port_buf_ = 7777;
    std::string name_buf_;
    std::string last_pick_;
    double last_pick_time_ = 0;
};

}  // namespace

IMFORGE_REGISTER_PANEL(LivePanel, 800);

}  // namespace imforge::builder
