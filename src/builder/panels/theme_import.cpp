#include "theme_import.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "imgui.h"

#include "../ui/theme_apply.h"

#if __has_include("codegen.h")
#include "codegen.h"
#define IMFORGE_THEME_HAS_CODEGEN 1
#else
#define IMFORGE_THEME_HAS_CODEGEN 0
#endif

namespace imforge::builder {

namespace {

struct Num {
    double v = 0;
    bool is_int = true;
    bool is_hex = false;
};

bool IsIdentChar(char c) { return std::isalnum((unsigned char)c) || c == '_'; }

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

std::vector<Num> ExtractNumbers(const std::string& s) {
    std::vector<Num> out;
    size_t i = 0;
    while (i < s.size()) {
        char c = s[i];
        if (std::isalpha((unsigned char)c) || c == '_') {
            while (i < s.size() && IsIdentChar(s[i])) ++i;
            continue;
        }
        bool neg = false;
        if (c == '-' && i + 1 < s.size() && (std::isdigit((unsigned char)s[i + 1]) || s[i + 1] == '.')) {
            neg = true;
            ++i;
            c = s[i];
        }
        bool starts_number = std::isdigit((unsigned char)c) ||
                             (c == '.' && i + 1 < s.size() && std::isdigit((unsigned char)s[i + 1]));
        if (!starts_number) {
            ++i;
            continue;
        }
        Num n;
        if (c == '0' && i + 1 < s.size() && (s[i + 1] == 'x' || s[i + 1] == 'X')) {
            char* end = nullptr;
            n.v = (double)std::strtoull(s.c_str() + i, &end, 16);
            n.is_hex = true;
            i = (size_t)(end - s.c_str());
        } else {
            char* end = nullptr;
            n.v = std::strtod(s.c_str() + i, &end);
            std::string tok(s.c_str() + i, (size_t)(end - (s.c_str() + i)));
            n.is_int = tok.find_first_of(".eE") == std::string::npos;
            i = (size_t)(end - s.c_str());
            if (i < s.size() && (s[i] == 'f' || s[i] == 'F')) {
                n.is_int = false;
                ++i;
            }
        }
        if (neg) n.v = -n.v;
        out.push_back(n);
    }
    return out;
}

std::string IdentAfter(const std::string& s, size_t pos) {
    size_t e = pos;
    while (e < s.size() && IsIdentChar(s[e])) ++e;
    return s.substr(pos, e - pos);
}

size_t FindAssign(const std::string& s, size_t from) {
    for (size_t i = from; i < s.size(); ++i) {
        if (s[i] != '=') continue;
        if (i + 1 < s.size() && s[i + 1] == '=') {
            ++i;
            continue;
        }
        if (i > 0 && (s[i - 1] == '=' || s[i - 1] == '!' || s[i - 1] == '<' || s[i - 1] == '>')) continue;
        return i;
    }
    return std::string::npos;
}

std::string Trim(const std::string& s) {
    size_t b = 0, e = s.size();
    while (b < e && std::isspace((unsigned char)s[b])) ++b;
    while (e > b && std::isspace((unsigned char)s[e - 1])) --e;
    std::string t = s.substr(b, e - b);
    if (t.size() > 60) t = t.substr(0, 57) + "...";
    return t;
}

float Clamp01(double v) { return v < 0 ? 0.0f : (v > 1 ? 1.0f : (float)v); }

Vec4 CurrentColor(const Theme& theme, const ImGuiStyle& def, const std::string& name) {
    for (const auto& c : theme.colors)
        if (c.first == name) return c.second;
    int idx = ImGuiColIndexByName(name.c_str());
    return idx >= 0 ? ToVec4(def.Colors[idx]) : Vec4{1, 1, 1, 1};
}

bool ParseColorExpr(const std::string& rhs, const Theme& theme, const ImGuiStyle& def, Vec4& out,
                    std::string& warn) {
    size_t cp = rhs.find("ImGuiCol_");
    if (cp != std::string::npos) {
        std::string other = ResolveColorAlias(IdentAfter(rhs, cp + 9).c_str());
        out = CurrentColor(theme, def, other);
        return true;
    }
    std::vector<Num> nums = ExtractNumbers(rhs);
    if (nums.empty()) return false;
    bool col32 = rhs.find("IM_COL32") != std::string::npos;
    bool imcolor = rhs.find("ImColor") != std::string::npos;
    bool u32 = rhs.find("ColorConvertU32ToFloat4") != std::string::npos || (nums.size() == 1 && nums[0].is_hex);
    if (!col32 && (u32 || (imcolor && nums.size() == 1))) {
        out = ToVec4(ImGui::ColorConvertU32ToFloat4((ImU32)(unsigned long long)nums[0].v));
        return true;
    }
    if (nums.size() < 3) return false;
    bool all_int = true, any_big = false;
    for (size_t i = 0; i < nums.size() && i < 4; ++i) {
        if (!nums[i].is_int) all_int = false;
        if (nums[i].v > 1.0) any_big = true;
    }
    bool bytes = col32 || (imcolor && all_int) || (all_int && any_big);
    if (!col32 && !imcolor && all_int && any_big) warn = "integer components treated as 0-255";
    double div = bytes ? 255.0 : 1.0;
    out.x = Clamp01(nums[0].v / div);
    out.y = Clamp01(nums[1].v / div);
    out.z = Clamp01(nums[2].v / div);
    out.w = nums.size() >= 4 ? Clamp01(nums[3].v / div) : 1.0f;
    return true;
}

bool IsKnownVarName(const std::string& name) {
    size_t n = 0;
    const char* const* names = ImGuiStyleVarNames(n);
    for (size_t i = 0; i < n; ++i)
        if (name == names[i]) return true;
    names = ImGuiStyleVarNamesDocking(n);
    for (size_t i = 0; i < n; ++i)
        if (name == names[i]) return true;
    return ImGuiStyleVarIndexByName(name.c_str()) >= 0;
}

bool VarIsVec2(const std::string& name) {
    bool vec2 = false;
    if (ImGuiStyleVarIndexByName(name.c_str(), &vec2) >= 0) return vec2;
    return StyleVarIsVec2(name.c_str());
}

struct Importer {
    Theme& theme;
    ThemeImportResult& res;
    ImGuiStyle def;

    void SetColor(const std::string& name, const Vec4& v) {
        for (auto& c : theme.colors) {
            if (c.first == name) {
                c.second = v;
                ++res.colors;
                return;
            }
        }
        theme.colors.emplace_back(name, v);
        ++res.colors;
    }

    void SetVar(const std::string& name, Json j) {
        for (auto& v : theme.vars) {
            if (v.first == name) {
                v.second = std::move(j);
                ++res.vars;
                return;
            }
        }
        theme.vars.emplace_back(name, std::move(j));
        ++res.vars;
    }

    Json CurrentVar(const std::string& name) {
        for (const auto& v : theme.vars)
            if (v.first == name) return v.second;
        return DefaultStyleVarJson(name.c_str());
    }

    void ApplyBase(const char* which) {
        ImGuiStyle base;
        if (std::strcmp(which, "Light") == 0) ImGui::StyleColorsLight(&base);
        else if (std::strcmp(which, "Classic") == 0) ImGui::StyleColorsClassic(&base);
        else ImGui::StyleColorsDark(&base);
        def = base;
        Theme t = ThemeFromStyle(base, theme.name);
        for (const auto& c : t.colors) SetColor(c.first, c.second);
        res.base = which;
    }

    void HandleColor(const std::string& st, size_t col) {
        std::string raw = IdentAfter(st, col + 9);
        if (raw == "COUNT" || raw.empty()) return;
        std::string name = ResolveColorAlias(raw.c_str());
        size_t after = col + 9 + raw.size();
        int comp = -1;
        size_t br = st.find(']', after);
        if (br != std::string::npos && br + 2 < st.size() && st[br + 1] == '.') {
            char c = st[br + 2];
            comp = c == 'x' ? 0 : c == 'y' ? 1 : c == 'z' ? 2 : c == 'w' ? 3 : -1;
        }
        size_t eq = FindAssign(st, after);
        size_t comma = st.find(',', after);
        size_t rhs_pos = eq != std::string::npos ? eq + 1 : (comma != std::string::npos ? comma + 1 : std::string::npos);
        if (rhs_pos == std::string::npos) return;
        std::string rhs = st.substr(rhs_pos);
        if (comp >= 0) {
            std::vector<Num> nums = ExtractNumbers(rhs);
            if (nums.empty()) {
                res.warnings.push_back(name + ": cannot parse component value");
                return;
            }
            Vec4 cur = CurrentColor(theme, def, name);
            float* comps[4] = {&cur.x, &cur.y, &cur.z, &cur.w};
            *comps[comp] = Clamp01(nums[0].v > 1.0 ? nums[0].v / 255.0 : nums[0].v);
            SetColor(name, cur);
            return;
        }
        Vec4 v;
        std::string warn;
        if (ParseColorExpr(rhs, theme, def, v, warn)) {
            SetColor(name, v);
            if (!warn.empty()) res.warnings.push_back(name + ": " + warn);
        } else {
            res.warnings.push_back("cannot parse color for " + name + ": " + Trim(rhs));
        }
    }

    void HandleStyleVarEnum(const std::string& st, size_t sv) {
        std::string name = IdentAfter(st, sv + 14);
        if (name == "COUNT" || name.empty()) return;
        size_t comma = st.find(',', sv + 14 + name.size());
        if (comma == std::string::npos) return;
        std::vector<Num> nums = ExtractNumbers(st.substr(comma + 1));
        bool vec2 = VarIsVec2(name);
        if (vec2 && nums.size() >= 2) SetVar(name, StyleVarJson(true, 0, ImVec2((float)nums[0].v, (float)nums[1].v)));
        else if (!vec2 && !nums.empty()) SetVar(name, StyleVarJson(false, (float)nums[0].v, ImVec2()));
        else if (vec2 && nums.size() == 1) SetVar(name, StyleVarJson(true, 0, ImVec2((float)nums[0].v, (float)nums[0].v)));
        else res.warnings.push_back(name + ": cannot parse value");
    }

    void HandleMember(const std::string& st) {
        size_t eq = FindAssign(st, 0);
        if (eq == std::string::npos) return;
        size_t e = eq;
        while (e > 0 && std::isspace((unsigned char)st[e - 1])) --e;
        int comp = -1;
        if (e >= 2 && st[e - 2] == '.' && (st[e - 1] == 'x' || st[e - 1] == 'y')) {
            comp = st[e - 1] == 'x' ? 0 : 1;
            e -= 2;
        }
        size_t b = e;
        while (b > 0 && IsIdentChar(st[b - 1])) --b;
        std::string member = st.substr(b, e - b);
        if (member.empty() || b == 0 || !(st[b - 1] == '.' || st[b - 1] == '>')) return;
        if (!IsKnownVarName(member)) {
            if (member != "Colors") res.warnings.push_back("ignored style member " + member);
            return;
        }
        std::vector<Num> nums = ExtractNumbers(st.substr(eq + 1));
        if (nums.empty()) {
            res.warnings.push_back(member + ": cannot parse value: " + Trim(st.substr(eq + 1)));
            return;
        }
        bool vec2 = VarIsVec2(member);
        if (!vec2) {
            SetVar(member, StyleVarJson(false, (float)nums[0].v, ImVec2()));
            return;
        }
        float f = 0;
        ImVec2 v;
        StyleVarValue(CurrentVar(member), true, f, v);
        if (comp == 0) v.x = (float)nums[0].v;
        else if (comp == 1) v.y = (float)nums[0].v;
        else if (nums.size() >= 2) v = ImVec2((float)nums[0].v, (float)nums[1].v);
        else v = ImVec2((float)nums[0].v, (float)nums[0].v);
        SetVar(member, StyleVarJson(true, 0, v));
    }

    void Run(const std::string& code) {
        size_t pos = 0;
        while (pos < code.size()) {
            size_t semi = code.find(';', pos);
            if (semi == std::string::npos) semi = code.size();
            std::string st = code.substr(pos, semi - pos);
            pos = semi + 1;
            if (st.find("StyleColorsDark") != std::string::npos) ApplyBase("Dark");
            else if (st.find("StyleColorsLight") != std::string::npos) ApplyBase("Light");
            else if (st.find("StyleColorsClassic") != std::string::npos) ApplyBase("Classic");
            if (st.find("ScaleAllSizes") != std::string::npos) res.warnings.push_back("ScaleAllSizes() ignored");
            size_t col = st.find("ImGuiCol_");
            if (col != std::string::npos) {
                HandleColor(st, col);
                continue;
            }
            size_t sv = st.find("ImGuiStyleVar_");
            if (sv != std::string::npos) {
                HandleStyleVarEnum(st, sv);
                continue;
            }
            HandleMember(st);
        }
    }
};

}  // namespace

ThemeImportResult ImportThemeFromCode(const std::string& code_in, Theme& theme, bool replace_existing) {
    ThemeImportResult res;
    if (replace_existing) {
        theme.colors.clear();
        theme.vars.clear();
    }
    Importer imp{theme, res, ImGuiStyle()};
    ImGui::StyleColorsDark(&imp.def);
    imp.Run(StripComments(code_in));
    if (res.colors == 0 && res.vars == 0) res.warnings.push_back("no ImGuiCol_/style assignments recognized");
    return res;
}

std::string ThemeFunctionName(const std::string& theme_name) {
    std::string fn = "ApplyTheme_";
    for (char c : theme_name) fn.push_back(std::isalnum((unsigned char)c) ? c : '_');
    return fn;
}

std::string ExportThemeAsCode(const Theme& theme, const std::string& function_name) {
    std::string out = "void " + function_name + "(ImGuiStyle& style)\n{\n";
    char buf[256];
    if (!theme.colors.empty()) out += "    ImVec4* colors = style.Colors;\n";
    for (const auto& c : theme.colors) {
        std::snprintf(buf, sizeof(buf), "    colors[ImGuiCol_%s] = ImVec4(%.3ff, %.3ff, %.3ff, %.3ff);\n", c.first.c_str(),
                      (double)c.second.x, (double)c.second.y, (double)c.second.z, (double)c.second.w);
        out += buf;
    }
    if (!theme.colors.empty() && !theme.vars.empty()) out += "\n";
    for (const auto& v : theme.vars) {
        bool vec2 = VarIsVec2(v.first);
        float f = 0;
        ImVec2 v2;
        if (!StyleVarValue(v.second, vec2, f, v2)) continue;
        if (vec2)
            std::snprintf(buf, sizeof(buf), "    style.%s = ImVec2(%.3ff, %.3ff);\n", v.first.c_str(), (double)v2.x,
                          (double)v2.y);
        else
            std::snprintf(buf, sizeof(buf), "    style.%s = %.3ff;\n", v.first.c_str(), (double)f);
        out += buf;
    }
    out += "}\n";
    return out;
}

std::string ExportThemeText(const Project& project, const Theme& theme) {
#if IMFORGE_THEME_HAS_CODEGEN
    imforge::codegen::Options opt;
    opt.settings = project.codegen;
    opt.imgui_version = project.ImGuiVersionNum();
    imforge::codegen::Result r = imforge::codegen::GenerateTheme(project, theme, opt);
    std::string out;
    for (const auto& f : r.files) {
        if (!out.empty()) out += "\n";
        out += "// " + f.path + "\n" + f.content;
    }
    if (!out.empty()) return out;
#else
    (void)project;
#endif
    return ExportThemeAsCode(theme, ThemeFunctionName(theme.name));
}

}  // namespace imforge::builder
