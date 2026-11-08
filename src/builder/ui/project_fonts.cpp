#include "project_fonts.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <system_error>

#include "../builder_fonts.h"
#include "../editor_context.h"
#include "fonts_state.h"
#include "icon_table.h"

namespace fs = std::filesystem;

namespace imforge::builder {

namespace {

const char* kRangeNames[] = {"default",  "latin-ext", "greek",    "cyrillic", "chinese-full", "chinese-simplified",
                             "japanese", "korean",    "thai",     "vietnamese", "icons"};
const char* kMergeNames[] = {"none", "fontawesome6", "material", "lucide"};

std::vector<LoadedProjectFont>& Loaded() {
    static std::vector<LoadedProjectFont> v;
    return v;
}

bool IsFile(const fs::path& p) {
    std::error_code ec;
    return fs::is_regular_file(p, ec);
}

std::string FirstExistingIn(const std::vector<std::string>& dirs, const std::vector<const char*>& names) {
    for (const std::string& d : dirs) {
        if (d.empty()) continue;
        for (const char* n : names) {
            fs::path p = fs::u8path(d) / n;
            if (IsFile(p)) return p.u8string();
        }
    }
    return std::string();
}

ImWchar ClampWchar(unsigned cp) {
    if (sizeof(ImWchar) == 2 && cp > 0xFFFF) return (ImWchar)0xFFFF;
    return (ImWchar)cp;
}

}  // namespace

const char* const* GlyphRangeNames(size_t& count) {
    count = sizeof(kRangeNames) / sizeof(kRangeNames[0]);
    return kRangeNames;
}

const char* const* MergeIconNames(size_t& count) {
    count = sizeof(kMergeNames) / sizeof(kMergeNames[0]);
    return kMergeNames;
}

const ImWchar* GlyphRangesByName(const std::string& r, ImFontAtlas* atlas) {
    static const ImWchar latin_ext[] = {0x0020, 0x00FF, 0x0100, 0x017F, 0x0180, 0x024F, 0x2000,
                                        0x206F, 0x20A0, 0x20CF, 0x2100, 0x214F, 0};
    static ImWchar icons[3] = {0, 0, 0};
    if (r == "latin-ext") return latin_ext;
#if IMGUI_VERSION_NUM >= 18800
    if (r == "greek") return atlas->GetGlyphRangesGreek();
#endif
    if (r == "cyrillic") return atlas->GetGlyphRangesCyrillic();
    if (r == "chinese-full") return atlas->GetGlyphRangesChineseFull();
    if (r == "chinese-simplified") return atlas->GetGlyphRangesChineseSimplifiedCommon();
    if (r == "japanese") return atlas->GetGlyphRangesJapanese();
    if (r == "korean") return atlas->GetGlyphRangesKorean();
    if (r == "thai") return atlas->GetGlyphRangesThai();
    if (r == "vietnamese") return atlas->GetGlyphRangesVietnamese();
    if (r == "icons") {
        unsigned lo = 0, hi = 0;
        IconCodepointRange(lo, hi);
        icons[0] = ClampWchar(lo);
        icons[1] = ClampWchar(hi);
        return icons;
    }
    return atlas->GetGlyphRangesDefault();
}

const ImWchar* IconRangesFor(const std::string& merge_icons) {
    static ImWchar fa[3] = {0, 0, 0};
    static const ImWchar pua[] = {0xe000, 0xf8ff, 0};
    if (merge_icons == "fontawesome6") {
        unsigned lo = 0, hi = 0;
        IconCodepointRange(lo, hi);
        fa[0] = ClampWchar(lo);
        fa[1] = ClampWchar(hi);
        return fa;
    }
    return pua;
}

bool LooksLikeFontFile(const std::string& path) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    unsigned char magic[4] = {0, 0, 0, 0};
    size_t n = std::fread(magic, 1, 4, f);
    std::fclose(f);
    if (n < 4) return false;
    if (magic[0] == 0 && magic[1] == 1 && magic[2] == 0 && magic[3] == 0) return true;
    if (std::memcmp(magic, "OTTO", 4) == 0 || std::memcmp(magic, "true", 4) == 0 || std::memcmp(magic, "ttcf", 4) == 0)
        return true;
    return false;
}

std::string ResolveFontFile(const std::string& file, const std::string& project_dir) {
    if (file.empty()) return std::string();
    // network paths would make Windows send credentials to whoever named them, and .. walks out of the project
    if (file.compare(0, 2, "\\\\") == 0 || file.compare(0, 2, "//") == 0) return std::string();
    fs::path p = fs::u8path(file);
    for (const fs::path& seg : p)
        if (seg == "..") return std::string();
    if (p.is_absolute()) return IsFile(p) ? file : std::string();
    std::string fname = p.filename().u8string();
    std::vector<std::string> dirs = {project_dir, FontsDir()};
    std::error_code ec;
    dirs.push_back(fs::current_path(ec).u8string());
    for (const std::string& d : dirs) {
        if (d.empty()) continue;
        fs::path full = fs::u8path(d) / p;
        if (IsFile(full)) return full.u8string();
    }
    if (!project_dir.empty()) {
        fs::path alt = fs::u8path(project_dir) / "fonts" / fname;
        if (IsFile(alt)) return alt.u8string();
    }
    fs::path builder = fs::u8path(FontsDir()) / fname;
    if (IsFile(builder)) return builder.u8string();
    return std::string();
}

std::string ResolveIconFontFile(const std::string& merge_icons, const std::string& project_dir) {
    std::vector<const char*> names;
    if (merge_icons == "fontawesome6")
        names = {"fa-solid-900.ttf", "Font Awesome 6 Free-Solid-900.otf", "fa-solid-900.otf", "FontAwesome6-Solid.ttf"};
    else if (merge_icons == "material")
        names = {"MaterialIcons-Regular.ttf", "MaterialSymbolsRounded.ttf", "MaterialSymbolsOutlined.ttf",
                 "MaterialIconsRound-Regular.otf"};
    else if (merge_icons == "lucide")
        names = {"lucide.ttf", "Lucide.ttf", "lucide-icons.ttf"};
    else
        return std::string();
    std::vector<std::string> dirs;
    if (!project_dir.empty()) {
        dirs.push_back((fs::u8path(project_dir) / "fonts").u8string());
        dirs.push_back(project_dir);
    }
    dirs.push_back(FontsDir());
    return FirstExistingIn(dirs, names);
}

void AddProjectFontsToAtlas(const Project& project, ImFontAtlas* atlas, float scale) {
    std::vector<LoadedProjectFont>& out = Loaded();
    out.clear();
    if (scale <= 0) scale = 1.0f;
    for (const FontSpec& spec : project.fonts) {
        LoadedProjectFont lf;
        lf.name = spec.name;
        lf.file = spec.file;
        lf.size = spec.size;
        lf.resolved = ResolveFontFile(spec.file, project.dir);
        if (lf.resolved.empty()) {
            lf.error = spec.file.empty() ? "no font file set" : "file not found: " + spec.file;
            out.push_back(std::move(lf));
            continue;
        }
        if (!LooksLikeFontFile(lf.resolved)) {
            lf.error = "not a TrueType/OpenType file: " + lf.resolved;
            out.push_back(std::move(lf));
            continue;
        }
        float px = (spec.size > 0 ? spec.size : 16.0f) * scale;
        ImFontConfig cfg;
        cfg.OversampleH = 2;
        std::snprintf(cfg.Name, sizeof(cfg.Name), "%s %.0fpx", spec.name.c_str(), (double)spec.size);
        lf.font = atlas->AddFontFromFileTTF(lf.resolved.c_str(), px, &cfg, GlyphRangesByName(spec.ranges, atlas));
        if (!lf.font) {
            lf.error = "failed to load " + lf.resolved;
            out.push_back(std::move(lf));
            continue;
        }
        if (!spec.merge_icons.empty() && spec.merge_icons != "none") {
            std::string icon_file = ResolveIconFontFile(spec.merge_icons, project.dir);
            if (icon_file.empty()) {
                lf.error = "icon font for \"" + spec.merge_icons + "\" not found (project fonts/ or builder fonts dir)";
            } else {
                ImFontConfig ic;
                ic.MergeMode = true;
                ic.PixelSnapH = true;
                ic.GlyphMinAdvanceX = px;
                ic.GlyphOffset = ImVec2(0, 1.0f * scale);
                if (atlas->AddFontFromFileTTF(icon_file.c_str(), px * 0.85f, &ic, IconRangesFor(spec.merge_icons)))
                    lf.icons_merged = true;
                else
                    lf.error = "failed to merge " + icon_file;
            }
        }
        out.push_back(std::move(lf));
    }
}

bool RebuildProjectFonts(EditorContext& ctx) {
    ImGuiIO& io = ImGui::GetIO();
    Fonts& bf = GetFonts();
    float scale = bf.scale > 0 ? bf.scale : 1.0f;
    LoadBuilderFonts(scale);
    if (ctx.has_project) AddProjectFontsToAtlas(ctx.project, io.Fonts, scale);
    else Loaded().clear();
    if (!io.Fonts->Build()) {
        for (LoadedProjectFont& lf : Loaded()) {
            lf.font = nullptr;
            if (lf.error.empty()) lf.error = "atlas build failed (font too large?)";
        }
        LoadBuilderFonts(scale);
        io.Fonts->Build();
    }
    ctx.fonts[(int)FontSlot::Ui] = UiFont();
    ctx.fonts[(int)FontSlot::Heading] = HeadingFont();
    ctx.fonts[(int)FontSlot::Mono] = MonoFont();
    for (const LoadedProjectFont& lf : Loaded())
        if (!lf.error.empty()) ctx.LogSource(LogLevel::Warning, "fonts", "%s: %s", lf.name.c_str(), lf.error.c_str());
    FontsDirtyFlag() = false;
    return true;
}

ImFont* FindProjectFont(const std::string& name) {
    const LoadedProjectFont* lf = FindLoadedProjectFont(name);
    return lf ? lf->font : nullptr;
}

const LoadedProjectFont* FindLoadedProjectFont(const std::string& name) {
    for (const LoadedProjectFont& lf : Loaded())
        if (lf.name == name) return &lf;
    return nullptr;
}

const std::vector<LoadedProjectFont>& LoadedProjectFonts() { return Loaded(); }

}  // namespace imforge::builder
