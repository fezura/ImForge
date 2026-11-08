#pragma once

#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "imforge/imforge_core.h"
#include "live_fonts.h"

#if __has_include("imforge/imforge_transport.h") && __has_include("imforge/imforge_protocol.h")
#include "imforge/imforge_protocol.h"
#include "imforge/imforge_transport.h"
#include "imforge/imforge_pairing_session.h"
#define IMFORGE_HAS_TRANSPORT 1
#else
#define IMFORGE_HAS_TRANSPORT 0
#endif

#ifdef SetProp
#undef SetProp
#endif
#ifdef RemoveProp
#undef RemoveProp
#endif
#ifdef GetProp
#undef GetProp
#endif

namespace imforge::builder {

struct EditorContext;

struct LiveClientInfo {
    bool connected = false;
    std::string app, backend, platform;
    int pid = 0;
    int imgui_version = 0;
    std::vector<std::string> widgets, textures, panels;
    float fps = 0;
    double connected_at = 0, last_message_at = 0;
    bool paired = false;
};

// pairing snapshot for the Live panel, status bar and automation
struct LivePairingInfo {
    bool available = false;
    bool required = true;
    bool running = false;
    bool transport_connected = false;
    bool handshake = false;
    bool paired = false;
    bool unauthenticated = false;
    bool refusing = false;
    double refusing_for = 0;
    double expires_in = 0;
    std::string code, file, file_error, peer_app, last_failure, last_security_event;
    int peer_pid = 0;
    int failures = 0, total_failures = 0, attempts_left = 0, pairings = 0, generation = 0;
    int security_events = 0, abandoned = 0;
    bool client_info = false;
};

struct LiveStats {
    unsigned sent = 0, received = 0;
    size_t bytes_sent = 0, bytes_received = 0;
};

// resolves and reads the project's font files (set by the app, see LiveFontCache)
using LiveFontPlanner = std::function<LiveFontPlan(const Project&)>;

class LiveServer {
public:
    LiveServer();
    ~LiveServer();

    static std::string MakeUrl(const LiveSettings& s, const std::string& bind_host);

    bool Start(const LiveSettings& settings, const std::string& bind_host, std::string* err);
    void Stop();
    bool IsRunning() const { return running_; }
    bool IsConnected() const;
    const std::string& Url() const { return url_; }
    const char* StatusText() const;
    bool TransportConnected() const;

    void SetRequirePairing(bool on);
    bool RequirePairing() const { return require_pairing_; }
    bool RotatePairingCode();
    LivePairingInfo Pairing() const;

    void Poll(EditorContext& ctx);

    bool PushPanel(const Panel& panel);
    bool PushProject(const Project& project);
    bool PushComponents(const Project& project);
    bool PushWidgets(const Project& project);
    bool PushTheme(const Theme& theme);
    // font files the app doesn't hold yet (chunked), then the font list; empty projects send nothing
    bool PushFonts(const Project& project);
    void SetFontPlanner(LiveFontPlanner fn) { font_planner_ = std::move(fn); }
    bool PushAll(const Project& project);
    bool RemovePanel(const std::string& name);
    bool SetPickerMode(bool picker, const std::string& selected, const std::string& panel);
    bool Ping();

    const LiveClientInfo& Client() const { return client_; }
    const LiveStats& Stats() const { return stats_; }
    bool PickerMode() const { return picker_; }
    const std::string& HoverPanel() const { return hover_panel_; }
    const std::string& HoverId() const { return hover_id_; }
    const std::string* BlockHash(const std::string& panel, const std::string& node_id) const;
    const std::map<std::string, std::map<std::string, std::string>>& BlockHashes() const { return block_hashes_; }
    bool TransportAvailable() const { return IMFORGE_HAS_TRANSPORT != 0; }

private:
    bool Send(const Json& msg);
    bool SendRaw(const Json& msg);
    bool SendBytes(const std::string& bytes);
    void Dispatch(EditorContext& ctx, const Json& msg);
    void OnDisconnected(EditorContext& ctx);
    void DropConnection(EditorContext& ctx, uint64_t now);
    void Relisten(EditorContext& ctx, uint64_t now);
    void DrainPairingLog(EditorContext& ctx);

#if IMFORGE_HAS_TRANSPORT
    std::unique_ptr<Transport> transport_;
    pairing::Server pair_;
#endif
    LiveFontPlanner font_planner_;
    std::set<std::string> fonts_held_;
    bool fonts_announced_ = false;
    bool icons_sent_ = false;
    std::vector<std::string> font_warnings_, logged_font_warnings_;
    bool require_pairing_ = true;
    uint64_t drop_at_ = 0;
    uint64_t relisten_at_ = 0;
    bool running_ = false;
    bool was_connected_ = false;
    bool picker_ = false;
    std::string url_;
    std::string last_error_;
    LiveClientInfo client_;
    LiveStats stats_;
    std::string hover_panel_, hover_id_;
    std::map<std::string, std::map<std::string, std::string>> block_hashes_;
    double last_ping_ = 0;
};

}  // namespace imforge::builder
