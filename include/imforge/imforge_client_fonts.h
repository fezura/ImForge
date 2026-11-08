#pragma once

// project fonts pushed by the builder, part of imforge_client_detail.h. Font files arrive as font_data
// chunks (sha256 named, checked, kept for the life of the app), the list as a fonts message. Nothing
// touches the atlas until PreNewFrame(), since 1.91 and older lock it between NewFrame and Render.

#include "imgui.h"
#include "imforge_font_check.h"
#include "imforge_pairing.h"
#include "imforge_protocol.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace imforge {
namespace live {
namespace client_detail {

// fonts are never removed from the atlas (the app may hold the pointers), so this caps the growth
constexpr size_t kMaxLiveFonts = 64;
constexpr uint64_t kPreNewFrameGraceMs = 2000;
// atlas pixels all live fonts together may ask for, about an 8192x4096 texture. Matters before 1.92, where
// the whole range is rasterized up front; a 256 px CJK font would be gigabytes
constexpr uint64_t kMaxLiveAtlasPixels = uint64_t(32) << 20;

using FontLogs = std::vector<std::pair<int, std::string>>;

struct LiveFont {
    FontSpecMsg spec;
    ImFont* font = nullptr;
    bool failed = false;
    // the atlas keeps pointers to these, LiveFont objects are heap allocated and never freed
    ImWchar base_ranges[3] = {0, 0, 0};
    ImWchar icon_ranges[3] = {0, 0, 0};
};

struct FontState {
    std::map<std::string, std::vector<unsigned char>> files;
    size_t file_bytes = 0;
    uint64_t atlas_pixels = 0;
    std::string part_hash, skip_hash;
    std::vector<unsigned char> part;
    size_t part_total = 0;
    std::vector<FontSpecMsg> specs;
    std::vector<std::unique_ptr<LiveFont>> loaded;
    std::map<std::string, std::string> icons;
    bool pending = false;
    uint64_t pending_since_ms = 0;
    bool pre_new_frame_seen = false;
    bool warned_no_pre_new_frame = false;
    bool warned_no_rebuild = false;
    bool warned_locked = false;
    bool warned_full = false;
    void (*rebuild_texture)() = nullptr;
};

inline std::string FontHash(const unsigned char* d, size_t n) {
    unsigned char out[32];
    pairing::Sha256 h;
    h.Update(d, n);
    h.Final(out);
    return pairing::ToHex(out, sizeof(out));
}

inline std::vector<std::string> FontFileHashes(const FontState& s) {
    std::vector<std::string> out;
    for (const auto& kv : s.files) out.push_back(kv.first);
    return out;
}

inline bool SameFontSpec(const FontSpecMsg& a, const FontSpecMsg& b) {
    return a.name == b.name && a.size == b.size && a.ranges == b.ranges && a.file == b.file && a.icons == b.icons &&
           a.icon_lo == b.icon_lo && a.icon_hi == b.icon_hi;
}

inline LiveFont* FindLoadedFont(FontState& s, const FontSpecMsg& spec) {
    for (size_t i = s.loaded.size(); i-- > 0;)
        if (SameFontSpec(s.loaded[i]->spec, spec)) return s.loaded[i].get();
    return nullptr;
}

inline bool FontInAtlas(const ImFont* f) {
    if (!f || !ImGui::GetCurrentContext()) return false;
    const ImFontAtlas* atlas = ImGui::GetIO().Fonts;
    for (int i = 0; atlas && i < atlas->Fonts.Size; ++i)
        if (atlas->Fonts[i] == f) return true;
    return false;
}

inline ImFont* FindLiveFont(FontState& s, const std::string& name) {
    for (const FontSpecMsg& spec : s.specs) {
        if (spec.name != name) continue;
        LiveFont* lf = FindLoadedFont(s, spec);
        if (!lf || !lf->font) return nullptr;
        if (FontInAtlas(lf->font)) return lf->font;
        // the app cleared the atlas, load it again on the next PreNewFrame
        lf->font = nullptr;
        s.pending = true;
        return nullptr;
    }
    return nullptr;
}

// what generated code sets as io.FontDefault: the first project font
inline const std::string* DefaultLiveFontName(const FontState& s) {
    return s.specs.empty() ? nullptr : &s.specs[0].name;
}

inline void DropFontPart(FontState& s) {
    s.part_hash.clear();
    s.part.clear();
    s.part.shrink_to_fit();
    s.part_total = 0;
}

inline void OnFontData(FontState& s, const Json& m, uint64_t now, FontLogs& logs) {
    FontDataMsg d;
    if (!ParseFontData(m, d)) {
        DropFontPart(s);
        logs.emplace_back(2, "imforge: malformed font_data dropped");
        return;
    }
    if (d.offset == 0) {
        DropFontPart(s);
        s.skip_hash.clear();
        if (s.files.count(d.hash)) {
            s.skip_hash = d.hash;
            return;
        }
        if (s.files.size() >= kProtocolMaxFontFiles || s.file_bytes + d.total > kProtocolMaxFontTotal) {
            s.skip_hash = d.hash;
            logs.emplace_back(2, "imforge: font file dropped, the app already holds the maximum number of font files");
            return;
        }
        s.part_hash = d.hash;
        s.part_total = d.total;
        s.part.reserve(d.total);
    } else if (d.hash != s.part_hash || d.offset != s.part.size() || d.total != s.part_total) {
        if (d.hash != s.skip_hash) logs.emplace_back(2, "imforge: font_data chunk out of order, file dropped");
        if (d.hash == s.part_hash) DropFontPart(s);
        return;
    }
    s.part.insert(s.part.end(), d.bytes.begin(), d.bytes.end());
    if (s.part.size() < s.part_total) return;
    std::string why;
    if (FontHash(s.part.data(), s.part.size()) != s.part_hash) {
        logs.emplace_back(2, "imforge: font file failed its hash check, dropped");
    } else if (!fontcheck::PrepareFontData(s.part, &why)) {
        logs.emplace_back(2, "imforge: font file rejected: " + why);
    } else {
        s.file_bytes += s.part.size();
        s.files[s.part_hash].swap(s.part);
        if (!s.pending) s.pending_since_ms = now;
        s.pending = true;
    }
    DropFontPart(s);
}

inline void OnFontList(FontState& s, const Json& m, uint64_t now, FontLogs& logs) {
    std::vector<FontSpecMsg> specs;
    if (!ParseFonts(m, specs)) {
        logs.emplace_back(2, "imforge: malformed fonts message dropped");
        return;
    }
    s.specs.swap(specs);
    if (!s.pending) s.pending_since_ms = now;
    s.pending = !s.specs.empty();
}

inline std::string Utf8Of(unsigned cp) {
    std::string s;
    if (cp < 0x80) {
        s += (char)cp;
    } else if (cp < 0x800) {
        s += (char)(0xC0 | (cp >> 6));
        s += (char)(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        s += (char)(0xE0 | (cp >> 12));
        s += (char)(0x80 | ((cp >> 6) & 0x3F));
        s += (char)(0x80 | (cp & 0x3F));
    } else {
        s += (char)(0xF0 | (cp >> 18));
        s += (char)(0x80 | ((cp >> 12) & 0x3F));
        s += (char)(0x80 | ((cp >> 6) & 0x3F));
        s += (char)(0x80 | (cp & 0x3F));
    }
    return s;
}

inline void OnIconList(FontState& s, const Json& m, FontLogs& logs) {
    std::vector<std::pair<std::string, unsigned>> icons;
    if (!ParseIcons(m, icons)) {
        logs.emplace_back(2, "imforge: malformed icons message dropped");
        return;
    }
    s.icons.clear();
    for (const auto& kv : icons) s.icons[kv.first] = Utf8Of(kv.second);
}

inline const char* FindIconUtf8(const FontState& s, const std::string& name) {
    auto it = s.icons.find(name);
    return it == s.icons.end() ? nullptr : it->second.c_str();
}

inline ImWchar ClampFontChar(unsigned cp) {
    if (sizeof(ImWchar) == 2 && cp > 0xFFFF) return (ImWchar)0xFFFF;
    return (ImWchar)cp;
}

#if IMGUI_VERSION_NUM < 19200
// 1.92 loads glyphs on demand and hides these helpers under IMGUI_DISABLE_OBSOLETE_FUNCTIONS
inline const ImWchar* LiveGlyphRanges(const std::string& r, ImFontAtlas* atlas, LiveFont& lf) {
    static const ImWchar latin_ext[] = {0x0020, 0x00FF, 0x0100, 0x017F, 0x0180, 0x024F, 0x2000,
                                        0x206F, 0x20A0, 0x20CF, 0x2100, 0x214F, 0};
    if (r == "latin-ext") return latin_ext;
#if IMGUI_VERSION_NUM >= 18800
    if (r == "greek") return atlas->GetGlyphRangesGreek();
#endif
    if (r == "cyrillic") return atlas->GetGlyphRangesCyrillic();
    if (r == "chinese-full" || r == "chinese" || r == "chineseFull") return atlas->GetGlyphRangesChineseFull();
    if (r == "chinese-simplified" || r == "chineseSimplified") return atlas->GetGlyphRangesChineseSimplifiedCommon();
    if (r == "japanese") return atlas->GetGlyphRangesJapanese();
    if (r == "korean") return atlas->GetGlyphRangesKorean();
    if (r == "thai") return atlas->GetGlyphRangesThai();
    if (r == "vietnamese") return atlas->GetGlyphRangesVietnamese();
    if (r == "icons" && lf.spec.icon_hi) {
        lf.base_ranges[0] = ClampFontChar(lf.spec.icon_lo);
        lf.base_ranges[1] = ClampFontChar(lf.spec.icon_hi);
        return lf.base_ranges;
    }
    return atlas->GetGlyphRangesDefault();
}
#endif

inline void* CopyForAtlas(const std::vector<unsigned char>& v) {
    void* p = IM_ALLOC(v.size());
    std::memcpy(p, v.data(), v.size());
    return p;
}

// same settings the builder canvas uses: icons at 85% size, at least one em wide, 1px lower
inline ImFont* AddLiveFont(ImFontAtlas* atlas, const FontState& s, LiveFont& lf) {
    const FontSpecMsg& spec = lf.spec;
    const std::vector<unsigned char>& base = s.files.find(spec.file)->second;
    ImFontConfig cfg;
    cfg.OversampleH = 2;
    cfg.FontDataOwnedByAtlas = true;
    std::snprintf(cfg.Name, sizeof(cfg.Name), "%s %.0fpx", spec.name.c_str(), (double)spec.size);
#if IMGUI_VERSION_NUM >= 19200
    ImFont* f = atlas->AddFontFromMemoryTTF(CopyForAtlas(base), (int)base.size(), spec.size, &cfg);
#else
    ImFont* f = atlas->AddFontFromMemoryTTF(CopyForAtlas(base), (int)base.size(), spec.size, &cfg,
                                            LiveGlyphRanges(spec.ranges, atlas, lf));
#endif
    if (!f || spec.icons.empty()) return f;
    const std::vector<unsigned char>& icons = s.files.find(spec.icons)->second;
    lf.icon_ranges[0] = ClampFontChar(spec.icon_lo);
    lf.icon_ranges[1] = ClampFontChar(spec.icon_hi);
    ImFontConfig ic;
    ic.MergeMode = true;
    ic.PixelSnapH = true;
    ic.GlyphMinAdvanceX = spec.size;
    ic.GlyphOffset = ImVec2(0.0f, 1.0f);
    ic.FontDataOwnedByAtlas = true;
    std::snprintf(ic.Name, sizeof(ic.Name), "%s icons", spec.name.c_str());
    atlas->AddFontFromMemoryTTF(CopyForAtlas(icons), (int)icons.size(), spec.size * 0.85f, &ic, lf.icon_ranges);
    return f;
}

#if IMGUI_VERSION_NUM < 19200
// a failed Build() leaves the real atlas unbuilt and NewFrame asserts, so try each font on its own first
inline bool TrialBuildFont(const FontState& s, const LiveFont& lf) {
    LiveFont probe;
    probe.spec = lf.spec;
    ImFontAtlas tmp;
    return AddLiveFont(&tmp, s, probe) && tmp.Build();
}
#endif

inline bool FontFilesReady(const FontState& s, const FontSpecMsg& spec) {
    return s.files.count(spec.file) && (spec.icons.empty() || s.files.count(spec.icons));
}

#if IMGUI_VERSION_NUM < 19200
// pixels this spec adds to the atlas, kMaxLiveAtlasPixels + 1 when it doesn't fit in what's left
inline uint64_t SpecAtlasPixels(const FontState& s, const FontSpecMsg& spec, const ImWchar* ranges) {
    const uint64_t left = kMaxLiveAtlasPixels - s.atlas_pixels;
    const std::vector<unsigned char>& base = s.files.find(spec.file)->second;
    uint64_t px = fontcheck::AtlasPixels(base.data(), base.size(), ranges, spec.size, left);
    if (px > left || spec.icons.empty()) return px;
    const ImWchar icon_ranges[3] = {ClampFontChar(spec.icon_lo), ClampFontChar(spec.icon_hi), 0};
    const std::vector<unsigned char>& icons = s.files.find(spec.icons)->second;
    return px + fontcheck::AtlasPixels(icons.data(), icons.size(), icon_ranges, spec.size * 0.85f, left - px);
}
#endif

// PreNewFrame body: adds what's ready, rebuilds the atlas, lets the app recreate its font texture
inline void LoadPendingFonts(FontState& s, FontLogs& logs) {
    s.pre_new_frame_seen = true;
    if (!s.pending || !ImGui::GetCurrentContext()) return;
    ImGuiIO& io = ImGui::GetIO();
    ImFontAtlas* atlas = io.Fonts;
    bool dynamic = false;
#if IMGUI_VERSION_NUM >= 19200
    dynamic = (io.BackendFlags & ImGuiBackendFlags_RendererHasTextures) != 0;
#endif
    if (atlas->Locked && !dynamic) {
        if (!s.warned_locked)
            logs.emplace_back(1, "imforge: PreNewFrame() was called inside a frame; call it before the backend NewFrame and ImGui::NewFrame()");
        s.warned_locked = true;
        return;
    }
    if (!dynamic && !s.rebuild_texture) {
        if (!s.warned_no_rebuild)
            logs.emplace_back(1, "imforge: project fonts are waiting for imforge::live::SetFontTextureRebuild(fn); fn must recreate the "
                                 "renderer's font texture (e.g. ImGui_ImplDX11_InvalidateDeviceObjects), see docs/LIVE_ATTACH.md");
        s.warned_no_rebuild = true;
        return;
    }
    for (const std::unique_ptr<LiveFont>& lf : s.loaded)
        if (lf->font && !FontInAtlas(lf->font)) lf->font = nullptr;
    int added = 0;
    bool waiting = false;
    for (const FontSpecMsg& spec : s.specs) {
        LiveFont* lf = FindLoadedFont(s, spec);
        if (lf && (lf->font || lf->failed)) continue;
        if (!FontFilesReady(s, spec)) {
            waiting = true;
            continue;
        }
        const bool fresh = !lf;
        if (!lf) {
            if (s.loaded.size() >= kMaxLiveFonts) {
                if (!s.warned_full) logs.emplace_back(2, "imforge: too many live font changes, restart the app to load more fonts");
                s.warned_full = true;
                continue;
            }
            s.loaded.emplace_back(new LiveFont());
            lf = s.loaded.back().get();
            lf->spec = spec;
        }
#if IMGUI_VERSION_NUM < 19200
        const std::vector<unsigned char>& base = s.files.find(spec.file)->second;
        if (!fontcheck::MapsAnyGlyph(base.data(), base.size(), LiveGlyphRanges(spec.ranges, atlas, *lf))) {
            lf->failed = true;
            logs.emplace_back(2, "imforge: font '" + spec.name + "' has no glyphs in its ranges and is skipped");
            continue;
        }
        if (fresh) {
            uint64_t px = SpecAtlasPixels(s, spec, LiveGlyphRanges(spec.ranges, atlas, *lf));
            if (px > kMaxLiveAtlasPixels - s.atlas_pixels) {
                lf->failed = true;
                logs.emplace_back(2, "imforge: font '" + spec.name + "' would make the font atlas too large and is skipped");
                continue;
            }
            s.atlas_pixels += px;
        }
        if (!TrialBuildFont(s, *lf)) {
            lf->failed = true;
            logs.emplace_back(2, "imforge: font '" + spec.name + "' could not be built and is skipped");
            continue;
        }
#else
        (void)fresh;
#endif
        lf->font = AddLiveFont(atlas, s, *lf);
        if (!lf->font) {
            lf->failed = true;
            logs.emplace_back(2, "imforge: font '" + spec.name + "' could not be added and is skipped");
            continue;
        }
        ++added;
    }
    s.pending = waiting;
    if (!added) return;
    if (!dynamic) {
#if IMGUI_VERSION_NUM < 19200 || !defined(IMGUI_DISABLE_OBSOLETE_FUNCTIONS)
        atlas->Build();
#endif
        s.rebuild_texture();
    }
    logs.emplace_back(0, "imforge: loaded " + std::to_string(added) + " project font(s)");
}

// one hint for apps that never call PreNewFrame: fonts arrived and nothing picked them up
inline void CheckFontIntegration(FontState& s, uint64_t now, FontLogs& logs) {
    if (!s.pending || s.pre_new_frame_seen || s.warned_no_pre_new_frame || s.specs.empty()) return;
    if (now - s.pending_since_ms < kPreNewFrameGraceMs) return;
    s.warned_no_pre_new_frame = true;
    logs.emplace_back(1, "imforge: the builder sent " + std::to_string(s.specs.size()) +
                             " project font(s) but the app never calls imforge::live::PreNewFrame(); call it before the backend "
                             "NewFrame each frame (and SetFontTextureRebuild once), until then panels use the app's own fonts");
}

}  // namespace client_detail
}  // namespace live
}  // namespace imforge
