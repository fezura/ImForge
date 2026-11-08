#include "live_bridge.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>

#include "bundled_packs.h"
#include "editor_context.h"
#include "project_io.h"

#if __has_include("ui/client_snippet.h")
#include "ui/client_snippet.h"
#define IMFORGE_HAS_CLIENT_SNIPPET 1
#else
#define IMFORGE_HAS_CLIENT_SNIPPET 0
#endif

namespace imforge::builder {

namespace {

std::string Lower(std::string s) {
    for (char& ch : s) ch = (char)std::tolower((unsigned char)ch);
    return s;
}

std::string Capitalize(const std::string& s) {
    if (s.empty()) return s;
    std::string out = s;
    out[0] = (char)std::toupper((unsigned char)out[0]);
    return out;
}

bool IsHandlerEvent(const EventSpec& ev) {
    if (ev.handler) return true;
    for (const Action& a : ev.actions)
        if (a.type == "handler") return true;
    return false;
}

std::string ActivePanelName(const EditorContext& ctx) {
    return ctx.active_kind == DocKind::Panel ? ctx.active_panel : std::string();
}

void ParseLockedMarkers(const std::string& text, std::map<std::string, std::string>& out) {
    static const char kTag[] = "@imb:locked id=";
    size_t pos = 0;
    while ((pos = text.find(kTag, pos)) != std::string::npos) {
        pos += sizeof(kTag) - 1;
        size_t id_end = pos;
        while (id_end < text.size() && !std::isspace((unsigned char)text[id_end])) ++id_end;
        std::string id = text.substr(pos, id_end - pos);
        size_t p = id_end;
        while (p < text.size() && (text[p] == ' ' || text[p] == '\t')) ++p;
        if (text.compare(p, 2, "h=") == 0) {
            p += 2;
            size_t h_end = p;
            while (h_end < text.size() && std::isxdigit((unsigned char)text[h_end])) ++h_end;
            if (!id.empty()) out[id] = Lower(text.substr(p, h_end - p));
        }
        pos = id_end;
    }
}

}  // namespace

bool LiveRequirePairingPref(const Prefs& prefs) {
    const Json& v = prefs.extra["liveRequirePairing"];
    return !v.IsBool() || v.AsBool();
}

void SetLiveRequirePairingPref(Prefs& prefs, bool on) {
    if (!prefs.extra.IsObject()) prefs.extra = Json::Object();
    prefs.extra.Set("liveRequirePairing", Json(on));
}

std::string LivePairingStateText(const LivePairingInfo& p) {
    std::string peer = "'" + p.peer_app + "' pid " + std::to_string(p.peer_pid);
    if (!p.available) return "unavailable";
    if (!p.running) return "not listening";
    if (p.refusing) return "refusing connections after 5 failed pairing attempts";
    if (p.paired && !p.client_info) return "paired, waiting for the app's authenticated client_info";
    if (p.paired) return "paired automatically with " + peer;
    if (p.unauthenticated) return "UNAUTHENTICATED client " + peer;
    if (p.handshake) return "pairing with " + peer;
    if (p.transport_connected) return "app connected, waiting for its pairing hello";
    return "waiting for an app";
}

LiveBridge::LiveBridge() : alive_(std::make_shared<bool>(true)) {}

LiveBridge::~LiveBridge() { *alive_ = false; }

LiveBridge& GlobalLiveBridge() {
    static LiveBridge bridge;
    return bridge;
}

void LiveBridge::Attach(EditorContext& ctx) {
    if (attached_ == &ctx) return;
    attached_ = &ctx;
    std::weak_ptr<bool> alive = alive_;
    ctx.on_document_changed.push_back([this, alive](DocKind kind, const std::string& name) {
        std::shared_ptr<bool> a = alive.lock();
        if (a && *a) OnDocumentChanged(kind, name);
    });
    ctx.on_selection_changed.push_back([this, alive]() {
        std::shared_ptr<bool> a = alive.lock();
        if (a && *a) OnSelectionChanged();
    });
    ctx.on_project_changed.push_back([this, alive](const char*) {
        std::shared_ptr<bool> a = alive.lock();
        if (!a || !*a) return;
        exported_hashes_.clear();
        last_export_scan_ = 0;
        last_status_refresh_ = 0;
        pending_.clear();
        pending_everything_ = false;
    });
}

void LiveBridge::OnDocumentChanged(DocKind kind, const std::string& name) {
    if (kind == DocKind::None || !attached_) return;
    last_change_time_ = attached_->time;
    last_status_refresh_ = 0;
    if (kind == DocKind::Project) {
        pending_everything_ = true;
        return;
    }
    for (Pending& p : pending_) {
        if (p.kind == kind && p.name == name) {
            p.time = last_change_time_;
            return;
        }
    }
    Pending p;
    p.kind = kind;
    p.name = name;
    p.time = last_change_time_;
    pending_.push_back(std::move(p));
}

void LiveBridge::OnSelectionChanged() {
    if (attached_ && attached_->PickerMode()) selection_changed_in_picker_ = true;
}

void LiveBridge::Update(EditorContext& ctx) {
    Attach(ctx);
    ctx.live.SetRequirePairing(LiveRequirePairingPref(ctx.prefs));
    bool connected = ctx.live.IsConnected();
    if (connected && !was_connected_) {
        connected_since_ = ctx.time;
        last_status_refresh_ = 0;
    }
    if (!connected && was_connected_) {
        GetLiveStatus().fps_history.clear();
        last_status_refresh_ = 0;
    }
    was_connected_ = connected;

    FlushPending(ctx);
    TrackPickerSync(ctx);
    SampleFps(ctx);

    unsigned received = ctx.live.Stats().received;
    bool new_messages = received != last_received_;
    last_received_ = received;
    double since = ctx.time - last_status_refresh_;
    if (last_status_refresh_ == 0 || since >= kStatusRefreshSeconds || (new_messages && since >= 0.25)) RefreshStatus(ctx);

    LiveStatus& s = GetLiveStatus();
    s.connected = connected;
    s.listening = ctx.live.IsRunning();
    s.picker = ctx.PickerMode();
    s.auto_push = auto_push_;
    s.hover_panel = ctx.live.HoverPanel();
    s.hover_id = ctx.live.HoverId();
    s.fps = ctx.live.Client().fps;
    LivePairingInfo pi = ctx.live.Pairing();
    s.pairing_required = pi.required;
    s.paired = pi.paired;
    s.unauthenticated = pi.unauthenticated;
    s.pairing_in_progress = pi.transport_connected && !pi.paired && !pi.unauthenticated;
    s.pairing_failures = pi.total_failures;
    s.security_events = pi.security_events;
    s.last_security_event = pi.last_security_event;
    s.pairing_file = pi.file;
    s.pairing_state = LivePairingStateText(pi);
}

void LiveBridge::FlushPending(EditorContext& ctx) {
    if (pending_.empty() && !pending_everything_) return;
    if (ctx.time - last_change_time_ < kDebounceSeconds) return;
    if (!auto_push_ || !ctx.live.IsConnected() || !ctx.has_project) {
        pending_.clear();
        pending_everything_ = false;
        return;
    }
    size_t before = ctx.live.Stats().bytes_sent;
    if (pending_everything_) {
        ctx.live.PushAll(ctx.project);
    } else {
        bool components_sent = false;
        for (const Pending& p : pending_) {
            if (p.kind == DocKind::Panel) {
                if (Panel* panel = ctx.FindPanelDoc(p.name)) ctx.live.PushPanel(*panel);
            } else if (p.kind == DocKind::Component && !components_sent) {
                ctx.live.PushComponents(ctx.project);
                components_sent = true;
            }
        }
    }
    RecordPush(ctx, before);
    pending_.clear();
    pending_everything_ = false;
}

void LiveBridge::TrackPickerSync(EditorContext& ctx) {
    bool picker = ctx.PickerMode();
    if (!ctx.live.IsConnected()) {
        if (!picker) selection_changed_in_picker_ = false;
        return;
    }
    std::string panel = ActivePanelName(ctx);
    if (picker) {
        if (!ctx.live.PickerMode() || panel != last_mode_panel_) {
            ctx.live.SetPickerMode(true, ctx.SelectedId(), panel);
            last_mode_panel_ = panel;
        }
        return;
    }
    if (ctx.live.PickerMode()) {
        ctx.live.SetPickerMode(false, ctx.SelectedId(), panel);
        if (selection_changed_in_picker_ && !ctx.SelectedId().empty()) {
            pick_available_ = true;
            pick_panel_ = panel;
            pick_id_ = ctx.SelectedId();
            ctx.OpenPanel("Canvas", true);
            ctx.OpenPanel("Hierarchy", true);
            ctx.LogSource(LogLevel::Info, "live", "picked %s.%s in the app", panel.c_str(), pick_id_.c_str());
            ctx.SetStatus("Picked " + pick_id_ + " in " + ctx.live.Client().app);
        }
    }
    selection_changed_in_picker_ = false;
    last_mode_panel_.clear();
}

bool LiveBridge::ConsumePick(std::string& panel, std::string& id) {
    if (!pick_available_) return false;
    pick_available_ = false;
    panel = pick_panel_;
    id = pick_id_;
    return true;
}

void LiveBridge::SetPicker(EditorContext& ctx, bool on) {
    if (on == ctx.PickerMode()) return;
    ctx.SetPickerMode(on);
    last_mode_panel_ = ActivePanelName(ctx);
}

void LiveBridge::SampleFps(EditorContext& ctx) {
    if (!ctx.live.IsConnected()) return;
    if (ctx.time - last_fps_sample_ < kFpsSampleSeconds) return;
    last_fps_sample_ = ctx.time;
    std::vector<float>& h = GetLiveStatus().fps_history;
    h.push_back(ctx.live.Client().fps);
    if (h.size() > kFpsSamples) h.erase(h.begin(), h.begin() + (long)(h.size() - kFpsSamples));
}

void LiveBridge::RecordPush(EditorContext& ctx, size_t bytes_before) {
    LiveStatus& s = GetLiveStatus();
    size_t after = ctx.live.Stats().bytes_sent;
    s.last_push_time = ctx.time;
    s.last_push_bytes = after >= bytes_before ? after - bytes_before : 0;
}

bool LiveBridge::Start(EditorContext& ctx, std::string* err) {
    std::string e;
    if (!ctx.live.Start(ctx.project.live, ctx.prefs.live_bind_host, &e)) {
        last_error_ = e;
        ctx.LogSource(LogLevel::Warning, "live", "cannot start: %s", e.c_str());
        if (err) *err = e;
        last_status_refresh_ = 0;
        return false;
    }
    last_error_.clear();
    ctx.LogSource(LogLevel::Info, "live", "listening on %s", ctx.live.Url().c_str());
    last_status_refresh_ = 0;
    return true;
}

void LiveBridge::Stop(EditorContext& ctx) {
    if (!ctx.live.IsRunning()) return;
    ctx.live.Stop();
    ctx.LogSource(LogLevel::Info, "live", "stopped listening");
    GetLiveStatus().fps_history.clear();
    pending_.clear();
    pending_everything_ = false;
    last_status_refresh_ = 0;
}

bool LiveBridge::Restart(EditorContext& ctx, std::string* err) {
    Stop(ctx);
    return Start(ctx, err);
}

bool LiveBridge::NeedsRestart(const EditorContext& ctx) const {
    if (!ctx.live.IsRunning()) return false;
    return LiveServer::MakeUrl(ctx.project.live, ctx.prefs.live_bind_host) != ctx.live.Url();
}

bool LiveBridge::PushAll(EditorContext& ctx) {
    if (!ctx.has_project) {
        ctx.SetStatus("No project open");
        return false;
    }
    if (!ctx.live.IsRunning() && !Start(ctx, nullptr)) return false;
    if (!ctx.live.IsConnected()) {
        ctx.SetStatus("No live client connected");
        return false;
    }
    size_t before = ctx.live.Stats().bytes_sent;
    bool ok = ctx.live.PushAll(ctx.project);
    RecordPush(ctx, before);
    pending_.clear();
    pending_everything_ = false;
    ctx.SetStatus(ok ? "Pushed project, components, widgets, theme and all panels" : "Push failed (see log)");
    return ok;
}

bool LiveBridge::PushPanel(EditorContext& ctx, const std::string& name) {
    Panel* p = ctx.FindPanelDoc(name);
    if (!p || !ctx.live.IsConnected()) return false;
    size_t before = ctx.live.Stats().bytes_sent;
    bool ok = ctx.live.PushPanel(*p);
    RecordPush(ctx, before);
    ctx.SetStatus(ok ? "Pushed " + name : "Push failed (see log)");
    return ok;
}

void LiveBridge::ClearAppLog(EditorContext& ctx) { app_log_clear_time_ = ctx.time; }

void LiveBridge::ScanExportedHashes(const EditorContext& ctx) {
    if (!ctx.has_project) {
        exported_hashes_.clear();
        return;
    }
    if (ctx.time - last_export_scan_ < 2.0 && last_export_scan_ != 0) return;
    last_export_scan_ = ctx.time;
    std::string out_dir = JoinPath(ctx.project.dir, ctx.project.codegen.output_dir);
    std::map<std::string, std::map<std::string, std::string>> fresh;
    for (const Panel& p : ctx.project.panels) {
        const char* exts[2] = {".cpp", ".h"};
        for (const char* ext : exts) {
            std::string path = JoinPath(out_dir, p.name + ext);
            std::error_code ec;
            if (!std::filesystem::exists(path, ec)) continue;
            std::string text;
            if (!ReadTextFile(path, text)) continue;
            ParseLockedMarkers(text, fresh[p.name]);
        }
    }
    exported_hashes_ = std::move(fresh);
}

std::string LiveBridge::ExpectedHash(const EditorContext& ctx, const Panel& panel, const Node& node) const {
    if (hash_provider_) return Lower(hash_provider_(ctx.project, panel, node));
    auto it = exported_hashes_.find(panel.name);
    if (it == exported_hashes_.end()) return std::string();
    auto jt = it->second.find(node.id);
    return jt == it->second.end() ? std::string() : jt->second;
}

void LiveBridge::CollectBlocks(const EditorContext& ctx, const Panel& panel, const Node& node,
                               std::vector<LiveBlockStatus>& out) const {
    if (node.locked || node.type == "Code") {
        LiveBlockStatus b;
        b.panel = panel.name;
        b.node_id = node.id;
        b.key = LiveStatus::Key(panel.name, node.id);
        b.function_name = panel.name + "_" + node.id + "_Block";
        b.expected_hash = ExpectedHash(ctx, panel, node);
        const std::string* h = ctx.live.BlockHash(panel.name, node.id);
        b.registered = h != nullptr;
        b.app_hash = h ? Lower(*h) : std::string();
        b.stale = b.registered && !b.app_hash.empty() && !b.expected_hash.empty() && b.app_hash != b.expected_hash;
        out.push_back(std::move(b));
    }
    for (const auto& ev : node.events) {
        if (!IsHandlerEvent(ev.second)) continue;
        LiveBlockStatus b;
        b.panel = panel.name;
        b.node_id = node.id;
        b.event = ev.first;
        b.is_handler = true;
        b.key = LiveStatus::Key(panel.name, node.id) + "." + ev.first;
        b.function_name = panel.name + "_" + node.id + "_" + Capitalize(ev.first);
        const std::string* h = ctx.live.BlockHash(panel.name, node.id + "." + ev.first);
        b.registered = h != nullptr;
        b.app_hash = h ? Lower(*h) : std::string();
        out.push_back(std::move(b));
    }
    for (const Node& c : node.children) CollectBlocks(ctx, panel, c, out);
    for (const auto& slot : node.slots)
        for (const Node& c : slot.second) CollectBlocks(ctx, panel, c, out);
}

void LiveBridge::RefreshStatus(EditorContext& ctx) {
    last_status_refresh_ = ctx.time;
    LiveStatus& s = GetLiveStatus();
    const LiveServer& live = ctx.live;
    const LiveClientInfo& c = live.Client();
    s.transport_available = live.TransportAvailable();
    s.listening = live.IsRunning();
    s.connected = live.IsConnected();
    s.client_info_received = s.connected && (!c.app.empty() || c.pid != 0 || c.imgui_version != 0);
    s.picker = ctx.PickerMode();
    s.auto_push = auto_push_;
    s.url = live.Url();
    s.app = c.app;
    s.backend = c.backend;
    s.platform = c.platform;
    s.pid = c.pid;
    s.imgui_version = c.imgui_version;
    s.fps = c.fps;
    s.registered_widgets = c.widgets;
    s.texture_names = c.textures;
    s.app_panels = c.panels;
    s.hover_panel = live.HoverPanel();
    s.hover_id = live.HoverId();
    s.missing_panels.clear();
    for (const std::string& name : c.panels)
        if (!ctx.FindPanelDoc(name)) s.missing_panels.push_back(name);

    s.blocks.clear();
    s.stale_ids.clear();
    s.unregistered_ids.clear();
    s.stale_count = 0;
    s.unregistered_count = 0;
    if (!ctx.has_project) return;
    ScanExportedHashes(ctx);
    for (const Panel& p : ctx.project.panels) CollectBlocks(ctx, p, p.root, s.blocks);
    bool registry_known = s.connected && s.client_info_received;
    for (LiveBlockStatus& b : s.blocks) {
        if (!registry_known) {
            b.registered = false;
            b.stale = false;
            continue;
        }
        if (b.stale) {
            ++s.stale_count;
            s.stale_ids.insert(b.key);
            s.stale_ids.insert(LiveStatus::Key(b.panel, b.node_id));
        }
        if (!b.registered) {
            ++s.unregistered_count;
            s.unregistered_ids.insert(b.key);
            s.unregistered_ids.insert(LiveStatus::Key(b.panel, b.node_id));
        }
    }
}

std::string LiveBridge::ClientSnippet(const EditorContext& ctx) const {
#if IMFORGE_HAS_CLIENT_SNIPPET
    if (ctx.has_project) return MakeClientSnippet(ctx.project);
#endif
    std::string url = ctx.live.IsRunning() ? ctx.live.Url() : LiveServer::MakeUrl(ctx.project.live, ctx.prefs.live_bind_host);
    std::string out;
    out += "#define IMFORGE_LIVE                      // development builds only\n";
    out += "#include \"imforge_client.h\"            // or dist/imforge_client_single.h\n";
    out += "#include \"" + ctx.project.codegen.output_dir + "/imforge_reflect.h\"   // IMFORGE_REFLECT for bound types\n\n";
    out += "void InitLive() {                        // after ImGui::CreateContext() and backend init\n";
    out += "    imforge::live::Connect(\"" + url + "\");\n";
    out += "#include \"" + ctx.project.codegen.output_dir + "/imforge_dev.h\"   // registers handlers and locked blocks\n";
    out += "}\n\n";
    out += "void FrameLive() {                       // after ImGui::NewFrame()\n";
    out += "    imforge::live::NewFrame();\n";
    if (!ctx.has_project || ctx.project.panels.empty()) {
        out += "    imforge::live::Draw(\"MainMenu\", { imforge::live::Bind(\"player\", g_player) });\n";
    } else {
        for (const Panel& p : ctx.project.panels) {
            out += "    imforge::live::Draw(\"" + p.name + "\", {";
            for (size_t i = 0; i < p.params.size(); ++i) {
                out += i ? ", " : " ";
                out += "imforge::live::Bind(\"" + p.params[i].name + "\", g_" + p.params[i].name + ")";
            }
            out += p.params.empty() ? "});\n" : " });\n";
        }
    }
    out += "}\n";
    return out;
}

std::vector<LivePanelRow> LiveBridge::PanelRows(const EditorContext& ctx) const {
    std::vector<LivePanelRow> rows;
    auto find = [&rows](const std::string& name) -> LivePanelRow& {
        for (LivePanelRow& r : rows)
            if (r.name == name) return r;
        LivePanelRow r;
        r.name = name;
        rows.push_back(r);
        return rows.back();
    };
    for (const Panel& p : ctx.project.panels) find(p.name).in_project = true;
    for (const std::string& name : ctx.live.Client().panels) find(name).drawn_by_app = true;
    for (LivePanelRow& r : rows) {
        auto it = ctx.live.BlockHashes().find(r.name);
        r.registered_blocks = it == ctx.live.BlockHashes().end() ? 0 : (int)it->second.size();
    }
    return rows;
}

std::vector<LiveIssue> LiveBridge::Troubleshoot(const EditorContext& ctx) const {
    std::vector<LiveIssue> issues;
    auto add = [&issues](LiveIssue::Severity sev, const std::string& title, const std::string& detail) {
        LiveIssue i;
        i.severity = sev;
        i.title = title;
        i.detail = detail;
        issues.push_back(std::move(i));
    };
    const LiveServer& live = ctx.live;
    const LiveSettings& ls = ctx.project.live;
    const LiveStatus& s = GetLiveStatus();
    std::string url = live.IsRunning() ? live.Url() : LiveServer::MakeUrl(ls, ctx.prefs.live_bind_host);
    using Sev = LiveIssue::Severity;

    if (!live.TransportAvailable()) {
        add(Sev::Error, "Transport not compiled in",
            "imforge_transport.h / imforge_protocol.h were missing when the builder was built. Rebuild the builder.");
        return issues;
    }
    if (!live.IsRunning()) {
        if (!last_error_.empty()) {
            std::string hint = ls.transport == "tcp"
                                   ? "Port " + std::to_string(ls.tcp_port) + " is probably in use by another process (or a second builder). Pick another port or stop the other process."
                                   : "The endpoint name \"" + (ls.transport == "pipe" ? ls.pipe_name : ls.shm_name) + "\" is probably still owned by another builder instance.";
            add(Sev::Error, "Cannot listen on " + url, last_error_ + ". " + hint);
        } else {
            add(Sev::Info, "Not listening", "Press Start (or enable live in project settings) so the app can connect to " + url + ".");
        }
        return issues;
    }
    const LivePairingInfo pi = live.Pairing();
    if (!pi.required)
        add(Sev::Error, "Pairing is disabled",
            "Any local process can connect to " + url + " and drive the app link without proving it knows the one-time code. Turn 'Require pairing' back on in the Live panel.");
    if (pi.file.empty() && !pi.file_error.empty())
        add(Sev::Warning, "Pairing file could not be written",
            pi.file_error + ". Apps cannot pair automatically; as a fallback pass the code with IMFORGE_PAIRING_CODE or imforge::live::SetPairingCode().");
    if (pi.refusing) {
        add(Sev::Warning, "Refusing connections",
            "Five pairing attempts failed, so the code was burned and replaced. New connections are accepted again in a few seconds. Last failure: " + pi.last_failure);
        return issues;
    }
    if (pi.security_events > 0)
        add(Sev::Error, std::to_string(pi.security_events) + " message(s) failed authentication on a paired link",
            "Last: " + pi.last_security_event + ". After pairing every message carries an HMAC and a sequence number; a frame that was tampered with, replayed, reordered or injected by another process ends the link (nothing from it is applied) and the app re-pairs with the next code. Repeated events mean something is interfering with " + url + "; prefer a pipe:// transport.");
    if (pi.abandoned > 0 && pi.total_failures == 0 && !live.IsConnected())
        add(Sev::Info, std::to_string(pi.abandoned) + " abandoned handshake(s)",
            "A peer received the challenge and disconnected: usually an app holding an older code (it re-reads the pairing file and retries). These do not count as failed attempts.");
    if (pi.total_failures > 0)
        add(Sev::Warning, std::to_string(pi.total_failures) + " failed pairing attempt(s)",
            "Last failure: " + pi.last_failure + ". An app with a stale code retries with the current one automatically; repeated failures from an unknown process mean something else is probing the port.");
    if (!live.IsConnected() && pi.transport_connected) {
        add(Sev::Warning, "App connected but not paired yet",
            "The app reads the one-time code from " + (pi.file.empty() ? std::string("the pairing file") : pi.file) +
                ". It must run as the same user on this machine; otherwise pass the code with IMFORGE_PAIRING_CODE (Copy code button). State: " +
                LivePairingStateText(pi) + ".");
        return issues;
    }
    if (!live.IsConnected()) {
        add(Sev::Warning, "No client connected",
            "The app must call imforge::live::Connect(\"" + url + "\") with IMFORGE_LIVE defined before including imforge_client.h. The client retries every second, so start order does not matter.");
        if (ls.transport == "tcp") {
            add(Sev::Info, "Firewall",
                ctx.prefs.live_bind_host == "127.0.0.1" || ctx.prefs.live_bind_host.empty()
                    ? "Listening on loopback only; Windows Firewall does not prompt for 127.0.0.1. Set the bind host to 0.0.0.0 in preferences to accept a VM or another machine."
                    : "Listening on " + ctx.prefs.live_bind_host + ": allow imforge_builder through the firewall when Windows prompts, and connect the app to this machine's IP.");
        } else if (ls.transport == "pipe") {
            add(Sev::Info, "Pipe name must match",
                "Windows: \\\\.\\pipe\\" + ls.pipe_name + "  Linux: /tmp/imforge-" + ls.pipe_name + ".sock. The app URL is pipe://" + ls.pipe_name + ".");
        } else {
            add(Sev::Info, "Shared memory name must match",
                "Mapping ImForge." + ls.shm_name + "; the app URL is shm://" + ls.shm_name + ". Both processes must run in the same session.");
        }
        add(Sev::Info, "IMFORGE_LIVE not defined?",
            "Without the define every imforge::live call compiles to a no-op and nothing connects. Check the define is set in the app's dev configuration.");
        return issues;
    }
    if (!s.client_info_received) {
        if (ctx.time - connected_since_ > 2.0)
            add(Sev::Warning, "Paired but no client_info",
                "The app paired on " + url + " but did not send its authenticated client_info (name, pid, widgets). Make sure the app links the same imforge_client.h version as this builder.");
        else add(Sev::Info, "Waiting for client_info", "The app just paired; its client_info should arrive within a frame.");
        return issues;
    }
    int project_ver = ctx.project.ImGuiVersionNum();
    if (project_ver > 0 && s.imgui_version > 0 && s.imgui_version != project_ver)
        add(Sev::Warning, "ImGui version differs",
            "The app runs Dear ImGui " + Project::VersionString(s.imgui_version) + " while the project targets " +
                ctx.project.imgui_version + ". Flags and style vars missing in one of them are skipped.");
    if (!s.missing_panels.empty()) {
        std::string list;
        for (const std::string& p : s.missing_panels) list += (list.empty() ? "" : ", ") + p;
        add(Sev::Warning, "App draws panels that are not in the project",
            list + ". Each shows a placeholder until a panel with that name exists in the project.");
    }
    if (s.unregistered_count > 0)
        add(Sev::Warning, std::to_string(s.unregistered_count) + " block(s)/handler(s) not registered",
            "Include " + ctx.project.codegen.output_dir + "/imforge_dev.h after Connect(), or call imforge::live::RegisterBlock / RegisterHandler for them. Unregistered blocks render as placeholders.");
    if (s.stale_count > 0)
        add(Sev::Warning, std::to_string(s.stale_count) + " stale block(s)",
            "The app compiled an older version of these locked blocks. Export the code and rebuild the app.");
    if (ctx.project.widget_catalog) {
        std::string missing;
        std::set<std::string> used = UsedWidgetTypes(ctx.project);
        for (const CustomWidgetDef& w : ctx.project.widget_catalog->custom)
            if (used.count(w.type) && !s.HasWidget(w.type)) missing += (missing.empty() ? "" : ", ") + w.type;
        if (!missing.empty())
            add(Sev::Info, "Custom widgets without a live draw function", missing + ". Call imforge::live::RegisterWidget(type, fn) in the app, otherwise the preview shapes are used.");
    }
    if (issues.empty()) add(Sev::Info, "Everything looks good", "Connected to " + s.app + "; edits are pushed live.");
    return issues;
}

}  // namespace imforge::builder
