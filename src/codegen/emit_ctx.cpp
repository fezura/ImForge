#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "emit_internal.h"

namespace imforge {
namespace codegen {

namespace {

double Finite(double v) {
    if (std::isnan(v)) return 0;
    if (std::isinf(v)) return v > 0 ? 3.4028234663852886e38 : -3.4028234663852886e38;
    return v;
}

std::string Trim3(double v) {
    v = Finite(v);
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.3f", v);
    std::string s = buf;
    while (s.size() > 1 && s.back() == '0' && s[s.size() - 2] != '.') s.pop_back();
    return s + "f";
}

bool IsIntegral(double v) { return v > -1e15 && v < 1e15 && v == static_cast<double>(static_cast<long long>(v)); }

std::string Upper(const std::string& s) {
    std::string out;
    for (char c : s) out.push_back((c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A') : (c == '-' ? '_' : c));
    return out;
}

bool StartsWith(const std::string& s, const char* p) { return s.compare(0, std::strlen(p), p) == 0; }

}  // namespace

int ColGuardVersion(const char* name, int target, bool* known) {
    int v = ImGuiColMinVersion(ImGuiColCanonicalName(name));
    if (known) *known = v != 0;
    return v != 0 ? v : (target > 19200 ? target : 19200);
}

int VarGuardVersion(const char* name, int target, bool* known) {
    int v = ImGuiStyleVarMinVersion(name);
    if (known) *known = v != 0;
    return v != 0 ? v : (target > 19200 ? target : 19200);
}

std::string Guarded(int min_version, const std::string& lines) {
    if (min_version <= EmitCtx::kMinImGuiVersion) return lines;
    return "#if IMGUI_VERSION_NUM >= " + std::to_string(min_version) + "\n" + lines + "\n#endif";
}

bool IsDockingColorName(const char* name) {
    size_t n = 0;
    const char* const* names = ImGuiColNamesDocking(n);
    for (size_t i = 0; i < n; ++i)
        if (std::strcmp(names[i], name) == 0) return true;
    return false;
}

bool IsDockingVarName(const char* name) {
    size_t n = 0;
    const char* const* names = ImGuiStyleVarNamesDocking(n);
    for (size_t i = 0; i < n; ++i)
        if (std::strcmp(names[i], name) == 0) return true;
    return false;
}

std::string GuardedStyle(bool docking, int min_version, const std::string& lines) {
    std::string out = Guarded(min_version, lines);
    return docking ? "#ifdef IMGUI_HAS_DOCK\n" + out + "\n#endif" : out;
}

std::string PushFontLine(const std::string& font) {
    return "#if IMGUI_VERSION_NUM >= 19200\nImGui::PushFont(" + font + ", 0.0f);\n#else\nImGui::PushFont(" + font + ");\n#endif";
}

std::string FnCase(const Options& o, const std::string& name) { return ApplyCase(name, o.settings.function_case); }
std::string VarCase(const Options& o, const std::string& name) { return ApplyCase(name, o.settings.variable_case); }

std::string OwnerIdent(const Project&, const Panel* panel, const Component* component) {
    if (panel) return SanitizeIdentifier(panel->name);
    if (component) return SanitizeIdentifier(component->name);
    return "Panel";
}

std::string StateTypeName(const Panel& p) { return SanitizeIdentifier(p.name) + "State"; }
std::string DrawFnName(const Options& o, const Panel& p) { return FnCase(o, "Draw" + SanitizeIdentifier(p.name)); }
std::string ComponentFnName(const Options& o, const Component& c, const char* prefix) {
    return FnCase(o, std::string(prefix) + SanitizeIdentifier(c.name));
}
std::string EventFnSuffix(const std::string& event) { return ToPascal(event); }

std::string BlockFnName(const EmitCtx& ctx, const Node& n) {
    return FnCase(ctx.opts, OwnerIdent(ctx.project, ctx.panel, ctx.component)) + "_" + SanitizeIdentifier(n.id) + "_Block";
}

std::string ScaleDecl(const Options& o) {
    if (o.settings.dpi_mode == "none" || o.settings.dpi_mode == "variable") return std::string();
    return "const float imb_s = ImGui::GetFontSize() / " + FloatLitOf(o.settings.base_font_size) + "; (void)imb_s;";
}

std::string NumLit(double v) {
    if (IsIntegral(v)) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(v));
        return buf;
    }
    return FloatLitOf(v);
}

std::string FloatLitOf(double v) {
    float f = static_cast<float>(Finite(v));
    if (std::isinf(f)) f = f > 0 ? 3.4028234663852886e38f : -3.4028234663852886e38f;
    std::string s = core_detail::FormatFloat(f);
    if (s.find_first_of(".eE") == std::string::npos) s += ".0";
    return s + "f";
}

std::string SizeExpr(const SizeSpec& s, bool is_width, const std::string& scale, const std::string& fill_override) {
    const char* axis = is_width ? ".x" : ".y";
    std::string px = NumLit(s.value);
    std::string scaled = scale == "1.0f" ? px : px + " * " + scale;
    switch (s.mode) {
    case SizeSpec::Mode::Auto: return "0";
    case SizeSpec::Mode::Px: return scaled;
    case SizeSpec::Mode::Fill: return is_width && !fill_override.empty() ? fill_override : "-FLT_MIN";
    case SizeSpec::Mode::Percent: return std::string("ImGui::GetContentRegionAvail()") + axis + " * (" + px + " / 100.0f)";
    case SizeSpec::Mode::FillMinus:
        if (is_width) return "std::max(ImGui::GetContentRegionAvail().x - " + scaled + ", 1.0f)";
        return "ImGui::GetContentRegionAvail().y - " + scaled;
    }
    return "0";
}

bool HasInterpolation(const std::string& raw) {
    for (size_t i = 0; i < raw.size(); ++i) {
        if (raw[i] == '{') {
            if (i + 1 < raw.size() && raw[i + 1] == '{') {
                ++i;
                continue;
            }
            return true;
        }
    }
    return false;
}

bool IsContainerType(const std::string& t) {
    static const char* const kTypes[] = {"Window", "VStack", "HStack", "Group", "Child", "Grid", "TabBar", "Tab",
                                         "CollapsingHeader", "TreeNode", "Popup", "Modal", "MenuBar", "Menu",
                                         "Canvas"};
    for (const char* k : kTypes)
        if (t == k) return true;
    return false;
}

std::string ParamCppType(const Param& p, const Options& opts, bool* is_binding) {
    if (is_binding) *is_binding = false;
    const std::string& t = p.type;
    if (StartsWith(t, "binding<")) {
        if (is_binding) *is_binding = true;
        std::string inner = t.substr(8, t.size() - 9);
        if (inner == "bool") return "bool*";
        if (inner == "int") return "int*";
        if (inner == "float") return "float*";
        if (inner == "string") return opts.settings.std_string ? "std::string*" : "char*";
        if (inner == "vec2") return "ImVec2*";
        if (inner == "vec4" || inner == "color") return "ImVec4*";
        return "float*";
    }
    if (t == "string") return "const char*";
    if (t == "int" || StartsWith(t, "enum:")) return "int";
    if (t == "float") return "float";
    if (t == "bool") return "bool";
    if (t == "color" || t == "vec4") return "ImVec4";
    if (t == "vec2") return "ImVec2";
    return t;
}

EmitCtx::EmitCtx(const Project& p, const Panel* pn, const Component* c, const Options& o, Writer& wr,
                 std::vector<LiteralRef>& l, std::vector<Diagnostic>& d)
    : project(p), panel(pn), component(c), opts(o), w(wr), literals(l), diags(d) {
    state_var = panel ? "state" : "";
    const std::string& m = opts.settings.dpi_mode;
    scale_expr = m == "variable" ? "imb_scale" : (m == "none" ? "1.0f" : "imb_s");
}

std::string EmitCtx::OwnerName() const { return panel ? panel->name : (component ? component->name : std::string()); }

const WidgetDef* EmitCtx::Def(const Node& n) const {
    if (project.widget_catalog) return project.widget_catalog->Find(n.type.c_str());
    return FindWidget(n.type.c_str());
}

bool EmitCtx::Has(const Node& n, const char* prop) const {
    const PropValue* p = n.Prop(prop);
    return p && !p->raw.IsNull();
}

bool EmitCtx::IsExpr(const Node& n, const char* prop) const {
    const PropValue* p = n.Prop(prop);
    return p && p->IsExpr();
}

const Json& EmitCtx::PropJson(const Node& n, const char* prop) const {
    const PropValue* p = n.Prop(prop);
    return p ? p->raw : Json::Null();
}

std::string EmitCtx::Px(double px) const {
    std::string lit = NumLit(px);
    return scale_expr == "1.0f" ? lit : lit + " * " + scale_expr;
}

std::string EmitCtx::Scaled(const std::string& lit) const { return scale_expr == "1.0f" ? lit : lit + " * " + scale_expr; }
std::string EmitCtx::Id(const Node& n) const { return StrLit(n.id); }
std::string EmitCtx::Local(const Node& n, const char* suffix) const { return SanitizeIdentifier(n.id) + "_" + suffix; }
std::string EmitCtx::FloatLit(double v) const { return FloatLitOf(v); }
std::string EmitCtx::IntLit(long long v) const { return std::to_string(v); }
std::string EmitCtx::StrLit(const std::string& s) const { return CppStringLiteral(s); }

std::string EmitCtx::ColorLit(const Vec4& c) const {
    return "ImVec4(" + Trim3(c.x) + ", " + Trim3(c.y) + ", " + Trim3(c.z) + ", " + Trim3(c.w) + ")";
}

std::string EmitCtx::ColorU32Lit(const Vec4& c) const {
    auto b = [](float f) {
        float g = f * 255.0f + 0.5f;
        return !(g > 0.0f) ? 0 : (g >= 255.0f ? 255 : static_cast<int>(g));
    };
    return "IM_COL32(" + std::to_string(b(c.x)) + ", " + std::to_string(b(c.y)) + ", " + std::to_string(b(c.z)) + ", " +
           std::to_string(b(c.w)) + ")";
}

std::string EmitCtx::IconMacro(const std::string& icon) {
    std::string set = "fa";
    std::string name = icon;
    if (StartsWith(icon, "fa-")) name = icon.substr(3);
    else if (StartsWith(icon, "mdi-")) { set = "md"; name = icon.substr(4); }
    else if (StartsWith(icon, "md-")) { set = "md"; name = icon.substr(3); }
    else if (StartsWith(icon, "lucide-")) { set = "lc"; name = icon.substr(7); }
    else if (StartsWith(icon, "lc-")) { set = "lc"; name = icon.substr(3); }
    else {
        for (const FontSpec& f : project.fonts) {
            if (f.merge_icons == "material") set = "md";
            else if (f.merge_icons == "lucide") set = "lc";
            if (!f.merge_icons.empty()) break;
        }
    }
    if (set == "md") Include("\"IconsMaterialDesign.h\"");
    else if (set == "lc") Include("\"IconsLucide.h\"");
    else Include("\"IconsFontAwesome6.h\"");
    if (!opts.features_icons) {
        bool warned = false;
        for (const Diagnostic& d : diags) warned = warned || d.message.find("iconFonts") != std::string::npos;
        if (!warned) Diag(Diagnostic::Level::Warning, nullptr, nullptr, "icon used while the iconFonts feature is off; ICON_* macros are emitted anyway");
    }
    return "ICON_" + Upper(set) + "_" + SanitizeIdentifier(Upper(name));
}

void EmitCtx::PreLine(const std::string& line) { w.Line(line); }
void EmitCtx::MarkLiteral(const Node& n, const char* prop) { w.MarkNextLiteral(n.id, prop ? prop : ""); }
void EmitCtx::EmitChildren(const Node& n) { codegen::EmitChildren(*this, n); }

void EmitCtx::Include(const std::string& header) {
    for (const std::string& h : includes)
        if (h == header) return;
    includes.push_back(header);
}

void EmitCtx::Diag(Diagnostic::Level level, const Node* n, const char* prop, const std::string& msg) {
    Diagnostic d;
    d.level = level;
    d.message = msg;
    d.panel = OwnerName();
    if (n) d.node_id = n->id;
    if (prop) d.prop = prop;
    diags.push_back(d);
}

// prop helpers

namespace {

const Param* FindParam(const EmitCtx& ctx, const std::string& name) {
    if (!ctx.component) return nullptr;
    for (const Param& p : ctx.component->params)
        if (p.name == name) return &p;
    return nullptr;
}

std::string ParamRef(EmitCtx& ctx, const Node& n, const char* prop, const std::string& name) {
    if (!FindParam(ctx, name)) {
        ctx.Diag(Diagnostic::Level::Error, &n, prop, "unknown component parameter '" + name + "'");
        return "0";
    }
    return SanitizeIdentifier(name);
}

std::string DefaultString(const EmitCtx& ctx, const Node& n, const char* prop) {
    const WidgetDef* d = ctx.Def(n);
    const PropDef* p = d ? FindProp(*d, prop) : nullptr;
    if (!p) return std::string();
    Json j = PropDefaultJson(*p);
    return j.IsString() ? j.AsString() : std::string();
}

}  // namespace

std::string EmitCtx::Str(const Node& n, const char* prop, const char* def) {
    const PropValue* p = n.Prop(prop);
    if (!p || p->raw.IsNull()) return def ? w.MarkToken(n.id, prop) + StrLit(def) : std::string();
    if (p->IsExpr()) return ExprCStr(p->Expr());
    if (p->IsParam()) return ParamRef(*this, n, prop, p->Param());
    if (p->raw.IsString()) return w.MarkToken(n.id, prop) + StrLit(p->raw.AsString());
    return w.MarkToken(n.id, prop) + StrLit(Value::Infer(p->raw).ToString());
}

std::string EmitCtx::Num(const Node& n, const char* prop, double def, bool is_float) {
    const PropValue* p = n.Prop(prop);
    auto lit = [&](double v) {
        v = Finite(v);
        if (!is_float) v = v > 2147483647.0 ? 2147483647.0 : (v < -2147483648.0 ? -2147483648.0 : v);
        return w.MarkToken(n.id, prop) + (is_float ? FloatLitOf(v) : std::to_string(static_cast<long long>(v)));
    };
    if (!p || p->raw.IsNull()) return lit(def);
    if (p->IsExpr()) return Expr(p->Expr());
    if (p->IsParam()) return ParamRef(*this, n, prop, p->Param());
    if (p->raw.IsNumber()) return lit(p->raw.AsDouble());
    if (p->raw.IsBool()) return lit(p->raw.AsBool() ? 1 : 0);
    if (p->raw.IsString()) return lit(std::strtod(p->raw.AsString().c_str(), nullptr));
    return lit(def);
}

bool EmitCtx::Bool(const Node& n, const char* prop, bool def) {
    const PropValue* p = n.Prop(prop);
    if (!p || p->raw.IsNull()) return def;
    if (p->raw.IsBool()) return p->raw.AsBool();
    if (p->raw.IsNumber()) return p->raw.AsDouble() != 0;
    if (p->raw.IsString()) return p->raw.AsString() == "true" || p->raw.AsString() == "1";
    return def;
}

std::string EmitCtx::BoolLit(const Node& n, const char* prop, bool def) {
    const PropValue* p = n.Prop(prop);
    if (p && p->IsExpr()) return Expr(p->Expr());
    if (p && p->IsParam()) return ParamRef(*this, n, prop, p->Param());
    return w.MarkToken(n.id, prop) + (Bool(n, prop, def) ? "true" : "false");
}

std::string EmitCtx::Color(const Node& n, const char* prop, const char* def) {
    const PropValue* p = n.Prop(prop);
    if (!p || p->raw.IsNull()) {
        if (!def) return std::string();
        return w.MarkToken(n.id, prop) + ColorLit(ParseColor(def));
    }
    if (p->IsExpr()) return Expr(p->Expr());
    if (p->IsParam()) return ParamRef(*this, n, prop, p->Param());
    if (p->raw.IsString()) {
        bool ok = false;
        Vec4 c = ParseColor(p->raw.AsString(), &ok);
        if (!ok) Diag(Diagnostic::Level::Warning, &n, prop, "invalid color '" + p->raw.AsString() + "'");
        return w.MarkToken(n.id, prop) + ColorLit(c);
    }
    return w.MarkToken(n.id, prop) + ColorLit(Value::Infer(p->raw).ToVec4());
}

std::string EmitCtx::ColorU32(const Node& n, const char* prop, const char* def) {
    const PropValue* p = n.Prop(prop);
    if (!p || p->raw.IsNull()) return def ? w.MarkToken(n.id, prop) + ColorU32Lit(ParseColor(def)) : std::string();
    if (p->IsExpr()) return "ImGui::GetColorU32(" + Expr(p->Expr()) + ")";
    if (p->IsParam()) return "ImGui::GetColorU32(" + ParamRef(*this, n, prop, p->Param()) + ")";
    if (p->raw.IsString()) return w.MarkToken(n.id, prop) + ColorU32Lit(ParseColor(p->raw.AsString()));
    return w.MarkToken(n.id, prop) + ColorU32Lit(Value::Infer(p->raw).ToVec4());
}

std::string EmitCtx::Vec2(const Node& n, const char* prop, const char* def_json, bool scaled) {
    const PropValue* p = n.Prop(prop);
    Json j;
    if (p && !p->raw.IsNull()) {
        if (p->IsExpr()) return Expr(p->Expr());
        if (p->IsParam()) return ParamRef(*this, n, prop, p->Param());
        j = p->raw;
    } else {
        if (!def_json || !ParseJson(def_json, std::strlen(def_json), j, nullptr) || j.IsNull()) return std::string();
    }
    double x = 0, y = 0;
    if (j.IsObject()) {
        x = j["x"].AsDouble();
        y = j["y"].AsDouble();
    } else if (j.IsArray() && j.Size() >= 2) {
        x = j[0].AsDouble();
        y = j[1].AsDouble();
    } else if (j.IsNumber()) {
        x = y = j.AsDouble();
    }
    std::string sx = scaled ? Px(x) : NumLit(x), sy = scaled ? Px(y) : NumLit(y);
    return w.MarkToken(n.id, prop) + "ImVec2(" + sx + ", " + sy + ")";
}

std::string EmitCtx::Vec2(const Node& n, const char* prop, double dx, double dy, bool scaled) {
    char buf[96];
    std::snprintf(buf, sizeof(buf), "{\"x\":%.9g,\"y\":%.9g}", dx, dy);
    return Vec2(n, prop, buf, scaled);
}

std::string EmitCtx::Size(const Node& n, const char* prop) {
    const PropValue* p = n.Prop(prop);
    if (!p || p->raw.IsNull()) return std::string();
    if (p->IsExpr()) return Expr(p->Expr());
    if (p->IsParam()) return ParamRef(*this, n, prop, p->Param());
    SizeSpec sw, sh;
    const Json& j = p->raw;
    if (j.IsObject()) {
        if (!SizeSpec::Parse(j["w"], sw)) sw = SizeSpec();
        if (!SizeSpec::Parse(j["h"], sh)) sh = SizeSpec();
    } else if (!SizeSpec::Parse(j, sw)) {
        sw = SizeSpec();
    }
    if (sw.mode == SizeSpec::Mode::FillMinus) Include("<algorithm>");
    return w.MarkToken(n.id, prop) + "ImVec2(" + SizeExpr(sw, true, scale_expr, fill_w) + ", " +
           SizeExpr(sh, false, scale_expr, std::string()) + ")";
}

std::vector<std::pair<std::string, int>> EmitCtx::FlagList(const Node& n, const char* prop, const char* enum_name) {
    std::vector<std::pair<std::string, int>> out;
    std::string en = detail::StripImGuiPrefix(enum_name);
    std::string prefix = "ImGui" + en + "_";
    const PropValue* p = n.Prop(prop);
    std::vector<std::string> names;
    if (p && !p->raw.IsNull()) {
        if (p->raw.IsArray()) {
            for (size_t i = 0; i < p->raw.Size(); ++i)
                if (p->raw[i].IsString()) names.push_back(p->raw[i].AsString());
        } else if (p->raw.IsString()) {
            for (const std::string& s : SplitPipe(p->raw.AsString().c_str())) names.push_back(s);
        }
    }
    for (const std::string& name : names) {
        std::string bare = name;
        if (StartsWith(bare, prefix.c_str())) bare = bare.substr(prefix.size());
        const FlagDef* f = FindFlag(en.c_str(), bare.c_str());
        if (!f) {
            Diag(Diagnostic::Level::Warning, &n, prop, "unknown " + en + " flag '" + name + "' ignored");
            continue;
        }
        std::string text = prefix + bare;
        int minv = f->min_version;
        if (const FlagRename* r = FindFlagRename(en.c_str(), bare.c_str())) {
            text = prefix + r->new_name + "\x04" + prefix + r->old_name;
            minv = r->since;
        } else if (minv > kMinImGuiVersion && minv > Target()) {
            Diag(Diagnostic::Level::Info, &n, prop,
                 prefix + bare + " needs ImGui " + Project::VersionString(minv) + "; emitted under #if IMGUI_VERSION_NUM >= " +
                     std::to_string(minv));
        }
        bool dup = false;
        for (const auto& o : out) dup = dup || o.first == text;
        if (!dup) out.emplace_back(text, minv);
    }
    return out;
}

std::string EmitCtx::FlagsExpr(const Node& n, const char* prop, const std::vector<std::pair<std::string, int>>& flags,
                               int floor, std::vector<std::string>& pre_lines, const std::string& var) {
    std::string base;
    std::vector<std::pair<int, std::string>> guarded;
    std::vector<std::pair<int, std::pair<std::string, std::string>>> renamed;
    for (const auto& f : flags) {
        size_t sep = f.first.find('\x04');
        if (sep != std::string::npos) {
            renamed.emplace_back(f.second, std::make_pair(f.first.substr(0, sep), f.first.substr(sep + 1)));
        } else if (f.second <= floor) {
            base += (base.empty() ? "" : " | ") + f.first;
        } else {
            bool merged = false;
            for (auto& g : guarded) {
                if (g.first == f.second) {
                    g.second += " | " + f.first;
                    merged = true;
                }
            }
            if (!merged) guarded.emplace_back(f.second, f.first);
        }
    }
    if (guarded.empty() && renamed.empty()) return w.MarkToken(n.id, prop) + (base.empty() ? std::string("0") : base);
    pre_lines.push_back("int " + var + " = " + (base.empty() ? std::string("0") : base) + ";");
    for (const auto& g : guarded) {
        pre_lines.push_back("#if IMGUI_VERSION_NUM >= " + std::to_string(g.first));
        pre_lines.push_back(var + " |= " + g.second + ";");
        pre_lines.push_back("#endif");
    }
    for (const auto& r : renamed) {
        pre_lines.push_back("#if IMGUI_VERSION_NUM >= " + std::to_string(r.first));
        pre_lines.push_back(var + " |= " + r.second.first + ";");
        pre_lines.push_back("#else");
        pre_lines.push_back(var + " |= " + r.second.second + ";");
        pre_lines.push_back("#endif");
    }
    return var;
}

std::string EmitCtx::Flags(const Node& n, const char* prop, const char* enum_name) {
    const PropValue* p = n.Prop(prop);
    if (p && p->IsParam()) return ParamRef(*this, n, prop, p->Param());
    std::vector<std::string> pre;
    std::string e = FlagsExpr(n, prop, FlagList(n, prop, enum_name), Version(), pre, Local(n, prop));
    for (const std::string& l : pre) w.Line(l);
    return e;
}

std::string EmitCtx::EnumValue(const Node& n, const char* prop, const char* def) {
    const PropValue* p = n.Prop(prop);
    std::string name = def ? def : "";
    if (p && !p->raw.IsNull()) {
        if (p->IsParam()) return ParamRef(*this, n, prop, p->Param());
        if (p->raw.IsString()) name = p->raw.AsString();
    }
    const WidgetDef* d = Def(n);
    const PropDef* pd = d ? FindProp(*d, prop) : nullptr;
    if (pd && pd->type == PropType::Enum && !IsValidEnumValue(*pd, name.c_str())) {
        Diag(Diagnostic::Level::Warning, &n, prop, "unknown enum value '" + name + "'");
        std::vector<std::string> vals = EnumValues(*pd);
        if (!vals.empty()) name = vals[0];
    }
    return w.MarkToken(n.id, prop) + SanitizeIdentifier(name);
}

std::string EmitCtx::EnumValue(const Node& n, const char* prop, const char* prefix, const char* def) {
    std::string v = EnumValue(n, prop, def);
    size_t cut = v.find('\x02');
    if (cut != std::string::npos) return v.substr(0, cut + 1) + prefix + v.substr(cut + 1);
    return prefix + v;
}

std::vector<std::string> EmitCtx::StringList(const Node& n, const char* prop) {
    std::vector<std::string> out;
    const PropValue* p = n.Prop(prop);
    if (!p) return out;
    if (p->raw.IsArray()) {
        for (size_t i = 0; i < p->raw.Size(); ++i) out.push_back(p->raw[i].IsString() ? p->raw[i].AsString() : Value::Infer(p->raw[i]).ToString());
    } else if (p->raw.IsString()) {
        out.push_back(p->raw.AsString());
    }
    return out;
}

// interpolation

TextFmt EmitCtx::Text(const Node& n, const char* prop, const char* def) {
    const PropValue* p = n.Prop(prop);
    TextFmt t;
    if (p && p->IsExpr()) {
        t.fmt = "\"%s\"";
        t.args.push_back(ExprCStr(p->Expr()));
        t.plain = false;
        return t;
    }
    if (p && p->IsParam()) {
        t.fmt = "\"%s\"";
        t.args.push_back(ParamRef(*this, n, prop, p->Param()));
        t.plain = false;
        return t;
    }
    std::string raw;
    if (p && !p->raw.IsNull()) raw = p->raw.IsString() ? p->raw.AsString() : Value::Infer(p->raw).ToString();
    else if (def) raw = def;
    else raw = DefaultString(*this, n, prop);
    return TextOf(n, prop, raw);
}

TextFmt EmitCtx::TextOf(const Node& n, const char* prop, const std::string& raw) {
    TextFmt t;
    std::vector<expr::InterpSegment> segs;
    imforge::Error err;
    if (!expr::ParseInterpolation(raw, segs, &err)) {
        Diag(Diagnostic::Level::Error, &n, prop, "interpolation: " + err.message);
        t.fmt = w.MarkToken(n.id, prop) + StrLit(raw);
        return t;
    }
    bool any_interp = false;
    for (const expr::InterpSegment& s : segs) any_interp = any_interp || s.is_expr || s.is_icon;
    if (!any_interp) {
        std::string text;
        for (const expr::InterpSegment& s : segs) text += s.text_or_expr;
        std::string esc;
        for (char c : text) {
            esc.push_back(c);
            if (c == '%') esc.push_back('%');
        }
        t.fmt = w.MarkToken(n.id, prop) + StrLit(esc);
        return t;
    }
    std::vector<std::string> pieces;
    std::string lit;
    auto flush = [&]() {
        if (!lit.empty()) pieces.push_back(StrLit(lit));
        lit.clear();
    };
    for (const expr::InterpSegment& s : segs) {
        if (s.is_icon) {
            flush();
            pieces.push_back(IconMacro(s.text_or_expr));
            continue;
        }
        if (!s.is_expr) {
            for (char c : s.text_or_expr) {
                lit.push_back(c);
                if (c == '%') lit.push_back('%');
            }
            continue;
        }
        expr::Type ty = ExprType(s.text_or_expr);
        std::string fmt = s.fmt;
        char conv = 0;
        if (!fmt.empty()) {
            expr::detail::FmtSpec spec;
            if (expr::detail::ParseFmtSpec(fmt, spec) && spec.start != std::string::npos) conv = spec.conv;
        }
        if (conv == 0) {
            switch (ty) {
            case expr::Type::Int: case expr::Type::Enum: fmt = "%d"; conv = 'd'; break;
            case expr::Type::Float: fmt = "%.2f"; conv = 'f'; break;
            case expr::Type::String: case expr::Type::Bool: fmt = "%s"; conv = 's'; break;
            default:
                fmt = "%.2f";
                conv = 'f';
                Diag(Diagnostic::Level::Info, &n, prop, "type of '" + s.text_or_expr + "' is unknown; formatted as float");
                break;
            }
        }
        std::string cpp = Expr(s.text_or_expr);
        std::string arg;
        if (std::strchr("diuxXoc", conv)) {
            bool ll = fmt.find("ll") != std::string::npos;
            bool l = !ll && fmt.find('l') != std::string::npos;
            arg = std::string(ll ? "(long long)" : (l ? "(long)" : "(int)")) + cpp;
        } else if (std::strchr("fFeEgGaA", conv)) {
            arg = "(double)" + cpp;
        } else if (conv == 's') {
            if (ty == expr::Type::Bool) arg = "(" + cpp + ") ? \"true\" : \"false\"";
            else arg = ExprCStr(s.text_or_expr);
        } else {
            arg = cpp;
        }
        lit += fmt;
        t.args.push_back(arg);
    }
    flush();
    for (size_t i = 0; i < pieces.size(); ++i) t.fmt += (i ? " " : "") + pieces[i];
    if (t.fmt.empty()) t.fmt = "\"\"";
    t.plain = t.args.empty();
    return t;
}

bool EmitCtx::CStrIsLiteral(const std::string& raw) const {
    std::vector<expr::InterpSegment> segs;
    if (!expr::ParseInterpolation(raw, segs, nullptr)) return true;
    for (const expr::InterpSegment& s : segs)
        if (s.is_expr) return false;
    return true;
}

std::string EmitCtx::CStr(const Node& n, const char* prop, const std::string& raw, const char* buf_suffix) {
    std::vector<expr::InterpSegment> segs;
    imforge::Error err;
    if (!expr::ParseInterpolation(raw, segs, &err)) {
        Diag(Diagnostic::Level::Error, &n, prop, "interpolation: " + err.message);
        return w.MarkToken(n.id, prop) + StrLit(raw);
    }
    if (!CStrIsLiteral(raw)) {
        TextFmt t = TextOf(n, prop, raw);
        std::string buf = Local(n, buf_suffix);
        Include("<cstdio>");
        w.Line("char " + buf + "[256];");
        w.Line("std::snprintf(" + buf + ", sizeof(" + buf + "), " + t.CallArgs() + ");");
        return buf;
    }
    std::vector<std::string> pieces;
    std::string lit;
    for (const expr::InterpSegment& seg : segs) {
        if (seg.is_icon) {
            if (!lit.empty()) pieces.push_back(StrLit(lit));
            lit.clear();
            pieces.push_back(IconMacro(seg.text_or_expr));
        } else {
            lit += seg.text_or_expr;
        }
    }
    bool has_icon = !pieces.empty();
    if (!lit.empty() || pieces.empty()) pieces.push_back(StrLit(lit));
    std::string out = has_icon ? std::string() : w.MarkToken(n.id, prop);
    for (size_t i = 0; i < pieces.size(); ++i) out += (i ? " " : "") + pieces[i];
    return out;
}

std::string EmitCtx::Label(const Node& n, const char* prop) {
    const PropValue* p = n.Prop(prop);
    if (p && p->IsParam()) return ParamRef(*this, n, prop, p->Param());
    if (p && p->IsExpr()) return ExprCStr(p->Expr());
    std::string raw;
    if (p && !p->raw.IsNull()) raw = p->raw.IsString() ? p->raw.AsString() : Value::Infer(p->raw).ToString();
    else raw = DefaultString(*this, n, prop);
    bool collide = false;
    if (parent) {
        auto scan = [&](const std::vector<Node>& sibs) {
            if (sibs.empty() || &n < sibs.data() || &n >= sibs.data() + sibs.size()) return;
            auto key = std::make_pair(static_cast<const void*>(&sibs), std::string(prop));
            auto it = label_cache.find(key);
            if (it == label_cache.end()) {
                std::map<std::string, std::vector<std::pair<std::string, bool>>> by_label;
                for (const Node& s : sibs) {
                    if (s.type.empty()) continue;
                    const PropValue* sp = s.Prop(prop);
                    std::string other;
                    if (sp && sp->raw.IsString()) other = sp->raw.AsString();
                    else if (!sp || sp->raw.IsNull()) other = DefaultString(*this, s, prop);
                    by_label[other].emplace_back(s.type, sp != nullptr);
                }
                it = label_cache.emplace(key, std::move(by_label)).first;
            }
            auto hit = it->second.find(raw);
            if (hit == it->second.end()) return;
            int same = 0;
            for (const auto& e : hit->second)
                if (e.second || e.first == n.type) ++same;
            if (same > 1) collide = true;
        };
        scan(parent->children);
        for (const auto& sl : parent->slots) scan(sl.second);
    }
    std::string suffix = collide && raw.find("###") == std::string::npos && !raw.empty() ? "###" + n.id : std::string();
    if (!HasInterpolation(raw) || CStrIsLiteral(raw)) {
        std::string s = CStr(n, prop, raw, "label");
        return suffix.empty() ? s : s + " " + StrLit(suffix);
    }
    TextFmt t = TextOf(n, prop, raw);
    std::string buf = Local(n, "label");
    Include("<cstdio>");
    std::string fmt = t.fmt;
    if (!suffix.empty()) fmt += " " + StrLit(suffix);
    w.Line("char " + buf + "[256];");
    std::string call = "std::snprintf(" + buf + ", sizeof(" + buf + "), " + fmt;
    for (const std::string& a : t.args) call += ", " + a;
    w.Line(call + ");");
    return buf;
}

}  // namespace codegen
}  // namespace imforge
