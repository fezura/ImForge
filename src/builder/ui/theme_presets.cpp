#include "theme_presets.h"

#include <cctype>
#include <cstdlib>
#include <cstring>

namespace imforge::builder {

namespace {

struct Palette {
    unsigned text, text_dim, bg, bg2, bg_dark, frame, frame_hover, frame_active, border, accent, accent_bright, warn;
    float window_rounding, frame_rounding, frame_border;
};

struct PresetDef {
    const char* name;
    const char* description;
    Palette pal;
};

const PresetDef kPresets[] = {
    {"Dark", "Neutral graphite surfaces with a blue accent. The builder's own look.",
     {0xe8eaed, 0x7b818c, 0x15181d, 0x181b21, 0x111418, 0x22272f, 0x2a303a, 0x323947, 0x2b313b, 0x3d8bfd, 0x74adff, 0xff8c42, 8, 5, 0}},
    {"Light", "Paper-white panels, soft grey frames and a deep blue accent for daylight tools.",
     {0x1c2128, 0x6b7480, 0xf4f5f7, 0xffffff, 0xe4e7eb, 0xe8ebf0, 0xdde2e9, 0xcfd6df, 0xd0d5dc, 0x2f6feb, 0x1f5ed0, 0xd9480f, 6, 4, 1}},
    {"Neon", "Near-black with magenta and cyan highlights; built for overlays and game menus.",
     {0xf2f2ff, 0x8a8aa8, 0x0b0b12, 0x11111c, 0x07070d, 0x1a1a2b, 0x24243a, 0x2e2e4a, 0x2a2a44, 0xff2d95, 0x00e5ff, 0xffe600, 4, 2, 1}},
    {"Midnight", "Deep navy slate with a sky-blue accent; calm contrast for long sessions.",
     {0xe2e8f0, 0x7c8aa0, 0x0f172a, 0x111c33, 0x0a1020, 0x1e293b, 0x263449, 0x334155, 0x1f2b40, 0x38bdf8, 0x7dd3fc, 0xfbbf24, 10, 6, 0}},
    {"Nord", "Arctic, bluish palette in the spirit of Nord: polar night surfaces, frost accents.",
     {0xeceff4, 0x9aa4b6, 0x2e3440, 0x3b4252, 0x272c36, 0x3b4252, 0x434c5e, 0x4c566a, 0x434c5e, 0x88c0d0, 0x8fbcbb, 0xd08770, 6, 4, 0}},
    {"Dracula", "Purple-grey backgrounds with violet and pink accents in the spirit of Dracula.",
     {0xf8f8f2, 0x6272a4, 0x282a36, 0x21222c, 0x191a21, 0x343746, 0x44475a, 0x4d5066, 0x44475a, 0xbd93f9, 0xff79c6, 0xffb86c, 8, 6, 0}},
};

struct VarDef {
    const char* name;
    bool vec2;
};

const VarDef kVars[] = {
    {"Alpha", false}, {"DisabledAlpha", false}, {"WindowPadding", true}, {"WindowRounding", false},
    {"WindowBorderSize", false}, {"WindowMinSize", true}, {"WindowTitleAlign", true}, {"ChildRounding", false},
    {"ChildBorderSize", false}, {"PopupRounding", false}, {"PopupBorderSize", false}, {"FramePadding", true},
    {"FrameRounding", false}, {"FrameBorderSize", false}, {"ItemSpacing", true}, {"ItemInnerSpacing", true},
    {"IndentSpacing", false}, {"CellPadding", true}, {"ScrollbarSize", false}, {"ScrollbarRounding", false},
    {"GrabMinSize", false}, {"GrabRounding", false}, {"TabRounding", false}, {"TabBorderSize", false},
    {"TabBarBorderSize", false}, {"TabBarOverlineSize", false}, {"ButtonTextAlign", true}, {"SelectableTextAlign", true},
    {"SeparatorTextBorderSize", false}, {"SeparatorTextAlign", true}, {"SeparatorTextPadding", true},
    {"DockingSeparatorSize", false},
};

Vec4 Hex(unsigned rgb, unsigned a = 0xff) {
    Vec4 c;
    c.x = (float)((rgb >> 16) & 255) / 255.0f;
    c.y = (float)((rgb >> 8) & 255) / 255.0f;
    c.z = (float)(rgb & 255) / 255.0f;
    c.w = (float)a / 255.0f;
    return c;
}

void SetColor(Theme& t, const std::string& name, const Vec4& c) {
    for (auto& kv : t.colors)
        if (kv.first == name) {
            kv.second = c;
            return;
        }
    t.colors.emplace_back(name, c);
}

Json Num(float v) { return Json((double)v); }

Json V2(float x, float y) {
    Json o = Json::Object();
    o.Set("x", Num(x));
    o.Set("y", Num(y));
    return o;
}

void SetVar(Theme& t, const std::string& name, Json v) {
    for (auto& kv : t.vars)
        if (kv.first == name) {
            kv.second = std::move(v);
            return;
        }
    t.vars.emplace_back(name, std::move(v));
}

void BuildColors(Theme& t, const Palette& p) {
    SetColor(t, "Text", Hex(p.text));
    SetColor(t, "TextDisabled", Hex(p.text_dim));
    SetColor(t, "WindowBg", Hex(p.bg));
    SetColor(t, "ChildBg", Hex(p.bg2));
    SetColor(t, "PopupBg", Hex(p.bg2, 0xfa));
    SetColor(t, "Border", Hex(p.border));
    SetColor(t, "BorderShadow", Hex(0, 0));
    SetColor(t, "FrameBg", Hex(p.frame));
    SetColor(t, "FrameBgHovered", Hex(p.frame_hover));
    SetColor(t, "FrameBgActive", Hex(p.frame_active));
    SetColor(t, "TitleBg", Hex(p.bg_dark));
    SetColor(t, "TitleBgActive", Hex(p.bg2));
    SetColor(t, "TitleBgCollapsed", Hex(p.bg_dark, 0x99));
    SetColor(t, "MenuBarBg", Hex(p.bg2));
    SetColor(t, "ScrollbarBg", Hex(p.bg_dark, 0x99));
    SetColor(t, "ScrollbarGrab", Hex(p.frame_hover));
    SetColor(t, "ScrollbarGrabHovered", Hex(p.frame_active));
    SetColor(t, "ScrollbarGrabActive", Hex(p.text_dim));
    SetColor(t, "CheckMark", Hex(p.accent_bright));
    SetColor(t, "SliderGrab", Hex(p.accent));
    SetColor(t, "SliderGrabActive", Hex(p.accent_bright));
    SetColor(t, "Button", Hex(p.frame));
    SetColor(t, "ButtonHovered", Hex(p.frame_hover));
    SetColor(t, "ButtonActive", Hex(p.accent));
    SetColor(t, "Header", Hex(p.frame));
    SetColor(t, "HeaderHovered", Hex(p.frame_hover));
    SetColor(t, "HeaderActive", Hex(p.accent, 0x99));
    SetColor(t, "Separator", Hex(p.border));
    SetColor(t, "SeparatorHovered", Hex(p.accent, 0xab));
    SetColor(t, "SeparatorActive", Hex(p.accent));
    SetColor(t, "ResizeGrip", Hex(p.accent, 0x33));
    SetColor(t, "ResizeGripHovered", Hex(p.accent, 0xab));
    SetColor(t, "ResizeGripActive", Hex(p.accent));
    SetColor(t, "InputTextCursor", Hex(p.text));
    SetColor(t, "TabHovered", Hex(p.frame_hover));
    SetColor(t, "Tab", Hex(p.bg2));
    SetColor(t, "TabSelected", Hex(p.frame));
    SetColor(t, "TabSelectedOverline", Hex(p.accent));
    SetColor(t, "TabDimmed", Hex(p.bg_dark));
    SetColor(t, "TabDimmedSelected", Hex(p.bg2));
    SetColor(t, "TabDimmedSelectedOverline", Hex(p.accent, 0x80));
    SetColor(t, "DockingPreview", Hex(p.accent, 0xb2));
    SetColor(t, "DockingEmptyBg", Hex(p.bg_dark));
    SetColor(t, "PlotLines", Hex(p.text_dim));
    SetColor(t, "PlotLinesHovered", Hex(p.warn));
    SetColor(t, "PlotHistogram", Hex(p.accent));
    SetColor(t, "PlotHistogramHovered", Hex(p.accent_bright));
    SetColor(t, "TableHeaderBg", Hex(p.bg2));
    SetColor(t, "TableBorderStrong", Hex(p.border));
    SetColor(t, "TableBorderLight", Hex(p.border, 0x80));
    SetColor(t, "TableRowBg", Hex(0, 0));
    SetColor(t, "TableRowBgAlt", Hex(p.text, 0x08));
    SetColor(t, "TextLink", Hex(p.accent_bright));
    SetColor(t, "TextSelectedBg", Hex(p.accent, 0x59));
    SetColor(t, "DragDropTarget", Hex(p.warn, 0xe6));
    SetColor(t, "NavCursor", Hex(p.accent_bright));
    SetColor(t, "NavWindowingHighlight", Hex(p.text, 0xb2));
    SetColor(t, "NavWindowingDimBg", Hex(p.bg_dark, 0x33));
    SetColor(t, "ModalWindowDimBg", Hex(p.bg_dark, 0x99));
}

void BuildVars(Theme& t, const Palette& p) {
    float wr = p.window_rounding, fr = p.frame_rounding;
    SetVar(t, "Alpha", Num(1));
    SetVar(t, "DisabledAlpha", Num(0.5f));
    SetVar(t, "WindowPadding", V2(10, 10));
    SetVar(t, "WindowRounding", Num(wr));
    SetVar(t, "WindowBorderSize", Num(1));
    SetVar(t, "WindowMinSize", V2(64, 64));
    SetVar(t, "WindowTitleAlign", V2(0, 0.5f));
    SetVar(t, "ChildRounding", Num(wr * 0.75f));
    SetVar(t, "ChildBorderSize", Num(1));
    SetVar(t, "PopupRounding", Num(wr * 0.75f));
    SetVar(t, "PopupBorderSize", Num(1));
    SetVar(t, "FramePadding", V2(8, 5));
    SetVar(t, "FrameRounding", Num(fr));
    SetVar(t, "FrameBorderSize", Num(p.frame_border));
    SetVar(t, "ItemSpacing", V2(8, 6));
    SetVar(t, "ItemInnerSpacing", V2(6, 4));
    SetVar(t, "IndentSpacing", Num(20));
    SetVar(t, "CellPadding", V2(6, 4));
    SetVar(t, "ScrollbarSize", Num(12));
    SetVar(t, "ScrollbarRounding", Num(12));
    SetVar(t, "GrabMinSize", Num(10));
    SetVar(t, "GrabRounding", Num(fr > 1 ? fr - 1 : fr));
    SetVar(t, "TabRounding", Num(fr));
    SetVar(t, "TabBorderSize", Num(0));
    SetVar(t, "TabBarBorderSize", Num(1));
    SetVar(t, "ButtonTextAlign", V2(0.5f, 0.5f));
    SetVar(t, "SelectableTextAlign", V2(0, 0));
    SetVar(t, "SeparatorTextBorderSize", Num(2));
    SetVar(t, "SeparatorTextAlign", V2(0, 0.5f));
    SetVar(t, "SeparatorTextPadding", V2(16, 3));
}

const PresetDef* FindDef(const std::string& name) {
    for (const PresetDef& d : kPresets)
        if (name == d.name) return &d;
    return nullptr;
}

bool IsIdent(char c) { return std::isalnum((unsigned char)c) || c == '_'; }

std::string Trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace((unsigned char)s[a])) ++a;
    while (b > a && std::isspace((unsigned char)s[b - 1])) --b;
    return s.substr(a, b - a);
}

bool ParseNumbers(const std::string& s, size_t pos, std::vector<double>& out, bool& saw_dot) {
    out.clear();
    saw_dot = false;
    size_t end = s.find(')', pos);
    if (end == std::string::npos) return false;
    std::string body = s.substr(pos, end - pos);
    size_t i = 0;
    while (i < body.size()) {
        while (i < body.size() && (std::isspace((unsigned char)body[i]) || body[i] == ',')) ++i;
        if (i >= body.size()) break;
        const char* start = body.c_str() + i;
        char* stop = nullptr;
        double v = std::strtod(start, &stop);
        if (stop == start) return false;
        size_t len = (size_t)(stop - start);
        if (std::memchr(start, '.', len)) saw_dot = true;
        out.push_back(v);
        i += len;
        while (i < body.size() && (body[i] == 'f' || body[i] == 'F' || std::isspace((unsigned char)body[i]))) ++i;
        if (i < body.size() && body[i] != ',') return false;
    }
    return !out.empty();
}

std::string StripComments(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    for (size_t i = 0; i < in.size(); ++i) {
        if (in[i] == '/' && i + 1 < in.size() && in[i + 1] == '/') {
            while (i < in.size() && in[i] != '\n') ++i;
            out.push_back('\n');
            continue;
        }
        if (in[i] == '/' && i + 1 < in.size() && in[i + 1] == '*') {
            i += 2;
            while (i + 1 < in.size() && !(in[i] == '*' && in[i + 1] == '/')) ++i;
            ++i;
            out.push_back(' ');
            continue;
        }
        out.push_back(in[i]);
    }
    return out;
}

std::string IdentAt(const std::string& s, size_t pos) {
    size_t e = pos;
    while (e < s.size() && IsIdent(s[e])) ++e;
    return s.substr(pos, e - pos);
}

std::string TrailingIdent(const std::string& s) {
    size_t e = s.size();
    while (e > 0 && std::isspace((unsigned char)s[e - 1])) --e;
    size_t b = e;
    while (b > 0 && IsIdent(s[b - 1])) --b;
    return s.substr(b, e - b);
}

const VarDef* FindVarDef(const std::string& name) {
    for (const VarDef& v : kVars)
        if (name == v.name) return &v;
    return nullptr;
}

bool ParseColorExpr(const std::string& stmt, size_t pos, Vec4& out) {
    size_t paren = stmt.find('(', pos);
    if (paren == std::string::npos) return false;
    std::string fn = Trim(stmt.substr(pos, paren - pos));
    size_t ns = fn.find_last_of(": ");
    if (ns != std::string::npos) fn = fn.substr(ns + 1);
    std::vector<double> nums;
    bool saw_dot = false;
    if (!ParseNumbers(stmt, paren + 1, nums, saw_dot) || nums.size() < 3 || nums.size() > 4) return false;
    bool bytes = fn == "IM_COL32" || (fn == "ImColor" && !saw_dot);
    if (fn != "ImVec4" && fn != "ImColor" && fn != "IM_COL32") return false;
    double a = nums.size() == 4 ? nums[3] : (bytes ? 255.0 : 1.0);
    double k = bytes ? 1.0 / 255.0 : 1.0;
    out.x = (float)(nums[0] * k);
    out.y = (float)(nums[1] * k);
    out.z = (float)(nums[2] * k);
    out.w = (float)(a * k);
    return true;
}

bool ParseVarValue(const std::string& stmt, size_t pos, bool vec2, Json& out) {
    std::vector<double> nums;
    bool saw_dot = false;
    if (vec2) {
        size_t paren = stmt.find('(', pos);
        if (paren == std::string::npos || !ParseNumbers(stmt, paren + 1, nums, saw_dot) || nums.size() != 2) return false;
        out = V2((float)nums[0], (float)nums[1]);
        return true;
    }
    size_t i = pos;
    while (i < stmt.size() && std::isspace((unsigned char)stmt[i])) ++i;
    const char* start = stmt.c_str() + i;
    char* stop = nullptr;
    double v = std::strtod(start, &stop);
    if (stop == start) return false;
    out = Num((float)v);
    return true;
}

}  // namespace

const std::vector<ThemePresetInfo>& ThemePresets() {
    static std::vector<ThemePresetInfo> list;
    if (list.empty()) {
        for (const PresetDef& d : kPresets)
            list.push_back(ThemePresetInfo{d.name, d.description, {d.pal.bg, d.pal.frame, d.pal.accent, d.pal.text}});
    }
    return list;
}

const ThemePresetInfo* FindThemePreset(const std::string& name) {
    for (const ThemePresetInfo& p : ThemePresets())
        if (name == p.name) return &p;
    return nullptr;
}

Theme MakeThemePreset(const std::string& name) {
    const PresetDef* d = FindDef(name);
    if (!d) d = &kPresets[0];
    Theme t;
    t.name = d->name;
    BuildColors(t, d->pal);
    BuildVars(t, d->pal);
    return t;
}

const char* const* ThemeStyleVarNames(size_t& count, const bool** is_vec2) {
    static const char* names[sizeof(kVars) / sizeof(kVars[0])];
    static bool vec2[sizeof(kVars) / sizeof(kVars[0])];
    static bool init = false;
    if (!init) {
        for (size_t i = 0; i < sizeof(kVars) / sizeof(kVars[0]); ++i) {
            names[i] = kVars[i].name;
            vec2[i] = kVars[i].vec2;
        }
        init = true;
    }
    count = sizeof(kVars) / sizeof(kVars[0]);
    if (is_vec2) *is_vec2 = vec2;
    return names;
}

bool ParseThemeFromCode(const std::string& code_in, Theme& out, std::string* err, int* colors_found, int* vars_found) {
    out = Theme();
    out.name = "Imported";
    const std::string code = StripComments(code_in);
    int ncol = 0, nvar = 0;
    size_t start = 0;
    while (start < code.size()) {
        size_t semi = code.find(';', start);
        std::string stmt = code.substr(start, semi == std::string::npos ? std::string::npos : semi - start);
        start = semi == std::string::npos ? code.size() : semi + 1;
        size_t cpos = stmt.find("ImGuiCol_");
        if (cpos != std::string::npos) {
            std::string name = IdentAt(stmt, cpos + 9);
            size_t after = cpos + 9 + name.size();
            size_t sep = stmt.find_first_of("=,", after);
            if (name.empty() || name == "COUNT" || sep == std::string::npos) continue;
            Vec4 c;
            if (ParseColorExpr(stmt, sep + 1, c)) {
                SetColor(out, ImGuiColCanonicalName(name.c_str()), c);
                ++ncol;
            }
            continue;
        }
        size_t vpos = stmt.find("ImGuiStyleVar_");
        if (vpos != std::string::npos) {
            std::string name = IdentAt(stmt, vpos + 14);
            const VarDef* def = FindVarDef(name);
            size_t comma = stmt.find(',', vpos);
            Json v;
            if (def && comma != std::string::npos && ParseVarValue(stmt, comma + 1, def->vec2, v)) {
                SetVar(out, name, std::move(v));
                ++nvar;
            }
            continue;
        }
        size_t eq = stmt.find('=');
        if (eq == std::string::npos || (eq + 1 < stmt.size() && stmt[eq + 1] == '=')) continue;
        std::string lhs = TrailingIdent(stmt.substr(0, eq));
        const VarDef* def = FindVarDef(lhs);
        if (!def) continue;
        Json v;
        if (ParseVarValue(stmt, eq + 1, def->vec2, v)) {
            SetVar(out, lhs, std::move(v));
            ++nvar;
        }
    }
    if (colors_found) *colors_found = ncol;
    if (vars_found) *vars_found = nvar;
    if (ncol == 0 && nvar == 0) {
        if (err) *err = "no ImGuiCol_* color assignments or style variables were recognized";
        return false;
    }
    return true;
}

}  // namespace imforge::builder
