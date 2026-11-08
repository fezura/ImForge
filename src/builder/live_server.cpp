#include "live_server.h"

#include <cstdio>

#include "editor_context.h"

namespace imforge::builder {

LiveServer::LiveServer() {}
LiveServer::~LiveServer() { Stop(); }

std::string LiveServer::MakeUrl(const LiveSettings& s, const std::string& bind_host) {
    if (s.transport == "pipe") return "pipe://" + s.pipe_name;
    if (s.transport == "shm") return "shm://" + s.shm_name;
    char buf[256];
    std::snprintf(buf, sizeof(buf), "tcp://%s:%d", bind_host.empty() ? "127.0.0.1" : bind_host.c_str(), s.tcp_port);
    return buf;
}

bool LiveServer::Start(const LiveSettings& settings, const std::string& bind_host, std::string* err) {
    Stop();
    url_ = MakeUrl(settings, bind_host);
#if IMFORGE_HAS_TRANSPORT
    const bool explicit_remote = settings.transport != "pipe" && settings.transport != "shm" && !bind_host.empty() &&
                                 bind_host != "127.0.0.1" && bind_host != "localhost" && bind_host != "::1" &&
                                 bind_host != "[::1]";
    SetAllowRemoteListen(explicit_remote);
    transport_ = CreateTransport(url_.c_str());
    if (!transport_) {
        last_error_ = "unsupported transport url: " + url_;
        if (err) *err = last_error_;
        return false;
    }
    if (!transport_->Listen(url_.c_str())) {
        last_error_ = "cannot listen on " + url_;
        if (err) *err = last_error_;
        transport_.reset();
        return false;
    }
    running_ = true;
    was_connected_ = false;
    drop_at_ = 0;
    relisten_at_ = 0;
    client_ = LiveClientInfo();
    stats_ = LiveStats();
    pair_.endpoint = url_;
    pair_.require = require_pairing_;
    pair_.Start(imforge::detail::NowMs());
    return true;
#else
    last_error_ = "live transport not available in this build (imforge_transport.h missing)";
    if (err) *err = last_error_;
    return false;
#endif
}

void LiveServer::Stop() {
#if IMFORGE_HAS_TRANSPORT
    if (transport_) {
        transport_->Close();
        transport_.reset();
    }
    pair_.Stop();
#endif
    running_ = false;
    was_connected_ = false;
    drop_at_ = 0;
    relisten_at_ = 0;
    client_ = LiveClientInfo();
    block_hashes_.clear();
    hover_panel_.clear();
    hover_id_.clear();
    fonts_held_.clear();
    fonts_announced_ = false;
    icons_sent_ = false;
}

bool LiveServer::TransportConnected() const {
#if IMFORGE_HAS_TRANSPORT
    return running_ && transport_ && transport_->IsConnected();
#else
    return false;
#endif
}

bool LiveServer::IsConnected() const {
#if IMFORGE_HAS_TRANSPORT
    return TransportConnected() && pair_.Established();
#else
    return false;
#endif
}

const char* LiveServer::StatusText() const {
    if (!TransportAvailable()) return "unavailable";
    if (!running_) return "stopped";
#if IMFORGE_HAS_TRANSPORT
    if (!transport_) return "refusing connections";
    if (TransportConnected() && !pair_.Established()) return "pairing";
#endif
    return IsConnected() ? "connected" : "listening";
}

void LiveServer::SetRequirePairing(bool on) {
    if (require_pairing_ == on) return;
    require_pairing_ = on;
#if IMFORGE_HAS_TRANSPORT
    pair_.require = on;
    if (on && pair_.GetState() == pairing::Server::State::Unauthenticated) drop_at_ = 1;
#endif
}

bool LiveServer::RotatePairingCode() {
#if IMFORGE_HAS_TRANSPORT
    if (!running_ || !pair_.Running()) return false;
    pair_.Rotate(imforge::detail::NowMs(), "rotated from the Live panel");
    return true;
#else
    return false;
#endif
}

LivePairingInfo LiveServer::Pairing() const {
    LivePairingInfo p;
    p.required = require_pairing_;
#if IMFORGE_HAS_TRANSPORT
    using St = pairing::Server::State;
    uint64_t now = imforge::detail::NowMs();
    p.available = true;
    p.running = running_ && pair_.Running();
    p.transport_connected = TransportConnected();
    p.handshake = pair_.GetState() == St::Challenged;
    p.paired = pair_.GetState() == St::Paired && p.transport_connected;
    p.unauthenticated = pair_.GetState() == St::Unauthenticated && p.transport_connected;
    p.refusing = running_ && !transport_;
    p.refusing_for = p.refusing && pair_.RefuseUntil() > now ? (double)(pair_.RefuseUntil() - now) / 1000.0 : 0.0;
    p.expires_in = pair_.ExpiresAt() > now ? (double)(pair_.ExpiresAt() - now) / 1000.0 : 0.0;
    p.code = pair_.Code();
    p.file = pair_.FileWritten() ? pair_.FilePath() : std::string();
    p.file_error = pair_.FileError();
    p.peer_app = pair_.PeerApp();
    p.peer_pid = pair_.PeerPid();
    p.client_info = pair_.ClientInfoReceived();
    p.last_failure = pair_.LastFailure();
    p.security_events = pair_.SecurityEvents();
    p.last_security_event = pair_.LastSecurityEvent();
    p.abandoned = pair_.AbandonedHandshakes();
    p.failures = pair_.Failures();
    p.total_failures = pair_.TotalFailures();
    p.attempts_left = pair_.AttemptsLeft();
    p.pairings = pair_.Pairings();
    p.generation = pair_.Generation();
#endif
    return p;
}

bool LiveServer::SendBytes(const std::string& bytes) {
#if IMFORGE_HAS_TRANSPORT
    if (!TransportConnected()) return false;
    if (!transport_->Send(bytes.data(), bytes.size())) return false;
    ++stats_.sent;
    stats_.bytes_sent += bytes.size();
    return true;
#else
    (void)bytes;
    return false;
#endif
}

// only handshake replies (pair_challenge, pair_ok, pair_fail) go out unsealed
bool LiveServer::SendRaw(const Json& msg) { return SendBytes(Frame(msg)); }

// everything else is sealed with the builder-to-app key and the next sequence number (pairing::Channel)
bool LiveServer::Send(const Json& msg) {
#if IMFORGE_HAS_TRANSPORT
    std::string payload;
    return IsConnected() && pair_.Encode(msg, payload) && SendBytes(payload);
#else
    (void)msg;
    return false;
#endif
}

#if IMFORGE_HAS_TRANSPORT

bool LiveServer::PushPanel(const Panel& panel) { return Send(MsgPanel(PanelToJson(panel))); }

bool LiveServer::PushProject(const Project& project) {
    Json m = MsgProject(ProjectToJson(project));
    Json themes = Json::Array();
    for (const Theme& t : project.themes) themes.Push(ThemeToJson(t));
    m.Set("themes", std::move(themes));
    Json comps = Json::Array();
    for (const Component& c : project.components) comps.Push(ComponentToJson(c));
    m.Set("components", std::move(comps));
    Json panels = Json::Array();
    for (const Panel& p : project.panels) panels.Push(PanelToJson(p));
    m.Set("panels", std::move(panels));
    if (project.widget_catalog && !project.widget_catalog->custom.empty()) {
        Json manifests = Json::Array();
        for (const CustomWidgetDef& w : project.widget_catalog->custom) manifests.Push(w.manifest);
        m.Set("widgets", std::move(manifests));
    }
    if (!Send(m)) return false;
    // older clients log unknown messages, so only send this when the project has custom animations
    if (project.animations.empty()) return true;
    return Send(MsgAnimations(project.animations));
}

bool LiveServer::PushComponents(const Project& project) {
    std::vector<Json> comps;
    for (const Component& c : project.components) comps.push_back(ComponentToJson(c));
    return Send(MsgComponents(comps));
}

bool LiveServer::PushWidgets(const Project& project) {
    std::vector<Json> manifests;
    if (project.widget_catalog)
        for (const CustomWidgetDef& w : project.widget_catalog->custom) manifests.push_back(w.manifest);
    if (manifests.empty()) return true;
    return Send(MsgWidgets(manifests));
}

bool LiveServer::PushTheme(const Theme& theme) { return Send(MsgTheme(ThemeToJson(theme))); }

// only the project folder and the builder's fonts folder are read; the app checks the bytes again
bool LiveServer::PushFonts(const Project& project) {
    if (!font_planner_ || (project.fonts.empty() && !fonts_announced_)) return true;
    if (!IsConnected()) return false;
    LiveFontPlan plan = font_planner_(project);
    font_warnings_ = plan.warnings;
    for (const LiveFontFile* f : plan.files) {
        if (fonts_held_.count(f->hash)) continue;
        for (const Json& m : FontDataMessages(*f))
            if (!Send(m)) return false;
        fonts_held_.insert(f->hash);
    }
    if (!plan.icons.empty() && !icons_sent_) {
        if (!Send(MsgIcons(plan.icons))) return false;
        icons_sent_ = true;
    }
    if (plan.specs.empty() && !fonts_announced_) return true;
    fonts_announced_ = !plan.specs.empty();
    return Send(MsgFonts(plan.specs));
}

bool LiveServer::RemovePanel(const std::string& name) { return Send(MsgRemovePanel(name.c_str())); }

bool LiveServer::SetPickerMode(bool picker, const std::string& selected, const std::string& panel) {
    picker_ = picker;
    return Send(MsgMode(picker, selected.c_str(), panel.c_str()));
}

bool LiveServer::Ping() { return Send(MsgPing()); }

#else

bool LiveServer::PushPanel(const Panel&) { return false; }
bool LiveServer::PushProject(const Project&) { return false; }
bool LiveServer::PushComponents(const Project&) { return false; }
bool LiveServer::PushWidgets(const Project&) { return false; }
bool LiveServer::PushTheme(const Theme&) { return false; }
bool LiveServer::PushFonts(const Project&) { return false; }
bool LiveServer::RemovePanel(const std::string&) { return false; }
bool LiveServer::SetPickerMode(bool picker, const std::string&, const std::string&) {
    picker_ = picker;
    return false;
}
bool LiveServer::Ping() { return false; }

#endif

bool LiveServer::PushAll(const Project& project) {
    bool ok = PushFonts(project);
    ok = PushProject(project) && ok;
    for (const Theme& t : project.themes)
        if (t.name == project.active_theme) ok = PushTheme(t) && ok;
    return ok;
}

const std::string* LiveServer::BlockHash(const std::string& panel, const std::string& node_id) const {
    auto it = block_hashes_.find(panel);
    if (it == block_hashes_.end()) return nullptr;
    auto jt = it->second.find(node_id);
    return jt == it->second.end() ? nullptr : &jt->second;
}

void LiveServer::OnDisconnected(EditorContext& ctx) {
    ctx.LogSource(LogLevel::Info, "live", "client disconnected (%s)", client_.app.empty() ? "unknown" : client_.app.c_str());
    client_ = LiveClientInfo();
    block_hashes_.clear();
    hover_panel_.clear();
    hover_id_.clear();
    fonts_held_.clear();
    fonts_announced_ = false;
    icons_sent_ = false;
}

void LiveServer::DrainPairingLog(EditorContext& ctx) {
#if IMFORGE_HAS_TRANSPORT
    for (const pairing::LogLine& l : pair_.TakeLog()) {
        LogLevel lvl = l.level <= 0 ? LogLevel::Debug : l.level == 1 ? LogLevel::Info : l.level == 2 ? LogLevel::Warning : LogLevel::Error;
        ctx.LogSource(lvl, l.security ? "security" : "pairing", "%s", l.text.c_str());
        if (l.security) ctx.SetStatus("Live link dropped: a message failed authentication (see Log)");
    }
#else
    (void)ctx;
#endif
}

// The transports serve a single peer and can't kick it while listening, so we close and listen again
// (after the refusal window if the code was just burned).
void LiveServer::DropConnection(EditorContext& ctx, uint64_t now) {
#if IMFORGE_HAS_TRANSPORT
    drop_at_ = 0;
    if (was_connected_) {
        bool established = pair_.Established() || client_.connected;
        pair_.OnDisconnect(now);
        if (established) OnDisconnected(ctx);
    }
    was_connected_ = false;
    if (transport_) {
        transport_->Close();
        transport_.reset();
    }
    relisten_at_ = now;
    if (!pair_.Refusing(now)) Relisten(ctx, now);
#else
    (void)ctx;
    (void)now;
#endif
}

void LiveServer::Relisten(EditorContext& ctx, uint64_t now) {
#if IMFORGE_HAS_TRANSPORT
    transport_ = CreateTransport(url_.c_str());
    if (!transport_ || !transport_->Listen(url_.c_str())) {
        transport_.reset();
        relisten_at_ = now + 1000;
        ctx.LogSource(LogLevel::Warning, "live", "cannot listen on %s again; retrying", url_.c_str());
    }
#else
    (void)ctx;
    (void)now;
#endif
}

void LiveServer::Poll(EditorContext& ctx) {
#if IMFORGE_HAS_TRANSPORT
    if (!running_) {
        DrainPairingLog(ctx);
        return;
    }
    uint64_t now = imforge::detail::NowMs();
    if (!transport_) {
        if (now >= pair_.RefuseUntil() && now >= relisten_at_) Relisten(ctx, now);
        pair_.Tick(now);
        DrainPairingLog(ctx);
        if (!transport_) return;
    }
    transport_->Poll();
    bool connected = transport_->IsConnected();
    if (connected && !was_connected_) {
        pair_.OnConnect(now);
        client_ = LiveClientInfo();
        client_.connected_at = ctx.time;
        ctx.LogSource(LogLevel::Info, "live", "client connected on %s; waiting for pairing", url_.c_str());
    } else if (!connected && was_connected_) {
        bool established = pair_.Established() || client_.connected;
        pair_.OnDisconnect(now);
        if (established) OnDisconnected(ctx);
        else ctx.LogSource(LogLevel::Info, "live", "unpaired client disconnected");
    }
    was_connected_ = connected;
    if (connected) {
        std::vector<unsigned char> bytes;
        int budget = 256;
        while (budget-- > 0 && drop_at_ == 0 && transport_->Receive(bytes)) {
            ++stats_.received;
            stats_.bytes_received += bytes.size();
            client_.last_message_at = ctx.time;
            Json msg, reply;
            switch (pair_.OnFrame(bytes, now, msg, reply)) {
            case pairing::Server::Verdict::Accept: Dispatch(ctx, msg); break;
            case pairing::Server::Verdict::Reply: SendRaw(reply); break;
            case pairing::Server::Verdict::ReplyAndDrop:
                SendRaw(reply);
                drop_at_ = now + 200;
                break;
            case pairing::Server::Verdict::Paired:
                SendRaw(reply);
                client_.paired = true;
                ctx.LogSource(LogLevel::Info, "live", "paired on %s; waiting for the app's authenticated client_info", url_.c_str());
                break;
            case pairing::Server::Verdict::Drop: drop_at_ = now; break;
            case pairing::Server::Verdict::Ignore: break;
            }
        }
    }
    if (pair_.Tick(now) && drop_at_ == 0) drop_at_ = now;
    if (drop_at_ != 0 && now >= drop_at_) DropConnection(ctx, now);
    DrainPairingLog(ctx);
    if (font_warnings_ != logged_font_warnings_) {
        for (const std::string& w : font_warnings_) ctx.LogSource(LogLevel::Warning, "fonts", "%s", w.c_str());
        logged_font_warnings_ = font_warnings_;
    }
    if (IsConnected() && ctx.time - last_ping_ > 5.0) {
        last_ping_ = ctx.time;
        Ping();
    }
#else
    (void)ctx;
#endif
}

void LiveServer::Dispatch(EditorContext& ctx, const Json& msg) {
#if IMFORGE_HAS_TRANSPORT
    const std::string& t = msg["t"].AsString();
    // client_info comes sealed right after pair_ok; hello is the old unauthenticated client (pairing off)
    if (t == msg::kClientInfo || t == msg::kHello) {
        // once per connection: every one makes the builder send the whole project and all fonts again
        if (client_.connected) {
            ctx.LogSource(LogLevel::Debug, "live", "ignored a repeated client_info");
            return;
        }
        ClientInfoMsg h;
        if (t == msg::kClientInfo) {
            if (!ParseClientInfo(msg, h)) return;
        } else {
            HelloMsg legacy;
            if (!ParseHello(msg, legacy)) return;
            h.app = legacy.app;
            h.pid = legacy.pid;
            h.imgui_version = legacy.imgui_version;
            h.backend = legacy.backend;
            h.platform = legacy.platform;
            h.widgets = legacy.widgets;
            h.textures = legacy.textures;
        }
        client_.connected = true;
        client_.paired = pair_.GetState() == pairing::Server::State::Paired;
        client_.app = SanitizeText(h.app, kProtocolMaxName);
        client_.pid = h.pid;
        client_.imgui_version = h.imgui_version;
        client_.backend = SanitizeText(h.backend, kProtocolMaxName);
        client_.platform = SanitizeText(h.platform, kProtocolMaxName);
        client_.widgets.clear();
        for (const std::string& w : h.widgets) client_.widgets.push_back(SanitizeText(w, kProtocolMaxName));
        client_.textures.clear();
        for (const std::string& w : h.textures) client_.textures.push_back(SanitizeText(w, kProtocolMaxName));
        fonts_held_ = std::set<std::string>(h.font_hashes.begin(), h.font_hashes.end());
        fonts_announced_ = false;
        icons_sent_ = false;
        ctx.LogSource(client_.paired ? LogLevel::Info : LogLevel::Warning, "live",
                      "%s %s (pid %d, ImGui %d, %s/%s, %d widgets, %d textures)",
                      client_.paired ? "paired automatically with" : "UNAUTHENTICATED client", client_.app.c_str(),
                      client_.pid, client_.imgui_version, client_.platform.c_str(),
                      client_.backend.c_str(), (int)client_.widgets.size(), (int)client_.textures.size());
        if (ctx.has_project) {
            PushAll(ctx.project);
            SetPickerMode(picker_, ctx.SelectedId(), ctx.active_kind == DocKind::Panel ? ctx.active_panel : "");
        }
        return;
    }
    if (t == msg::kBlocks) {
        BlocksMsg b;
        if (!ParseBlocks(msg, b)) return;
        if (block_hashes_.size() >= 256 && !block_hashes_.count(b.panel)) return;
        std::map<std::string, std::string>& m = block_hashes_[b.panel];
        m.clear();
        for (const BlockHeight& e : b.blocks) m[e.id] = e.h;
        return;
    }
    if (t == msg::kSelect) {
        TargetMsg s;
        if (!ParseSelect(msg, s)) return;
        const Node* root = nullptr;
        if (s.panel.empty()) root = ctx.ActiveRoot();
        else if (const Panel* p = ctx.FindPanelDoc(s.panel)) root = &p->root;
        if (!root || (!s.id.empty() && !FindNode(*root, s.id))) {
            ctx.LogSource(LogLevel::Debug, "live", "ignored select of unknown node");
            return;
        }
        ctx.OnLiveSelect(s.panel, s.id);
        return;
    }
    if (t == msg::kHover) {
        TargetMsg s;
        if (!ParseHover(msg, s)) return;
        hover_panel_ = s.panel;
        hover_id_ = s.id;
        ctx.OnLiveHover(hover_panel_, hover_id_);
        return;
    }
    if (t == msg::kLog) {
        LogMsg l;
        if (!ParseLog(msg, l)) return;
        LogLevel lvl = l.level <= 0 ? LogLevel::Debug : l.level == 1 ? LogLevel::Info : l.level == 2 ? LogLevel::Warning : LogLevel::Error;
        ctx.LogSource(lvl, "app", "%s", SanitizeText(l.msg).c_str());
        return;
    }
    if (t == msg::kStats) {
        StatsMsg s;
        if (!ParseStats(msg, s)) return;
        client_.fps = s.fps;
        client_.panels.clear();
        for (const std::string& p : s.panels) client_.panels.push_back(SanitizeText(p, kProtocolMaxName));
        return;
    }
    if (t == msg::kPong) return;
    ctx.LogSource(LogLevel::Debug, "live", "unhandled message \"%s\"", SanitizeText(t, 64).c_str());
#else
    (void)ctx;
    (void)msg;
#endif
}

}  // namespace imforge::builder
