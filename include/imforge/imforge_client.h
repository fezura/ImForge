#pragma once

// ImForge live-attach client.
// Define IMFORGE_LIVE in dev builds. Without it every function here is an empty inline no-op.
//
// Root<T>(ctx, name): the object passed to Draw() under `name` (asserts T is the type it was bound with).
// State<S>(ctx): one S per panel, for generated handlers. Not synced with the interpreter's `state.*`.
// In picker mode the panel is drawn inside BeginDisabled (1.86+), so widgets don't react or fire events.
//
// Pairing: the builder writes a one-time code to a per-user file and Connect() reads it before every
// (re)connect, then does an HMAC handshake before applying anything. Codes are single use. Once paired,
// every message is MAC'd and sequenced; a bad frame drops the link ("security: ..." in Status()) and
// the app pairs again with the next code. If the app can't read the builder's file (VM, remote box,
// sandbox, other user) use SetPairingCode() or the IMFORGE_PAIRING_CODE env var.
//
// Project fonts: the builder sends the font files with the project. PreNewFrame() (before the backend
// NewFrame) adds them to the atlas; below 1.92, or when the renderer lacks RendererHasTextures, the atlas
// texture is then recreated through the function given to SetFontTextureRebuild().

#include "imforge_client_config.h"

#include <cstddef>
#include <initializer_list>

#ifndef IMFORGE_LIVE

#ifndef IMFORGE_LIVE_ASSERT
#define IMFORGE_LIVE_ASSERT(x) ((void)0)
#endif

struct ImFont;

namespace imforge {
struct DrawCtx;
namespace live {

struct BindingRoot {
    const char* name = "";
    void* ptr = nullptr;
};
using BlockFn = void (*)(DrawCtx&);
using HandlerFn = void (*)(DrawCtx&);
using WidgetDrawFn = bool (*)(DrawCtx&);

inline bool Connect(const char*) { return false; }
inline void Disconnect() {}
inline bool IsConnected() { return false; }
inline bool IsPaired() { return false; }
inline bool SetPairingCode(const char*) { return false; }
inline const char* Status() { return "disabled"; }
inline void NewFrame() {}
inline void PreNewFrame() {}
inline void SetFontTextureRebuild(void (*)()) {}
template <class T> inline BindingRoot Bind(const char* name, T& obj) {
    BindingRoot r;
    r.name = name;
    r.ptr = static_cast<void*>(&obj);
    return r;
}
inline void Draw(const char*, std::initializer_list<BindingRoot>) {}
template <class Roots> inline void Draw(const char*, const Roots&) {}
inline void RegisterBlock(const char*, BlockFn, const char* = nullptr) {}
inline void RegisterHandler(const char*, HandlerFn) {}
inline void RegisterWidget(const char*, WidgetDrawFn) {}
template <class Tex> inline void RegisterTexture(const char*, Tex) {}
inline void RegisterFont(const char*, ImFont*) {}
inline void SetLogCallback(void (*)(int, const char*)) {}
inline void SetAppName(const char*) {}
inline void SetApplyTheme(bool) {}
inline void SetDpiScale(float) {}
inline void ShowMissingPanels(bool) {}
inline void RegisterBundledPacks() {}
template <class T> inline T& Root(DrawCtx&, const char*) {
    static T fallback{};
    return fallback;
}
template <class S> inline S& State(DrawCtx&) {
    static S fallback{};
    return fallback;
}

}  // namespace live
}  // namespace imforge

#else  // IMFORGE_LIVE

#include "imgui.h"
#include "imforge_client_detail.h"

// pack adapters are picked up when packs/<pack>/ is on the include path (angle brackets so the
// amalgamated header keeps them optional too)
#if __has_include(<imforge_adapters_essentials.h>)
#include <imforge_adapters_essentials.h>
#define IMFORGE_LIVE_HAS_PACK_ESSENTIALS 1
#endif
#if __has_include(<imforge_adapters_controls.h>)
#include <imforge_adapters_controls.h>
#define IMFORGE_LIVE_HAS_PACK_CONTROLS 1
#endif
#if __has_include(<imforge_adapters_display.h>)
#include <imforge_adapters_display.h>
#define IMFORGE_LIVE_HAS_PACK_DISPLAY 1
#endif
#if __has_include(<imforge_adapters_gaming.h>)
#include <imforge_adapters_gaming.h>
#define IMFORGE_LIVE_HAS_PACK_GAMING 1
#endif

#include <initializer_list>
#include <map>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

#ifndef IMFORGE_LIVE_ASSERT
#define IMFORGE_LIVE_ASSERT(x) IM_ASSERT(x)
#endif

namespace imforge {
namespace live {

inline bool Connect(const char* url) {
    using namespace client_detail;
    Client& c = Get();
    uint64_t now = detail::NowMs();
    c.link = Link::Off;
    EndLink(c, now, "disconnected", false, 0);
    if (!url || !*url) {
        c.last_error = "empty url";
        c.status = "error: empty url";
        return false;
    }
    if (!CreateTransport(url)) {
        c.last_error = std::string("unsupported transport url: ") + url;
        c.status = "error: " + c.last_error;
        Log(c, 2, "imforge: " + c.last_error);
        return false;
    }
    c.url = url;
    c.link = Link::WaitingForCode;
    if (!TryStartLink(c, now)) {
        c.link = Link::Off;
        Log(c, 2, "imforge: " + c.last_error);
        return false;
    }
    return true;
}

inline void Disconnect() {
    using namespace client_detail;
    Client& c = Get();
    c.link = Link::Off;
    EndLink(c, detail::NowMs(), "disconnected", false, 0);
    pairing::SecureWipe(c.explicit_code);
}

inline bool IsConnected() {
    client_detail::Client& c = client_detail::Get();
    return c.link == client_detail::Link::Ready && c.transport && c.transport->IsConnected();
}

inline bool IsPaired() { return client_detail::Get().link == client_detail::Link::Ready; }

// Wins over IMFORGE_PAIRING_CODE and the pairing file. Wiped once used, null/empty clears it.
inline bool SetPairingCode(const char* code) {
    client_detail::Client& c = client_detail::Get();
    pairing::SecureWipe(c.explicit_code);
    if (!code || !*code) return true;
    std::string n;
    if (!pairing::NormalizeCode(code, n)) {
        client_detail::Log(c, 1, "imforge: SetPairingCode: not a valid pairing code");
        return false;
    }
    c.explicit_code = pairing::FormatCode(n);
    pairing::SecureWipe(n);
    c.next_code_check_ms = 0;
    return true;
}

inline const char* Status() {
    client_detail::Client& c = client_detail::Get();
    if (IsConnected()) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), " (%d panels)", (int)c.panels.size());
        c.status_buf = c.status + buf;
        return c.status_buf.c_str();
    }
    return c.status.c_str();
}

inline const char* LastError() { return client_detail::Get().last_error.c_str(); }

inline void NewFrame() {
    using namespace client_detail;
    Client& c = Get();
    c.last_items.swap(c.items);
    c.items.clear();
    c.missing_rows = 0;
    if (c.link == Link::Off) return;
    uint64_t now = detail::NowMs();
    if (c.link == Link::Ready) {
        FontLogs logs;
        CheckFontIntegration(c.font_state, now, logs);
        FlushFontLogs(c, logs);
    }
    if (c.link == Link::WaitingForCode) {
        if (now < c.next_code_check_ms) return;
        if (!TryStartLink(c, now) || c.link != Link::Connecting) return;
    }
    if (c.link == Link::Ready && c.transport->IsConnected()) FlushHover(c);
    c.transport->Poll();
    bool connected = c.transport->IsConnected();
    if (connected && !c.was_connected) OnConnected(c, now);
    else if (!connected && c.was_connected) OnDisconnected(c, now);
    if (!c.transport) return;
    if (c.link == Link::Connecting && now >= c.next_code_check_ms) {
        c.next_code_check_ms = now + kTransportReconnectMs;
        std::string why;
        if (!FindCode(c, nullptr, nullptr, &why)) {
            EndLink(c, now, "waiting for ImForge builder (" + why + ")", false, kTransportReconnectMs);
            return;
        }
    }
    std::vector<unsigned char> buf;
    Json m;
    for (int budget = 256; budget > 0 && c.transport && c.transport->Receive(buf); --budget) {
        if (c.link == Link::Ready) {
            const unsigned char* payload = nullptr;
            size_t n = 0;
            pairing::Channel::Status st = c.handshake.Open(buf.data(), buf.size(), &payload, &n);
            if (st != pairing::Channel::Status::Ok) {
                SecurityDrop(c, now, c.handshake.Describe(st));
                break;
            }
            if (!Unframe(payload, n, m)) {
                Log(c, 1, "imforge: malformed message dropped");
                continue;
            }
            Dispatch(c, m);
            continue;
        }
        if (buf.size() > pairing::kMaxHandshakeBytes) {
            c.pair_error = "oversized message before pairing";
            EndLink(c, now, "builder on " + c.url + " sent an oversized message before pairing; retrying", false, kTransportReconnectMs);
            break;
        }
        if (!Unframe(buf, m)) {
            Log(c, 1, "imforge: malformed message dropped");
            continue;
        }
        HandlePairing(c, m, now);
    }
    if (c.link == Link::Pairing && now >= c.handshake_deadline_ms) {
        c.pair_error = "pairing timed out";
        Log(c, 1, "imforge: pairing with " + c.url + " timed out");
        EndLink(c, now, "pairing with " + c.url + " timed out; retrying", false, kTransportReconnectMs);
    }
    if (c.link == Link::Ready) MaybeSendStats(c);
}

// call every frame before the backend NewFrame (ImGui_ImplXXX_NewFrame), outside NewFrame/Render
inline void PreNewFrame() {
    client_detail::Client& c = client_detail::Get();
    client_detail::FontLogs logs;
    client_detail::LoadPendingFonts(c.font_state, logs);
    client_detail::FlushFontLogs(c, logs);
}

// fn recreates the renderer's font texture after PreNewFrame rebuilt the atlas, e.g.
// ImGui_ImplDX11_InvalidateDeviceObjects (recreated by the next ImGui_ImplDX11_NewFrame). Not needed
// with 1.92+ backends that set RendererHasTextures.
inline void SetFontTextureRebuild(void (*fn)()) { client_detail::Get().font_state.rebuild_texture = fn; }

template <class T> inline BindingRoot Bind(const char* name, T& obj) {
    static_assert(!std::is_const<T>::value, "imforge::live::Bind: bind a non-const object");
    static_assert(TypeName<T>::value != nullptr, "imforge::live::Bind: T needs IMFORGE_REFLECT(T, ...)");
    BindingRoot r;
    r.name = name ? name : "";
    r.ptr = static_cast<void*>(&obj);
    r.type = StructInfoOf<T>();
    if (!r.type) r.type = TypeRegistry::Global().FindStruct(TypeName<T>::value);
    return r;
}

inline void Draw(const char* panel, const std::vector<BindingRoot>& roots) {
    using namespace client_detail;
    Client& c = Get();
    if (!panel) return;
    auto it = c.panels.find(panel);
    if (it == c.panels.end()) {
        if (c.show_missing) DrawMissing(c, panel);
        return;
    }
    char key[320];
    std::snprintf(key, sizeof(key), "%s#%u", panel, (unsigned)ImGui::GetID(panel));
    PanelInstance& inst = c.instances[key];
    // like generated code with io.FontDefault, panels start in the first project font
    ImFont* base_font = nullptr;
    if (const std::string* def = DefaultLiveFontName(c.font_state)) base_font = c.interp.hooks.font(*def);
    else if (c.have_project && !c.project.fonts.empty()) base_font = c.interp.hooks.font(c.project.fonts[0].name);
    if (base_font == ImGui::GetFont()) base_font = nullptr;
#if IMGUI_VERSION_NUM >= 19200
    if (base_font) ImGui::PushFont(base_font, base_font->LegacySize);
#else
    if (base_font) ImGui::PushFont(base_font);
#endif
    bool panel_match = c.mode.panel.empty() || c.mode.panel == panel;
    bool picker = c.mode.picker && panel_match;
    InterpOptions opts;
    opts.design_mode = false;
    opts.dpi_scale = DpiScale(c);
    opts.picker_mode = picker ? panel : nullptr;
    if (panel_match && !c.mode.selected.empty()) opts.selected_id = &c.mode.selected;
    c.cur_panel = panel;
    c.cur_picker = picker;
    size_t first_item = c.items.size();
#if IMGUI_VERSION_NUM >= 18600
    float disabled_alpha = ImGui::GetStyle().DisabledAlpha;
    if (picker) {
        ImGui::GetStyle().DisabledAlpha = 1.0f;
        ImGui::BeginDisabled();
        ImGui::GetStyle().DisabledAlpha = disabled_alpha;
    }
#endif
    c.interp.DrawPanel(it->second, inst, roots, opts);
#if IMGUI_VERSION_NUM >= 18600
    if (picker) ImGui::EndDisabled();
#endif
    c.cur_picker = false;
    if (base_font) ImGui::PopFont();
    if (!HasString(c.drawn_panels, panel)) c.drawn_panels.push_back(panel);
    if (picker) PickerFeedback(c, panel, first_item);
    ForwardDiagnostics(c);
}

inline void Draw(const char* panel, std::initializer_list<BindingRoot> roots) {
    Draw(panel, std::vector<BindingRoot>(roots));
}

inline void RegisterBlock(const char* panel_dot_id, BlockFn fn, const char* hash = nullptr) {
    client_detail::Client& c = client_detail::Get();
    if (!panel_dot_id || !fn) return;
    c.interp.RegisterBlock(panel_dot_id, fn);
    std::string h = hash ? hash : "";
    for (auto& e : c.block_hashes) {
        if (e.first == panel_dot_id) {
            e.second = h;
            return;
        }
    }
    c.block_hashes.emplace_back(panel_dot_id, h);
}

inline void RegisterHandler(const char* panel_dot_id_dot_event, HandlerFn fn) {
    if (!panel_dot_id_dot_event || !fn) return;
    client_detail::Get().interp.RegisterHandler(panel_dot_id_dot_event, fn);
}

inline void RegisterWidget(const char* type, WidgetDrawFn fn) {
    client_detail::Client& c = client_detail::Get();
    if (!type || !fn) return;
    c.interp.RegisterWidget(type, fn);
    if (!client_detail::HasString(c.widget_names, type)) c.widget_names.push_back(type);
}

inline void RegisterTexture(const char* name, ImTextureID tex) {
    client_detail::Client& c = client_detail::Get();
    if (!name) return;
    for (auto& kv : c.textures) {
        if (kv.first == name) {
            kv.second = tex;
            return;
        }
    }
    c.textures.emplace_back(name, tex);
}

inline void RegisterFont(const char* name, ImFont* font) {
    client_detail::Client& c = client_detail::Get();
    if (!name) return;
    for (auto& kv : c.fonts) {
        if (kv.first == name) {
            kv.second = font;
            return;
        }
    }
    c.fonts.emplace_back(name, font);
}

inline void SetLogCallback(void (*fn)(int level, const char* msg)) { client_detail::Get().log_cb = fn; }
inline void SetAppName(const char* name) { client_detail::Get().app_name = name ? name : ""; }
inline void SetApplyTheme(bool enable) {
    client_detail::Client& c = client_detail::Get();
    c.apply_theme = enable;
    if (enable) client_detail::ApplyActiveTheme(c);
}
inline void SetDpiScale(float scale) { client_detail::Get().dpi_override = scale; }
inline void ShowMissingPanels(bool enable) { client_detail::Get().show_missing = enable; }

// draw functions for every bundled pack whose adapter header is reachable; call before Connect()
// so client_info lists the widget types
inline void RegisterBundledPacks() {
    client_detail::Client& c = client_detail::Get();
#ifdef IMFORGE_LIVE_HAS_PACK_ESSENTIALS
    RegisterPackWidgets_essentials(c.interp);
#endif
#ifdef IMFORGE_LIVE_HAS_PACK_CONTROLS
    RegisterPackWidgets_controls(c.interp);
#endif
#ifdef IMFORGE_LIVE_HAS_PACK_DISPLAY
    RegisterPackWidgets_display(c.interp);
#endif
#ifdef IMFORGE_LIVE_HAS_PACK_GAMING
    RegisterPackWidgets_gaming(c.interp);
#endif
    for (const auto& kv : c.interp.widgets)
        if (!client_detail::HasString(c.widget_names, kv.first)) c.widget_names.push_back(kv.first);
}

template <class T> inline T& Root(DrawCtx& ctx, const char* name) {
    const char* tn = TypeName<T>::value;
    for (const BindingRoot& r : ctx.roots) {
        if (!name || r.name != name) continue;
        bool type_ok = r.ptr && r.type && tn && r.type->name == tn;
        IMFORGE_LIVE_ASSERT(type_ok && "imforge::live::Root<T>: root bound with a different type");
        if (type_ok) return *static_cast<T*>(r.ptr);
        break;
    }
    IMFORGE_LIVE_ASSERT(false && "imforge::live::Root<T>: no binding root with that name");
    static T fallback{};
    return fallback;
}

template <class S> inline S& State(DrawCtx& ctx) {
    static std::map<std::string, std::unique_ptr<S>> table;
    std::unique_ptr<S>& slot = table[ctx.panel.name];
    if (!slot) slot.reset(new S());
    return *slot;
}

inline size_t DebugItemCount() { return client_detail::Get().items.size(); }

inline bool DebugItemRect(const char* panel, const char* id, ImVec2* min, ImVec2* max) {
    using namespace client_detail;
    Client& c = Get();
    const ItemRect* r = FindItem(c.items, panel ? panel : "", id ? id : "");
    if (!r) r = FindItem(c.last_items, panel ? panel : "", id ? id : "");
    if (!r) return false;
    if (min) *min = r->min;
    if (max) *max = r->max;
    return true;
}

inline size_t DebugPanelCount() { return client_detail::Get().panels.size(); }

}  // namespace live
}  // namespace imforge

#endif  // IMFORGE_LIVE
