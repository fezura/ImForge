#pragma once

#include <string>
#include <vector>

#include "imgui.h"

namespace imforge::builder {

struct Fonts {
    ImFont* ui = nullptr;
    ImFont* heading = nullptr;
    ImFont* mono = nullptr;
    ImFont* icons_large = nullptr;
    bool has_icons = false;
    bool loaded_from_files = false;
    float scale = 1.0f;
    float ui_size = 16.0f, heading_size = 20.0f, mono_size = 14.0f;
    std::string dir;
    std::vector<std::string> warnings;
};

Fonts& GetFonts();
std::string ExeDir();
std::string FontsDir();
bool LoadBuilderFonts(float scale);
ImFont* UiFont();
ImFont* HeadingFont();
ImFont* MonoFont();
ImFont* IconFontLarge();

}  // namespace imforge::builder

#if __has_include("IconsFontAwesome6.h")
#include "IconsFontAwesome6.h"
#define IMFORGE_HAS_ICON_HEADER 1
#else
#define IMFORGE_HAS_ICON_HEADER 0
#define ICON_MIN_FA 0xe005
#define ICON_MAX_16_FA 0xf8ff
#define ICON_MAX_FA 0xf8ff
#define FONT_ICON_FILE_NAME_FAS "fa-solid-900.ttf"
#endif
