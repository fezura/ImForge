#include "emit_internal.h"

namespace imforge {
namespace codegen {

Result GenerateDevGlue(const Project& project, const Options& opts) {
    Result r;
    Writer w;
    w.opts = &opts;
    WriteFileHead(w, "dev schema=1");
    w.Line("#pragma once");
    w.Line("#include \"imforge/imforge_client.h\"");
    if (!project.binding_types.empty() || !project.types_json.empty() || opts.types) w.Line("#include \"imforge_reflect.h\"");
    for (const Panel& p : project.panels) w.Line("#include \"" + SanitizeIdentifier(p.name) + ".h\"");
    w.UserSlot("includes");
    OpenNamespace(w, opts);
    w.Line("inline void ImForgeRegisterAll() {");
    {
        Writer::Scope s = w.Scoped();
        std::string ns = opts.settings.nmspace.empty() ? std::string() : opts.settings.nmspace + "::";
        for (const Panel& p : project.panels) {
            std::vector<LiteralRef> lits;
            Writer dummy;
            dummy.opts = &opts;
            EmitCtx ctx(project, &p, nullptr, opts, dummy, lits, r.diagnostics);
            std::string args = "imforge::live::State<" + ns + StateTypeName(p) + ">(c)";
            for (const Param& prm : p.params)
                args += ", imforge::live::Root<" + prm.type + ">(c, " + CppStringLiteral(prm.name) + ")";
            std::string block_args = args;
            if (opts.settings.dpi_mode == "variable") block_args += ", c.S()";
            std::vector<std::pair<const Node*, std::string>> handlers;
            CollectHandlers(p.root, handlers);
            for (const auto& h : handlers) {
                std::string key = p.name + "." + h.first->id + "." + h.second;
                w.Line("imforge::live::RegisterHandler(" + CppStringLiteral(key) + ", +[](imforge::DrawCtx& c) { " +
                       ctx.HandlerName(*h.first, h.second.c_str()) + "(" + args + "); });");
            }
            std::vector<const Node*> locked;
            CollectLocked(p.root, locked);
            for (const Node* n : locked) {
                std::string key = p.name + "." + n->id;
                w.Line("imforge::live::RegisterBlock(" + CppStringLiteral(key) + ", +[](imforge::DrawCtx& c) { " +
                       BlockFnName(ctx, *n) + "(" + block_args + "); });");
            }
        }
        for (const Component& c : project.components) {
            std::vector<std::pair<const Node*, std::string>> handlers;
            CollectHandlers(c.root, handlers);
            if (!handlers.empty()) {
                Diagnostic d;
                d.level = Diagnostic::Level::Info;
                d.panel = c.name;
                d.message = "component handlers are not registered with the live interpreter (parameters are not available from DrawCtx)";
                r.diagnostics.push_back(d);
            }
        }
        w.UserSlot("register_extra");
    }
    w.Line("}");
    CloseNamespace(w, opts);
    FileOut f;
    f.path = "imforge_dev.h";
    f.content = w.out;
    r.files.push_back(f);
    return r;
}

}  // namespace codegen
}  // namespace imforge
