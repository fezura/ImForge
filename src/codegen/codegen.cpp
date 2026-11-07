#include "codegen.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "emit_internal.h"

namespace imforge {
namespace codegen {

bool Result::HasErrors() const {
    for (const Diagnostic& d : diagnostics)
        if (d.level == Diagnostic::Level::Error) return true;
    return false;
}

const FileOut* Result::Find(const std::string& path) const {
    for (const FileOut& f : files)
        if (f.path == path) return &f;
    return nullptr;
}

Options OptionsFromProject(const Project& p) {
    Options o;
    o.settings = p.codegen;
    o.imgui_version = p.ImGuiVersionNum();
    if (o.imgui_version <= 0) o.imgui_version = 19190;
    o.features_animations = p.Feature("animations", true);
    o.features_fx = p.Feature("drawlistFx", true) || p.Feature("shaderFx", true);
    o.features_icons = p.Feature("iconFonts", true);
    return o;
}

std::string EmitSize(const SizeSpec& w, const SizeSpec& h, const Options& opts) {
    const std::string& m = opts.settings.dpi_mode;
    std::string scale = m == "variable" ? "imb_scale" : (m == "none" ? "1.0f" : "imb_s");
    return "ImVec2(" + SizeExpr(w, true, scale, std::string()) + ", " + SizeExpr(h, false, scale, std::string()) + ")";
}

std::string EmitExpression(const Panel& panel, const std::string& expr, const Options& opts) {
    static const Project kEmpty;
    Writer w;
    w.opts = &opts;
    std::vector<LiteralRef> lits;
    std::vector<Diagnostic> diags;
    EmitCtx ctx(kEmpty, &panel, nullptr, opts, w, lits, diags);
    return ctx.Expr(expr);
}

ParamList PanelParams(const Panel& p, const Options& opts) {
    ParamList out;
    out.emplace_back(StateTypeName(p) + "&", "state");
    for (const Param& prm : p.params) out.emplace_back(prm.type + "&", SanitizeIdentifier(prm.name));
    if (opts.settings.dpi_mode == "variable") out.emplace_back("float", "imb_scale");
    return out;
}

ParamList ComponentParams(const Component& c, const Options& opts) {
    ParamList out;
    for (const Param& prm : c.params) out.emplace_back(ParamCppType(prm, opts), SanitizeIdentifier(prm.name));
    if (opts.settings.dpi_mode == "variable") out.emplace_back("float", "imb_scale");
    return out;
}

std::string DeclList(const ParamList& params) {
    std::string s;
    for (size_t i = 0; i < params.size(); ++i) s += (i ? ", " : "") + params[i].first + " " + params[i].second;
    return s;
}

std::string NameList(const ParamList& params) {
    std::string s;
    for (size_t i = 0; i < params.size(); ++i) s += (i ? ", " : "") + params[i].second;
    return s;
}

std::string VoidList(const ParamList& params) {
    std::string s;
    for (size_t i = 0; i < params.size(); ++i) s += (i ? " " : "") + std::string("(void)") + params[i].second + ";";
    return s;
}

namespace {

ParamList WithoutScale(const ParamList& params) {
    ParamList out;
    for (const auto& p : params)
        if (p.second != "imb_scale") out.push_back(p);
    return out;
}

}  // namespace

void EmitHandlerFns(EmitCtx& ctx, const Node& root, const std::string& linkage, const ParamList& params) {
    std::vector<std::pair<const Node*, std::string>> handlers;
    CollectHandlers(root, handlers);
    ParamList hp = WithoutScale(params);
    for (const auto& h : handlers) {
        ctx.w.Line(linkage + "void " + ctx.HandlerName(*h.first, h.second.c_str()) + "(" + DeclList(hp) + ") {");
        ctx.w.Indent();
        if (!hp.empty()) ctx.w.Line(VoidList(hp));
        ctx.w.UserSlot(h.first->id + "." + h.second);
        ctx.w.Unindent();
        ctx.w.Line("}");
        ctx.w.Blank();
    }
}

void EmitLockedFns(EmitCtx& ctx, const Node& root, const std::string& linkage, const ParamList& params) {
    std::vector<const Node*> locked;
    CollectLocked(root, locked);
    for (const Node* n : locked) {
        ctx.w.BeginBlock(n->id, "locked");
        ctx.w.Line(linkage + "void " + BlockFnName(ctx, *n) + "(" + DeclList(params) + ") {");
        ctx.w.Indent();
        if (!params.empty()) ctx.w.Line(VoidList(params));
        std::string scale = ScaleDecl(ctx.opts);
        if (!scale.empty()) ctx.w.Line(scale);
        if (n->type == "Code") {
            const PropValue* code = n->Prop("code");
            std::string text = code && code->raw.IsString() ? code->raw.AsString() : std::string();
            if (!text.empty()) ctx.w.Line(text);
        } else {
            ++ctx.w.suppress_markers;
            EmitNodeBody(ctx, *n);
            --ctx.w.suppress_markers;
        }
        ctx.w.Unindent();
        ctx.w.Line("}");
        ctx.w.EndBlock();
        ctx.w.Blank();
    }
}

void InsertUnusedVoids(Writer& w, size_t offset, const ParamList& params, size_t body_start) {
    std::vector<Token> toks = Tokenize(w.out.substr(body_start));
    std::string line;
    for (const auto& p : params) {
        bool used = false;
        for (const Token& t : toks) used = used || (t.kind == Token::Kind::Ident && t.text == p.second);
        if (!used) line += (line.empty() ? "" : " ") + std::string("(void)") + p.second + ";";
    }
    if (line.empty()) return;
    w.InsertAt(offset, w.IndentString() + line + "\n");
}

void WriteFileHead(Writer& w, const std::string& file_marker) {
    w.Line(kGeneratedBanner);
    if (w.MarkersOn()) w.Line("// @imb:file " + file_marker);
}

void WriteIncludes(Writer& w, const std::vector<std::string>& includes) {
    std::vector<std::string> angle, quoted;
    for (const std::string& i : includes) {
        if (i.empty()) continue;
        std::vector<std::string>& dst = i[0] == '<' ? angle : quoted;
        bool have = false;
        for (const std::string& s : dst) have = have || s == i;
        if (!have) dst.push_back(i);
    }
    std::sort(angle.begin(), angle.end());
    std::sort(quoted.begin(), quoted.end());
    for (const std::string& i : angle) w.Line("#include " + i);
    for (const std::string& i : quoted) w.Line("#include " + i);
}

bool LoadTypes(const Project& project, const Options& opts, TypesDb& db, std::vector<Diagnostic>& diags) {
    Error err;
    if (opts.types) {
        if (db.Load(*opts.types, &err)) return true;
        Diagnostic d;
        d.level = Diagnostic::Level::Warning;
        d.message = "types: " + err.message;
        diags.push_back(d);
        return false;
    }
    if (project.types_json.empty()) return false;
    std::string path = project.types_json;
    bool absolute = !path.empty() && (path[0] == '/' || path[0] == '\\' || (path.size() > 1 && path[1] == ':'));
    if (!absolute && !project.dir.empty()) {
        char last = project.dir.back();
        path = project.dir + (last == '/' || last == '\\' ? "" : "/") + path;
    }
    if (db.LoadFile(path, &err)) return true;
    if (err.message.compare(0, 11, "cannot open") == 0) return false;
    Diagnostic d;
    d.level = Diagnostic::Level::Warning;
    d.message = "types: " + err.message;
    diags.push_back(d);
    return false;
}

void OpenNamespace(Writer& w, const Options& opts) {
    if (!opts.settings.nmspace.empty()) w.Line("namespace " + opts.settings.nmspace + " {");
}

void CloseNamespace(Writer& w, const Options& opts) {
    if (!opts.settings.nmspace.empty()) w.Line("}  // namespace " + opts.settings.nmspace);
}

// panel

namespace {

std::string StateCppType(const std::string& t, bool& needs_string) {
    if (t == "bool") return "bool";
    if (t == "int") return "int";
    if (t == "float") return "float";
    if (t == "double") return "double";
    if (t == "string") {
        needs_string = true;
        return "std::string";
    }
    if (t == "vec2") return "ImVec2";
    if (t == "vec4" || t == "color") return "ImVec4";
    return "int";
}

std::string StateDefault(const std::string& t, const Json& def) {
    if (t == "bool") return Value::FromJson(def, Value::Kind::Bool).b ? "true" : "false";
    if (t == "int") return std::to_string(def.AsInt(0));
    if (t == "float") return FloatLitOf(def.AsDouble(0));
    if (t == "double") {
        std::string s = FloatLitOf(def.AsDouble(0));
        s.pop_back();
        return s;
    }
    if (t == "string") return CppStringLiteral(def.IsString() ? def.AsString() : std::string());
    if (t == "vec2") {
        imforge::Vec2 v = Value::Infer(def).ToVec2();
        return "ImVec2(" + NumLit(v.x) + ", " + NumLit(v.y) + ")";
    }
    if (t == "color") {
        imforge::Vec4 c = def.IsString() ? ParseColor(def.AsString()) : Value::Infer(def).ToVec4();
        return "ImVec4(" + FloatLitOf(c.x) + ", " + FloatLitOf(c.y) + ", " + FloatLitOf(c.z) + ", " + FloatLitOf(c.w) + ")";
    }
    if (t == "vec4") {
        imforge::Vec4 v = Value::Infer(def).ToVec4();
        return "ImVec4(" + NumLit(v.x) + ", " + NumLit(v.y) + ", " + NumLit(v.z) + ", " + NumLit(v.w) + ")";
    }
    return "0";
}

void EmitStateStruct(Writer& w, const Panel& panel, const Options& opts, const std::vector<std::string>& tabbars) {
    w.Line("struct " + StateTypeName(panel) + " {");
    {
        Writer::Scope s = w.Scoped();
        bool dummy = false;
        for (const StateVar& v : panel.state)
            w.Line(StateCppType(v.type, dummy) + " " + VarCase(opts, v.name) + " = " + StateDefault(v.type, v.def) + ";");
        for (const std::string& tb : tabbars) w.Line("int " + TabStateMember(tb) + " = -1;");
    }
    w.Line("};");
}

void EmitForwardDecls(Writer& w, const Panel& panel, std::vector<Diagnostic>& diags) {
    std::vector<std::string> done;
    for (const Param& p : panel.params) {
        const std::string& t = p.type;
        bool seen = false;
        for (const std::string& d : done) seen = seen || d == t;
        if (seen) continue;
        done.push_back(t);
        if (t.find('<') != std::string::npos || t.find('*') != std::string::npos || t.find(' ') != std::string::npos) {
            Diagnostic d;
            d.level = Diagnostic::Level::Info;
            d.panel = panel.name;
            d.message = "parameter type '" + t + "' is not forward-declared; add its include in @imb:user id=includes";
            diags.push_back(d);
            continue;
        }
        std::vector<std::string> parts;
        size_t start = 0;
        while (true) {
            size_t p2 = t.find("::", start);
            parts.push_back(t.substr(start, p2 == std::string::npos ? std::string::npos : p2 - start));
            if (p2 == std::string::npos) break;
            start = p2 + 2;
        }
        std::string line;
        for (size_t i = 0; i + 1 < parts.size(); ++i) line += "namespace " + parts[i] + " { ";
        line += "struct " + parts.back() + ";";
        for (size_t i = 0; i + 1 < parts.size(); ++i) line += " }";
        w.Line(line);
    }
}

}  // namespace

Result GeneratePanel(const Project& project, const Panel& panel, const Options& opts) {
    Result r;
    TypesDb db;
    LoadTypes(project, opts, db, r.diagnostics);
    bool single = !opts.settings.split_header;
    std::string linkage = single ? "inline " : "";
    Writer src;
    src.opts = &opts;
    EmitCtx ctx(project, &panel, nullptr, opts, src, r.literals, r.diagnostics);
    ctx.types = db.loaded ? &db : nullptr;
    ParamList params = PanelParams(panel, opts);
    std::vector<std::string> tabbars;
    CollectSetTabTargets(panel.root, tabbars);

    EmitHandlerFns(ctx, panel.root, linkage, params);
    EmitLockedFns(ctx, panel.root, linkage, params);
    std::string draw = DrawFnName(opts, panel);
    src.Line(linkage + "void " + draw + "(" + DeclList(params) + ") {");
    src.Indent();
    std::string scale = ScaleDecl(opts);
    if (!scale.empty()) src.Line(scale);
    size_t void_off = src.Offset();
    src.UserSlot("before_draw");
    EmitNode(ctx, panel.root);
    src.UserSlot("after_draw");
    InsertUnusedVoids(src, void_off, params, void_off);
    src.Unindent();
    src.Line("}");

    bool needs_string = false;
    for (const StateVar& v : panel.state) StateCppType(v.type, needs_string);
    std::vector<std::string> includes = ctx.includes;
    includes.push_back("<cfloat>");
    if (ctx.uses_runtime) includes.push_back("\"" + opts.settings.runtime_header + "\"");
    if (ctx.needs_stdlib) includes.push_back("\"misc/cpp/imgui_stdlib.h\"");
    std::vector<std::string> type_headers;
    for (const Param& p : panel.params) {
        std::string h = db.loaded ? db.HeaderOf(p.type) : std::string();
        if (h.empty()) continue;
        bool have = false;
        for (const std::string& s : type_headers) have = have || s == h;
        if (!have) type_headers.push_back(h);
    }
    std::sort(type_headers.begin(), type_headers.end());
    std::string base = SanitizeIdentifier(panel.name);
    std::string marker = "panel=" + panel.name + " schema=1";

    std::vector<std::pair<const Node*, std::string>> handlers;
    CollectHandlers(panel.root, handlers);
    std::vector<const Node*> locked;
    CollectLocked(panel.root, locked);
    ParamList hp;
    for (const auto& p : params)
        if (p.second != "imb_scale") hp.push_back(p);

    if (!single) {
        Writer hdr;
        hdr.opts = &opts;
        WriteFileHead(hdr, marker);
        hdr.Line("#pragma once");
        hdr.Line("#include \"imgui.h\"");
        if (needs_string) hdr.Line("#include <string>");
        EmitForwardDecls(hdr, panel, r.diagnostics);
        OpenNamespace(hdr, opts);
        EmitStateStruct(hdr, panel, opts, tabbars);
        hdr.Line("void " + draw + "(" + DeclList(params) + ");");
        for (const auto& h : handlers) hdr.Line("void " + ctx.HandlerName(*h.first, h.second.c_str()) + "(" + DeclList(hp) + ");");
        for (const Node* n : locked) hdr.Line("void " + BlockFnName(ctx, *n) + "(" + DeclList(params) + ");");
        hdr.UserSlot("header_extra");
        CloseNamespace(hdr, opts);
        FileOut h;
        h.path = base + ".h";
        h.content = hdr.out;
        r.files.push_back(h);

        Writer pre;
        pre.opts = &opts;
        WriteFileHead(pre, marker);
        pre.Line("#include \"" + base + ".h\"");
        for (const std::string& t : type_headers) pre.Line("#include \"" + t + "\"");
        WriteIncludes(pre, includes);
        pre.UserSlot("includes");
        OpenNamespace(pre, opts);
        pre.UserSlot("file_top");
        pre.Blank();
        src.Prepend(pre.out);
        CloseNamespace(src, opts);
        FileOut s;
        s.path = base + ".cpp";
        s.content = src.out;
        r.files.push_back(s);
        src.Finalize(s.path, r.literals);
    } else {
        Writer pre;
        pre.opts = &opts;
        WriteFileHead(pre, marker);
        pre.Line("#pragma once");
        pre.Line("#include \"imgui.h\"");
        if (needs_string) includes.push_back("<string>");
        for (const std::string& t : type_headers) pre.Line("#include \"" + t + "\"");
        WriteIncludes(pre, includes);
        pre.UserSlot("includes");
        if (type_headers.empty()) EmitForwardDecls(pre, panel, r.diagnostics);
        OpenNamespace(pre, opts);
        pre.UserSlot("file_top");
        EmitStateStruct(pre, panel, opts, tabbars);
        pre.Blank();
        src.Prepend(pre.out);
        src.UserSlot("header_extra");
        CloseNamespace(src, opts);
        FileOut s;
        s.path = base + ".h";
        s.content = src.out;
        r.files.push_back(s);
        src.Finalize(s.path, r.literals);
    }
    return r;
}

namespace {

void Merge(Result& into, Result&& part) {
    for (FileOut& f : part.files) into.files.push_back(std::move(f));
    for (Diagnostic& d : part.diagnostics) into.diagnostics.push_back(std::move(d));
    for (LiteralRef& l : part.literals) into.literals.push_back(std::move(l));
}

}  // namespace

Result GenerateAll(const Project& project, const Options& opts) {
    Result r;
    for (const Panel& p : project.panels) Merge(r, GeneratePanel(project, p, opts));
    Merge(r, GenerateComponents(project, opts));
    for (const Theme& t : project.themes) Merge(r, GenerateTheme(project, t, opts));
    if (!project.style_classes.empty()) Merge(r, GenerateStyles(project, opts));
    if (!project.fonts.empty()) Merge(r, GenerateFonts(project, opts));
    if (!project.binding_types.empty() || !project.types_json.empty() || opts.types) Merge(r, GenerateReflection(project, opts));
    if (project.live.enabled) Merge(r, GenerateDevGlue(project, opts));
    std::stable_sort(r.files.begin(), r.files.end(), [](const FileOut& a, const FileOut& b) { return a.path < b.path; });
    return r;
}

}  // namespace codegen
}  // namespace imforge
