#include <cstdio>
#include <cstring>

#include "emit_internal.h"

namespace imforge {
namespace codegen {

namespace {

struct RangeInfo {
    const char* name;
    const char* call;
    int min_version;
};

const RangeInfo kRanges[] = {
    {"default", "GetGlyphRangesDefault", 18500},
    {"greek", "GetGlyphRangesGreek", 18800},
    {"korean", "GetGlyphRangesKorean", 18500},
    {"japanese", "GetGlyphRangesJapanese", 18500},
    {"chinese", "GetGlyphRangesChineseFull", 18500},
    {"chineseFull", "GetGlyphRangesChineseFull", 18500},
    {"chineseSimplified", "GetGlyphRangesChineseSimplifiedCommon", 18500},
    {"cyrillic", "GetGlyphRangesCyrillic", 18500},
    {"thai", "GetGlyphRangesThai", 18500},
    {"vietnamese", "GetGlyphRangesVietnamese", 18500},
};

struct IconSet {
    const char* key;
    const char* file;
    const char* header;
    const char* var;
    unsigned lo, hi;
};

const IconSet kIconSets[] = {
    {"fontawesome6", "fa-solid-900.ttf", "IconsFontAwesome6.h", "icon_ranges_fa", 0xe005, 0xf8ff},
    {"fontawesome", "fa-solid-900.ttf", "IconsFontAwesome6.h", "icon_ranges_fa", 0xe005, 0xf8ff},
    {"material", "MaterialIcons-Regular.ttf", "IconsMaterialDesign.h", "icon_ranges_md", 0xe000, 0xf8ff},
    {"lucide", "lucide.ttf", "IconsLucide.h", "icon_ranges_lc", 0xe000, 0xf8ff},
};

const IconSet* FindIconSet(const std::string& key) {
    for (const IconSet& s : kIconSets)
        if (key == s.key) return &s;
    return nullptr;
}

std::string DirOf(const std::string& file) {
    size_t p = file.find_last_of("/\\");
    return p == std::string::npos ? std::string() : file.substr(0, p + 1);
}

}  // namespace

Result GenerateFonts(const Project& project, const Options& opts) {
    Result r;
    Writer w;
    w.opts = &opts;
    WriteFileHead(w, "fonts schema=1");
    w.Line("#pragma once");
    w.Line("#include \"imgui.h\"");
    std::vector<const IconSet*> used_sets;
    for (const FontSpec& f : project.fonts) {
        if (f.merge_icons.empty() || f.merge_icons == "none") continue;
        const IconSet* s = FindIconSet(f.merge_icons);
        if (!s) {
            Diagnostic d;
            d.level = Diagnostic::Level::Warning;
            d.message = "font '" + f.name + "': unknown icon set '" + f.merge_icons + "'";
            r.diagnostics.push_back(d);
            continue;
        }
        bool have = false;
        for (const IconSet* u : used_sets) have = have || u->var == s->var;
        if (!have) used_sets.push_back(s);
    }
    if (opts.features_icons)
        for (const IconSet* s : used_sets) w.Line(std::string("#include \"") + s->header + "\"");
    if (opts.settings.cpp_standard < 17) {
        Diagnostic d;
        d.level = Diagnostic::Level::Warning;
        d.message = "imforge_fonts.h uses inline variables (C++17); cppStandard is " + std::to_string(opts.settings.cpp_standard);
        r.diagnostics.push_back(d);
    }
    OpenNamespace(w, opts);
    for (const FontSpec& f : project.fonts) w.Line("inline ImFont* Font_" + SanitizeIdentifier(f.name) + " = nullptr;");
    w.Line("inline void LoadFonts(ImGuiIO& io, float scale = 1.0f) {");
    {
        Writer::Scope s = w.Scoped();
        w.Line("(void)scale;");
        if (!used_sets.empty()) w.Line("#if IMGUI_VERSION_NUM < 19200");
        for (const IconSet* s2 : used_sets) {
            char buf[128];
            std::snprintf(buf, sizeof(buf), "static const ImWchar %s[] = { 0x%04x, 0x%04x, 0 };", s2->var, s2->lo, s2->hi);
            w.Line(buf);
        }
        if (!used_sets.empty()) w.Line("#endif");
        for (const FontSpec& f : project.fonts) {
            std::string var = "Font_" + SanitizeIdentifier(f.name);
            std::string size = FloatLitOf(f.size) + " * scale";
            std::string file = CppStringLiteral(f.file);
            const RangeInfo* ri = nullptr;
            std::string rkey = f.ranges.empty() ? "default" : f.ranges;
            for (const RangeInfo& x : kRanges)
                if (rkey == x.name) ri = &x;
            if (!ri) {
                Diagnostic d;
                d.level = Diagnostic::Level::Warning;
                d.message = "font '" + f.name + "': unknown glyph ranges '" + f.ranges + "', using default";
                r.diagnostics.push_back(d);
                ri = &kRanges[0];
            }
            w.Line("{");
            {
                Writer::Scope s3 = w.Scoped();
                w.Line("ImFontConfig cfg;");
                if (f.file.empty()) {
                    w.Line("cfg.SizePixels = " + size + ";");
                    w.Line(var + " = io.Fonts->AddFontDefault(&cfg);");
                } else {
                    std::string ranges = std::string("io.Fonts->") + ri->call + "()";
                    w.Line("#if IMGUI_VERSION_NUM >= 19200");
                    w.Line(var + " = io.Fonts->AddFontFromFileTTF(" + file + ", " + size + ", &cfg);");
                    if (ri->min_version > 18500) {
                        w.Line("#elif IMGUI_VERSION_NUM >= " + std::to_string(ri->min_version));
                        w.Line(var + " = io.Fonts->AddFontFromFileTTF(" + file + ", " + size + ", &cfg, " + ranges + ");");
                        w.Line("#else");
                        w.Line(var + " = io.Fonts->AddFontFromFileTTF(" + file + ", " + size + ", &cfg, io.Fonts->GetGlyphRangesDefault());");
                    } else {
                        w.Line("#else");
                        w.Line(var + " = io.Fonts->AddFontFromFileTTF(" + file + ", " + size + ", &cfg, " + ranges + ");");
                    }
                    w.Line("#endif");
                }
                const IconSet* is = FindIconSet(f.merge_icons);
                if (is) {
                    std::string icon_file = CppStringLiteral(DirOf(f.file) + is->file);
                    w.Line("ImFontConfig icons;");
                    w.Line("icons.MergeMode = true;");
                    w.Line("icons.PixelSnapH = true;");
                    w.Line("icons.GlyphMinAdvanceX = " + size + ";");
                    w.Line("#if IMGUI_VERSION_NUM >= 19200");
                    w.Line("io.Fonts->AddFontFromFileTTF(" + icon_file + ", " + size + ", &icons);");
                    w.Line("#else");
                    w.Line("io.Fonts->AddFontFromFileTTF(" + icon_file + ", " + size + ", &icons, " + is->var + ");");
                    w.Line("#endif");
                }
            }
            w.Line("}");
        }
        if (!project.fonts.empty()) w.Line("io.FontDefault = Font_" + SanitizeIdentifier(project.fonts[0].name) + ";");
        w.UserSlot("fonts_extra");
    }
    w.Line("}");
    CloseNamespace(w, opts);
    FileOut f;
    f.path = "imforge_fonts.h";
    f.content = w.out;
    r.files.push_back(f);
    return r;
}

}  // namespace codegen
}  // namespace imforge
