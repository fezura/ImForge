#if defined(_MSC_VER) && !defined(_CRT_SECURE_NO_WARNINGS)
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "builder_fonts.h"

#include <cmath>
#include <cstdlib>
#include <filesystem>

#include "ui/icon_table.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#else
#include <unistd.h>
#endif

namespace imforge::builder {

namespace fs = std::filesystem;

Fonts& GetFonts() {
    static Fonts f;
    return f;
}

std::string ExeDir() {
    std::string path;
#ifdef _WIN32
    wchar_t buf[MAX_PATH * 2];
    DWORD n = GetModuleFileNameW(nullptr, buf, (DWORD)(sizeof(buf) / sizeof(buf[0])));
    if (n > 0) path = fs::path(std::wstring(buf, n)).parent_path().string();
#elif defined(__APPLE__)
    char buf[4096];
    uint32_t size = sizeof(buf);
    if (_NSGetExecutablePath(buf, &size) == 0) path = fs::path(buf).parent_path().string();
#else
    char buf[4096];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n > 0) {
        buf[n] = 0;
        path = fs::path(buf).parent_path().string();
    }
#endif
    if (path.empty()) path = ".";
    return path;
}

std::string FontsDir() {
    std::error_code ec;
    if (const char* env = std::getenv("IMFORGE_FONTS_DIR"))
        if (*env && fs::is_directory(env, ec)) return env;
    fs::path exe = ExeDir();
    const char* candidates[] = {"fonts", "../fonts", "../share/imforge/fonts", "../../fonts", "../../../fonts"};
    for (const char* c : candidates) {
        fs::path p = exe / c;
        if (fs::is_directory(p, ec)) return p.lexically_normal().string();
    }
    return (exe / "fonts").string();
}

static std::string FirstExisting(const std::string& dir, std::initializer_list<const char*> names) {
    std::error_code ec;
    for (const char* n : names) {
        fs::path p = fs::path(dir) / n;
        if (fs::is_regular_file(p, ec)) return p.string();
    }
    return std::string();
}

bool LoadBuilderFonts(float scale) {
    Fonts& f = GetFonts();
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();
    f = Fonts();
    f.scale = scale <= 0 ? 1.0f : scale;
    f.dir = FontsDir();
    const float ui_px = f.ui_size * f.scale;
    const float heading_px = f.heading_size * f.scale;
    const float mono_px = f.mono_size * f.scale;

    std::string inter = FirstExisting(f.dir, {"Inter-Regular.ttf", "Inter-Regular.otf", "Inter.ttf", "InterVariable.ttf"});
    std::string inter_bold = FirstExisting(f.dir, {"Inter-Bold.ttf", "Inter-SemiBold.ttf", "Inter-Bold.otf"});
    std::string mono = FirstExisting(f.dir, {"JetBrainsMono-Regular.ttf", "JetBrainsMono[wght].ttf", "JetBrainsMono.ttf"});
    std::string icons = FirstExisting(f.dir, {FONT_ICON_FILE_NAME_FAS, "fa-solid-900.ttf", "Font Awesome 6 Free-Solid-900.otf"});

    static const ImWchar icon_ranges[] = {(ImWchar)ICON_MIN_FA, (ImWchar)ICON_MAX_16_FA, 0};
    auto merge_icons = [&](float px) {
        if (icons.empty()) return;
        ImFontConfig cfg;
        cfg.MergeMode = true;
        cfg.PixelSnapH = true;
        cfg.GlyphMinAdvanceX = px;
        cfg.GlyphOffset = ImVec2(0, 1.0f * f.scale);
        if (io.Fonts->AddFontFromFileTTF(icons.c_str(), px * 0.85f, &cfg, icon_ranges)) f.has_icons = true;
    };

    if (!inter.empty()) {
        ImFontConfig cfg;
        cfg.OversampleH = 2;
        f.ui = io.Fonts->AddFontFromFileTTF(inter.c_str(), ui_px, &cfg);
    }
    if (!f.ui) {
        ImFontConfig cfg;
        cfg.SizePixels = 13.0f * f.scale;
        f.ui = io.Fonts->AddFontDefault(&cfg);
        f.warnings.push_back("Inter-Regular.ttf not found in " + f.dir + "; using ImGui default font");
    }
    merge_icons(ui_px);

    std::string heading_file = !inter_bold.empty() ? inter_bold : inter;
    if (!heading_file.empty()) {
        ImFontConfig cfg;
        cfg.OversampleH = 2;
        f.heading = io.Fonts->AddFontFromFileTTF(heading_file.c_str(), heading_px, &cfg);
        if (f.heading) merge_icons(heading_px);
    }
    if (!f.heading) f.heading = f.ui;

    if (!mono.empty()) {
        ImFontConfig cfg;
        cfg.OversampleH = 2;
        f.mono = io.Fonts->AddFontFromFileTTF(mono.c_str(), mono_px, &cfg);
    }
    if (!f.mono) {
        f.mono = f.ui;
        f.warnings.push_back("JetBrainsMono-Regular.ttf not found in " + f.dir + "; code view uses the UI font");
    }
    if (!icons.empty()) {
        static const ImWchar large_ranges[] = {0x0021, 0x007e, (ImWchar)ICON_MIN_FA, (ImWchar)ICON_MAX_16_FA, 0};
        ImFontConfig cfg;
        cfg.PixelSnapH = true;
        const float px = std::round(ui_px * 1.5f);
        cfg.GlyphMinAdvanceX = px;
        f.icons_large = io.Fonts->AddFontFromFileTTF(icons.c_str(), px, &cfg, large_ranges);
        static bool header_dir_added = false;
        if (!header_dir_added) {
            header_dir_added = true;
            AddIconHeaderSearchDir(f.dir);
        }
    }
    if (icons.empty()) f.warnings.push_back("fa-solid-900.ttf not found in " + f.dir + "; icons render as text");
    f.loaded_from_files = !inter.empty();
    io.FontDefault = f.ui;
    return f.loaded_from_files;
}

ImFont* UiFont() { return GetFonts().ui; }
ImFont* HeadingFont() { return GetFonts().heading ? GetFonts().heading : GetFonts().ui; }
ImFont* MonoFont() { return GetFonts().mono ? GetFonts().mono : GetFonts().ui; }
ImFont* IconFontLarge() { return GetFonts().icons_large; }

}  // namespace imforge::builder
