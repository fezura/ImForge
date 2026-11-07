#include <cstring>

#include "emit_widgets_util.h"
#include "imforge/imforge_widgets.h"

// custom widgets from .imw.json manifests, see docs/WIDGET_SDK.md

namespace imforge {
namespace codegen {

namespace {

using namespace wdg;

const char kChildrenMarker[] = "\x03children\x03";

bool IsIdentChar(char c, bool first) {
    bool alpha = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
    bool digit = c >= '0' && c <= '9';
    return alpha || (digit && !first);
}

bool ParsePlaceholder(const std::string& body, std::string& name, std::string& mod) {
    size_t colon = body.find(':');
    name = colon == std::string::npos ? body : body.substr(0, colon);
    mod = colon == std::string::npos ? std::string() : body.substr(colon + 1);
    if (name.empty()) return false;
    for (size_t i = 0; i < name.size(); ++i)
        if (!IsIdentChar(name[i], i == 0)) return false;
    for (char c : mod)
        if (!IsIdentChar(c, false)) return false;
    return true;
}

const PropDef* FindCustomProp(const CustomWidgetDef& def, const std::string& name) {
    for (const PropDef& p : def.props)
        if (name == p.name) return &p;
    return nullptr;
}

const BindDef* FindCustomBind(const CustomWidgetDef& def, const std::string& name) {
    for (const BindDef& b : def.binds)
        if (name == b.name) return &b;
    return nullptr;
}

const Json& ManifestProp(const CustomWidgetDef& def, const std::string& name) {
    const Json& props = def.manifest["props"];
    for (size_t i = 0; i < props.Size(); ++i)
        if (props[i]["name"].AsString() == name) return props[i];
    static const Json kNull;
    return kNull;
}

std::string EnumPrefix(const CustomWidgetDef& def, const std::string& prop) {
    const Json& p = ManifestProp(def, prop);
    if (p["enumPrefix"].IsString()) return p["enumPrefix"].AsString();
    return def.manifest["enumPrefix"].AsString("");
}

std::string Vec4Expr(EmitCtx& ctx, const Node& n, const PropDef& p) {
    const Json* j = RawProp(n, p.name);
    Json d = PropDefaultJson(p);
    const Json& src = j && j->IsObject() ? *j : d;
    const char* keys[4] = {"x", "y", "z", "w"};
    std::vector<std::string> comps;
    for (const char* k : keys) comps.push_back(ctx.FloatLit(src[k].AsDouble(0)));
    return Marked(ctx, n, p.name, "ImVec4(" + Join(comps) + ")");
}

std::string StringProp(EmitCtx& ctx, const Node& n, const PropDef& p, const std::string& mod) {
    std::string def = PropDefaultJson(p).AsString("");
    FmtArgs t = ctx.Text(n, p.name, def.c_str());
    if (mod == "fmt") return t.fmt;
    if (mod == "args") {
        std::string r;
        for (const std::string& a : t.args) r += ", " + a;
        return r;
    }
    if (t.IsLiteral()) return t.fmt;
    std::string buf = Local(ctx, n, p.name);
    NeedInclude(ctx, "<cstdio>");
    Line(ctx, "char " + buf + "[128];");
    Line(ctx, "std::snprintf(" + buf + ", sizeof(" + buf + "), " + t.CallArgs() + ");");
    return buf;
}

std::string ExprProp(EmitCtx& ctx, const Node& n, const PropDef& p) {
    const PropValue* pv = n.Prop(p.name);
    if (pv && pv->IsExpr()) return Expr(ctx, pv->Expr());
    if (pv && pv->raw.IsString() && !pv->raw.AsString().empty()) return Expr(ctx, pv->raw.AsString());
    if (pv && pv->raw.IsNumber()) return Marked(ctx, n, p.name, ctx.FloatLit(pv->raw.AsDouble()));
    if (pv && pv->raw.IsBool()) return pv->raw.AsBool() ? "true" : "false";
    std::string def = PropDefaultJson(p).AsString("");
    return def.empty() ? std::string("0") : Expr(ctx, def);
}

std::string PropExpr(EmitCtx& ctx, const Node& n, const CustomWidgetDef& def, const PropDef& p,
                     const std::string& mod) {
    Json d = PropDefaultJson(p);
    switch (p.type) {
    case PropType::String: return StringProp(ctx, n, p, mod);
    case PropType::Bool: return BoolExpr(ctx, n, p.name, d.AsBool(false));
    case PropType::Int: return IntP(ctx, n, p.name, d.AsDouble(0));
    case PropType::Float: return FloatP(ctx, n, p.name, d.AsDouble(0));
    case PropType::Color: {
        std::string hex = d.AsString("#ffffffff");
        return mod == "u32" ? ctx.ColorU32(n, p.name, hex.c_str()) : Color(ctx, n, p.name, hex.c_str());
    }
    case PropType::Vec2: return ctx.Vec2(n, p.name, p.def_json ? p.def_json : R"({"x":0,"y":0})", mod == "px");
    case PropType::Vec4: return Vec4Expr(ctx, n, p);
    case PropType::Size: {
        std::string s = ctx.Size(n, p.name);
        if (!s.empty()) return s;
        Node tmp = n;
        tmp.children.clear();
        tmp.SetProp(p.name, d.IsNull() ? Json::Object() : d);
        s = ctx.Size(tmp, p.name);
        return s.empty() ? std::string("ImVec2(0, 0)") : s;
    }
    case PropType::Enum: {
        std::string prefix = EnumPrefix(def, p.name);
        std::string first = d.AsString("");
        return EnumName(ctx, n, p.name, prefix.c_str(), first.c_str());
    }
    case PropType::Flags: return Flags(ctx, n, p.name, p.enum_values ? p.enum_values : "");
    case PropType::StringList: {
        std::vector<std::string> items = ctx.StringList(n, p.name);
        if (items.empty() && d.IsArray())
            for (size_t i = 0; i < d.Size(); ++i) items.push_back(d[i].AsString(""));
        if (mod == "array" || mod == "count") {
            std::string name = Local(ctx, n, p.name);
            if (mod == "count") return "IM_ARRAYSIZE(" + name + ")";
            std::vector<std::string> quoted;
            for (const std::string& s : items) quoted.push_back(Quote(ctx, s));
            if (quoted.empty()) quoted.push_back("\"\"");
            quoted[0] = Marked(ctx, n, p.name, quoted[0]);
            Line(ctx, "static const char* " + name + "[] = {" + Join(quoted) + "};");
            return name;
        }
        std::string zero;
        for (const std::string& s : items) {
            zero += s;
            zero += '\0';
        }
        return Marked(ctx, n, p.name, Quote(ctx, zero));
    }
    case PropType::Expr: return ExprProp(ctx, n, p);
    case PropType::Code: {
        const Json* j = RawProp(n, p.name);
        return j && j->IsString() ? j->AsString() : d.AsString("");
    }
    case PropType::SizeList: break;
    }
    Warn(ctx, n, std::string("prop '") + p.name + "' has a type the call template cannot receive", p.name);
    return "0";
}

std::string BindExprFor(EmitCtx& ctx, const Node& n, const BindDef& b, const std::string& mod) {
    Slot s = GetSlot(ctx, n, b.name);
    if (!s.ok) {
        if (b.required) Warn(ctx, n, std::string("required binding '") + b.name + "' is not bound", b.name);
        return "nullptr";
    }
    if (mod == "value" || mod == "deref") return s.cpp;
    if (mod == "count" || mod == "size") return CountExpr(s);
    if (mod == "f") return "&" + s.cpp + ".x";
    if (s.Kind() == CKind::TexID || PipeListContains(b.types, "texture")) return TextureExpr(s);
    if (s.IsVec()) return "&" + s.cpp;
    if (s.IsEnum()) return s.pointer.empty() ? "(int*)&" + s.cpp : s.pointer;
    return ScalarPtr(s);
}

std::string Placeholder(EmitCtx& ctx, const Node& n, const CustomWidgetDef& def, const std::string& name,
                        const std::string& mod, bool& children) {
    if (name == "children") {
        children = true;
        return kChildrenMarker;
    }
    if (name == "id") return IdLit(ctx, n);
    if (const PropDef* p = FindCustomProp(def, name)) return PropExpr(ctx, n, def, *p, mod);
    if (const BindDef* b = FindCustomBind(def, name)) return BindExprFor(ctx, n, *b, mod);
    if (name == "size") return ctx.Size(n);
    if (name == "label") return Label(ctx, n);
    Warn(ctx, n, "call template references unknown placeholder {" + name + "}");
    return "{" + name + (mod.empty() ? std::string() : ":" + mod) + "}";
}

std::string Expand(EmitCtx& ctx, const Node& n, const CustomWidgetDef& def, const std::string& tmpl,
                   bool& children) {
    std::string out;
    size_t i = 0;
    while (i < tmpl.size()) {
        char c = tmpl[i];
        if (c != '{') {
            out += c;
            ++i;
            continue;
        }
        size_t close = tmpl.find('}', i + 1);
        std::string name, mod;
        if (close == std::string::npos || !ParsePlaceholder(tmpl.substr(i + 1, close - i - 1), name, mod)) {
            out += c;
            ++i;
            continue;
        }
        out += Placeholder(ctx, n, def, name, mod, children);
        i = close + 1;
    }
    return out;
}

std::vector<std::string> SplitLines(const std::string& s) {
    std::vector<std::string> lines;
    size_t start = 0;
    while (start <= s.size()) {
        size_t nl = s.find('\n', start);
        std::string line = s.substr(start, nl == std::string::npos ? std::string::npos : nl - start);
        size_t b = 0;
        while (b < line.size() && (line[b] == ' ' || line[b] == '\t' || line[b] == '\r')) ++b;
        line.erase(0, b);
        while (!line.empty() && (line.back() == ' ' || line.back() == '\t' || line.back() == '\r')) line.pop_back();
        if (!line.empty()) lines.push_back(line);
        if (nl == std::string::npos) break;
        start = nl + 1;
    }
    return lines;
}

// Writes a (possibly multi-line) call whose text contains the children marker.
void EmitMarkedCall(EmitCtx& ctx, const Node& n, const std::string& call, const std::string& prefix,
                    const std::string& suffix) {
    std::vector<std::string> lines = SplitLines(call);
    for (size_t i = 0; i < lines.size(); ++i) {
        std::string text = lines[i];
        if (i == 0) text = prefix + text;
        if (i + 1 == lines.size()) text += suffix;
        bool cont = i > 0 && text[0] != '}' && text[0] != ')';
        if (cont) Indent(ctx);
        size_t m = text.find(kChildrenMarker);
        if (m == std::string::npos) {
            Line(ctx, text);
        } else {
            std::string before = text.substr(0, m), after = text.substr(m + std::strlen(kChildrenMarker));
            if (!before.empty()) Line(ctx, before);
            Indent(ctx);
            Children(ctx, n);
            Unindent(ctx);
            if (!after.empty()) Line(ctx, after);
        }
        if (cont) Unindent(ctx);
    }
}

void EmitOpenWidget(EmitCtx& ctx, const Node& n, const CustomWidgetDef& def, const std::string& call,
                    const std::string& call_end) {
    bool track = HasEvent(ctx, n, "onOpen") || HasEvent(ctx, n, "onClose");
    std::string cond = call;
    std::string open, was;
    if (track) {
        open = Local(ctx, n, "open");
        was = Local(ctx, n, "was_open");
        Line(ctx, "static bool " + was + " = false;");
        Line(ctx, "const bool " + open + " = " + call + ";");
        cond = open;
    }
    Line(ctx, "if (" + cond + ") {");
    Indent(ctx);
    if (def.container) Children(ctx, n);
    if (!call_end.empty()) Line(ctx, call_end + ";");
    Unindent(ctx);
    Line(ctx, "}");
    if (!track) return;
    if (HasEvent(ctx, n, "onOpen")) {
        Line(ctx, "if (" + open + " && !" + was + ") {");
        Indent(ctx);
        Fire(ctx, n, "onOpen");
        Unindent(ctx);
        Line(ctx, "}");
    }
    if (HasEvent(ctx, n, "onClose")) {
        Line(ctx, "if (!" + open + " && " + was + ") {");
        Indent(ctx);
        Fire(ctx, n, "onClose");
        Unindent(ctx);
        Line(ctx, "}");
    }
    Line(ctx, was + " = " + open + ";");
}

}  // namespace

bool EmitCustomWidget(EmitCtx& ctx, const Node& n, const CustomWidgetDef& def) {
    if (def.call.empty()) return false;
    if (!def.include.empty()) ctx.Include(def.include.c_str());
    if (def.min_version > ctx.Version())
        Info(ctx, n, "'" + def.type + "' needs IMGUI_VERSION_NUM >= " + std::to_string(def.min_version));
    bool marker = false, end_marker = false;
    std::string call = Expand(ctx, n, def, def.call, marker);
    std::string call_end = def.manifest["callEnd"].IsString()
                               ? Expand(ctx, n, def, def.manifest["callEnd"].AsString(), end_marker)
                               : std::string();
    const char* event = def.returns == "changed" ? "onChange" : def.returns == "clicked" ? "onClick" : nullptr;

    if (marker) {
        bool wrap = event && HasEvent(ctx, n, event);
        EmitMarkedCall(ctx, n, call, wrap ? "if (" : "", wrap ? ") {" : ";");
        if (wrap) {
            Indent(ctx);
            Fire(ctx, n, event);
            Unindent(ctx);
            Line(ctx, "}");
        }
        if (!call_end.empty()) Line(ctx, call_end + ";");
        return true;
    }
    if (def.returns == "open") {
        EmitOpenWidget(ctx, n, def, call, call_end);
        return true;
    }
    EmitCall(ctx, n, call, event);
    if (def.container) Children(ctx, n);
    if (!call_end.empty()) Line(ctx, call_end + ";");
    return true;
}

}  // namespace codegen
}  // namespace imforge
