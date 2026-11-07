#include "panel_refactor.h"

#include "imforge/imforge_expr.h"

namespace imforge::builder {

namespace {

using expr::detail::Tok;

enum class SiteKind { Expr, Interp };
enum class PropSite { Skip, Expr, Interp, PathOrInterp };

struct Hit {
    size_t pos = 0, member_pos = 0;
    bool has_member = false;
    std::string root, member;
};

bool ScanExpr(const std::string& src, size_t base, std::vector<Hit>& hits) {
    std::vector<expr::detail::Token> toks;
    Error err;
    if (!expr::detail::Lex(src, toks, &err)) return false;
    for (size_t k = 0; k + 1 < toks.size(); ++k) {
        const expr::detail::Token& t = toks[k];
        if (t.kind != Tok::Ident) continue;
        Tok prev = k ? toks[k - 1].kind : Tok::End;
        if (prev == Tok::Dot || prev == Tok::ColonColon || toks[k + 1].kind == Tok::ColonColon) continue;
        Hit h;
        h.pos = base + (size_t)t.pos;
        h.root = t.text;
        if (toks[k + 1].kind == Tok::Dot && toks[k + 2].kind == Tok::Ident) {
            h.has_member = true;
            h.member = toks[k + 2].text;
            h.member_pos = base + (size_t)toks[k + 2].pos;
        }
        hits.push_back(std::move(h));
    }
    return true;
}

void ScanInterp(const std::string& s, std::vector<Hit>& hits) {
    size_t i = 0, n = s.size();
    while (i < n) {
        char c = s[i];
        if (c == '{') {
            if (i + 1 < n && s[i + 1] == '{') {
                i += 2;
                continue;
            }
            size_t close = expr::detail::FindInterpClose(s, i + 1);
            if (close == std::string::npos) return;
            std::string raw = s.substr(i + 1, close - i - 1);
            std::string body = expr::detail::Trim(raw);
            bool icon = body.compare(0, 5, "icon:") == 0 && (body.size() == 5 || body[5] != '%');
            if (!body.empty() && !icon) {
                size_t sep = expr::detail::FindFmtSep(raw);
                ScanExpr(sep == std::string::npos ? raw : raw.substr(0, sep), i + 1, hits);
            }
            i = close + 1;
            continue;
        }
        if (c == '}' && i + 1 < n && s[i + 1] == '}') {
            i += 2;
            continue;
        }
        ++i;
    }
}

std::vector<Hit> Scan(const std::string& s, SiteKind kind) {
    std::vector<Hit> hits;
    if (kind == SiteKind::Expr) ScanExpr(s, 0, hits);
    else if (s.find('{') != std::string::npos) ScanInterp(s, hits);
    return hits;
}

bool Matches(const Hit& h, const RenameTarget& t, size_t& at) {
    if (t.state) {
        if (h.root != "state" || !h.has_member || h.member != t.from) return false;
        at = h.member_pos;
        return true;
    }
    if (h.root != t.from) return false;
    at = h.pos;
    return true;
}

std::string Rewrite(const std::string& src, SiteKind kind, const RenameTarget& t, int& count) {
    if (t.from.empty() || t.from == t.to) return src;
    std::vector<Hit> hits = Scan(src, kind);
    std::string out;
    size_t last = 0;
    int n = 0;
    for (const Hit& h : hits) {
        size_t at = 0;
        if (!Matches(h, t, at) || at < last) continue;
        out.append(src, last, at - last);
        out += t.to;
        last = at + t.from.size();
        ++n;
    }
    if (n == 0) return src;
    out.append(src, last, std::string::npos);
    count += n;
    return out;
}

bool IsPurePath(const std::string& s) {
    expr::Ast ast;
    Error err;
    return expr::Parse(s, ast, &err) && ast.kind == expr::Ast::Kind::Path;
}

const WidgetDef* FindDef(const Project* project, const std::string& type) {
    if (project && project->widget_catalog) return project->widget_catalog->Find(type.c_str());
    return FindWidget(type.c_str());
}

PropSite ComponentPropSite(const Node& n, const std::string& key, const Project* project) {
    if (key == "component") return PropSite::Skip;
    const Component* def = nullptr;
    if (project)
        for (const Component& c : project->components)
            if (c.name == n.component) def = &c;
    if (!def) return PropSite::PathOrInterp;
    for (const Param& p : def->params) {
        if (p.name != key) continue;
        if (p.type.compare(0, 8, "binding<") == 0) return PropSite::Expr;
        return p.type == "string" ? PropSite::Interp : PropSite::Skip;
    }
    return PropSite::Skip;
}

PropSite StringPropSite(const Node& n, const std::string& key, const Project* project) {
    if (n.type == "Component") return ComponentPropSite(n, key, project);
    const WidgetDef* def = FindDef(project, n.type);
    if (!def) return PropSite::Interp;
    for (int i = 0; i < def->prop_count; ++i) {
        if (key != def->props[i].name) continue;
        switch (def->props[i].type) {
        case PropType::String:
        case PropType::StringList: return PropSite::Interp;
        case PropType::Expr: return PropSite::Expr;
        default: return PropSite::Skip;
        }
    }
    return PropSite::Interp;
}

template <class JsonT, class F>
void VisitJsonExprs(JsonT& j, const std::string& where, F& f) {
    if (j.IsObject()) {
        for (auto& kv : j.obj) {
            if (kv.first == "expr" && kv.second.IsString()) f(kv.second.str, SiteKind::Expr, where);
            else VisitJsonExprs(kv.second, where, f);
        }
    } else if (j.IsArray()) {
        for (auto& e : j.arr) VisitJsonExprs(e, where, f);
    }
}

template <class NodeT, class F>
void VisitNodeSites(NodeT& n, const Project* project, F& f) {
    f(n.visible_if, SiteKind::Expr, std::string("visibleIf"));
    f(n.enabled_if, SiteKind::Expr, std::string("enabledIf"));
    f(n.tooltip, SiteKind::Interp, std::string("tooltip"));
    for (auto& b : n.bind) {
        std::string where = "bind." + b.first;
        if (b.second.raw.IsString()) f(b.second.raw.str, SiteKind::Expr, where);
        else VisitJsonExprs(b.second.raw, where, f);
    }
    for (auto& p : n.props) {
        std::string where = "props." + p.first;
        auto& raw = p.second.raw;
        if (!raw.IsString() && !raw.IsArray()) {
            VisitJsonExprs(raw, where, f);
            continue;
        }
        PropSite site = StringPropSite(n, p.first, project);
        if (site == PropSite::Skip) continue;
        if (raw.IsString()) {
            SiteKind kind = site == PropSite::Expr ? SiteKind::Expr
                            : site == PropSite::Interp ? SiteKind::Interp
                            : IsPurePath(raw.str) ? SiteKind::Expr : SiteKind::Interp;
            f(raw.str, kind, where);
            continue;
        }
        for (auto& e : raw.arr) {
            if (e.IsString()) f(e.str, site == PropSite::Expr ? SiteKind::Expr : SiteKind::Interp, where);
            else VisitJsonExprs(e, where, f);
        }
    }
    for (auto& ev : n.events) {
        std::string where = "events." + ev.first;
        for (auto& a : ev.second.actions) {
            if (a.type == "set" || a.type == "toggle") f(a.target, SiteKind::Expr, where + ".target");
            if (a.value.IsString()) {
                if (a.type == "set" || a.type == "copyClipboard") f(a.value.str, SiteKind::Interp, where + ".value");
            } else {
                VisitJsonExprs(a.value, where + ".value", f);
            }
        }
    }
    for (auto& fx : n.fx) VisitJsonExprs(fx.params, "fx." + fx.type, f);
    for (auto& t : n.anim.transitions) VisitJsonExprs(t.to, "anim." + t.prop, f);
    for (auto& v : n.style.vars) VisitJsonExprs(v.second, "style." + v.first, f);
}

int RenameInTree(Node& root, const RenameTarget& t, const Project* project) {
    int count = 0;
    auto rename = [&](std::string& s, SiteKind kind, const std::string&) { s = Rewrite(s, kind, t, count); };
    Walk(root, [&](Node& n) { VisitNodeSites(n, project, rename); });
    return count;
}

bool IsUpperC(char c) { return c >= 'A' && c <= 'Z'; }
bool IsLowerC(char c) { return c >= 'a' && c <= 'z'; }
bool IsDigitC(char c) { return c >= '0' && c <= '9'; }
bool IsAlnumC(char c) { return IsUpperC(c) || IsLowerC(c) || IsDigitC(c); }
char LowerC(char c) { return IsUpperC(c) ? (char)(c - 'A' + 'a') : c; }
char UpperC(char c) { return IsLowerC(c) ? (char)(c - 'a' + 'A') : c; }

std::vector<std::string> SplitWords(const std::string& ident) {
    std::vector<std::string> words;
    std::string cur;
    for (size_t i = 0; i < ident.size(); ++i) {
        char c = ident[i];
        if (!IsAlnumC(c)) {
            if (!cur.empty()) words.push_back(cur);
            cur.clear();
            continue;
        }
        if (!cur.empty()) {
            char prev = cur.back();
            bool boundary = (IsUpperC(c) && (IsLowerC(prev) || IsDigitC(prev))) || (IsDigitC(c) != IsDigitC(prev) && !IsDigitC(c)) ||
                            (IsUpperC(c) && IsUpperC(prev) && i + 1 < ident.size() && IsLowerC(ident[i + 1]));
            if (boundary) {
                words.push_back(cur);
                cur.clear();
            }
        }
        cur.push_back(c);
    }
    if (!cur.empty()) words.push_back(cur);
    return words;
}

std::string Sanitize(const std::string& text) {
    std::string out;
    for (char c : text) out.push_back(IsAlnumC(c) || c == '_' ? c : '_');
    if (out.empty()) return "_";
    if (IsDigitC(out[0])) out.insert(out.begin(), '_');
    return out;
}

std::string NumLit(double v, bool f32) {
    std::string s = core_detail::FormatFloat((float)v);
    if (s.find_first_of(".eEni") == std::string::npos) s += ".0";
    return f32 ? s + "f" : s;
}

std::string StringLit(const std::string& s) {
    std::string out = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\') out += '\\';
        if (c == '\n') {
            out += "\\n";
            continue;
        }
        out += c;
    }
    return out + "\"";
}

std::string StateCpp(const std::string& t) {
    if (t == "bool" || t == "int" || t == "float" || t == "double") return t;
    if (t == "string") return "std::string";
    if (t == "vec2") return "ImVec2";
    if (t == "vec4" || t == "color") return "ImVec4";
    return "int";
}

std::string StateInit(const std::string& t, const Json& d) {
    if (t == "bool") return (d.IsBool() ? d.b : d.AsDouble(0) != 0) ? "true" : "false";
    if (t == "int") return std::to_string(d.AsInt(0));
    if (t == "float" || t == "double") return NumLit(d.AsDouble(0), t == "float");
    if (t == "string") return StringLit(d.AsString(""));
    if (t == "vec2") return "ImVec2(" + NumLit(d["x"].AsDouble(0), true) + ", " + NumLit(d["y"].AsDouble(0), true) + ")";
    Vec4 v;
    if (t == "color" && d.IsString()) v = ParseColor(d.AsString());
    else v = Vec4{(float)d["x"].AsDouble(0), (float)d["y"].AsDouble(0), (float)d["z"].AsDouble(0), (float)d["w"].AsDouble(0)};
    if (t == "vec4" || t == "color")
        return "ImVec4(" + NumLit(v.x, true) + ", " + NumLit(v.y, true) + ", " + NumLit(v.z, true) + ", " + NumLit(v.w, true) + ")";
    return "0";
}

}  // namespace

std::string RenameInExpr(const std::string& src, const RenameTarget& target, int* count) {
    int n = 0;
    std::string out = Rewrite(src, SiteKind::Expr, target, n);
    if (count) *count = n;
    return out;
}

std::string RenameInInterpolation(const std::string& src, const RenameTarget& target, int* count) {
    int n = 0;
    std::string out = Rewrite(src, SiteKind::Interp, target, n);
    if (count) *count = n;
    return out;
}

int RenameRootInPanel(Panel& panel, const std::string& old_name, const std::string& new_name, const Project* project) {
    if (old_name.empty() || old_name == new_name) return 0;
    for (Param& p : panel.params)
        if (p.name == old_name) p.name = new_name;
    RenameTarget t;
    t.from = old_name;
    t.to = new_name;
    return RenameInTree(panel.root, t, project);
}

int RenameStateVarInPanel(Panel& panel, const std::string& old_name, const std::string& new_name, const Project* project) {
    if (old_name.empty() || old_name == new_name) return 0;
    for (StateVar& s : panel.state)
        if (s.name == old_name) s.name = new_name;
    RenameTarget t;
    t.state = true;
    t.from = old_name;
    t.to = new_name;
    return RenameInTree(panel.root, t, project);
}

std::vector<PathRef> CollectPathRefs(const Panel& panel, const Project* project) {
    std::vector<PathRef> out;
    const Node* current = nullptr;
    auto collect = [&](const std::string& s, SiteKind kind, const std::string& where) {
        if (s.empty()) return;
        for (const Hit& h : Scan(s, kind)) {
            PathRef r;
            r.node_id = current->id;
            r.where = where;
            r.root = h.root;
            r.member = h.member;
            out.push_back(std::move(r));
        }
    };
    Walk(panel.root, [&](const Node& n) {
        current = &n;
        VisitNodeSites(n, project, collect);
    });
    return out;
}

std::string ApplyNameCase(const std::string& ident, const std::string& style) {
    if (style != "pascal" && style != "camel" && style != "snake") return ident;
    std::string out;
    for (const std::string& w : SplitWords(ident)) {
        if (style == "snake") {
            if (!out.empty()) out.push_back('_');
            for (char c : w) out.push_back(LowerC(c));
            continue;
        }
        out.push_back(out.empty() && style == "camel" ? LowerC(w[0]) : UpperC(w[0]));
        out.append(w, 1, std::string::npos);
    }
    return out.empty() ? ident : out;
}

std::string PanelSignaturePreview(const Panel& panel, const CodegenSettings& s) {
    std::string ind = s.use_tabs ? std::string("\t") : std::string((size_t)(s.indent > 0 ? s.indent : 4), ' ');
    std::string pre = s.nmspace.empty() ? std::string() : ind;
    std::string name = Sanitize(panel.name);
    std::string state_type = name + "State";
    std::string out;
    if (!s.nmspace.empty()) out += "namespace " + s.nmspace + " {\n";
    out += pre + "struct " + state_type + " {\n";
    for (const StateVar& v : panel.state)
        out += pre + ind + StateCpp(v.type) + " " + ApplyNameCase(Sanitize(v.name), s.variable_case) + " = " +
               StateInit(v.type, v.def) + ";\n";
    out += pre + "};\n";
    out += pre + "void " + ApplyNameCase("Draw" + name, s.function_case) + "(" + state_type + "& state";
    for (const Param& p : panel.params) out += ", " + (p.type.empty() ? std::string("?") : p.type) + "& " + Sanitize(p.name);
    if (s.dpi_mode == "variable") out += ", float imb_scale";
    out += ");\n";
    if (!s.nmspace.empty()) out += "}  // namespace " + s.nmspace + "\n";
    return out;
}

}  // namespace imforge::builder
