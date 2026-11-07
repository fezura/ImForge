#include <cstring>

#include "emit_internal.h"

namespace imforge {
namespace codegen {

namespace {

const char* OldColorName(const char* canon) {
    static const struct { const char* old_name; const char* new_name; } kAliases[] = {
        {"TabActive", "TabSelected"}, {"TabUnfocused", "TabDimmed"}, {"TabUnfocusedActive", "TabDimmedSelected"},
        {"NavHighlight", "NavCursor"}};
    for (const auto& a : kAliases)
        if (std::strcmp(a.new_name, canon) == 0) return a.old_name;
    return nullptr;
}

bool IsDockingColor(const char* name) { return IsDockingColorName(name); }
bool IsDockingVar(const char* name) { return IsDockingVarName(name); }

std::string ColorLit(const Vec4& c) {
    auto t3 = [](float v) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.3f", static_cast<double>(v));
        std::string s = buf;
        while (s.size() > 1 && s.back() == '0' && s[s.size() - 2] != '.') s.pop_back();
        return s + "f";
    };
    return "ImVec4(" + t3(c.x) + ", " + t3(c.y) + ", " + t3(c.z) + ", " + t3(c.w) + ")";
}

struct GuardInfo {
    bool emitted = false;
    std::vector<std::string> open, close;   // guards that a matching Pop must repeat
};

// Emits `line_fn(name)` for a color, guarded when it is newer than 1.85 (with the pre-rename
// spelling in the #else branch), docking-only, or unknown to the tables (assumed newer).
template <class F>
GuardInfo GuardedColor(Writer& w, const std::string& raw_name, int target, std::vector<Diagnostic>& diags,
                       const std::string& owner, F line_fn) {
    GuardInfo g;
    const char* canon = ImGuiColCanonicalName(raw_name.c_str());
    std::string safe_canon = SanitizeIdentifier(canon);
    bool known = true;
    int minv = ColGuardVersion(canon, target, &known);
    if (!known) {
        Diagnostic d;
        d.level = Diagnostic::Level::Info;
        d.panel = owner;
        d.message = "unknown ImGuiCol '" + raw_name + "' emitted under #if IMGUI_VERSION_NUM >= " + std::to_string(minv);
        diags.push_back(d);
    }
    if (IsDockingColor(canon)) {
        g.open.push_back("#ifdef IMGUI_HAS_DOCK");
        g.close.push_back("#endif");
    }
    const char* old_name = OldColorName(canon);
    bool versioned = minv > EmitCtx::kMinImGuiVersion;
    for (const std::string& l : g.open) w.Line(l);
    if (versioned) {
        w.Line("#if IMGUI_VERSION_NUM >= " + std::to_string(minv));
        w.Line(line_fn(safe_canon));
        if (old_name) {
            w.Line("#else");
            w.Line(line_fn(std::string(old_name)));
        }
        w.Line("#endif");
    } else {
        w.Line(line_fn(safe_canon));
    }
    for (const std::string& l : g.close) w.Line(l);
    if (versioned && !old_name) {
        g.open.push_back("#if IMGUI_VERSION_NUM >= " + std::to_string(minv));
        g.close.insert(g.close.begin(), "#endif");
    }
    g.emitted = true;
    return g;
}

std::string VarValue(const std::string& name, const Json& v, bool known, bool& ok) {
    ok = true;
    if (StyleVarIsVec2(name.c_str()) || (!known && v.IsObject())) {
        if (!v.IsObject()) {
            ok = false;
            return std::string();
        }
        return "ImVec2(" + NumLit(v["x"].AsDouble()) + ", " + NumLit(v["y"].AsDouble()) + ")";
    }
    if (!v.IsNumber()) {
        ok = false;
        return std::string();
    }
    return FloatLitOf(v.AsDouble());
}

template <class F>
GuardInfo GuardedVar(Writer& w, const std::string& name, const Json& value, int target, std::vector<Diagnostic>& diags,
                     const std::string& owner, F line_fn) {
    GuardInfo g;
    bool known = true;
    int minv = VarGuardVersion(name.c_str(), target, &known);
    bool ok = true;
    std::string val = VarValue(name, value, known, ok);
    if (!known || !ok) {
        Diagnostic d;
        d.level = ok ? Diagnostic::Level::Info : Diagnostic::Level::Warning;
        d.panel = owner;
        d.message = !ok ? "ImGuiStyleVar '" + name + "' has a value of the wrong shape; ignored"
                        : "unknown ImGuiStyleVar '" + name + "' emitted under #if IMGUI_VERSION_NUM >= " + std::to_string(minv);
        diags.push_back(d);
        if (!ok) return g;
    }
    if (IsDockingVar(name.c_str())) {
        g.open.push_back("#ifdef IMGUI_HAS_DOCK");
        g.close.push_back("#endif");
    }
    if (minv > EmitCtx::kMinImGuiVersion) {
        g.open.push_back("#if IMGUI_VERSION_NUM >= " + std::to_string(minv));
        g.close.insert(g.close.begin(), "#endif");
    }
    for (const std::string& l : g.open) w.Line(l);
    w.Line(line_fn(val));
    for (const std::string& l : g.close) w.Line(l);
    g.emitted = true;
    return g;
}

void InsertPop(std::vector<std::string>& pops, const GuardInfo& g, const char* pop) {
    std::vector<std::string> block = g.open;
    block.push_back(pop);
    block.insert(block.end(), g.close.begin(), g.close.end());
    pops.insert(pops.begin(), block.begin(), block.end());
}

void ExpandClass(const Project& project, const std::string& cls, std::vector<std::pair<std::string, Vec4>>& colors,
                 std::vector<std::pair<std::string, Json>>& vars, int depth) {
    if (depth > 8) return;
    for (const auto& sc : project.style_classes) {
        if (sc.first != cls) continue;
        for (const std::string& nested : sc.second.classes) ExpandClass(project, nested, colors, vars, depth + 1);
        for (const auto& c : sc.second.colors) colors.push_back(c);
        for (const auto& v : sc.second.vars) vars.push_back(v);
    }
}

}  // namespace

Result GenerateTheme(const Project&, const Theme& theme, const Options& opts) {
    Result r;
    Writer w;
    w.opts = &opts;
    std::string name = SanitizeIdentifier(theme.name);
    WriteFileHead(w, "theme=" + theme.name + " schema=1");
    w.Line("#pragma once");
    w.Line("#include \"imgui.h\"");
    OpenNamespace(w, opts);
    w.Line("inline void ApplyTheme_" + name + "(ImGuiStyle& style) {");
    {
        Writer::Scope s = w.Scoped();
        if (!theme.colors.empty()) w.Line("ImVec4* colors = style.Colors;");
        for (const auto& c : theme.colors) {
            Vec4 col = c.second;
            GuardedColor(w, c.first, opts.imgui_version, r.diagnostics, theme.name,
                         [&](const std::string& n) { return "colors[ImGuiCol_" + n + "] = " + ColorLit(col) + ";"; });
        }
        for (const auto& v : theme.vars) {
            std::string vn = SanitizeIdentifier(v.first);
            GuardedVar(w, v.first, v.second, opts.imgui_version, r.diagnostics, theme.name,
                       [&](const std::string& val) { return "style." + vn + " = " + val + ";"; });
        }
        w.UserSlot("theme_" + name + "_extra");
    }
    w.Line("}");
    CloseNamespace(w, opts);
    FileOut f;
    f.path = "imforge_theme_" + name + ".h";
    f.content = w.out;
    r.files.push_back(f);
    return r;
}

Result GenerateStyles(const Project& project, const Options& opts) {
    Result r;
    Writer w;
    w.opts = &opts;
    WriteFileHead(w, "styles schema=1");
    w.Line("#pragma once");
    w.Line("#include \"imgui.h\"");
    OpenNamespace(w, opts);
    const std::string& m = opts.settings.dpi_mode;
    std::string scale = m == "variable" ? "imb_scale" : (m == "none" ? "1.0f" : "imb_s");
    for (const auto& sc : project.style_classes) {
        std::string id = SanitizeIdentifier(sc.first);
        std::vector<std::pair<std::string, Vec4>> colors;
        std::vector<std::pair<std::string, Json>> vars;
        ExpandClass(project, sc.first, colors, vars, 0);
        std::string param = m == "variable" ? "float imb_scale" : "";
        w.Line("inline void Style_" + id + "_Push(" + param + ") {");
        std::vector<std::string> pops;
        {
            Writer::Scope s = w.Scoped();
            std::string decl = ScaleDecl(opts);
            bool any_px = false;
            for (const auto& v : vars) any_px = any_px || (v.first != "Alpha" && v.first != "DisabledAlpha" && v.first.find("Align") == std::string::npos && v.first.find("Angle") == std::string::npos);
            if (!decl.empty() && any_px) w.Line(decl);
            for (const auto& c : colors) {
                Vec4 col = c.second;
                GuardInfo g = GuardedColor(w, c.first, opts.imgui_version, r.diagnostics, sc.first,
                                           [&](const std::string& n) { return "ImGui::PushStyleColor(ImGuiCol_" + n + ", " + ColorLit(col) + ");"; });
                if (g.emitted) InsertPop(pops, g, "ImGui::PopStyleColor();");
            }
            for (const auto& v : vars) {
                std::string vn = SanitizeIdentifier(v.first);
                bool px = vn != "Alpha" && vn != "DisabledAlpha" && vn.find("Align") == std::string::npos && vn.find("Angle") == std::string::npos;
                GuardInfo g = GuardedVar(w, v.first, v.second, opts.imgui_version, r.diagnostics, sc.first, [&](const std::string& val) {
                    std::string value = val;
                    if (px && scale != "1.0f") {
                        if (value.compare(0, 7, "ImVec2(") == 0) {
                            std::string inner = value.substr(7, value.size() - 8);
                            size_t comma = inner.find(',');
                            value = "ImVec2(" + inner.substr(0, comma) + " * " + scale + "," + inner.substr(comma + 1) + " * " + scale + ")";
                        } else {
                            value += " * " + scale;
                        }
                    }
                    return "ImGui::PushStyleVar(ImGuiStyleVar_" + vn + ", " + value + ");";
                });
                if (g.emitted) InsertPop(pops, g, "ImGui::PopStyleVar();");
            }
            if (m == "variable" && !any_px) w.Line("(void)imb_scale;");
        }
        w.Line("}");
        w.Line("inline void Style_" + id + "_Pop() {");
        {
            Writer::Scope s = w.Scoped();
            for (const std::string& p : pops) w.Line(p);
        }
        w.Line("}");
    }
    CloseNamespace(w, opts);
    FileOut f;
    f.path = "imforge_styles.h";
    f.content = w.out;
    r.files.push_back(f);
    return r;
}

}  // namespace codegen
}  // namespace imforge
