#include <cmath>
#include <cstring>

#include "emit_internal.h"
#include "imforge/imforge_anim_names.h"

namespace imforge {
namespace codegen {

namespace {

struct Decor {
    std::vector<std::string> after;   // lines written after the item, in order
};

bool IsNonPxVar(const std::string& name) {
    static const char* const kNames[] = {"Alpha", "DisabledAlpha", "WindowTitleAlign", "ButtonTextAlign",
                                         "SelectableTextAlign", "SeparatorTextAlign", "TableAngledHeadersAngle",
                                         "TableAngledHeadersTextAlign"};
    for (const char* k : kNames)
        if (name == k) return true;
    return false;
}

std::string StyleVarValue(EmitCtx& ctx, const Node& n, const std::string& name, const Json& v) {
    bool vec2 = StyleVarIsVec2(name.c_str());
    bool scale = !IsNonPxVar(name);
    if (vec2) {
        if (!v.IsObject()) {
            ctx.Diag(Diagnostic::Level::Warning, &n, "style", "style var " + name + " expects a vec2");
            return std::string();
        }
        double x = v["x"].AsDouble(), y = v["y"].AsDouble();
        return "ImVec2(" + (scale ? ctx.Px(x) : NumLit(x)) + ", " + (scale ? ctx.Px(y) : NumLit(y)) + ")";
    }
    if (!v.IsNumber()) {
        ctx.Diag(Diagnostic::Level::Warning, &n, "style", "style var " + name + " expects a number");
        return std::string();
    }
    return scale ? ctx.Px(v.AsDouble()) : FloatLitOf(v.AsDouble());
}

void PushStyle(EmitCtx& ctx, const Node& n, const StyleOverride& st, std::vector<std::string>& pops) {
    int plain_colors = 0;
    for (const auto& c : st.colors) {
        const char* canon = ImGuiColCanonicalName(c.first.c_str());
        bool known = true;
        int minv = ColGuardVersion(canon, ctx.Target(), &known);
        if (!known)
            ctx.Diag(Diagnostic::Level::Info, &n, "style",
                     "unknown ImGuiCol '" + c.first + "' emitted under #if IMGUI_VERSION_NUM >= " + std::to_string(minv));
        bool docking = IsDockingColorName(canon);
        std::string line = "ImGui::PushStyleColor(ImGuiCol_" + SanitizeIdentifier(canon) + ", " + ctx.ColorLit(c.second) + ");";
        if (docking || minv > ctx.Version()) {
            ctx.w.Line(GuardedStyle(docking, minv, line));
            pops.insert(pops.begin(), GuardedStyle(docking, minv, "ImGui::PopStyleColor();"));
        } else {
            ctx.w.Line(line);
            ++plain_colors;
        }
    }
    if (plain_colors > 0)
        pops.insert(pops.begin(), plain_colors == 1 ? "ImGui::PopStyleColor();" : "ImGui::PopStyleColor(" + std::to_string(plain_colors) + ");");
    int plain_vars = 0;
    for (const auto& v : st.vars) {
        bool known = true;
        int minv = VarGuardVersion(v.first.c_str(), ctx.Target(), &known);
        if (!known)
            ctx.Diag(Diagnostic::Level::Info, &n, "style",
                     "unknown ImGuiStyleVar '" + v.first + "' emitted under #if IMGUI_VERSION_NUM >= " + std::to_string(minv));
        std::string val = StyleVarValue(ctx, n, v.first, v.second);
        if (val.empty()) continue;
        bool docking = IsDockingVarName(v.first.c_str());
        std::string line = "ImGui::PushStyleVar(ImGuiStyleVar_" + SanitizeIdentifier(v.first) + ", " + val + ");";
        if (docking || minv > ctx.Version()) {
            ctx.w.Line(GuardedStyle(docking, minv, line));
            pops.insert(pops.begin(), GuardedStyle(docking, minv, "ImGui::PopStyleVar();"));
        } else {
            ctx.w.Line(line);
            ++plain_vars;
        }
    }
    if (plain_vars > 0)
        pops.insert(pops.begin(), plain_vars == 1 ? "ImGui::PopStyleVar();" : "ImGui::PopStyleVar(" + std::to_string(plain_vars) + ");");
    for (const std::string& cls : st.classes) {
        bool known = false;
        for (const auto& sc : ctx.project.style_classes) known = known || sc.first == cls;
        if (!known) {
            ctx.Diag(Diagnostic::Level::Warning, &n, "style", "unknown style class '" + cls + "' ignored");
            continue;
        }
        ctx.Include("\"imforge_styles.h\"");
        std::string id = SanitizeIdentifier(cls);
        ctx.w.Line("Style_" + id + "_Push(" + (ctx.opts.settings.dpi_mode == "variable" ? "imb_scale" : "") + ");");
        pops.insert(pops.begin(), "Style_" + id + "_Pop();");
    }
}

std::string TriggerExpr(EmitCtx& ctx, const Node& n, const std::string& on, std::vector<std::string>& stores) {
    if (on == "hover" || on == "active" || on == "focus" || on == "focused") {
        std::string key = n.id + "/" + (on == "focused" ? "focus" : on);
        const char* probe = on == "hover" ? "ImGui::IsItemHovered()" : (on == "active" ? "ImGui::IsItemActive()" : "ImGui::IsItemFocused()");
        std::string store = "ImGui::GetStateStorage()->SetBool(ImGui::GetID(" + ctx.StrLit(key) + "), " + probe + ");";
        bool have = false;
        for (const std::string& s : stores) have = have || s == store;
        if (!have) stores.push_back(store);
        return "ImGui::GetStateStorage()->GetBool(ImGui::GetID(" + ctx.StrLit(key) + "))";
    }
    if (on == "always" || on == "visible" || on.empty()) return "true";
    return ctx.Expr(on);
}

void PushTransitions(EmitCtx& ctx, const Node& n, Decor& d) {
    if (!ctx.opts.features_animations || n.anim.transitions.empty()) return;
    std::vector<std::string> stores;
    std::vector<std::string> pops;
    int k = 0;
    for (const Transition& tr : n.anim.transitions) {
        const std::string& prop = tr.prop;
        std::string tvar = ctx.Local(n, ("t" + std::to_string(k)).c_str());
        std::string push, pop;
        if (prop.compare(0, 6, "color.") == 0) {
            std::string col = ImGuiColCanonicalName(prop.substr(6).c_str());
            if (ImGuiColMinVersion(col.c_str()) == 0) {
                ctx.Diag(Diagnostic::Level::Warning, &n, "anim", "unknown ImGuiCol in transition '" + prop + "'");
                continue;
            }
            int minv = ImGuiColMinVersion(col.c_str());
            Vec4 to = tr.to.IsString() ? ParseColor(tr.to.AsString()) : Value::Infer(tr.to).ToVec4();
            push = Guarded(minv, "ImGui::PushStyleColor(ImGuiCol_" + col + ", imb::Lerp(ImGui::GetStyleColorVec4(ImGuiCol_" + col + "), " +
                                     ctx.ColorLit(to) + ", " + tvar + "));");
            if (minv > ctx.Version()) push = "(void)" + tvar + ";\n" + push;
            pop = Guarded(minv, "ImGui::PopStyleColor();");
        } else if (prop.compare(0, 4, "var.") == 0 || prop == "alpha" || prop == "rounding" || prop == "border") {
            std::string var = prop == "alpha" ? "Alpha" : prop == "rounding" ? "FrameRounding" : prop == "border" ? "FrameBorderSize" : prop.substr(4);
            if (ImGuiStyleVarMinVersion(var.c_str()) == 0) {
                ctx.Diag(Diagnostic::Level::Warning, &n, "anim", "unknown ImGuiStyleVar in transition '" + prop + "'");
                continue;
            }
            std::string to = StyleVarValue(ctx, n, var, tr.to);
            if (to.empty()) continue;
            int minv = ImGuiStyleVarMinVersion(var.c_str());
            push = Guarded(minv, "ImGui::PushStyleVar(ImGuiStyleVar_" + var + ", imb::Lerp(ImGui::GetStyle()." + var + ", " + to + ", " + tvar + "));");
            if (minv > ctx.Version()) push = "(void)" + tvar + ";\n" + push;
            pop = Guarded(minv, "ImGui::PopStyleVar();");
        } else if (prop == "offset.x" || prop == "offset.y") {
            const char* axis = prop == "offset.x" ? "X" : "Y";
            push = std::string("ImGui::SetCursorPos") + axis + "(ImGui::GetCursorPos" + axis + "() + " + ctx.Px(tr.to.AsDouble()) + " * " + tvar + ");";
        } else {
            ctx.Diag(Diagnostic::Level::Info, &n, "anim", "transition prop '" + prop + "' is preview-only (not generated)");
            continue;
        }
        std::string on = TriggerExpr(ctx, n, tr.on, stores);
        imb::Ease ease = imb::Ease::OutCubic;
        if (!TryEaseFromName(tr.ease.c_str(), &ease))
            ctx.Diag(Diagnostic::Level::Warning, &n, "anim", "unknown ease '" + tr.ease + "', using OutCubic");
        std::string key = ctx.StrLit(n.id + "/t" + std::to_string(k));
        if (tr.stiffness > 0)
            ctx.w.Line("const float " + tvar + " = imb::Spring(ImGui::GetID(" + key + "), " + on + " ? 1.0f : 0.0f, " +
                       FloatLitOf(tr.stiffness) + ", " + FloatLitOf(tr.damping > 0 ? tr.damping : 26.0f) + ");");
        else
            ctx.w.Line("const float " + tvar + " = imb::Transition(ImGui::GetID(" + key + "), " + on + ", " +
                       FloatLitOf(tr.duration) + ", imb::Ease::" + EaseName(ease) + ");");
        ctx.w.Line(push);
        if (!pop.empty()) pops.insert(pops.begin(), pop);
        ctx.uses_runtime = true;
        ++k;
    }
    std::vector<std::string> after = stores;
    after.insert(after.end(), pops.begin(), pops.end());
    d.after.insert(d.after.begin(), after.begin(), after.end());
}

std::string FxColor(EmitCtx& ctx, const Json& params, const char* key, const char* def) {
    const Json& v = params[key];
    if (v.IsString()) return ctx.ColorU32Lit(ParseColor(v.AsString()));
    return ctx.ColorU32Lit(ParseColor(def));
}

std::string FxNum(EmitCtx& ctx, const Json& params, const char* key, double def, bool scaled) {
    double v = params[key].IsNumber() ? params[key].AsDouble() : def;
    return scaled ? ctx.Px(v) : FloatLitOf(v);
}

void EmitFx(EmitCtx& ctx, const Node& n, const std::string& dl, const std::string& min, const std::string& max) {
    if (!ctx.opts.features_fx || n.fx.empty()) return;
    for (const FxSpec& fx : n.fx) {
        const Json& p = fx.params;
        std::string rounding = p["rounding"].IsNumber() ? ctx.Px(p["rounding"].AsDouble()) : "ImGui::GetStyle().FrameRounding";
        if (fx.type == "blur") {
            ctx.uses_fx = true;
            ctx.w.Line("#ifdef IMB_FX_AVAILABLE");
            ctx.w.Line("imb::fx::Blur(" + dl + ", " + min + ", " + max + ", " + FxNum(ctx, p, "radius", 8, false) + ", " + rounding + ");");
            ctx.w.Line("#endif");
            continue;
        }
        ctx.uses_runtime = true;
        if (fx.type == "shadow") {
            std::string offset = "ImVec2(0, 2)";
            if (p["offset"].IsObject()) offset = "ImVec2(" + ctx.Px(p["offset"]["x"].AsDouble()) + ", " + ctx.Px(p["offset"]["y"].AsDouble()) + ")";
            ctx.w.Line("imb::Shadow(" + dl + ", " + min + ", " + max + ", " + rounding + ", " + FxColor(ctx, p, "color", "#00000080") +
                       ", " + FxNum(ctx, p, "size", 12, true) + ", " + offset + ");");
        } else if (fx.type == "glow") {
            ctx.w.Line("imb::Glow(" + dl + ", " + min + ", " + max + ", " + rounding + ", " + FxColor(ctx, p, "color", "#2ecc71a0") +
                       ", " + FxNum(ctx, p, "size", 12, true) + ");");
        } else if (fx.type == "gradient") {
            const char* k0 = p.Has("c0") ? "c0" : "from";
            const char* k1 = p.Has("c1") ? "c1" : "to";
            ctx.w.Line("imb::GradientRect(" + dl + ", " + min + ", " + max + ", " + FxColor(ctx, p, k0, "#ffffffff") + ", " +
                       FxColor(ctx, p, k1, "#000000ff") + ", " + (p["vertical"].AsBool(true) ? "true" : "false") + ", " + rounding + ");");
        } else if (fx.type == "border") {
            std::string style = p["style"].AsString("Solid");
            if (style != "Solid" && style != "Rainbow" && style != "Sweep" && style != "Pulse") style = "Solid";
            ctx.w.Line("imb::AnimatedBorder(" + dl + ", " + min + ", " + max + ", " + rounding + ", " + FxNum(ctx, p, "thickness", 1, true) +
                       ", imb::BorderStyle::" + style + ", " + FxColor(ctx, p, "color", "#ffffffff") + ", " + FxNum(ctx, p, "speed", 1, false) + ");");
        } else if (fx.type == "particles") {
            std::string st = ctx.Local(n, "particles");
            ctx.w.Line("imb::ParticleStyle " + st + ";");
            if (p["count"].IsNumber()) ctx.w.Line(st + ".count = " + std::to_string(p["count"].AsInt()) + ";");
            if (p["speed"].IsNumber()) ctx.w.Line(st + ".speed = " + FloatLitOf(p["speed"].AsDouble()) + ";");
            if (p["size"].IsNumber()) ctx.w.Line(st + ".size = " + FloatLitOf(p["size"].AsDouble()) + ";");
            if (p["color"].IsString()) ctx.w.Line(st + ".color = " + FxColor(ctx, p, "color", "#40ffffff") + ";");
            if (p["connect"].IsBool()) ctx.w.Line(st + ".connect = " + std::string(p["connect"].AsBool() ? "true" : "false") + ";");
            if (p["connectDist"].IsNumber()) ctx.w.Line(st + ".connect_dist = " + FloatLitOf(p["connectDist"].AsDouble()) + ";");
            ctx.w.Line("imb::Particles(" + dl + ", ImGui::GetID(" + ctx.Id(n) + "), " + min + ", " + max + ", " + st + ");");
        } else if (fx.type == "noise") {
            ctx.w.Line("imb::NoiseBackground(" + dl + ", " + min + ", " + max + ", " + FxColor(ctx, p, "color", "#ffffff10") + ", " +
                       FxNum(ctx, p, "density", 0.5, false) + ", ImGui::GetID(" + ctx.Id(n) + "));");
        } else if (fx.type == "cursorHighlight" || fx.type == "cursor_highlight") {
            ctx.w.Line("imb::CursorHighlight(" + dl + ", " + min + ", " + max + ", " + rounding + ", " + FxColor(ctx, p, "color", "#ffffff20") +
                       ", " + FxNum(ctx, p, "radius", 80, true) + ");");
        } else {
            ctx.Diag(Diagnostic::Level::Warning, &n, "fx", "unknown fx type '" + fx.type + "' ignored");
        }
    }
}

void EmitItem(EmitCtx& ctx, const Node& n) {
    if (n.type == "Component") {
        EmitComponentInstance(ctx, n);
        return;
    }
    if (IsContainerType(n.type)) {
        EmitContainer(ctx, n);
        return;
    }
    if (ctx.project.widget_catalog) {
        if (const CustomWidgetDef* c = ctx.project.widget_catalog->FindCustom(n.type.c_str())) {
            if (!EmitCustomWidget(ctx, n, *c)) ctx.w.Line("ImGui::TextDisabled(" + ctx.StrLit("[" + n.type + "]") + ");");
            return;
        }
    }
    if (!EmitStockWidget(ctx, n)) {
        ctx.Diag(Diagnostic::Level::Error, &n, nullptr, "unknown widget type '" + n.type + "'");
        ctx.w.Line("ImGui::TextDisabled(" + ctx.StrLit("[" + n.type + "]") + ");");
    }
}

}  // namespace

void PushNodeStyle(EmitCtx& ctx, const Node& n, std::vector<std::string>& pops) { PushStyle(ctx, n, n.style, pops); }

void EmitNodeBody(EmitCtx& ctx, const Node& n) {
    Writer& w = ctx.w;
    Decor d;
    int close_braces = 0;
    const AnimSpec* src = nullptr;
    float delay = 0;
    if (ctx.opts.features_animations) {
        bool delegates = n.anim.stagger > 0 && !n.children.empty();
        if (!delegates && (!n.anim.enter_preset.empty() || !n.anim.exit_preset.empty())) {
            src = &n.anim;
        } else if (ctx.parent && ctx.parent->anim.stagger > 0 && !ctx.parent->anim.enter_preset.empty()) {
            src = &ctx.parent->anim;
            double raw = static_cast<double>(ctx.parent->anim.stagger) * static_cast<double>(IndexIn(*ctx.parent, n));
            delay = static_cast<float>(std::floor(raw * 10000.0 + 0.5) / 10000.0);
        }
    }
    std::string visible = n.visible_if.empty() ? std::string("true") : ctx.Expr(n.visible_if);
    if (src) {
        const std::string& in_name = src->enter_preset.empty() ? src->exit_preset : src->enter_preset;
        const std::string& out_name = src->exit_preset.empty() ? src->enter_preset : src->exit_preset;
        imb::Preset in = imb::Preset::None, out = imb::Preset::None;
        if (!TryPresetFromName(in_name.c_str(), &in))
            ctx.Diag(Diagnostic::Level::Warning, &n, "anim", "unknown enter preset '" + in_name + "'");
        if (!TryPresetFromName(out_name.c_str(), &out))
            ctx.Diag(Diagnostic::Level::Warning, &n, "anim", "unknown exit preset '" + out_name + "'");
        ctx.uses_runtime = true;
        std::string vis = visible, preset = std::string("imb::Preset::") + PresetName(in), dur = FloatLitOf(src->enter_duration);
        if (!n.visible_if.empty()) {
            vis = ctx.Local(n, "visible");
            w.Line("const bool " + vis + " = " + visible + ";");
            if (out != in) preset = vis + " ? imb::Preset::" + PresetName(in) + " : imb::Preset::" + PresetName(out);
            if (src->exit_duration != src->enter_duration) dur = vis + " ? " + FloatLitOf(src->enter_duration) + " : " + FloatLitOf(src->exit_duration);
        }
        std::string extra = delay > 0 ? ", " + FloatLitOf(delay) : std::string();
        w.Line("if (imb::BeginEnter(ImGui::GetID(" + ctx.Id(n) + "), " + vis + ", " + dur + ", " + preset + extra + ")) {");
        w.Indent();
        d.after.push_back("imb::EndEnter();");
        ++close_braces;
    } else if (!n.visible_if.empty()) {
        w.Line("if (" + visible + ") {");
        w.Indent();
        ++close_braces;
    }
    if (!n.enabled_if.empty()) {
        std::string e = ctx.Expr(n.enabled_if);
        if (ctx.Version() >= 18600) {
            w.Line("ImGui::BeginDisabled(!" + e + ");");
            d.after.push_back("ImGui::EndDisabled();");
        } else {
            ctx.Include("\"imgui_internal.h\"");
            w.Line("#if IMGUI_VERSION_NUM >= 18600");
            w.Line("ImGui::BeginDisabled(!" + e + ");");
            w.Line("#else");
            w.Line("ImGui::PushItemFlag(ImGuiItemFlags_Disabled, !" + e + ");");
            w.Line("ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * (" + e + " ? 1.0f : 0.5f));");
            w.Line("#endif");
            d.after.push_back("#if IMGUI_VERSION_NUM >= 18600\nImGui::EndDisabled();\n#else\nImGui::PopStyleVar();\nImGui::PopItemFlag();\n#endif");
        }
    }
    if (!n.font.empty()) {
        bool known = false;
        for (const FontSpec& f : ctx.project.fonts) known = known || f.name == n.font;
        if (!known) {
            ctx.Diag(Diagnostic::Level::Warning, &n, "font", "unknown font '" + n.font + "' ignored");
        } else {
            ctx.Include("\"imforge_fonts.h\"");
            std::string font = "Font_" + SanitizeIdentifier(n.font);
            w.Line(PushFontLine(font));
            d.after.push_back("ImGui::PopFont();");
        }
    }
    std::vector<std::string> style_pops;
    PushStyle(ctx, n, n.style, style_pops);
    Decor trans;
    PushTransitions(ctx, n, trans);

    EmitItem(ctx, n);

    bool is_window = n.type == "Window";
    if (!n.tooltip.empty() && !is_window) {
        TextFmt t = ctx.TextOf(n, "tooltip", n.tooltip);
        w.Line("if (ImGui::IsItemHovered()) ImGui::SetTooltip(" + t.CallArgs() + ");");
    }
    if (!is_window && !n.fx.empty() && ctx.opts.features_fx) {
        w.Line("{");
        {
            Writer::Scope s = w.Scoped();
            w.Line("ImDrawList* fx_dl = ImGui::GetWindowDrawList();");
            w.Line("const ImVec2 fx_min = ImGui::GetItemRectMin(), fx_max = ImGui::GetItemRectMax();");
            EmitFx(ctx, n, "fx_dl", "fx_min", "fx_max");
        }
        w.Line("}");
    }
    for (const std::string& l : trans.after) w.Line(l);
    for (const std::string& l : style_pops) w.Line(l);
    for (size_t i = d.after.size(); i > 0; --i) {
        const std::string& l = d.after[i - 1];
        if (l == "imb::EndEnter();") {
            w.Line(l);
            w.Unindent();
            w.Line("}");
            --close_braces;
            continue;
        }
        w.Line(l);
    }
    while (close_braces-- > 0) {
        w.Unindent();
        w.Line("}");
    }
}

void EmitWindowFx(EmitCtx& ctx, const Node& n) {
    if (!ctx.opts.features_fx || n.fx.empty()) return;
    ctx.w.Line("{");
    {
        Writer::Scope s = ctx.w.Scoped();
        ctx.w.Line("ImDrawList* fx_dl = ImGui::GetWindowDrawList();");
        ctx.w.Line("const ImVec2 fx_min = ImGui::GetWindowPos();");
        ctx.w.Line("const ImVec2 fx_max = ImVec2(fx_min.x + ImGui::GetWindowSize().x, fx_min.y + ImGui::GetWindowSize().y);");
        EmitFx(ctx, n, "fx_dl", "fx_min", "fx_max");
    }
    ctx.w.Line("}");
}

void EmitNode(EmitCtx& ctx, const Node& n) {
    std::string saved_fill = ctx.fill_w;
    ctx.fill_w = ctx.pending_fill;
    ctx.pending_fill.clear();
    if (n.type == "Slot") {
        ctx.Diag(Diagnostic::Level::Warning, &n, nullptr, "Slot node outside a component slot chain is ignored");
        ctx.fill_w = saved_fill;
        return;
    }
    ctx.w.BeginBlock(n.id);
    if (n.locked || n.type == "Code") {
        std::string args = ctx.HandlerArgs();
        if (ctx.opts.settings.dpi_mode == "variable") args += (args.empty() ? "" : ", ") + std::string("imb_scale");
        ctx.w.Line(BlockFnName(ctx, n) + "(" + args + ");");
    } else {
        EmitNodeBody(ctx, n);
    }
    ctx.w.EndBlock();
    ctx.fill_w = saved_fill;
}

void EmitChildren(EmitCtx& ctx, const Node& n) {
    const Node* saved = ctx.parent;
    ctx.parent = &n;
    for (size_t i = 0; i < n.children.size(); ++i) {
        ChildPrefix(ctx, n, n.children[i], i);
        EmitNode(ctx, n.children[i]);
        ChildSuffix(ctx, n, n.children[i], i);
    }
    ctx.parent = saved;
}

void CollectHandlers(const Node& root, std::vector<std::pair<const Node*, std::string>>& out) {
    Walk(root, [&](const Node& n) {
        for (const auto& e : n.events) {
            bool handler = e.second.handler;
            for (const Action& a : e.second.actions) handler = handler || a.type == "handler";
            if (handler) out.emplace_back(&n, e.first);
        }
    });
}

void CollectLocked(const Node& root, std::vector<const Node*>& out) {
    Walk(root, [&](const Node& n) {
        if (n.locked || n.type == "Code") out.push_back(&n);
    });
}

void CollectSetTabTargets(const Node& root, std::vector<std::string>& tabbars) {
    Walk(root, [&](const Node& n) {
        for (const auto& e : n.events) {
            for (const Action& a : e.second.actions) {
                if (a.type != "setTab") continue;
                bool have = false;
                for (const std::string& t : tabbars) have = have || t == a.target;
                if (!have && FindNode(root, a.target)) tabbars.push_back(a.target);
            }
        }
    });
}

const std::vector<std::string>& TabTargets(EmitCtx& ctx) {
    if (!ctx.tab_targets_ready) {
        ctx.tab_targets_ready = true;
        if (ctx.panel) CollectSetTabTargets(ctx.panel->root, ctx.tab_targets);
    }
    return ctx.tab_targets;
}

void CollectComponentUses(const Node& root, std::vector<std::string>& names) {
    Walk(root, [&](const Node& n) {
        if (n.type != "Component") return;
        std::string c = n.component;
        if (c.empty()) {
            const PropValue* p = n.Prop("component");
            if (p && p->raw.IsString()) c = p->raw.AsString();
        }
        bool have = false;
        for (const std::string& s : names) have = have || s == c;
        if (!have) names.push_back(c);
    });
}

}  // namespace codegen
}  // namespace imforge
