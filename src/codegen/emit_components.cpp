#include <cstring>

#include "emit_internal.h"

namespace imforge {
namespace codegen {

namespace {

bool StartsWith(const std::string& s, const char* p) { return s.compare(0, std::strlen(p), p) == 0; }

std::string EnumIndex(const Param& p, const Json& j) {
    if (j.IsNumber()) return std::to_string(j.AsInt());
    std::string list = p.type.substr(5);
    std::string name = j.IsString() ? j.AsString() : std::string();
    size_t start = 0;
    int idx = 0;
    while (start <= list.size()) {
        size_t comma = list.find(',', start);
        std::string item = list.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
        if (item == name) return std::to_string(idx);
        if (comma == std::string::npos) break;
        start = comma + 1;
        ++idx;
    }
    return "0";
}

std::string ParamArg(EmitCtx& ctx, const Node& inst, const Param& p) {
    const char* key = p.name.c_str();
    const PropValue* pv = inst.Prop(key);
    const PropValue* bv = inst.Bind(key);
    bool is_binding = false;
    ParamCppType(p, ctx.opts, &is_binding);
    if (is_binding) {
        std::string inner = p.type.substr(8, p.type.size() - 9);
        if (pv && pv->IsParam()) {
            BindInfo b = ctx.BindPath(pv->Param(), inner.c_str());
            return b.ok ? b.pointer : "nullptr";
        }
        if (bv && bv->IsParam()) {
            BindInfo b = ctx.BindPath(bv->Param(), inner.c_str());
            return b.ok ? b.pointer : "nullptr";
        }
        std::string path;
        if (bv && bv->raw.IsString()) path = bv->raw.AsString();
        else if (pv && pv->raw.IsString()) path = pv->raw.AsString();
        if (path.empty()) {
            ctx.Diag(Diagnostic::Level::Warning, &inst, key, "binding parameter '" + p.name + "' is not bound; passing nullptr");
            return "nullptr";
        }
        BindInfo b = ctx.BindPath(path, inner == "color" ? "vec4" : inner.c_str());
        return b.pointer;
    }
    if (pv && pv->IsExpr()) return p.type == "string" ? ctx.ExprCStr(pv->Expr()) : ctx.Expr(pv->Expr());
    if (pv && pv->IsParam()) return SanitizeIdentifier(pv->Param());
    const Json& j = pv && !pv->raw.IsNull() ? pv->raw : p.def;
    std::string mark = ctx.w.MarkToken(inst.id, p.name);
    if (p.type == "string") {
        std::string raw = j.IsString() ? j.AsString() : (j.IsNull() ? std::string() : Value::Infer(j).ToString());
        return ctx.CStr(inst, key, raw, (SanitizeIdentifier(p.name) + "_buf").c_str());
    }
    if (p.type == "int") return mark + std::to_string(j.AsInt(0));
    if (p.type == "float") return mark + FloatLitOf(j.AsDouble(0));
    if (p.type == "bool") return mark + (Value::FromJson(j, Value::Kind::Bool).b ? "true" : "false");
    if (p.type == "color") return mark + ctx.ColorLit(j.IsString() ? ParseColor(j.AsString()) : Value::Infer(j).ToVec4());
    if (p.type == "vec4") {
        imforge::Vec4 v = Value::Infer(j).ToVec4();
        return mark + "ImVec4(" + NumLit(v.x) + ", " + NumLit(v.y) + ", " + NumLit(v.z) + ", " + NumLit(v.w) + ")";
    }
    if (p.type == "vec2") {
        imforge::Vec2 v = Value::Infer(j).ToVec2();
        return mark + "ImVec2(" + NumLit(v.x) + ", " + NumLit(v.y) + ")";
    }
    if (StartsWith(p.type, "enum:")) return mark + EnumIndex(p, j);
    ctx.Diag(Diagnostic::Level::Warning, &inst, key, "unsupported component parameter type '" + p.type + "'");
    return "0";
}

std::string ComponentName(const Node& n) {
    if (!n.component.empty()) return n.component;
    const PropValue* p = n.Prop("component");
    return p && p->raw.IsString() ? p->raw.AsString() : std::string();
}

}  // namespace

const Node* FindSlotNode(const Component& c, std::string& slot_name) {
    slot_name = c.slots.empty() ? std::string() : c.slots[0];
    const Node* found = nullptr;
    const Node* any = nullptr;
    Walk(c.root, [&](const Node& n) {
        if (n.type != "Slot") return;
        if (!any) any = &n;
        const PropValue* p = n.Prop("name");
        std::string name = p && p->raw.IsString() ? p->raw.AsString() : "content";
        if (!found && (slot_name.empty() || name == slot_name)) found = &n;
    });
    if (!found) found = any;
    if (found && slot_name.empty()) {
        const PropValue* p = found->Prop("name");
        slot_name = p && p->raw.IsString() ? p->raw.AsString() : "content";
    }
    return found;
}

void EmitComponentInstance(EmitCtx& ctx, const Node& n) {
    std::string cname = ComponentName(n);
    const Component* c = ctx.project.FindComponent(cname);
    if (!c) {
        ctx.Diag(Diagnostic::Level::Error, &n, "component", "unknown component '" + cname + "'");
        ctx.w.Line("ImGui::TextDisabled(" + ctx.StrLit("[" + cname + "]") + ");");
        return;
    }
    ctx.Include("\"imforge_components.h\"");
    std::vector<std::string> args;
    for (const Param& p : c->params) args.push_back(ParamArg(ctx, n, p));
    if (ctx.opts.settings.dpi_mode == "variable") args.push_back("imb_scale");
    std::string arglist;
    for (size_t i = 0; i < args.size(); ++i) arglist += (i ? ", " : "") + args[i];
    std::string slot_name;
    const Node* slot = FindSlotNode(*c, slot_name);
    if (c->slots.size() > 1)
        ctx.Diag(Diagnostic::Level::Warning, &n, "slots", "component '" + cname + "' declares several slots; only '" + slot_name + "' is generated");
    ctx.w.Line("ImGui::PushID(" + ctx.Id(n) + ");");
    ctx.w.Line("ImGui::BeginGroup();");
    if (!slot) {
        if (!n.slots.empty()) ctx.Diag(Diagnostic::Level::Warning, &n, "slots", "component '" + cname + "' has no slot; slot content ignored");
        ctx.w.Line(ComponentFnName(ctx.opts, *c, "") + "(" + arglist + ");");
    } else {
        std::string var = ctx.Local(n, "open");
        ctx.w.Line("const bool " + var + " = " + ComponentFnName(ctx.opts, *c, "Begin") + "(" + arglist + ");");
        ctx.w.Line("if (" + var + ") {");
        ctx.w.Indent();
        const std::vector<Node>* content = nullptr;
        for (const auto& s : n.slots)
            if (s.first == slot_name) content = &s.second;
        if (content) {
            const Node* saved = ctx.parent;
            ctx.parent = &n;
            for (const Node& child : *content) EmitNode(ctx, child);
            ctx.parent = saved;
        }
        ctx.w.Unindent();
        ctx.w.Line("}");
        std::string end_args = Writer::StripMarks(arglist);
        ctx.w.Line(ComponentFnName(ctx.opts, *c, "End") + "(" + var + (end_args.empty() ? "" : ", " + end_args) + ");");
    }
    ctx.w.Line("ImGui::EndGroup();");
    ctx.w.Line("ImGui::PopID();");
}

namespace {

struct Level {
    const Node* node;
    const Node* next;
    size_t idx;
    ContainerPlan plan;
    std::vector<std::string> pops;
};

bool FindChain(const Node& n, const Node* target, std::vector<const Node*>& chain) {
    if (&n == target) return true;
    chain.push_back(&n);
    for (const Node& c : n.children)
        if (FindChain(c, target, chain)) return true;
    chain.pop_back();
    return false;
}

void CloseLevel(EmitCtx& ctx, Level& lv, bool inside_open, bool restore) {
    const Node* saved = ctx.parent;
    ctx.parent = lv.node;
    if (inside_open) {
        if (restore) SplitCarry(ctx, *lv.node, lv.idx, false);
        ChildSuffix(ctx, *lv.node, *lv.next, lv.idx);
        for (size_t i = lv.idx + 1; i < lv.node->children.size(); ++i) {
            ChildPrefix(ctx, *lv.node, lv.node->children[i], i);
            EmitNode(ctx, lv.node->children[i]);
            ChildSuffix(ctx, *lv.node, lv.node->children[i], i);
        }
        for (const std::string& l : lv.plan.after_children) ctx.w.Line(l);
        for (const std::string& l : lv.plan.close_inside) ctx.w.Line(l);
    }
    for (const std::string& l : lv.plan.close_outside) ctx.w.Line(l);
    for (const std::string& l : lv.pops) ctx.w.Line(l);
    ctx.parent = saved;
}

void EmitSplitComponent(EmitCtx& ctx, const Component& c, const Node& slot, const std::string& linkage, const ParamList& params) {
    Writer& w = ctx.w;
    std::vector<const Node*> chain;
    FindChain(c.root, &slot, chain);
    std::vector<Level> levels;
    ctx.split_chain = chain;
    std::string chain_fill;
    ++w.suppress_markers;
    w.Line(linkage + "bool " + ComponentFnName(ctx.opts, c, "Begin") + "(" + DeclList(params) + ") {");
    w.Indent();
    std::string scale = ScaleDecl(ctx.opts);
    if (!scale.empty()) w.Line(scale);
    size_t void_off = w.Offset();
    for (size_t L = 0; L < chain.size(); ++L) {
        Level lv;
        lv.node = chain[L];
        lv.next = L + 1 < chain.size() ? chain[L + 1] : &slot;
        lv.idx = IndexIn(*lv.node, *lv.next);
        ctx.parent = L > 0 ? chain[L - 1] : nullptr;
        const Node& n = *lv.node;
        if (!n.enabled_if.empty() || !n.anim.Empty() || !n.fx.empty() || !n.tooltip.empty())
            ctx.Diag(Diagnostic::Level::Info, &n, nullptr, "enabledIf/anim/fx/tooltip are not generated for slot ancestors");
        if (!n.font.empty()) {
            std::string font = "Font_" + SanitizeIdentifier(n.font);
            ctx.Include("\"imforge_fonts.h\"");
            w.Line(PushFontLine(font));
            lv.pops.push_back("ImGui::PopFont();");
        }
        PushNodeStyle(ctx, n, lv.pops);
        auto fail_path = [&](bool current_open) {
            if (!current_open) {
                for (const std::string& l : lv.plan.close_outside) w.Line(l);
                for (const std::string& l : lv.pops) w.Line(l);
            }
            for (size_t j = levels.size(); j > 0; --j) CloseLevel(ctx, levels[j - 1], true, false);
            w.Line("return false;");
        };
        if (!n.visible_if.empty()) {
            w.Line("if (!" + ctx.Expr(n.visible_if) + ") {");
            w.Indent();
            for (const std::string& l : lv.pops) w.Line(l);
            for (size_t j = levels.size(); j > 0; --j) CloseLevel(ctx, levels[j - 1], true, false);
            w.Line("return false;");
            w.Unindent();
            w.Line("}");
        }
        ctx.fill_w = chain_fill;
        lv.plan = IsContainerType(n.type) ? PlanContainer(ctx, n) : ContainerPlan();
        ctx.fill_w.clear();
        if (!IsContainerType(n.type) && n.type != "Component")
            ctx.Diag(Diagnostic::Level::Warning, &n, nullptr, "slot ancestor '" + n.type + "' is not a container; treated as a flow container");
        for (const std::string& l : lv.plan.guard_pre) w.Line(l);
        for (const std::string& l : lv.plan.pre) w.Line(l);
        if (!lv.plan.cond.empty()) {
            std::string cond = lv.plan.guard.empty() ? lv.plan.cond : "(" + lv.plan.guard + ") && (" + lv.plan.cond + ")";
            w.Line("if (!(" + cond + ")) {");
            w.Indent();
            fail_path(false);
            w.Unindent();
            w.Line("}");
        }
        for (const std::string& l : lv.plan.body_pre) w.Line(l);
        ctx.parent = lv.node;
        for (size_t i = 0; i < lv.idx; ++i) {
            ChildPrefix(ctx, n, n.children[i], i);
            EmitNode(ctx, n.children[i]);
            ChildSuffix(ctx, n, n.children[i], i);
        }
        ChildPrefix(ctx, n, *lv.next, lv.idx);
        chain_fill = ctx.pending_fill;
        ctx.pending_fill.clear();
        SplitCarry(ctx, n, lv.idx, true);
        levels.push_back(lv);
    }
    w.Line("return true;");
    InsertUnusedVoids(w, void_off, params, void_off);
    w.Unindent();
    w.Line("}");
    w.Blank();
    w.Line(linkage + "void " + ComponentFnName(ctx.opts, c, "End") + "(bool open" + (params.empty() ? "" : ", " + DeclList(params)) + ") {");
    w.Indent();
    if (!scale.empty()) w.Line(scale);
    size_t void_off2 = w.Offset();
    w.Line("if (!open) return;");
    for (size_t j = levels.size(); j > 0; --j) CloseLevel(ctx, levels[j - 1], true, true);
    InsertUnusedVoids(w, void_off2, params, void_off2);
    w.Unindent();
    w.Line("}");
    --w.suppress_markers;
    ctx.split_chain.clear();
}

}  // namespace

Result GenerateComponents(const Project& project, const Options& opts) {
    Result r;
    if (project.components.empty()) return r;
    bool single = !opts.settings.split_header;
    std::string linkage = single ? "inline " : "";
    Writer hdr, src;
    hdr.opts = &opts;
    src.opts = &opts;
    std::vector<std::string> includes;
    bool uses_runtime = false, needs_string = false;
    std::vector<std::string> decls;
    TypesDb db;
    LoadTypes(project, opts, db, r.diagnostics);
    for (const Component& c : project.components) {
        EmitCtx ctx(project, nullptr, &c, opts, src, r.literals, r.diagnostics);
        ctx.types = db.loaded ? &db : nullptr;
        ParamList params = ComponentParams(c, opts);
        for (const auto& p : params) needs_string = needs_string || p.first.find("std::string") != std::string::npos;
        EmitHandlerFns(ctx, c.root, "static ", params);
        EmitLockedFns(ctx, c.root, "static ", params);
        std::string slot_name;
        const Node* slot = FindSlotNode(c, slot_name);
        if (c.slots.size() > 1)
            ctx.Diag(Diagnostic::Level::Warning, &c.root, "slots", "component declares several slots; only '" + slot_name + "' is generated");
        if (!slot) {
            std::string sig = "void " + ComponentFnName(opts, c, "") + "(" + DeclList(params) + ")";
            decls.push_back(sig + ";");
            src.Line(linkage + sig + " {");
            src.Indent();
            std::string scale = ScaleDecl(opts);
            if (!scale.empty()) src.Line(scale);
            size_t void_off = src.Offset();
            EmitNode(ctx, c.root);
            InsertUnusedVoids(src, void_off, params, void_off);
            src.Unindent();
            src.Line("}");
        } else {
            decls.push_back("bool " + ComponentFnName(opts, c, "Begin") + "(" + DeclList(params) + ");");
            decls.push_back("void " + ComponentFnName(opts, c, "End") + "(bool open" + (params.empty() ? "" : ", " + DeclList(params)) + ");");
            EmitSplitComponent(ctx, c, *slot, linkage, params);
        }
        src.Blank();
        for (const std::string& inc : ctx.includes) {
            bool have = false;
            for (const std::string& h : includes) have = have || h == inc;
            if (!have) includes.push_back(inc);
        }
        uses_runtime = uses_runtime || ctx.uses_runtime;
        if (ctx.needs_stdlib) {
            bool have = false;
            for (const std::string& h : includes) have = have || h == "\"misc/cpp/imgui_stdlib.h\"";
            if (!have) includes.push_back("\"misc/cpp/imgui_stdlib.h\"");
        }
    }
    if (uses_runtime) includes.push_back("\"" + opts.settings.runtime_header + "\"");
    includes.push_back("<cfloat>");
    if (needs_string) includes.push_back("<string>");

    if (!single) {
        WriteFileHead(hdr, "components schema=1");
        hdr.Line("#pragma once");
        hdr.Line("#include \"imgui.h\"");
        if (needs_string) hdr.Line("#include <string>");
        OpenNamespace(hdr, opts);
        for (const std::string& d : decls) hdr.Line(d);
        hdr.UserSlot("header_extra");
        CloseNamespace(hdr, opts);
        FileOut h;
        h.path = "imforge_components.h";
        h.content = hdr.out;
        r.files.push_back(h);

        Writer pre;
        pre.opts = &opts;
        WriteFileHead(pre, "components schema=1");
        pre.Line("#include \"imforge_components.h\"");
        WriteIncludes(pre, includes);
        pre.UserSlot("includes");
        OpenNamespace(pre, opts);
        pre.UserSlot("file_top");
        pre.Blank();
        src.Prepend(pre.out);
        CloseNamespace(src, opts);
        FileOut s;
        s.path = "imforge_components.cpp";
        s.content = src.out;
        r.files.push_back(s);
        src.Finalize(s.path, r.literals);
    } else {
        Writer pre;
        pre.opts = &opts;
        WriteFileHead(pre, "components schema=1");
        pre.Line("#pragma once");
        pre.Line("#include \"imgui.h\"");
        if (needs_string) includes.push_back("<string>");
        WriteIncludes(pre, includes);
        pre.UserSlot("includes");
        OpenNamespace(pre, opts);
        pre.UserSlot("file_top");
        pre.Blank();
        src.Prepend(pre.out);
        src.UserSlot("header_extra");
        CloseNamespace(src, opts);
        FileOut h;
        h.path = "imforge_components.h";
        h.content = src.out;
        r.files.push_back(h);
        src.Finalize(h.path, r.literals);
    }
    return r;
}

}  // namespace codegen
}  // namespace imforge
