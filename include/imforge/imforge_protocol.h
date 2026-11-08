#pragma once

#include "imforge_json.h"

#include <string>
#include <utility>
#include <vector>

namespace imforge {

// compact JSON with sorted keys, the transport adds the length prefix
inline std::string Frame(const Json& msg) { return DumpJson(msg, 0, true); }

constexpr size_t kProtocolMaxPayload = size_t(16) << 20;
constexpr size_t kProtocolMaxList = 4096;
constexpr size_t kProtocolMaxBlocks = 20000;
constexpr size_t kProtocolMaxName = 256;
constexpr size_t kProtocolMaxLogText = 4096;
constexpr size_t kProtocolMaxAnimations = 2048;
constexpr size_t kProtocolMaxAnimKeys = 64;
constexpr size_t kProtocolMaxAnimTracks = 32;
constexpr size_t kProtocolMaxAnimText = 4096;
constexpr size_t kProtocolMaxAnimTags = 32;
// project fonts: files go in chunks (the shm ring is 1 MB), each file at most 8 MB, 32 MB in all
constexpr size_t kProtocolMaxFontBytes = size_t(8) << 20;
constexpr size_t kProtocolMaxFontTotal = size_t(32) << 20;
constexpr size_t kProtocolMaxFontFiles = 16;
constexpr size_t kProtocolMaxFonts = 32;
constexpr size_t kProtocolFontChunk = size_t(192) << 10;
constexpr size_t kProtocolFontHashHex = 64;
// icon names ("fa-gear") to code points, for {icon:...} text and icon props
constexpr size_t kProtocolMaxIcons = 8192;
constexpr size_t kProtocolMaxIconName = 64;

inline bool Unframe(const void* data, size_t len, Json& out) {
    if (len > kProtocolMaxPayload) return false;
    if (!ParseJson((const char*)data, len, out)) return false;
    return out.IsObject() && out["t"].IsString();
}

inline bool Unframe(const std::vector<unsigned char>& payload, Json& out) {
    return Unframe((const void*)payload.data(), payload.size(), out);
}

// for peer text going into the UI/log: no control chars (ESC included), clipped without splitting UTF-8
inline std::string SanitizeText(const std::string& s, size_t max_len = kProtocolMaxLogText) {
    std::string out;
    out.reserve(s.size() < max_len ? s.size() : max_len);
    for (size_t i = 0; i < s.size() && out.size() < max_len; ++i) {
        unsigned char c = (unsigned char)s[i];
        if (c == '\t' || c == '\n' || c == '\r') out.push_back(' ');
        else if (c < 0x20 || c == 0x7F) out.push_back('?');
        else out.push_back((char)c);
    }
    if (s.size() > max_len) {
        size_t n = out.size();
        while (n > 0 && ((unsigned char)out[n - 1] & 0xC0) == 0x80) --n;
        if (n > 0 && ((unsigned char)out[n - 1] & 0xC0) == 0xC0) --n;
        out.resize(n);
    }
    return out;
}

inline const char* MsgType(const Json& msg) { return msg.IsObject() ? msg["t"].AsString().c_str() : ""; }

namespace msg {
constexpr const char* kHello = "hello";
constexpr const char* kBlocks = "blocks";
constexpr const char* kSelect = "select";
constexpr const char* kHover = "hover";
constexpr const char* kLog = "log";
constexpr const char* kStats = "stats";
constexpr const char* kPong = "pong";
constexpr const char* kProject = "project";
constexpr const char* kPanel = "panel";
constexpr const char* kComponents = "components";
constexpr const char* kWidgets = "widgets";
constexpr const char* kRemovePanel = "remove_panel";
constexpr const char* kTheme = "theme";
constexpr const char* kMode = "mode";
constexpr const char* kPing = "ping";
constexpr const char* kPairChallenge = "pair_challenge";
constexpr const char* kPairResponse = "pair_response";
constexpr const char* kPairOk = "pair_ok";
constexpr const char* kPairFail = "pair_fail";
constexpr const char* kClientInfo = "client_info";
constexpr const char* kAnimations = "animations";
constexpr const char* kFontData = "font_data";
constexpr const char* kFonts = "fonts";
constexpr const char* kIcons = "icons";
}  // namespace msg

// v2: hello is just {t, v, pairing, nonce_a}, app metadata moved to the sealed client_info
constexpr int kProtocolVersion = 2;

constexpr size_t kPairNonceHex = 32;
constexpr size_t kPairMacHex = 64;

inline bool IsMsg(const Json& m, const char* type) { return m.IsObject() && m["t"].IsString() && m["t"].AsString() == type; }

namespace detail {
inline Json NewMsg(const char* type) {
    Json m = Json::Object();
    m.Set("t", type);
    return m;
}
inline Json StringArray(const std::vector<std::string>& v) {
    Json a = Json::Array();
    for (const std::string& s : v) a.Push(s);
    return a;
}
inline std::string Clip(const std::string& s, size_t max_len = kProtocolMaxName) {
    return s.size() > max_len ? s.substr(0, max_len) : s;
}
inline void ReadStringArray(const Json& a, std::vector<std::string>& out) {
    out.clear();
    if (!a.IsArray()) return;
    for (size_t i = 0; i < a.Size() && out.size() < kProtocolMaxList; ++i) {
        if (a[i].IsString()) out.push_back(Clip(a[i].AsString()));
    }
}
inline Json JsonArray(const std::vector<Json>& v) {
    Json a = Json::Array();
    for (const Json& j : v) a.Push(j);
    return a;
}
inline bool IsLowerHexField(const Json& v, size_t chars) {
    if (!v.IsString() || v.AsString().size() != chars) return false;
    for (char c : v.AsString())
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    return true;
}
inline std::string Base64Encode(const unsigned char* d, size_t n) {
    static const char kAlpha[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((n + 2) / 3 * 4);
    for (size_t i = 0; i < n; i += 3) {
        unsigned v = unsigned(d[i]) << 16;
        if (i + 1 < n) v |= unsigned(d[i + 1]) << 8;
        if (i + 2 < n) v |= d[i + 2];
        out.push_back(kAlpha[(v >> 18) & 63]);
        out.push_back(kAlpha[(v >> 12) & 63]);
        out.push_back(i + 1 < n ? kAlpha[(v >> 6) & 63] : '=');
        out.push_back(i + 2 < n ? kAlpha[v & 63] : '=');
    }
    return out;
}
// strict: no whitespace, padding only at the end
inline bool Base64Decode(const std::string& s, std::vector<unsigned char>& out) {
    out.clear();
    if (s.size() % 4) return false;
    out.reserve(s.size() / 4 * 3);
    for (size_t i = 0; i < s.size(); i += 4) {
        unsigned v = 0;
        int pad = 0;
        for (size_t k = 0; k < 4; ++k) {
            char c = s[i + k];
            int x = c >= 'A' && c <= 'Z' ? c - 'A' : c >= 'a' && c <= 'z' ? c - 'a' + 26 : c >= '0' && c <= '9' ? c - '0' + 52
                    : c == '+' ? 62 : c == '/' ? 63 : -1;
            if (c == '=' && i + 4 == s.size() && k >= 2) {
                ++pad;
                x = 0;
            } else if (x < 0 || pad) {
                return false;
            }
            v = (v << 6) | unsigned(x);
        }
        out.push_back((unsigned char)(v >> 16));
        if (pad < 2) out.push_back((unsigned char)(v >> 8));
        if (pad < 1) out.push_back((unsigned char)v);
    }
    return true;
}
}  // namespace detail

struct BlockHeight {
    std::string id;
    std::string h;
};

// pairing hello or legacy hello with app metadata
struct HelloMsg {
    std::string app;
    int pid = 0;
    int imgui_version = 0;
    std::string backend;
    std::string platform;
    std::vector<std::string> widgets;
    std::vector<std::string> textures;
    bool pairing = false;
    int version = 0;
    std::string nonce_a;
};

// first sealed message after pair_ok, so only an authenticated builder sees the metadata
struct ClientInfoMsg {
    std::string app;
    int pid = 0;
    int imgui_version = 0;
    std::string backend;
    std::string platform;
    std::vector<std::string> widgets;
    std::vector<std::string> textures;
    std::vector<std::string> font_hashes;
};

// one project font as the app should load it; file/icons are sha256 hex of font_data files
struct FontSpecMsg {
    std::string name;
    float size = 16.0f;
    std::string ranges;
    std::string merge_icons;
    std::string file;
    std::string icons;
    unsigned icon_lo = 0, icon_hi = 0;
};

struct FontDataMsg {
    std::string hash;
    size_t total = 0;
    size_t offset = 0;
    std::vector<unsigned char> bytes;
};

struct PairChallengeMsg {
    std::string nonce_b, session_id, mac_b;
};

struct PairResponseMsg {
    std::string session_id, mac_a;
};

struct PairFailMsg {
    int attempts_left = 0;
    std::string reason;
};

struct BlocksMsg {
    std::string panel;
    std::vector<BlockHeight> blocks;
};

struct TargetMsg {
    std::string panel;
    std::string id;
};

struct LogMsg {
    int level = 0;
    std::string msg;
};

struct StatsMsg {
    float fps = 0;
    std::vector<std::string> panels;
};

struct ModeMsg {
    bool picker = false;
    std::string selected;
    std::string panel;
};

// old clients, only accepted when the builder doesn't require pairing
inline Json MsgHello(const char* app, int pid, int imgui_version, const char* backend, const char* platform,
                     const std::vector<std::string>& widgets, const std::vector<std::string>& textures) {
    Json m = detail::NewMsg(msg::kHello);
    m.Set("app", app ? app : "");
    m.Set("pid", pid);
    m.Set("imgui_version", imgui_version);
    m.Set("backend", backend ? backend : "");
    m.Set("platform", platform ? platform : "");
    m.Set("widgets", detail::StringArray(widgets));
    m.Set("textures", detail::StringArray(textures));
    return m;
}

// nothing but the version and nonce before authentication
inline Json MsgPairingHello(const std::string& nonce_a) {
    Json m = detail::NewMsg(msg::kHello);
    m.Set("v", kProtocolVersion);
    m.Set("pairing", true);
    m.Set("nonce_a", nonce_a);
    return m;
}

// font_hashes: font files the app already holds from an earlier connection, so they aren't sent again
inline Json MsgClientInfo(const char* app, int pid, int imgui_version, const char* backend, const char* platform,
                          const std::vector<std::string>& widgets, const std::vector<std::string>& textures,
                          const std::vector<std::string>& font_hashes = std::vector<std::string>()) {
    Json m = detail::NewMsg(msg::kClientInfo);
    m.Set("app", app ? app : "");
    m.Set("pid", pid);
    m.Set("imgui_version", imgui_version);
    m.Set("backend", backend ? backend : "");
    m.Set("platform", platform ? platform : "");
    m.Set("widgets", detail::StringArray(widgets));
    m.Set("textures", detail::StringArray(textures));
    if (!font_hashes.empty()) m.Set("fonts", detail::StringArray(font_hashes));
    return m;
}

inline Json MsgFontData(const std::string& hash, size_t total, size_t offset, const unsigned char* data, size_t n) {
    Json m = detail::NewMsg(msg::kFontData);
    m.Set("hash", hash);
    m.Set("total", (int64_t)total);
    m.Set("offset", (int64_t)offset);
    m.Set("data", detail::Base64Encode(data, n));
    return m;
}

inline Json MsgFonts(const std::vector<FontSpecMsg>& fonts) {
    Json m = detail::NewMsg(msg::kFonts);
    Json arr = Json::Array();
    for (const FontSpecMsg& f : fonts) {
        Json e = Json::Object();
        e.Set("name", f.name);
        e.Set("size", (double)f.size);
        e.Set("ranges", f.ranges);
        e.Set("mergeIcons", f.merge_icons);
        e.Set("file", f.file);
        e.Set("icons", f.icons);
        e.Set("iconLo", (int64_t)f.icon_lo);
        e.Set("iconHi", (int64_t)f.icon_hi);
        arr.Push(e);
    }
    m.Set("fonts", arr);
    return m;
}

inline Json MsgPairChallenge(const std::string& nonce_b, const std::string& session_id, const std::string& mac_b) {
    Json m = detail::NewMsg(msg::kPairChallenge);
    m.Set("nonce_b", nonce_b);
    m.Set("session_id", session_id);
    m.Set("mac_b", mac_b);
    return m;
}

inline Json MsgPairResponse(const std::string& session_id, const std::string& mac_a) {
    Json m = detail::NewMsg(msg::kPairResponse);
    m.Set("session_id", session_id);
    m.Set("mac_a", mac_a);
    return m;
}

inline Json MsgPairOk(const std::string& session_id) {
    Json m = detail::NewMsg(msg::kPairOk);
    m.Set("session_id", session_id);
    return m;
}

inline Json MsgPairFail(int attempts_left, const char* reason) {
    Json m = detail::NewMsg(msg::kPairFail);
    m.Set("attempts_left", attempts_left < 0 ? 0 : attempts_left);
    m.Set("reason", reason ? reason : "");
    return m;
}

inline Json MsgBlocks(const char* panel, const std::vector<BlockHeight>& blocks) {
    Json m = detail::NewMsg(msg::kBlocks);
    m.Set("panel", panel ? panel : "");
    Json arr = Json::Array();
    for (const BlockHeight& b : blocks) {
        Json e = Json::Object();
        e.Set("id", b.id);
        e.Set("h", b.h);
        arr.Push(e);
    }
    m.Set("blocks", arr);
    return m;
}

inline Json MsgSelect(const char* panel, const char* id) {
    Json m = detail::NewMsg(msg::kSelect);
    m.Set("panel", panel ? panel : "");
    m.Set("id", id ? id : "");
    return m;
}

inline Json MsgHover(const char* panel, const char* id) {
    Json m = detail::NewMsg(msg::kHover);
    m.Set("panel", panel ? panel : "");
    m.Set("id", id ? id : "");
    return m;
}

inline Json MsgLog(int level, const char* text) {
    Json m = detail::NewMsg(msg::kLog);
    m.Set("level", level);
    m.Set("msg", text ? text : "");
    return m;
}

inline Json MsgStats(float fps, const std::vector<std::string>& panels) {
    Json m = detail::NewMsg(msg::kStats);
    m.Set("fps", (double)fps);
    m.Set("panels", detail::StringArray(panels));
    return m;
}

inline Json MsgPong() { return detail::NewMsg(msg::kPong); }

inline Json MsgProject(Json project) {
    Json m = detail::NewMsg(msg::kProject);
    m.Set("project", std::move(project));
    return m;
}

inline Json MsgPanel(Json panel) {
    Json m = detail::NewMsg(msg::kPanel);
    m.Set("panel", std::move(panel));
    return m;
}

inline Json MsgComponents(const std::vector<Json>& components) {
    Json m = detail::NewMsg(msg::kComponents);
    m.Set("components", detail::JsonArray(components));
    return m;
}

inline Json MsgWidgets(const std::vector<Json>& manifests) {
    Json m = detail::NewMsg(msg::kWidgets);
    m.Set("manifests", detail::JsonArray(manifests));
    return m;
}

inline Json MsgAnimations(const std::vector<Json>& animations) {
    Json m = detail::NewMsg(msg::kAnimations);
    m.Set("animations", detail::JsonArray(animations));
    return m;
}

inline Json MsgRemovePanel(const char* name) {
    Json m = detail::NewMsg(msg::kRemovePanel);
    m.Set("name", name ? name : "");
    return m;
}

inline Json MsgTheme(Json theme) {
    Json m = detail::NewMsg(msg::kTheme);
    m.Set("theme", std::move(theme));
    return m;
}

inline Json MsgMode(bool picker, const char* selected, const char* panel) {
    Json m = detail::NewMsg(msg::kMode);
    m.Set("picker", picker);
    m.Set("selected", selected ? selected : "");
    m.Set("panel", panel ? panel : "");
    return m;
}

inline Json MsgPing() { return detail::NewMsg(msg::kPing); }

inline bool ParseHello(const Json& m, HelloMsg& out) {
    if (!IsMsg(m, msg::kHello)) return false;
    out.pairing = m["pairing"].IsBool() && m["pairing"].AsBool();
    if (!out.pairing && !m["app"].IsString()) return false;
    out.app = detail::Clip(m["app"].AsString());
    out.pid = m["pid"].AsInt();
    out.imgui_version = m["imgui_version"].AsInt();
    out.backend = detail::Clip(m["backend"].AsString());
    out.platform = detail::Clip(m["platform"].AsString());
    detail::ReadStringArray(m["widgets"], out.widgets);
    detail::ReadStringArray(m["textures"], out.textures);
    out.version = m["v"].IsNumber() ? m["v"].AsInt() : 0;
    out.nonce_a = detail::IsLowerHexField(m["nonce_a"], kPairNonceHex) ? m["nonce_a"].AsString() : std::string();
    return true;
}

inline bool ParseClientInfo(const Json& m, ClientInfoMsg& out) {
    if (!IsMsg(m, msg::kClientInfo) || !m["app"].IsString()) return false;
    out.app = detail::Clip(m["app"].AsString());
    out.pid = m["pid"].AsInt();
    out.imgui_version = m["imgui_version"].AsInt();
    out.backend = detail::Clip(m["backend"].AsString());
    out.platform = detail::Clip(m["platform"].AsString());
    detail::ReadStringArray(m["widgets"], out.widgets);
    detail::ReadStringArray(m["textures"], out.textures);
    out.font_hashes.clear();
    const Json& fonts = m["fonts"];
    for (size_t i = 0; fonts.IsArray() && i < fonts.Size() && out.font_hashes.size() < kProtocolMaxFontFiles; ++i)
        if (detail::IsLowerHexField(fonts[i], kProtocolFontHashHex)) out.font_hashes.push_back(fonts[i].AsString());
    return true;
}

// a chunk must fit the file it claims to belong to, and the file must fit the cap
inline bool ParseFontData(const Json& m, FontDataMsg& out) {
    if (!IsMsg(m, msg::kFontData) || !detail::IsLowerHexField(m["hash"], kProtocolFontHashHex) || !m["total"].IsNumber() ||
        !m["offset"].IsNumber() || !m["data"].IsString())
        return false;
    double total = m["total"].AsDouble(), offset = m["offset"].AsDouble();
    if (!(total >= 1 && total <= (double)kProtocolMaxFontBytes && offset >= 0 && offset < total)) return false;
    if (total != (double)(size_t)total || offset != (double)(size_t)offset) return false;
    if (m["data"].AsString().size() > (kProtocolFontChunk + 2) / 3 * 4) return false;
    out.hash = m["hash"].AsString();
    out.total = (size_t)total;
    out.offset = (size_t)offset;
    if (!detail::Base64Decode(m["data"].AsString(), out.bytes) || out.bytes.empty()) return false;
    return out.bytes.size() <= out.total - out.offset;
}

inline bool ParseFonts(const Json& m, std::vector<FontSpecMsg>& out) {
    out.clear();
    if (!IsMsg(m, msg::kFonts) || !m["fonts"].IsArray() || m["fonts"].Size() > kProtocolMaxFonts) return false;
    const Json& arr = m["fonts"];
    for (size_t i = 0; i < arr.Size(); ++i) {
        const Json& e = arr[i];
        if (!e.IsObject() || !e["name"].IsString() || e["name"].AsString().empty() || !e["size"].IsNumber() ||
            !detail::IsLowerHexField(e["file"], kProtocolFontHashHex))
            return false;
        FontSpecMsg f;
        f.name = SanitizeText(e["name"].AsString(), kProtocolMaxName);
        double size = e["size"].AsDouble();
        if (!(size >= 4.0 && size <= 256.0)) return false;
        f.size = (float)size;
        f.ranges = detail::Clip(e["ranges"].AsString(), 64);
        f.merge_icons = detail::Clip(e["mergeIcons"].AsString(), 64);
        f.file = e["file"].AsString();
        if (e["icons"].IsString() && !e["icons"].AsString().empty()) {
            if (!detail::IsLowerHexField(e["icons"], kProtocolFontHashHex)) return false;
            f.icons = e["icons"].AsString();
            double lo = e["iconLo"].AsDouble(), hi = e["iconHi"].AsDouble();
            if (!(lo >= 0x20 && hi >= lo && hi <= 0x10FFFF)) return false;
            f.icon_lo = (unsigned)lo;
            f.icon_hi = (unsigned)hi;
        }
        out.push_back(f);
    }
    return true;
}

inline Json MsgIcons(const std::vector<std::pair<std::string, unsigned>>& icons) {
    Json m = detail::NewMsg(msg::kIcons);
    Json map = Json::Object();
    for (const auto& kv : icons) map.Set(kv.first, (int64_t)kv.second);
    m.Set("icons", map);
    return m;
}

// names are lowercase letters, digits and '-', code points printable and not surrogates
inline bool ParseIcons(const Json& m, std::vector<std::pair<std::string, unsigned>>& out) {
    out.clear();
    if (!IsMsg(m, msg::kIcons) || !m["icons"].IsObject() || m["icons"].obj.size() > kProtocolMaxIcons) return false;
    for (const auto& kv : m["icons"].obj) {
        const std::string& name = kv.first;
        if (name.empty() || name.size() > kProtocolMaxIconName || !kv.second.IsNumber()) return false;
        for (char c : name)
            if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-')) return false;
        double cp = kv.second.AsDouble();
        if (!(cp >= 0x20 && cp <= 0x10FFFF) || cp != (double)(unsigned)cp || (cp >= 0xD800 && cp <= 0xDFFF)) return false;
        if (cp >= 0x7F && cp <= 0x9F) return false;
        out.emplace_back(name, (unsigned)cp);
    }
    return true;
}

inline bool ParsePairChallenge(const Json& m, PairChallengeMsg& out) {
    if (!IsMsg(m, msg::kPairChallenge) || !detail::IsLowerHexField(m["nonce_b"], kPairNonceHex) ||
        !detail::IsLowerHexField(m["session_id"], kPairNonceHex) || !detail::IsLowerHexField(m["mac_b"], kPairMacHex))
        return false;
    out.nonce_b = m["nonce_b"].AsString();
    out.session_id = m["session_id"].AsString();
    out.mac_b = m["mac_b"].AsString();
    return true;
}

inline bool ParsePairResponse(const Json& m, PairResponseMsg& out) {
    if (!IsMsg(m, msg::kPairResponse) || !detail::IsLowerHexField(m["session_id"], kPairNonceHex) ||
        !detail::IsLowerHexField(m["mac_a"], kPairMacHex))
        return false;
    out.session_id = m["session_id"].AsString();
    out.mac_a = m["mac_a"].AsString();
    return true;
}

inline bool ParsePairOk(const Json& m, std::string& session_id) {
    if (!IsMsg(m, msg::kPairOk) || !detail::IsLowerHexField(m["session_id"], kPairNonceHex)) return false;
    session_id = m["session_id"].AsString();
    return true;
}

inline bool ParsePairFail(const Json& m, PairFailMsg& out) {
    if (!IsMsg(m, msg::kPairFail)) return false;
    int left = m["attempts_left"].AsInt();
    out.attempts_left = left < 0 ? 0 : left > 1000 ? 1000 : left;
    out.reason = SanitizeText(m["reason"].AsString(), kProtocolMaxName);
    return true;
}

inline bool ParseBlocks(const Json& m, BlocksMsg& out) {
    if (!IsMsg(m, msg::kBlocks) || !m["panel"].IsString() || !m["blocks"].IsArray()) return false;
    out.panel = detail::Clip(m["panel"].AsString());
    out.blocks.clear();
    const Json& arr = m["blocks"];
    if (arr.Size() > kProtocolMaxBlocks) return false;
    for (size_t i = 0; i < arr.Size(); ++i) {
        const Json& e = arr[i];
        if (!e.IsObject() || !e["id"].IsString()) return false;
        BlockHeight b;
        b.id = detail::Clip(e["id"].AsString());
        if (e["h"].IsString()) b.h = detail::Clip(e["h"].AsString());
        else if (e["h"].IsNumber()) b.h = std::to_string(e["h"].AsInt64());
        out.blocks.push_back(b);
    }
    return true;
}

inline bool ParseTarget(const Json& m, const char* type, TargetMsg& out) {
    if (!IsMsg(m, type) || !m["panel"].IsString() || !m["id"].IsString()) return false;
    if (m["panel"].AsString().size() > kProtocolMaxName || m["id"].AsString().size() > kProtocolMaxName) return false;
    out.panel = m["panel"].AsString();
    out.id = m["id"].AsString();
    return true;
}

inline bool ParseSelect(const Json& m, TargetMsg& out) { return ParseTarget(m, msg::kSelect, out); }
inline bool ParseHover(const Json& m, TargetMsg& out) { return ParseTarget(m, msg::kHover, out); }

inline bool ParseLog(const Json& m, LogMsg& out) {
    if (!IsMsg(m, msg::kLog) || !m["msg"].IsString()) return false;
    out.level = m["level"].AsInt();
    out.msg = detail::Clip(m["msg"].AsString(), kProtocolMaxLogText);
    return true;
}

inline bool ParseStats(const Json& m, StatsMsg& out) {
    if (!IsMsg(m, msg::kStats) || !m["fps"].IsNumber()) return false;
    out.fps = (float)m["fps"].AsDouble();
    detail::ReadStringArray(m["panels"], out.panels);
    return true;
}

inline bool ParseProject(const Json& m, Json& project) {
    if (!IsMsg(m, msg::kProject) || !m["project"].IsObject()) return false;
    project = m["project"];
    return true;
}

inline bool ParsePanel(const Json& m, Json& panel) {
    if (!IsMsg(m, msg::kPanel) || !m["panel"].IsObject()) return false;
    panel = m["panel"];
    return true;
}

inline bool ParseJsonList(const Json& m, const char* type, const char* key, std::vector<Json>& out) {
    if (!IsMsg(m, type) || !m[key].IsArray() || m[key].Size() > kProtocolMaxList) return false;
    out.clear();
    const Json& arr = m[key];
    for (size_t i = 0; i < arr.Size(); ++i) out.push_back(arr[i]);
    return true;
}

inline bool ParseComponents(const Json& m, std::vector<Json>& components) {
    return ParseJsonList(m, msg::kComponents, "components", components);
}

inline bool ParseWidgets(const Json& m, std::vector<Json>& manifests) {
    return ParseJsonList(m, msg::kWidgets, "manifests", manifests);
}

// one AnimJson entry: named object, at most 32 tracks of at most 64 keyframes each
inline bool AnimationWithinCaps(const Json& a) {
    if (!a.IsObject() || !a["name"].IsString() || a["name"].AsString().empty() ||
        a["name"].AsString().size() > kProtocolMaxName)
        return false;
    for (const char* key : {"description", "credit"})
        if (a[key].IsString() && a[key].AsString().size() > kProtocolMaxAnimText) return false;
    if (a["tags"].IsArray()) {
        if (a["tags"].Size() > kProtocolMaxAnimTags) return false;
        for (size_t i = 0; i < a["tags"].Size(); ++i)
            if (a["tags"][i].IsString() && a["tags"][i].AsString().size() > kProtocolMaxName) return false;
    }
    const Json& tracks = a["tracks"];
    if (tracks.IsNull()) return true;
    if (!tracks.IsObject() || tracks.obj.size() > kProtocolMaxAnimTracks) return false;
    for (const auto& kv : tracks.obj)
        if (!kv.second.IsArray() || kv.second.Size() > kProtocolMaxAnimKeys) return false;
    return true;
}

// the whole message is refused past 2048 entries; entries over the per-track cap are dropped and counted
inline bool ParseAnimations(const Json& m, std::vector<Json>& out, size_t* dropped = nullptr) {
    if (dropped) *dropped = 0;
    if (!IsMsg(m, msg::kAnimations) || !m["animations"].IsArray() || m["animations"].Size() > kProtocolMaxAnimations)
        return false;
    out.clear();
    const Json& arr = m["animations"];
    for (size_t i = 0; i < arr.Size(); ++i) {
        if (AnimationWithinCaps(arr[i])) out.push_back(arr[i]);
        else if (dropped) ++*dropped;
    }
    return true;
}

inline bool ParseRemovePanel(const Json& m, std::string& name) {
    if (!IsMsg(m, msg::kRemovePanel) || !m["name"].IsString()) return false;
    name = detail::Clip(m["name"].AsString());
    return true;
}

inline bool ParseTheme(const Json& m, Json& theme) {
    if (!IsMsg(m, msg::kTheme) || !m["theme"].IsObject()) return false;
    theme = m["theme"];
    return true;
}

inline bool ParseMode(const Json& m, ModeMsg& out) {
    if (!IsMsg(m, msg::kMode) || !m["picker"].IsBool()) return false;
    out.picker = m["picker"].AsBool();
    out.selected = detail::Clip(m["selected"].AsString());
    out.panel = detail::Clip(m["panel"].AsString());
    return true;
}

}  // namespace imforge
