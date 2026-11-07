#pragma once

// guts of imforge_client.h, only pulled in with IMFORGE_LIVE

#include "imgui.h"

#include "imforge_client_config.h"
#include "imforge_core.h"
#include "imforge_widgets.h"
#include "imforge_expr.h"
#include "imforge_interp.h"
#include "imforge_transport.h"
#include "imforge_protocol.h"
#include "imforge_pairing_session.h"
#include "imb_runtime.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

namespace imforge {
namespace live {

using imforge::BindingRoot;
using imforge::BlockFn;
using imforge::HandlerFn;
using imforge::WidgetDrawFn;

namespace client_detail {

// WaitingForCode: no usable code yet, transport closed. Ready: paired, everything sealed both ways.
enum class Link { Off, WaitingForCode, Connecting, Pairing, Ready };

constexpr size_t kMaxBurnedCodes = 32;
constexpr size_t kMaxSuspectCodes = 8;
constexpr uint64_t kPairRetryMs = 2000;
constexpr const char* kPairingCodeEnv = "IMFORGE_PAIRING_CODE";

struct ItemRect {
    std::string panel, id;
    ImVec2 min, max;
    bool hovered = false;
};

struct Client {
    std::unique_ptr<Transport> transport;
    std::string url;
    std::string app_name = IMFORGE_LIVE_APP_NAME;
    std::string status = "disconnected";
    std::string status_buf;
    std::string last_error;
    bool was_connected = false;

    Link link = Link::Off;
    pairing::ClientHandshake handshake;
    std::string explicit_code;
    std::vector<std::string> burned_codes;
    std::vector<std::string> suspect_codes;
    std::string pending_digest;
    std::string code_source;
    std::string pair_error;
    int attempts_left = -1;
    uint64_t next_code_check_ms = 0;
    uint64_t handshake_deadline_ms = 0;
    uint64_t ignored_unpaired = 0;
    int security_events = 0;

    Project project;
    bool have_project = false;
    std::map<std::string, Panel> panels;
    Interpreter interp;
    std::map<std::string, PanelInstance> instances;

    std::vector<std::pair<std::string, ImTextureID>> textures;
    std::vector<std::pair<std::string, ImFont*>> fonts;
    std::vector<std::string> widget_names;
    std::vector<std::pair<std::string, std::string>> block_hashes;
    void (*log_cb)(int, const char*) = nullptr;
    bool apply_theme = true;
    bool show_missing = false;
    float dpi_override = 0.0f;

    ModeMsg mode;
    std::string cur_panel;
    bool cur_picker = false;
    std::string sent_hover_panel, sent_hover_id;
    std::string frame_hover_panel, frame_hover_id;
    std::vector<ItemRect> items, last_items;
    std::vector<std::string> drawn_panels;
    std::vector<std::string> sent_logs;
    uint64_t last_stats_ms = 0;
    int missing_rows = 0;
    bool hooks_installed = false;
};

inline Client& Get();

inline int Pid() {
#ifdef _WIN32
    return (int)_getpid();
#else
    return (int)getpid();
#endif
}

inline const char* PlatformName() {
#if defined(_WIN32)
    return "windows";
#elif defined(__APPLE__)
    return "macos";
#elif defined(__linux__)
    return "linux";
#else
    return "unknown";
#endif
}

inline void SendRaw(Client& c, const Json& m) {
    if (!c.transport || !c.transport->IsConnected()) return;
    std::string s = Frame(m);
    c.transport->Send(s.data(), s.size());
}

// everything but the handshake goes through here, so nothing leaks out before pairing
inline void SendJson(Client& c, const Json& m) {
    if (c.link != Link::Ready || !c.transport || !c.transport->IsConnected()) return;
    std::string sealed;
    if (c.handshake.Seal(Frame(m), sealed)) c.transport->Send(sealed.data(), sealed.size());
}

inline void Log(Client& c, int level, const std::string& full_text) {
    const std::string text = full_text.size() > 2048 ? full_text.substr(0, 2048) : full_text;
    if (c.log_cb) c.log_cb(level, text.c_str());
    if (level >= 1) SendJson(c, MsgLog(level, text.c_str()));
}

inline bool HasString(const std::vector<std::string>& v, const std::string& s) {
    for (const std::string& e : v)
        if (e == s) return true;
    return false;
}

inline void InstallHooks(Client& c) {
    if (c.hooks_installed) return;
    c.hooks_installed = true;
    c.interp.project = &c.project;
    c.project.widget_catalog = &c.interp.catalog;
    c.interp.hooks.on_item = [](const Node& n, const ImVec2& mn, const ImVec2& mx, bool hovered) {
        Client& cl = Get();
        ItemRect r;
        r.panel = cl.cur_panel;
        r.id = n.id;
        r.min = mn;
        r.max = mx;
        r.hovered = hovered;
        cl.items.push_back(std::move(r));
    };
    c.interp.hooks.on_click = [](const Node& n) {
        Client& cl = Get();
        if (!cl.cur_picker) return;
        SendJson(cl, MsgSelect(cl.cur_panel.c_str(), n.id.c_str()));
        cl.mode.selected = n.id;
        cl.mode.panel = cl.cur_panel;
    };
    c.interp.hooks.texture = [](const std::string& name) -> ImTextureID {
        Client& cl = Get();
        for (const auto& kv : cl.textures)
            if (kv.first == name) return kv.second;
        return ImTextureID();
    };
    c.interp.hooks.font = [](const std::string& name) -> ImFont* {
        Client& cl = Get();
        for (const auto& kv : cl.fonts)
            if (kv.first == name) return kv.second;
        ImFontAtlas* atlas = ImGui::GetIO().Fonts;
        if (!atlas || name.empty()) return nullptr;
        for (int i = 0; i < atlas->Fonts.Size; ++i) {
            ImFont* f = atlas->Fonts[i];
            const char* dbg = f ? f->GetDebugName() : nullptr;
            if (dbg && std::strncmp(dbg, name.c_str(), name.size()) == 0) return f;
        }
        return nullptr;
    };
}

inline Client& Get() {
    static Client c;
    InstallHooks(c);
    return c;
}

inline std::vector<std::string> TextureNames(const Client& c) {
    std::vector<std::string> out;
    for (const auto& kv : c.textures) out.push_back(kv.first);
    return out;
}

inline void SendBlocks(Client& c) {
    std::vector<std::pair<std::string, Json>> per_panel;
    for (const auto& kv : c.interp.blocks) {
        size_t dot = kv.first.find('.');
        std::string panel = dot == std::string::npos ? std::string() : kv.first.substr(0, dot);
        std::string id = dot == std::string::npos ? kv.first : kv.first.substr(dot + 1);
        std::string hash;
        for (const auto& h : c.block_hashes)
            if (h.first == kv.first) hash = h.second;
        Json e = Json::Object();
        e.Set("id", id);
        e.Set("h", hash);
        Json* arr = nullptr;
        for (auto& p : per_panel)
            if (p.first == panel) arr = &p.second;
        if (!arr) {
            per_panel.emplace_back(panel, Json::Array());
            arr = &per_panel.back().second;
        }
        arr->Push(std::move(e));
    }
    for (auto& p : per_panel) {
        Json m = Json::Object();
        m.Set("t", msg::kBlocks);
        m.Set("panel", p.first);
        m.Set("blocks", std::move(p.second));
        SendJson(c, m);
    }
}

inline std::string CodeDigest(const std::string& code) {
    std::string n;
    if (!pairing::NormalizeCode(code, n)) return std::string();
    unsigned char d[32];
    pairing::Sha256 h;
    h.Update("imforge-used-code|", 18);
    h.Update(n);
    h.Final(d);
    pairing::SecureWipe(n);
    std::string out = pairing::ToHex(d, sizeof(d));
    pairing::SecureWipe(d, sizeof(d));
    return out;
}

inline bool IsBurned(const Client& c, const std::string& digest) {
    if (digest.empty()) return true;
    for (const std::string& b : c.burned_codes)
        if (b == digest) return true;
    return false;
}

inline bool IsSuspect(const Client& c, const std::string& digest) {
    for (const std::string& b : c.suspect_codes)
        if (b == digest) return true;
    return false;
}

// only burn a code once it paired, the builder rotates it right away then
inline void BurnPendingCode(Client& c) {
    if (!c.pending_digest.empty() && !IsBurned(c, c.pending_digest)) {
        if (c.burned_codes.size() >= kMaxBurnedCodes) c.burned_codes.erase(c.burned_codes.begin());
        c.burned_codes.push_back(c.pending_digest);
    }
    pairing::SecureWipe(c.pending_digest);
}

// A port squatter and a stale code look the same from here (bad proof or unauthenticated pair_fail),
// so keep the code but try it last, and re-read the pairing file next time.
inline void SuspectPendingCode(Client& c) {
    if (!c.pending_digest.empty() && !IsSuspect(c, c.pending_digest)) {
        if (c.suspect_codes.size() >= kMaxSuspectCodes) c.suspect_codes.erase(c.suspect_codes.begin());
        c.suspect_codes.push_back(c.pending_digest);
    }
    pairing::SecureWipe(c.pending_digest);
}

// order: SetPairingCode(), IMFORGE_PAIRING_CODE, then the pairing file (re-read every time).
// burned codes are skipped, suspect ones only used as a last resort
inline bool FindCode(Client& c, std::string* code, std::string* source, std::string* why) {
    std::string best, best_source, candidate, err;
    bool have = false, fresh = false;
    auto consider = [&](std::string& cand, const std::string& src) {
        std::string d = CodeDigest(cand);
        if (!IsBurned(c, d) && (!have || (!fresh && !IsSuspect(c, d)))) {
            pairing::SecureWipe(best);
            best = cand;
            best_source = src;
            fresh = !IsSuspect(c, d);
            have = true;
        }
        pairing::SecureWipe(cand);
    };
    if (!c.explicit_code.empty()) {
        candidate = c.explicit_code;
        consider(candidate, "SetPairingCode");
    }
    if (!fresh && pairing::file_detail::GetEnv(kPairingCodeEnv, candidate)) consider(candidate, kPairingCodeEnv);
    if (!fresh) {
        std::string path = pairing::PairingFilePath(c.url.c_str());
        pairing::PairingFileData f;
        if (pairing::ReadPairingFile(path, f, &err)) {
            if (IsBurned(c, CodeDigest(f.code))) err = "the code in " + path + " was already used; waiting for a new one";
            consider(f.code, "pairing file " + path);
        }
    }
    if (!have) {
        if (why) *why = err;
        return false;
    }
    if (code) *code = best;
    if (source) *source = best_source;
    pairing::SecureWipe(best);
    return true;
}

inline void ClearHover(Client& c) {
    c.sent_hover_id.clear();
    c.sent_hover_panel.clear();
    c.frame_hover_id.clear();
    c.frame_hover_panel.clear();
}

inline void EndLink(Client& c, uint64_t now, const std::string& status, bool burn, uint64_t retry_ms) {
    if (burn) BurnPendingCode(c);
    pairing::SecureWipe(c.pending_digest);
    c.handshake.Wipe();
    if (c.transport) {
        c.transport->Close();
        c.transport.reset();
    }
    c.was_connected = false;
    c.mode = ModeMsg();
    ClearHover(c);
    if (c.link != Link::Off) c.link = Link::WaitingForCode;
    c.next_code_check_ms = now + retry_ms;
    c.status = status;
}

// false only on a hard transport error
inline bool TryStartLink(Client& c, uint64_t now) {
    c.next_code_check_ms = now + kTransportReconnectMs;
    std::string why;
    if (!FindCode(c, nullptr, nullptr, &why)) {
        c.status = "waiting for ImForge builder (" + why + ")";
        return true;
    }
    c.transport = CreateTransport(c.url.c_str());
    if (!c.transport || !c.transport->Connect(c.url.c_str())) {
        c.transport.reset();
        c.last_error = "connect failed: " + c.url;
        c.status = "error: " + c.last_error;
        return false;
    }
    c.was_connected = false;
    c.link = Link::Connecting;
    c.status = "connecting to " + c.url;
    return true;
}

inline void OnConnected(Client& c, uint64_t now) {
    std::string code, source, why;
    if (!FindCode(c, &code, &source, &why)) {
        EndLink(c, now, "waiting for ImForge builder (" + why + ")", false, kTransportReconnectMs);
        return;
    }
    c.pending_digest = CodeDigest(code);
    bool ok = c.handshake.Begin(code);
    pairing::SecureWipe(code);
    if (!ok) {
        c.pair_error = c.handshake.Error();
        EndLink(c, now, "pairing failed: " + c.pair_error, false, kPairRetryMs);
        return;
    }
    c.code_source = source;
    c.link = Link::Pairing;
    c.was_connected = true;
    c.handshake_deadline_ms = now + pairing::kHandshakeTimeoutMs;
    c.ignored_unpaired = 0;
    c.status = "pairing with " + c.url;
    SendRaw(c, MsgPairingHello(c.handshake.NonceA()));
}

inline void OnPaired(Client& c) {
    std::string used = c.pending_digest;
    BurnPendingCode(c);
    if (!c.explicit_code.empty()) {
        std::string d = CodeDigest(c.explicit_code);
        if (d == used || IsSuspect(c, d)) pairing::SecureWipe(c.explicit_code);
    }
    c.suspect_codes.clear();
    c.link = Link::Ready;
    c.pair_error.clear();
    c.attempts_left = -1;
    c.status = "connected to " + c.url + " (paired)";
    SendJson(c, MsgClientInfo(c.app_name.c_str(), Pid(), IMGUI_VERSION_NUM, IMFORGE_BACKEND_NAME, PlatformName(), c.widget_names,
                              TextureNames(c)));
    SendBlocks(c);
    c.last_stats_ms = detail::NowMs();
    ClearHover(c);
    c.sent_logs.clear();
    Log(c, 0, "imforge: paired with the builder on " + c.url + " (code from " + c.code_source + ")");
}

inline void HandlePairing(Client& c, const Json& m, uint64_t now) {
    using R = pairing::ClientHandshake::Result;
    Json reply;
    switch (c.handshake.OnMessage(m, reply)) {
    case R::Reply: SendRaw(c, reply); return;
    case R::Paired: OnPaired(c); return;
    case R::Rejected:
    case R::Failed: {
        c.attempts_left = c.handshake.AttemptsLeft();
        c.pair_error = c.handshake.Error();
        std::string left = c.attempts_left >= 0 ? " (" + std::to_string(c.attempts_left) + " attempts left)" : std::string();
        Log(c, 2, "imforge: " + c.pair_error + left);
        SuspectPendingCode(c);
        EndLink(c, now, "pairing failed: " + c.pair_error + left + "; retrying", false, kPairRetryMs);
        return;
    }
    case R::Ignore: ++c.ignored_unpaired; return;
    }
}

// bad MAC or sequence on a paired link. nothing was parsed and the channel already wiped the keys
inline void SecurityDrop(Client& c, uint64_t now, const std::string& why) {
    ++c.security_events;
    c.last_error = "security: " + why;
    EndLink(c, now, "security: dropped the link to " + c.url + " (" + why + "); re-pairing", false, kTransportReconnectMs);
    Log(c, 3, "imforge: SECURITY: " + why + " on the link to " + c.url + "; connection dropped, session keys wiped");
}

inline void OnDisconnected(Client& c, uint64_t now) {
    if (c.link == Link::Ready) {
        Log(c, 0, "imforge: disconnected from " + c.url);
        EndLink(c, now, "reconnecting to " + c.url, false, 0);
    } else {
        EndLink(c, now, "connection to " + c.url + " closed during pairing; retrying", false, kTransportReconnectMs);
    }
}

inline const Theme* FindTheme(const Project& p, const std::string& name) {
    for (const Theme& t : p.themes)
        if (t.name == name) return &t;
    return nullptr;
}

inline void UpsertTheme(Project& p, Theme t) {
    for (Theme& e : p.themes) {
        if (e.name == t.name) {
            e = std::move(t);
            return;
        }
    }
    p.themes.push_back(std::move(t));
}

inline void ApplyActiveTheme(Client& c) {
    if (!c.apply_theme) return;
    if (const Theme* t = FindTheme(c.project, c.project.active_theme)) c.interp.ApplyTheme(*t);
}

constexpr size_t kMaxPanels = 256;
constexpr size_t kMaxComponents = 1024;
constexpr size_t kMaxThemes = 128;
constexpr size_t kMaxNodesPerDoc = 20000;
constexpr int kMaxNodeDepth = 64;

inline bool CountNodes(const Node& n, int depth, size_t& count) {
    if (depth > kMaxNodeDepth || ++count > kMaxNodesPerDoc) return false;
    for (const Node& ch : n.children)
        if (!CountNodes(ch, depth + 1, count)) return false;
    for (const auto& sl : n.slots)
        for (const Node& ch : sl.second)
            if (!CountNodes(ch, depth + 1, count)) return false;
    return true;
}

inline std::string ClipForLog(const std::string& s) { return s.size() > 64 ? s.substr(0, 64) + "..." : s; }

inline void ApplyPanelJson(Client& c, const Json& pj) {
    Panel p;
    Error err;
    if (!PanelFromJson(pj, p, &err)) {
        c.last_error = err.message;
        Log(c, 2, "imforge: bad panel: " + err.message);
        return;
    }
    size_t nodes = 0;
    if (!CountNodes(p.root, 0, nodes)) {
        c.last_error = "panel too large or too deeply nested";
        Log(c, 2, "imforge: panel '" + ClipForLog(p.name) + "' rejected: too large or too deeply nested");
        return;
    }
    if (c.panels.find(p.name) == c.panels.end() && c.panels.size() >= kMaxPanels) {
        c.last_error = "too many panels";
        Log(c, 2, "imforge: panel '" + ClipForLog(p.name) + "' rejected: too many panels");
        return;
    }
    std::string name = p.name;
    c.panels[name] = std::move(p);
    Log(c, 0, "imforge: panel '" + name + "' updated");
}

inline bool ApplyComponentList(Client& c, const Json& arr, std::vector<Component>& out) {
    if (!arr.IsArray()) return false;
    bool any = false;
    for (size_t i = 0; i < arr.Size(); ++i) {
        if (!arr[i].IsObject()) continue;
        Component comp;
        Error err;
        if (!ComponentFromJson(arr[i], comp, &err)) {
            c.last_error = err.message;
            Log(c, 2, "imforge: bad component: " + err.message);
            continue;
        }
        size_t nodes = 0;
        if (!CountNodes(comp.root, 0, nodes)) {
            Log(c, 2, "imforge: component '" + ClipForLog(comp.name) + "' rejected: too large or too deeply nested");
            continue;
        }
        bool replaced = false;
        for (Component& e : out) {
            if (e.name == comp.name) {
                e = std::move(comp);
                replaced = true;
                break;
            }
        }
        if (!replaced && out.size() >= kMaxComponents) continue;
        if (!replaced) out.push_back(std::move(comp));
        any = true;
    }
    return any;
}

inline void ApplyThemeList(Client& c, const Json& arr, std::vector<Theme>& out) {
    if (!arr.IsArray()) return;
    for (size_t i = 0; i < arr.Size(); ++i) {
        if (!arr[i].IsObject()) continue;
        Theme t;
        Error err;
        if (!ThemeFromJson(arr[i], t, &err)) {
            c.last_error = err.message;
            Log(c, 2, "imforge: bad theme: " + err.message);
            continue;
        }
        bool replaced = false;
        for (Theme& e : out) {
            if (e.name == t.name) {
                e = std::move(t);
                replaced = true;
                break;
            }
        }
        if (!replaced && out.size() >= kMaxThemes) continue;
        if (!replaced) out.push_back(std::move(t));
    }
}

inline Json StringifyInline(const Json& arr) {
    Json out = Json::Array();
    if (!arr.IsArray()) return out;
    for (size_t i = 0; i < arr.Size(); ++i) {
        const Json& e = arr[i];
        if (e.IsString()) out.Push(e);
        else if (e.IsObject()) out.Push(e["file"].IsString() ? e["file"] : e["name"]);
    }
    return out;
}

inline void ApplyProject(Client& c, const Json& m) {
    const Json& pj = m["project"];
    if (!pj.IsObject()) {
        Log(c, 2, "imforge: project message without object");
        return;
    }
    std::vector<Theme> themes = c.project.themes;
    std::vector<Component> components = c.project.components;
    ApplyThemeList(c, pj["themes"], themes);
    ApplyThemeList(c, m["themes"], themes);
    ApplyComponentList(c, pj["components"], components);
    ApplyComponentList(c, m["components"], components);
    const Json* panel_lists[2] = {&pj["panels"], &m["panels"]};
    for (const Json* pl : panel_lists) {
        if (!pl->IsArray()) continue;
        for (size_t i = 0; i < pl->Size(); ++i)
            if ((*pl)[i].IsObject()) ApplyPanelJson(c, (*pl)[i]);
    }
    Json clean = pj;
    const char* lists[3] = {"themes", "panels", "components"};
    for (const char* key : lists)
        if (clean[key].IsArray()) clean.Set(key, StringifyInline(clean[key]));
    Project fresh;
    fresh.widget_catalog = &c.interp.catalog;
    Error err;
    if (!ProjectFromJson(clean, fresh, &err)) {
        c.last_error = err.message;
        Log(c, 2, "imforge: bad project: " + err.message);
        return;
    }
    fresh.themes = std::move(themes);
    fresh.components = std::move(components);
    c.project = std::move(fresh);
    c.project.widget_catalog = &c.interp.catalog;
    c.have_project = true;
    if (m["widgets"].IsArray()) {
        const Json& w = m["widgets"];
        for (size_t i = 0; i < w.Size(); ++i) c.interp.catalog.AddManifest(w[i], &err);
    }
    ApplyActiveTheme(c);
    Log(c, 0, "imforge: project '" + c.project.name + "' loaded");
}

inline void FlushHover(Client& c);

inline void RemoveInstances(Client& c, const std::string& panel) {
    std::string prefix = panel + "#";
    for (auto it = c.instances.begin(); it != c.instances.end();) {
        if (it->first.compare(0, prefix.size(), prefix) == 0) it = c.instances.erase(it);
        else ++it;
    }
}

inline void Dispatch(Client& c, const Json& m) {
    const std::string& t = m["t"].AsString();
    if (t == msg::kPanel) {
        if (m["panel"].IsObject()) ApplyPanelJson(c, m["panel"]);
    } else if (t == msg::kProject) {
        ApplyProject(c, m);
    } else if (t == msg::kComponents) {
        if (m["components"].IsArray()) {
            std::vector<Component> comps;
            ApplyComponentList(c, m["components"], comps);
            c.project.components = std::move(comps);
        }
    } else if (t == msg::kWidgets) {
        const Json& arr = m["manifests"];
        for (size_t i = 0; arr.IsArray() && i < arr.Size(); ++i) {
            Error err;
            if (!c.interp.catalog.AddManifest(arr[i], &err)) {
                c.last_error = err.message;
                Log(c, 2, "imforge: " + err.message);
            }
        }
    } else if (t == msg::kRemovePanel) {
        std::string name;
        if (ParseRemovePanel(m, name)) {
            c.panels.erase(name);
            RemoveInstances(c, name);
        }
    } else if (t == msg::kTheme) {
        Theme th;
        Error err;
        if (!m["theme"].IsObject() || !ThemeFromJson(m["theme"], th, &err)) {
            c.last_error = err.message;
            Log(c, 2, "imforge: bad theme: " + err.message);
            return;
        }
        if (c.apply_theme) c.interp.ApplyTheme(th);
        UpsertTheme(c.project, std::move(th));
    } else if (t == msg::kMode) {
        ModeMsg md;
        if (ParseMode(m, md)) {
            c.mode = md;
            if (!md.picker) FlushHover(c);
        }
    } else if (t == msg::kPing) {
        SendJson(c, MsgPong());
    } else {
        Log(c, 1, "imforge: unknown message '" + ClipForLog(t) + "'");
    }
}

inline void MaybeSendStats(Client& c) {
    uint64_t now = detail::NowMs();
    if (now - c.last_stats_ms < (uint64_t)IMFORGE_LIVE_STATS_INTERVAL_MS) return;
    c.last_stats_ms = now;
    SendJson(c, MsgStats(ImGui::GetIO().Framerate, c.drawn_panels));
    c.drawn_panels.clear();
}

inline void ForwardDiagnostics(Client& c) {
    for (const Diagnostic& d : c.interp.last_diagnostics) {
        std::string text = d.panel + "/" + d.node_id;
        if (!d.prop.empty()) text += "." + d.prop;
        text += ": " + d.message;
        if (text.size() > 2048) text.resize(2048);
        if (HasString(c.sent_logs, text)) continue;
        if (c.sent_logs.size() >= (size_t)IMFORGE_LIVE_MAX_LOG) c.sent_logs.clear();
        c.sent_logs.push_back(text);
        int level = d.level == Diagnostic::Level::Error ? 2 : d.level == Diagnostic::Level::Warning ? 1 : 0;
        if (c.log_cb) c.log_cb(level, text.c_str());
        SendJson(c, MsgLog(level, text.c_str()));
    }
}

inline const ItemRect* FindItem(const std::vector<ItemRect>& items, const std::string& panel, const std::string& id) {
    for (const ItemRect& r : items)
        if (r.id == id && (panel.empty() || r.panel == panel)) return &r;
    return nullptr;
}

inline void PickerFeedback(Client& c, const char* panel, size_t first_item) {
    const ItemRect* hovered = nullptr;
    float best = 0;
    for (size_t i = first_item; i < c.items.size(); ++i) {
        const ItemRect& r = c.items[i];
        if (!r.hovered) continue;
        float area = (r.max.x - r.min.x) * (r.max.y - r.min.y);
        if (!hovered || area < best) {
            hovered = &r;
            best = area;
        }
    }
    ImDrawList* fg = ImGui::GetForegroundDrawList();
    if (hovered && fg) {
        fg->AddRectFilled(hovered->min, hovered->max, IMFORGE_LIVE_HOVER_FILL_COLOR);
#if IMGUI_VERSION_NUM >= 19280
        fg->AddRect(hovered->min, hovered->max, IMFORGE_LIVE_HOVER_COLOR, 0.0f, 2.0f, 0);
#else
        fg->AddRect(hovered->min, hovered->max, IMFORGE_LIVE_HOVER_COLOR, 0.0f, 0, 2.0f);
#endif
    }
    if (hovered && c.frame_hover_id.empty()) {
        c.frame_hover_panel = panel;
        c.frame_hover_id = hovered->id;
    }
}

inline void FlushHover(Client& c) {
    bool changed = c.frame_hover_id != c.sent_hover_id ||
                   (!c.frame_hover_id.empty() && c.frame_hover_panel != c.sent_hover_panel);
    if (changed) {
        c.sent_hover_id = c.frame_hover_id;
        if (!c.frame_hover_panel.empty()) c.sent_hover_panel = c.frame_hover_panel;
        SendJson(c, MsgHover(c.sent_hover_panel.c_str(), c.sent_hover_id.c_str()));
    }
    c.frame_hover_panel.clear();
    c.frame_hover_id.clear();
}

inline float DpiScale(const Client& c) {
    if (c.dpi_override > 0) return c.dpi_override;
    if (c.have_project && c.project.codegen.dpi_mode == "fontSize" && c.project.codegen.base_font_size > 0)
        return ImGui::GetFontSize() / c.project.codegen.base_font_size;
    return 1.0f;
}

inline void DrawMissing(Client& c, const char* panel) {
    ImDrawList* fg = ImGui::GetForegroundDrawList();
    if (!fg) return;
    char buf[256];
    std::snprintf(buf, sizeof(buf), "ImForge: panel '%s' not received (%s)", panel, c.status.c_str());
    float y = 8.0f + (float)c.missing_rows++ * (ImGui::GetFontSize() + 4.0f);
    fg->AddText(ImVec2(8.0f, y), IMFORGE_LIVE_OVERLAY_TEXT_COLOR, buf);
}

}  // namespace client_detail

}  // namespace live
}  // namespace imforge
