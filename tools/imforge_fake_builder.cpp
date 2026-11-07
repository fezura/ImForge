// Stand-in for the builder's live server so the client examples can be smoke-tested without the editor.

#include "imforge/imforge_core.h"
#include "imforge/imforge_json.h"
#include "imforge/imforge_pairing_session.h"
#include "imforge/imforge_protocol.h"
#include "imforge/imforge_transport.h"

#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#ifndef IMFORGE_SOURCE_DIR
#define IMFORGE_SOURCE_DIR "."
#endif

// <windows.h> (pulled in by the transport headers) defines SetProp as a macro, which would rename
// the Node::SetProp calls below to SetPropA.
#ifdef SetProp
#undef SetProp
#endif

using namespace imforge;

namespace {

uint64_t g_start_ms = 0;

uint64_t NowMs() {
    using namespace std::chrono;
    return (uint64_t)duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

void Logf(const char* fmt, ...) {
    std::printf("[%7.3f] ", (double)(NowMs() - g_start_ms) / 1000.0);
    va_list ap;
    va_start(ap, fmt);
    std::vprintf(fmt, ap);
    va_end(ap);
    std::printf("\n");
    std::fflush(stdout);
}

bool ReadFile(const std::string& path, std::string& out) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
    long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    out.resize(n > 0 ? (size_t)n : 0);
    size_t got = n > 0 ? std::fread(&out[0], 1, (size_t)n, f) : 0;
    std::fclose(f);
    return got == out.size();
}

bool LoadJsonFile(const std::string& path, Json& out) {
    std::string text;
    if (!ReadFile(path, text)) {
        Logf("error: cannot read %s", path.c_str());
        return false;
    }
    JsonError jerr;
    if (!ParseJson(text, out, &jerr)) {
        Logf("error: %s: %s", path.c_str(), jerr.message.c_str());
        return false;
    }
    return true;
}

std::string JoinPath(const std::string& dir, const std::string& rel) {
    if (dir.empty()) return rel;
    char last = dir[dir.size() - 1];
    return (last == '/' || last == '\\') ? dir + rel : dir + "/" + rel;
}

struct Fake {
    std::string url, project_dir;
    int seconds = 0;
    uint64_t mutate_ms = 3000;

    Project project;
    std::unique_ptr<Transport> transport;
    pairing::Server pair;
    uint64_t drop_at = 0;
    int n_paired = 0, n_pair_fail = 0, n_security = 0;
    bool was_connected = false;
    uint64_t connected_at = 0, last_mutate = 0, last_ping = 0;
    int mutation = 0;

    int n_hello = 0, n_blocks = 0, n_stats = 0, n_select = 0, n_hover = 0, n_log = 0, n_pong = 0, n_other = 0;
    int n_sent = 0;
    float last_fps = 0;
    std::vector<std::string> drawn_panels;
    std::string client_app, client_backend;

    bool LoadProject() {
        Json pj;
        if (!LoadJsonFile(JoinPath(project_dir, "imforge.json"), pj)) return false;
        Error err;
        project.dir = project_dir;
        if (!ProjectFromJson(pj, project, &err)) {
            Logf("error: imforge.json: %s", err.message.c_str());
            return false;
        }
        for (const std::string& f : project.theme_files) {
            Json j;
            Theme t;
            if (!LoadJsonFile(JoinPath(project_dir, f), j)) return false;
            if (!ThemeFromJson(j, t, &err)) {
                Logf("error: %s: %s", f.c_str(), err.message.c_str());
                return false;
            }
            t.file = f;
            project.themes.push_back(std::move(t));
        }
        for (const std::string& f : project.component_files) {
            Json j;
            Component c;
            if (!LoadJsonFile(JoinPath(project_dir, f), j)) return false;
            if (!ComponentFromJson(j, c, &err)) {
                Logf("error: %s: %s", f.c_str(), err.message.c_str());
                return false;
            }
            c.file = f;
            project.components.push_back(std::move(c));
        }
        for (const std::string& f : project.panel_files) {
            Json j;
            Panel p;
            if (!LoadJsonFile(JoinPath(project_dir, f), j)) return false;
            if (!PanelFromJson(j, p, &err)) {
                Logf("error: %s: %s", f.c_str(), err.message.c_str());
                return false;
            }
            p.file = f;
            project.panels.push_back(std::move(p));
        }
        Logf("project '%s' loaded from %s: %d themes (active '%s'), %d components, %d panels", project.name.c_str(),
             project_dir.c_str(), (int)project.themes.size(), project.active_theme.c_str(), (int)project.components.size(),
             (int)project.panels.size());
        return true;
    }

    bool Send(const Json& m) {
        std::string s;
        if (!transport || !transport->IsConnected() || !pair.Encode(m, s)) return false;
        return SendBytes(m, s);
    }

    bool SendRaw(const Json& m) {
        if (!transport || !transport->IsConnected()) return false;
        return SendBytes(m, Frame(m));
    }

    bool SendBytes(const Json& m, const std::string& s) {
        bool ok = transport->Send(s.data(), s.size());
        if (ok) ++n_sent;
        Logf("send %-12s %6d bytes%s", MsgType(m), (int)s.size(), ok ? "" : " FAILED");
        return ok;
    }

    Json ProjectMsg() const {
        Json pj = ProjectToJson(project);
        Json themes = Json::Array(), comps = Json::Array(), panels = Json::Array();
        for (const Theme& t : project.themes) themes.Push(ThemeToJson(t));
        for (const Component& c : project.components) comps.Push(ComponentToJson(c));
        for (const Panel& p : project.panels) panels.Push(PanelToJson(p));
        pj.Set("themes", std::move(themes));
        pj.Set("components", std::move(comps));
        pj.Set("panels", std::move(panels));
        return MsgProject(std::move(pj));
    }

    void PushAll() {
        Send(ProjectMsg());
        std::vector<Json> comps;
        for (const Component& c : project.components) comps.push_back(ComponentToJson(c));
        Send(MsgComponents(comps));
        for (const Theme& t : project.themes)
            if (t.name == project.active_theme) Send(MsgTheme(ThemeToJson(t)));
        for (const Panel& p : project.panels) Send(MsgPanel(PanelToJson(p)));
        Send(MsgMode(false, "", ""));
    }

    static Node* FindNode(Node& n, const char* id) {
        if (n.id == id) return &n;
        for (Node& c : n.children)
            if (Node* r = FindNode(c, id)) return r;
        for (auto& s : n.slots)
            for (Node& c : s.second)
                if (Node* r = FindNode(c, id)) return r;
        return nullptr;
    }

    Panel* FindPanel(const char* name) {
        for (Panel& p : project.panels)
            if (p.name == name) return &p;
        return nullptr;
    }

    void Mutate() {
        Panel* p = FindPanel("MainMenu");
        if (!p) return;
        ++mutation;
        bool odd = (mutation & 1) != 0;
        if (Node* title = FindNode(p->root, "title_text")) {
            char buf[96];
            std::snprintf(buf, sizeof(buf), "{icon:fa-crosshairs} CheatMenu%s", odd ? " [live edit]" : "");
            title->SetProp("text", Json(buf));
            title->SetProp("color", Json(odd ? "#ff8c42ff" : "#3d8bfdff"));
        }
        if (Node* fov = FindNode(p->root, "aim_fov")) {
            double mx = 180.0 + 10.0 * (double)(mutation % 6);
            fov->SetProp("max", Json(mx));
        }
        if (Node* fps = FindNode(p->root, "fps_text")) {
            char buf[96];
            std::snprintf(buf, sizeof(buf), "FPS {state.fps:%%.0f}  (edit #%d)", mutation);
            fps->SetProp("text", Json(buf));
        }
        Logf("mutate MainMenu #%d (title%s, aim_fov.max=%d)", mutation, odd ? " +[live edit]" : "",
             180 + 10 * (mutation % 6));
        Send(MsgPanel(PanelToJson(*p)));
    }

    void OnMessage(const Json& m) {
        const std::string& t = m["t"].AsString();
        if (t == msg::kClientInfo || t == msg::kHello) {
            HelloMsg h;
            ClientInfoMsg info;
            if (t == msg::kClientInfo) ParseClientInfo(m, info);
            else if (ParseHello(m, h)) {
                info.app = h.app;
                info.pid = h.pid;
                info.imgui_version = h.imgui_version;
                info.backend = h.backend;
                info.platform = h.platform;
                info.widgets = h.widgets;
                info.textures = h.textures;
            }
            ++n_hello;
            client_app = info.app;
            client_backend = info.backend;
            Logf("recv %s (%s): app='%s' pid=%d imgui=%d backend='%s' platform='%s' widgets=%d textures=%d", t.c_str(),
                 pair.GetState() == pairing::Server::State::Paired ? "authenticated" : "UNAUTHENTICATED", info.app.c_str(),
                 info.pid, info.imgui_version, info.backend.c_str(), info.platform.c_str(), (int)info.widgets.size(),
                 (int)info.textures.size());
            PushAll();
        } else if (t == msg::kBlocks) {
            BlocksMsg b;
            ++n_blocks;
            if (ParseBlocks(m, b)) {
                std::string ids;
                for (const BlockHeight& e : b.blocks) ids += (ids.empty() ? "" : ",") + e.id;
                Logf("recv blocks: panel='%s' ids=[%s]", b.panel.c_str(), ids.c_str());
            } else {
                Logf("recv blocks: (unparsable) %s", DumpJson(m, 0).c_str());
            }
        } else if (t == msg::kStats) {
            StatsMsg s;
            ++n_stats;
            if (ParseStats(m, s)) {
                last_fps = s.fps;
                std::string names;
                for (const std::string& p : s.panels) {
                    names += (names.empty() ? "" : ",") + p;
                    bool seen = false;
                    for (const std::string& d : drawn_panels) seen = seen || d == p;
                    if (!seen) drawn_panels.push_back(p);
                }
                Logf("recv stats: fps=%.1f panels=[%s]", s.fps, names.c_str());
            }
        } else if (t == msg::kSelect) {
            TargetMsg s;
            ++n_select;
            ParseSelect(m, s);
            Logf("recv select: panel='%s' id='%s'", s.panel.c_str(), s.id.c_str());
        } else if (t == msg::kHover) {
            TargetMsg s;
            ++n_hover;
            ParseHover(m, s);
            Logf("recv hover: panel='%s' id='%s'", s.panel.c_str(), s.id.c_str());
        } else if (t == msg::kLog) {
            LogMsg l;
            ++n_log;
            ParseLog(m, l);
            Logf("recv log[%d]: %s", l.level, l.msg.c_str());
        } else if (t == msg::kPong) {
            ++n_pong;
            Logf("recv pong");
        } else {
            ++n_other;
            Logf("recv unknown '%s': %s", t.c_str(), DumpJson(m, 0).c_str());
        }
    }

    void DrainPairLog() {
        for (const pairing::LogLine& l : pair.TakeLog()) Logf("%s: %s", l.security ? "security" : "pairing", l.text.c_str());
    }

    bool Relisten() {
        transport = CreateTransport(url.c_str());
        if (transport && transport->Listen(url.c_str())) return true;
        transport.reset();
        return false;
    }

    void Drop(uint64_t now) {
        drop_at = 0;
        if (was_connected) pair.OnDisconnect(now);
        was_connected = false;
        if (transport) transport->Close();
        transport.reset();
        Logf("connection dropped%s", pair.Refusing(now) ? "; refusing connections for 3 s" : "");
        if (!pair.Refusing(now) && !Relisten()) Logf("error: cannot listen on %s again", url.c_str());
    }

    void Route(const std::vector<unsigned char>& frame, uint64_t now) {
        Json m, reply;
        switch (pair.OnFrame(frame, now, m, reply)) {
        case pairing::Server::Verdict::Accept: OnMessage(m); break;
        case pairing::Server::Verdict::Reply:
            Logf("recv %s -> %s", MsgType(m), MsgType(reply));
            SendRaw(reply);
            break;
        case pairing::Server::Verdict::ReplyAndDrop:
            ++n_pair_fail;
            Logf("recv %s -> %s", MsgType(m), MsgType(reply));
            SendRaw(reply);
            drop_at = now + 200;
            break;
        case pairing::Server::Verdict::Paired:
            ++n_paired;
            SendRaw(reply);
            Logf("paired; waiting for the app's sealed client_info");
            break;
        case pairing::Server::Verdict::Drop:
            if (pair.SecurityEvents() > n_security) n_security = pair.SecurityEvents();
            drop_at = now;
            break;
        case pairing::Server::Verdict::Ignore: Logf("ignored '%s'", m.IsNull() ? "(malformed)" : MsgType(m)); break;
        }
    }

    int Run() {
        transport = CreateTransport(url.c_str());
        if (!transport) {
            Logf("error: unsupported transport url: %s", url.c_str());
            return 2;
        }
        if (!transport->Listen(url.c_str())) {
            Logf("error: cannot listen on %s", url.c_str());
            return 2;
        }
        int port = transport->BoundPort();
        if (port > 0) Logf("listening on %s (port %d)", url.c_str(), port);
        else Logf("listening on %s", url.c_str());
        pair.endpoint = url;
        pair.Start(NowMs());
        DrainPairLog();
        uint64_t deadline = seconds > 0 ? g_start_ms + (uint64_t)seconds * 1000 : 0;
        std::vector<unsigned char> buf;
        while (!deadline || NowMs() < deadline) {
            uint64_t now = NowMs();
            if (!transport) {
                if (!pair.Refusing(now) && Relisten()) Logf("listening again on %s", url.c_str());
                pair.Tick(now);
                DrainPairLog();
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
                continue;
            }
            transport->Poll();
            bool connected = transport->IsConnected();
            if (connected && !was_connected) {
                connected_at = now;
                last_mutate = connected_at;
                last_ping = connected_at;
                pair.OnConnect(now);
                Logf("client connected");
            } else if (!connected && was_connected) {
                pair.OnDisconnect(now);
                Logf("client disconnected");
            }
            was_connected = connected;
            int budget = 256;
            while (budget-- > 0 && drop_at == 0 && transport->Receive(buf)) Route(buf, now);
            if (pair.Tick(now) && drop_at == 0) drop_at = now;
            if (drop_at && now >= drop_at) Drop(now);
            DrainPairLog();
            if (connected && pair.Established()) {
                if (mutate_ms > 0 && now - last_mutate >= mutate_ms) {
                    last_mutate = now;
                    Mutate();
                }
                if (now - last_ping >= 5000) {
                    last_ping = now;
                    Send(MsgPing());
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        std::string names;
        for (const std::string& p : drawn_panels) names += (names.empty() ? "" : ",") + p;
        TransportStats ts = transport ? transport->Stats() : TransportStats();
        Logf("summary: client_info=%d paired=%d pair_fail=%d security=%d blocks=%d stats=%d select=%d hover=%d log=%d "
             "pong=%d other=%d sent=%d last_fps=%.1f panels_drawn=[%s] app='%s' backend='%s' bytes_tx=%llu bytes_rx=%llu "
             "drops=%llu",
             n_hello, n_paired, n_pair_fail, n_security, n_blocks, n_stats, n_select, n_hover, n_log, n_pong, n_other, n_sent, last_fps,
             names.c_str(), client_app.c_str(), client_backend.c_str(), (unsigned long long)ts.bytes_sent,
             (unsigned long long)ts.bytes_received, (unsigned long long)ts.drops);
        if (transport) transport->Close();
        pair.Stop();
        DrainPairLog();
        bool ok = n_hello >= 1 && n_stats >= 1 && (n_paired >= 1 || !pair.require);
        Logf("result: %s", ok ? "PASS" : "FAIL");
        return ok ? 0 : 1;
    }
};

}  // namespace

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    g_start_ms = NowMs();
    Fake f;
    f.url = "tcp://127.0.0.1:7777";
    f.project_dir = JoinPath(IMFORGE_SOURCE_DIR, "examples/projects/CheatMenu");
    for (int i = 1; i < argc; ++i) {
        const char* a = argv[i];
        if (std::strcmp(a, "--project") == 0 && i + 1 < argc) f.project_dir = argv[++i];
        else if (std::strcmp(a, "--seconds") == 0 && i + 1 < argc) f.seconds = std::atoi(argv[++i]);
        else if (std::strcmp(a, "--mutate-ms") == 0 && i + 1 < argc) f.mutate_ms = (uint64_t)std::atoi(argv[++i]);
        else if (std::strcmp(a, "--code") == 0 && i + 1 < argc) {
            f.pair.fixed_code = argv[++i];
            if (!pairing::IsValidCode(f.pair.fixed_code)) {
                std::printf("error: --code needs %d Crockford base32 characters (dashes optional)\n", (int)pairing::kCodeChars);
                return 2;
            }
        } else if (std::strcmp(a, "--no-pairing") == 0) f.pair.require = false;
        else if (std::strstr(a, "://")) f.url = a;
        else if (std::strcmp(a, "--help") == 0 || std::strcmp(a, "-h") == 0) {
            std::printf("usage: %s [url] [--project DIR] [--seconds N] [--mutate-ms N] [--code CODE] [--no-pairing]\n", argv[0]);
            return 0;
        } else f.project_dir = a;
    }
    Logf("imforge_fake_builder url=%s project=%s seconds=%d mutate_ms=%d", f.url.c_str(), f.project_dir.c_str(),
         f.seconds, (int)f.mutate_ms);
    if (!f.LoadProject()) return 2;
    return f.Run();
}
