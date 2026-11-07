#include <algorithm>
#include <cstring>

#include "emit_internal.h"

namespace imforge {
namespace codegen {

void EmitWindowFx(EmitCtx& ctx, const Node& n);

namespace {

SizeSpec WidthSpecOf(const EmitCtx& ctx, const Node& c) {
    SizeSpec w;
    const PropValue* p = c.Prop("size");
    Json def;
    const Json* j = nullptr;
    if (p && !p->raw.IsNull()) {
        if (p->IsParam() || p->IsExpr()) return w;
        j = &p->raw;
    } else {
        const WidgetDef* d = ctx.Def(c);
        const PropDef* pd = d ? FindProp(*d, "size") : nullptr;
        if (!pd) return w;
        def = PropDefaultJson(*pd);
        j = &def;
    }
    bool ok = false;
    if (j->IsObject()) ok = SizeSpec::Parse((*j)["w"], w);
    else if (j->IsArray() && j->Size() == 2) ok = SizeSpec::Parse((*j)[0], w);
    else if (j->IsString() || j->IsNumber()) ok = SizeSpec::Parse(*j, w);
    if (!ok) w = SizeSpec();
    return w;
}

std::string WidthKey(const EmitCtx& ctx, const Node& n) { return "ImGui::GetID(" + ctx.StrLit(n.id + "##imb_w") + ")"; }
std::string OpenKey(const EmitCtx& ctx, const Node& n) { return "ImGui::GetID(" + ctx.StrLit(n.id + "##imb_open") + ")"; }
std::string StoredWidth(const EmitCtx& ctx, const Node& n) {
    return "ImGui::GetStateStorage()->GetFloat(" + WidthKey(ctx, n) + ", 0.0f)";
}

bool HasFillChild(EmitCtx& ctx, const Node& n, int& k, std::string& fixed_expr) {
    k = 0;
    double fixed_px = 0;
    std::string dynamic;
    for (const Node& c : n.children) {
        SizeSpec w = WidthSpecOf(ctx, c);
        if (w.mode == SizeSpec::Mode::Fill) {
            ++k;
        } else if (w.mode == SizeSpec::Mode::Px) {
            fixed_px += w.value;
        } else if (w.mode == SizeSpec::Mode::Percent) {
            dynamic += " - ImGui::GetContentRegionAvail().x * (" + NumLit(w.value) + " / 100.0f)";
        } else if (w.mode == SizeSpec::Mode::FillMinus) {
            ctx.Include("<algorithm>");
            dynamic += " - std::max(ImGui::GetContentRegionAvail().x - " + ctx.Px(w.value) + ", 1.0f)";
        } else {
            dynamic += " - " + StoredWidth(ctx, c);
        }
    }
    fixed_expr = (fixed_px > 0 ? " - " + ctx.Px(fixed_px) : std::string()) + dynamic;
    return k > 0;
}

bool InSplitChain(const EmitCtx& ctx, const Node& n) {
    for (const Node* c : ctx.split_chain)
        if (c == &n) return true;
    return false;
}

bool TrackAny(const EmitCtx& ctx, const Node& hstack) {
    if (hstack.children.size() < 2) return false;
    if (InSplitChain(ctx, hstack)) return true;
    for (const Node& c : hstack.children)
        if (!c.visible_if.empty()) return true;
    return false;
}

bool MeasureAuto(EmitCtx& ctx, const Node& hstack, const Node& child) {
    if (WidthSpecOf(ctx, child).mode != SizeSpec::Mode::Auto) return false;
    auto it = ctx.hstack_has_fill.find(&hstack);
    if (it != ctx.hstack_has_fill.end()) return it->second;
    bool fill = false;
    for (const Node& c : hstack.children) fill = fill || WidthSpecOf(ctx, c).mode == SizeSpec::Mode::Fill;
    ctx.hstack_has_fill[&hstack] = fill;
    return fill;
}

bool TrackAfter(const EmitCtx& ctx, const Node& hstack, size_t index) {
    return index + 1 < hstack.children.size() && TrackAny(ctx, hstack);
}

bool NeedBefore(EmitCtx& ctx, const Node& hstack, const Node& child, size_t index) {
    return TrackAfter(ctx, hstack, index) || MeasureAuto(ctx, hstack, child);
}

const Param* ComponentParam(const EmitCtx& ctx, const std::string& name) {
    if (!ctx.component) return nullptr;
    for (const Param& p : ctx.component->params)
        if (p.name == name) return &p;
    return nullptr;
}

std::string AlignFromCStr(EmitCtx& ctx, const std::string& s) {
    ctx.Include("<cstring>");
    return "(std::strcmp(" + s + ", \"Center\") == 0 ? 1 : (std::strcmp(" + s + ", \"End\") == 0 ? 2 : 0))";
}

int AlignOf(EmitCtx& ctx, const Node& n, std::string& expr) {
    const PropValue* ap = n.Prop("align");
    if (ap && ap->IsParam()) {
        const Param* p = ComponentParam(ctx, ap->Param());
        if (!p) {
            ctx.Diag(Diagnostic::Level::Error, &n, "align", "unknown component parameter '" + ap->Param() + "'");
            return 0;
        }
        std::string id = SanitizeIdentifier(p->name);
        if (p->type == "string") {
            expr = "(!" + id + " ? 0 : " + AlignFromCStr(ctx, id) + ")";
        } else if (p->type == "int") {
            expr = id;
        } else if (p->type == "binding<int>") {
            expr = "(" + id + " ? *" + id + " : 0)";
        } else if (p->type == "binding<string>") {
            expr = "(!" + id + " ? 0 : " + AlignFromCStr(ctx, ctx.opts.settings.std_string ? id + "->c_str()" : id) + ")";
        } else if (p->type.compare(0, 5, "enum:") == 0) {
            std::string list = p->type.substr(5), tail = "0";
            std::vector<std::pair<int, int>> map;
            size_t start = 0;
            for (int idx = 0; start <= list.size(); ++idx) {
                size_t comma = list.find(',', start);
                std::string item = list.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
                if (item == "Center" || item == "End") map.emplace_back(idx, item == "Center" ? 1 : 2);
                if (comma == std::string::npos) break;
                start = comma + 1;
            }
            if (map.empty()) return 0;
            for (size_t i = map.size(); i > 0; --i)
                tail = "(" + id + " == " + std::to_string(map[i - 1].first) + " ? " + std::to_string(map[i - 1].second) + " : " + tail + ")";
            expr = tail;
        } else {
            ctx.Diag(Diagnostic::Level::Warning, &n, "align", "align parameter of type '" + p->type + "' never names Start/Center/End; Start used");
            return 0;
        }
        return -1;
    }
    if (ap && ap->IsExpr()) {
        expr::Type et = ctx.ExprType(ap->Expr());
        if (et == expr::Type::String) {
            expr = AlignFromCStr(ctx, ctx.ExprCStr(ap->Expr()));
        } else if (et == expr::Type::Int || et == expr::Type::Enum) {
            expr = "static_cast<int>(" + ctx.Expr(ap->Expr()) + ")";
        } else {
            ctx.Diag(Diagnostic::Level::Warning, &n, "align", "align expression is not a string or an integer; Start used");
            return 0;
        }
        return -1;
    }
    std::string a = ctx.EnumValue(n, "align", "Start");
    size_t cut = a.find('\x02');
    if (cut != std::string::npos) a = a.substr(cut + 1);
    return a == "Center" ? 1 : (a == "End" ? 2 : 0);
}

std::string SpacingExpr(EmitCtx& ctx, const Node& n, const char* axis) {
    const PropValue* p = n.Prop("spacing");
    if (p && p->raw.IsNumber()) return ctx.Px(p->raw.AsDouble());
    if (p && (p->IsParam() || p->IsExpr())) return ctx.Scaled("(" + ctx.Num(n, "spacing", 0, true) + ")");
    return std::string("ImGui::GetStyle().ItemSpacing.") + axis;
}

std::string ChildFlagsFor(const std::vector<std::pair<std::string, int>>& list, bool border, int branch) {
    std::string body;
    for (const auto& f : list) border = border || f.first == "ImGuiChildFlags_Borders";
    if (border) body = branch >= 19110 ? "ImGuiChildFlags_Borders" : "ImGuiChildFlags_Border";
    for (const auto& f : list) {
        if (f.first == "ImGuiChildFlags_Borders" || f.second > branch) continue;
        body += (body.empty() ? "" : " | ") + f.first;
    }
    return body.empty() ? std::string("0") : body;
}

// ImGuiChildFlags arrived in 1.90 (Border became Borders in 1.91.1), before that BeginChild took
// a bool. Always emit all three branches so the file still builds on 1.85+
void PlanChildLike(EmitCtx& ctx, const Node& n, ContainerPlan& plan, const std::string& size, bool with_flags) {
    bool border = ctx.Bool(n, "border", false);
    std::string flags = with_flags ? ctx.Flags(n, "flags", "WindowFlags") : std::string("0");
    std::vector<std::pair<std::string, int>> child_flags;
    if (with_flags) child_flags = ctx.FlagList(n, "childFlags", "ChildFlags");
    std::string id = ctx.Id(n);
    std::string var = ctx.Local(n, "open");
    std::string head = "const bool " + var + " = ImGui::BeginChild(" + id + ", " + size + ", ";
    std::string tail = ", " + Writer::StripMarks(flags) + ");";
    plan.pre.push_back("#if IMGUI_VERSION_NUM >= 19110");
    plan.pre.push_back(head + ChildFlagsFor(child_flags, border, 19110) + ", " + flags + ");");
    plan.pre.push_back("#elif IMGUI_VERSION_NUM >= 19000");
    plan.pre.push_back(head + ChildFlagsFor(child_flags, border, 19000) + tail);
    plan.pre.push_back("#else");
    plan.pre.push_back(head + (border ? "true" : "false") + tail);
    plan.pre.push_back("#endif");
    plan.cond = var;
    plan.close_outside.push_back("ImGui::EndChild();");
}

}  // namespace

size_t IndexIn(const Node& parent, const Node& child) {
    for (size_t i = 0; i < parent.children.size(); ++i)
        if (&parent.children[i] == &child) return i;
    return 0;
}

ContainerPlan PlanContainer(EmitCtx& ctx, const Node& n) {
    ContainerPlan plan;
    const std::string& t = n.type;
    if (t == "Window") {
        std::string size = ctx.Vec2(n, "initialSize", R"({"x":400,"y":300})", true);
        std::string cond = ctx.EnumValue(n, "sizeCond", "ImGuiCond_", "FirstUseEver");
        if (!size.empty()) plan.pre.push_back("ImGui::SetNextWindowSize(" + size + ", " + cond + ");");
        std::string pos = ctx.Vec2(n, "initialPos", "null", true);
        if (!pos.empty()) plan.pre.push_back("ImGui::SetNextWindowPos(" + pos + ", " + cond + ");");
        std::string flags = ctx.Flags(n, "flags", "WindowFlags");
        bool menubar = ctx.Bool(n, "menuBar", false);
        for (const Node& c : n.children) menubar = menubar || c.type == "MenuBar";
        if (menubar) {
            size_t cut = flags.find('\x02');
            std::string mark = cut == std::string::npos ? std::string() : flags.substr(0, cut + 1);
            std::string body = cut == std::string::npos ? flags : flags.substr(cut + 1);
            flags = mark + (body == "0" ? std::string("ImGuiWindowFlags_MenuBar") : body + " | ImGuiWindowFlags_MenuBar");
        }
        BindInfo open = ctx.Bind(n, "open");
        std::string open_ptr = open.ok ? open.pointer : "nullptr";
        if (open.ok) plan.guard = open.cpp;
        plan.cond = "ImGui::Begin(" + ctx.Label(n, "title") + ", " + open_ptr + ", " + flags + ")";
        plan.close_outside.push_back("ImGui::End();");
        return plan;
    }
    if (t == "VStack") {
        const PropValue* sp = n.Prop("spacing");
        if (sp && !sp->raw.IsNull()) {
            plan.pre.push_back("ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, " +
                               ctx.Num(n, "spacing", 0, true) + (ctx.scale_expr == "1.0f" ? "" : " * " + ctx.scale_expr) + "));");
            plan.close_outside.insert(plan.close_outside.begin(), "ImGui::PopStyleVar();");
        }
        const PropValue* ind = n.Prop("indent");
        if (ind && ind->raw.IsNumber() && ind->raw.AsDouble() > 0) {
            plan.pre.push_back("ImGui::Indent(" + ctx.Px(ind->raw.AsDouble()) + ");");
            plan.close_outside.insert(plan.close_outside.begin(), "ImGui::Unindent(" + ctx.Px(ind->raw.AsDouble()) + ");");
        } else if (ind && (ind->IsParam() || ind->IsExpr())) {
            std::string e = "(" + ctx.Num(n, "indent", 0, true) + ")";
            plan.pre.push_back("if (" + e + " > 0) ImGui::Indent(" + ctx.Scaled(e) + ");");
            plan.close_outside.insert(plan.close_outside.begin(), "if (" + e + " > 0) ImGui::Unindent(" + ctx.Scaled(e) + ");");
        }
        plan.pre.push_back("ImGui::BeginGroup();");
        plan.close_outside.insert(plan.close_outside.begin(), "ImGui::EndGroup();");
        return plan;
    }
    if (t == "HStack") {
        int k = 0;
        std::string fixed;
        size_t count = n.children.size();
        if (HasFillChild(ctx, n, k, fixed) && count > 0) {
            ctx.Include("<algorithm>");
            std::string expr = "ImGui::GetContentRegionAvail().x" + fixed;
            if (count > 1) expr += " - " + SpacingExpr(ctx, n, "x") + " * " + std::to_string(count - 1);
            plan.pre.push_back("const float " + ctx.Local(n, "fill_w") + " = std::max((" + expr + ") / " + std::to_string(k) + ".0f, 1.0f);");
        }
        if (TrackAny(ctx, n)) plan.pre.push_back("bool " + ctx.Local(n, "any") + " = false;");
        std::string align_expr;
        int align = AlignOf(ctx, n, align_expr);
        bool aligned = align != 0;
        if (aligned) {
            std::string mw = ctx.Local(n, "align_w"), av = ctx.Local(n, "avail");
            std::string cond = mw + " > 0.0f && " + mw + " < " + av, factor = align == 1 ? " * 0.5f" : "";
            if (align < 0) {
                std::string ai = ctx.Local(n, "align");
                plan.pre.push_back("const int " + ai + " = " + align_expr + ";");
                cond = ai + " > 0 && " + cond;
                factor = " * (" + ai + " == 1 ? 0.5f : 1.0f)";
            }
            plan.pre.push_back("const float " + mw + " = " + StoredWidth(ctx, n) + ";");
            plan.pre.push_back("const float " + av + " = ImGui::GetContentRegionAvail().x;");
            plan.pre.push_back("if (" + cond + ") ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (" + av + " - " + mw + ")" + factor + ");");
        }
        plan.pre.push_back("ImGui::BeginGroup();");
        plan.close_outside.push_back("ImGui::EndGroup();");
        if (aligned) plan.close_outside.push_back("ImGui::GetStateStorage()->SetFloat(" + WidthKey(ctx, n) + ", ImGui::GetItemRectSize().x);");
        return plan;
    }
    if (t == "Group") {
        plan.pre.push_back("ImGui::BeginGroup();");
        plan.close_outside.push_back("ImGui::EndGroup();");
        return plan;
    }
    if (t == "Child") {
        std::string size = ctx.Size(n);
        if (size.empty()) size = "ImVec2(" + (ctx.fill_w.empty() ? std::string("-FLT_MIN") : ctx.fill_w) + ", -FLT_MIN)";
        PlanChildLike(ctx, n, plan, size, true);
        return plan;
    }
    if (t == "Canvas") {
        std::string size = ctx.Size(n);
        if (size.empty()) size = "ImVec2(0, 0)";
        if (ctx.Has(n, "flags") || ctx.Has(n, "childFlags"))
            ctx.Diag(Diagnostic::Level::Warning, &n, "flags", "Canvas has no flags/childFlags (the interpreter ignores them); not generated");
        PlanChildLike(ctx, n, plan, size, false);
        if (!n.children.empty()) {
            std::string last = ctx.Local(n, "last");
            plan.after_children.push_back("if (ImGui::GetCursorPos().x == " + last + ".x && ImGui::GetCursorPos().y == " + last +
                                          ".y) ImGui::Dummy(ImVec2(0, 0));");
        }
        plan.body_pre.push_back("const ImVec2 " + ctx.Local(n, "origin") + " = ImGui::GetCursorPos();");
        plan.body_pre.push_back("const ImVec2 " + ctx.Local(n, "avail") + " = ImGui::GetContentRegionAvail();");
        return plan;
    }
    if (t == "Grid") {
        const PropValue* colp = n.Prop("columns");
        std::string cols;
        int ncols = 2;
        bool fixed = true;
        if (colp && colp->raw.IsNumber() && !colp->IsExpr() && !colp->IsParam()) {
            int c = colp->raw.AsInt();
            ncols = c < 1 ? 1 : c;
            if (c > 63) {
                fixed = false;
                cols = "(IMGUI_VERSION_NUM >= 18990 ? " + std::to_string(c < 511 ? c : 511) + " : 63)";
            } else {
                cols = c >= 1 ? ctx.Num(n, "columns", 2, false) : std::string("1");
            }
        } else if (!colp || colp->raw.IsNull()) {
            cols = ctx.Num(n, "columns", 2, false);
        } else {
            fixed = false;
            ctx.Include("<algorithm>");
            cols = "std::max(1, std::min(static_cast<int>(" + ctx.Num(n, "columns", 2, false) + "), (IMGUI_VERSION_NUM >= 18990 ? 511 : 63)))";
        }
        if (!fixed) {
            std::string var = ctx.Local(n, "cols");
            plan.pre.push_back("const int " + var + " = " + cols + ";");
            cols = var;
        }
        plan.cond = "ImGui::BeginTable(" + ctx.Id(n) + ", " + cols + ", " + ctx.Flags(n, "flags", "TableFlags") + ")";
        std::vector<std::string> headers = ctx.StringList(n, "headers");
        const PropValue* cw = n.Prop("columnWidths");
        bool has_widths = cw && cw->raw.IsArray() && cw->raw.Size() > 0;
        if (!fixed) ncols = static_cast<int>(std::max(headers.size(), has_widths ? cw->raw.Size() : size_t(0)));
        if (!headers.empty() || has_widths) {
            for (int i = 0; i < ncols; ++i) {
                std::string name = i < static_cast<int>(headers.size()) ? ctx.StrLit(headers[static_cast<size_t>(i)]) : "\"\"";
                std::string line = "ImGui::TableSetupColumn(" + name;
                SizeSpec s;
                if (has_widths && static_cast<size_t>(i) < cw->raw.Size() && SizeSpec::Parse(cw->raw[static_cast<size_t>(i)], s)) {
                    if (s.mode == SizeSpec::Mode::Px) line += ", ImGuiTableColumnFlags_WidthFixed, " + ctx.Px(s.value);
                    else if (s.mode == SizeSpec::Mode::Percent) line += ", ImGuiTableColumnFlags_WidthStretch, " + FloatLitOf(s.value);
                    else if (s.mode == SizeSpec::Mode::Fill) line += ", ImGuiTableColumnFlags_WidthStretch, 1.0f";
                }
                plan.body_pre.push_back((fixed || i == 0 ? std::string() : "if (" + cols + " > " + std::to_string(i) + ") ") + line + ");");
            }
            if (!fixed) {
                std::string c = ctx.Local(n, "c");
                plan.body_pre.push_back("for (int " + c + " = " + std::to_string(ncols) + "; " + c + " < " + cols + "; ++" + c + ") ImGui::TableSetupColumn(\"\");");
            }
            if (!headers.empty()) plan.body_pre.push_back("ImGui::TableHeadersRow();");
        }
        plan.close_inside.push_back("ImGui::EndTable();");
        return plan;
    }
    if (t == "TabBar") {
        plan.cond = "ImGui::BeginTabBar(" + ctx.Id(n) + ", " + ctx.Flags(n, "flags", "TabBarFlags") + ")";
        plan.close_inside.push_back("ImGui::EndTabBar();");
        if (ctx.panel) {
            for (const std::string& tb : TabTargets(ctx)) {
                if (tb != n.id) continue;
                plan.body_pre.push_back("const int " + ctx.Local(n, "pending") + " = state." + TabStateMember(n.id) + ";");
                plan.body_pre.push_back("state." + TabStateMember(n.id) + " = -1;");
            }
        }
        return plan;
    }
    if (t == "Tab") {
        std::string flags = ctx.Flags(n, "flags", "TabItemFlags");
        if (ctx.panel && ctx.parent && ctx.parent->type == "TabBar") {
            for (const std::string& tb : TabTargets(ctx)) {
                if (tb != ctx.parent->id) continue;
                size_t idx = IndexIn(*ctx.parent, n);
                flags = "(" + flags + ") | (" + ctx.Local(*ctx.parent, "pending") + " == " + std::to_string(idx) + " ? ImGuiTabItemFlags_SetSelected : 0)";
            }
        }
        std::string open_ptr = "nullptr";
        const bool closable = ctx.Bool(n, "closable", false);
        BindInfo b = ctx.Bind(n, "open");
        if (b.ok) {
            if (closable) open_ptr = b.pointer;
            plan.guard = b.cpp;
        } else if (closable) {
            std::string var = ctx.Local(n, "open");
            plan.guard_pre.push_back("static bool " + var + " = true;");
            open_ptr = "&" + var;
            plan.guard = var;
        }
        plan.cond = "ImGui::BeginTabItem(" + ctx.Label(n) + ", " + open_ptr + ", " + flags + ")";
        plan.close_inside.push_back("ImGui::EndTabItem();");
        return plan;
    }
    if (t == "CollapsingHeader" || t == "TreeNode") {
        std::string flags = ctx.Flags(n, "flags", "TreeNodeFlags");
        if (ctx.Bool(n, "defaultOpen", false)) {
            size_t cut = flags.find('\x02');
            std::string mark = cut == std::string::npos ? std::string() : flags.substr(0, cut + 1);
            std::string body = cut == std::string::npos ? flags : flags.substr(cut + 1);
            flags = mark + (body == "0" ? std::string("ImGuiTreeNodeFlags_DefaultOpen") : body + " | ImGuiTreeNodeFlags_DefaultOpen");
        }
        plan.cond = (t == "TreeNode" ? "ImGui::TreeNodeEx(" : "ImGui::CollapsingHeader(") + ctx.Label(n) + ", " + flags + ")";
        bool no_push = false;
        const PropValue* fp = n.Prop("flags");
        if (fp && fp->raw.IsArray())
            for (size_t i = 0; i < fp->raw.Size(); ++i)
                if (fp->raw[i].IsString()) {
                    const std::string& f = fp->raw[i].AsString();
                    no_push = no_push || f == "NoTreePushOnOpen" || f == "ImGuiTreeNodeFlags_NoTreePushOnOpen";
                }
        if (t == "TreeNode" && !no_push) plan.close_inside.push_back("ImGui::TreePop();");
        return plan;
    }
    if (t == "Popup") {
        plan.cond = "ImGui::BeginPopup(" + ctx.Id(n) + ", " + ctx.Flags(n, "flags", "WindowFlags") + ")";
        plan.close_inside.push_back("ImGui::EndPopup();");
        return plan;
    }
    if (t == "Modal") {
        std::string pid = ctx.StrLit("###" + n.id), open_ptr = "nullptr";
        const bool closable = ctx.Bool(n, "closable", false);
        const bool split = InSplitChain(ctx, n);
        BindInfo b = ctx.Bind(n, "open");
        std::string prev = ctx.Local(n, "prev");
        if (b.ok || (!split && (ctx.HasEvent(n, "onOpen") || ctx.HasEvent(n, "onClose"))))
            plan.pre.push_back("const int " + prev + " = ImGui::GetStateStorage()->GetInt(" + OpenKey(ctx, n) + ", -1);");
        if (b.ok) {
            if (closable) open_ptr = b.pointer;
            std::string popup = ctx.Local(n, "popup");
            plan.pre.push_back("const bool " + popup + " = ImGui::IsPopupOpen(" + pid + ");");
            plan.pre.push_back("if (" + popup + " && " + prev + " != 1) " + b.cpp + " = true;");
            plan.pre.push_back("else if (!" + popup + " && " + prev + " == 1) " + b.cpp + " = false;");
            plan.pre.push_back("else if (!" + popup + " && " + b.cpp + ") ImGui::OpenPopup(" + pid + ");");
            if (split) plan.pre.push_back("ImGui::GetStateStorage()->SetInt(" + OpenKey(ctx, n) + ", ImGui::IsPopupOpen(" + pid + ") ? 1 : 0);");
        } else if (closable) {
            plan.pre.push_back("bool " + ctx.Local(n, "open") + " = true;");
            open_ptr = "&" + ctx.Local(n, "open");
        }
        std::string title = ctx.Label(n, "title");
        std::string bare_title = Writer::StripMarks(title);
        if (bare_title.empty()) {
            title = ctx.StrLit("###" + n.id);
        } else if (bare_title[0] == '"') {
            title += " " + ctx.StrLit("###" + n.id);
        } else {
            std::string buf = ctx.Local(n, "modal_title");
            ctx.Include("<cstdio>");
            ctx.w.Line("char " + buf + "[256];");
            ctx.w.Line("std::snprintf(" + buf + ", sizeof(" + buf + "), \"%s###%s\", " + title + ", " + ctx.StrLit(n.id) + ");");
            title = buf;
        }
        plan.cond = "ImGui::BeginPopupModal(" + title + ", " + open_ptr + ", " + ctx.Flags(n, "flags", "WindowFlags") + ")";
        if (b.ok) {
            std::string shown = ctx.Local(n, "shown");
            plan.pre.push_back("const bool " + shown + " = " + plan.cond + ";");
            plan.pre.push_back("if (" + shown + " && !" + b.cpp + ") { ImGui::CloseCurrentPopup(); ImGui::EndPopup(); }");
            plan.cond = shown + " && " + b.cpp;
        }
        plan.close_inside.push_back("ImGui::EndPopup();");
        return plan;
    }
    if (t == "MenuBar") {
        plan.cond = "ImGui::BeginMenuBar()";
        plan.close_inside.push_back("ImGui::EndMenuBar();");
        return plan;
    }
    if (t == "Menu") {
        plan.cond = "ImGui::BeginMenu(" + ctx.Label(n) + ", " + ctx.BoolLit(n, "enabled", true) + ")";
        plan.close_inside.push_back("ImGui::EndMenu();");
        return plan;
    }
    return plan;
}

void ChildPrefix(EmitCtx& ctx, const Node& parent, const Node& child, size_t index) {
    const std::string& t = parent.type;
    if (t == "HStack") {
        bool track = TrackAny(ctx, parent);
        if (index > 0) {
            const PropValue* sp = parent.Prop("spacing");
            std::string call = sp && !sp->raw.IsNull() ? "ImGui::SameLine(0.0f, " + SpacingExpr(ctx, parent, "x") + ");" : std::string("ImGui::SameLine();");
            ctx.w.Line(track ? "if (" + ctx.Local(parent, "any") + ") " + call : call);
        }
        bool measure = MeasureAuto(ctx, parent, child);
        if (NeedBefore(ctx, parent, child, index))
            ctx.w.Line("const ImVec2 " + ctx.Local(parent, ("before" + std::to_string(index)).c_str()) + " = ImGui::GetCursorPos();");
        if (measure) ctx.w.Line("const float " + ctx.Local(parent, ("x" + std::to_string(index)).c_str()) + " = ImGui::GetCursorScreenPos().x;");
        if (WidthSpecOf(ctx, child).mode == SizeSpec::Mode::Fill) ctx.pending_fill = ctx.Local(parent, "fill_w");
        return;
    }
    if (t == "Grid") {
        ctx.w.Line("ImGui::TableNextColumn();");
        ctx.w.Line("ImGui::PushID(" + std::to_string(index) + ");");
        return;
    }
    if (t == "Canvas") {
        static const struct { const char* name; float fx, fy; } kAnchors[] = {
            {"TopLeft", 0, 0}, {"Top", 0.5f, 0}, {"TopRight", 1, 0}, {"Left", 0, 0.5f}, {"Center", 0.5f, 0.5f},
            {"Right", 1, 0.5f}, {"BottomLeft", 0, 1}, {"Bottom", 0.5f, 1}, {"BottomRight", 1, 1}};
        float fx = 0, fy = 0;
        bool found = false;
        for (const auto& a : kAnchors) {
            if (child.layout.anchor == a.name) {
                fx = a.fx;
                fy = a.fy;
                found = true;
            }
        }
        if (!found) ctx.Diag(Diagnostic::Level::Warning, &child, "layout", "unknown anchor '" + child.layout.anchor + "'");
        SizeSpec sw, sh;
        const PropValue* p = child.Prop("size");
        if (p && p->raw.IsObject()) {
            if (!SizeSpec::Parse(p->raw["w"], sw)) sw = SizeSpec();
            if (!SizeSpec::Parse(p->raw["h"], sh)) sh = SizeSpec();
        }
        std::string origin = ctx.Local(parent, "origin"), avail = ctx.Local(parent, "avail");
        auto axis = [&](const char* ax, float f, double off, const SizeSpec& size) {
            std::string e = origin + "." + ax;
            if (f != 0) e += " + " + avail + "." + ax + (f == 1 ? "" : " * " + FloatLitOf(f));
            if (off != 0) e += (off < 0 ? " - " : " + ") + ctx.Px(off < 0 ? -off : off);
            if (f != 0 && size.mode == SizeSpec::Mode::Px && size.value != 0) e += " - " + ctx.Px(size.value * f);
            if (f != 0 && size.mode == SizeSpec::Mode::Percent && size.value != 0)
                e += " - " + avail + "." + ax + " * (" + NumLit(size.value * f) + " / 100.0f)";
            return e;
        };
        ctx.w.Line("ImGui::SetCursorPos(ImVec2(" + axis("x", fx, child.layout.offset.x, sw) + ", " + axis("y", fy, child.layout.offset.y, sh) + "));");
        if (index + 1 == parent.children.size()) ctx.w.Line("const ImVec2 " + ctx.Local(parent, "last") + " = ImGui::GetCursorPos();");
        return;
    }
}

void SplitCarry(EmitCtx& ctx, const Node& parent, size_t s, bool save) {
    const size_t count = parent.children.size();
    if (s >= count) return;
    const Node& child = parent.children[s];
    std::vector<std::pair<std::string, char>> vars;
    if (parent.type == "HStack") {
        bool fill_after = false;
        for (size_t i = s + 1; i < count; ++i) fill_after = fill_after || WidthSpecOf(ctx, parent.children[i]).mode == SizeSpec::Mode::Fill;
        if (fill_after) vars.emplace_back("fill_w", 'f');
        if (TrackAfter(ctx, parent, s)) vars.emplace_back("any", 'b');
        if (NeedBefore(ctx, parent, child, s)) vars.emplace_back("before" + std::to_string(s), 'v');
        if (MeasureAuto(ctx, parent, child)) vars.emplace_back("x" + std::to_string(s), 'f');
    } else if (parent.type == "Canvas") {
        if (s + 1 < count) {
            vars.emplace_back("origin", 'v');
            vars.emplace_back("avail", 'v');
        } else {
            vars.emplace_back("last", 'v');
        }
    }
    const std::string st = "ImGui::GetStateStorage()->";
    for (const auto& v : vars) {
        std::string local = ctx.Local(parent, v.first.c_str());
        auto key = [&](const char* tail) { return "ImGui::GetID(" + ctx.StrLit(parent.id + "##imb_" + v.first + tail) + ")"; };
        if (v.second == 'f') {
            ctx.w.Line(save ? st + "SetFloat(" + key("") + ", " + local + ");" : "const float " + local + " = " + st + "GetFloat(" + key("") + ", 0.0f);");
        } else if (v.second == 'b') {
            ctx.w.Line(save ? st + "SetBool(" + key("") + ", " + local + ");" : "bool " + local + " = " + st + "GetBool(" + key("") + ", false);");
        } else if (save) {
            ctx.w.Line(st + "SetFloat(" + key("_x") + ", " + local + ".x);");
            ctx.w.Line(st + "SetFloat(" + key("_y") + ", " + local + ".y);");
        } else {
            ctx.w.Line("const ImVec2 " + local + "(" + st + "GetFloat(" + key("_x") + ", 0.0f), " + st + "GetFloat(" + key("_y") + ", 0.0f));");
        }
    }
}

void ChildSuffix(EmitCtx& ctx, const Node& parent, const Node& child, size_t index) {
    bool track = parent.type == "HStack" && TrackAfter(ctx, parent, index);
    bool measure = parent.type == "HStack" && MeasureAuto(ctx, parent, child);
    if (track || measure) {
        std::string i = std::to_string(index);
        std::string before = ctx.Local(parent, ("before" + i).c_str());
        std::string moved = "ImGui::GetCursorPos().x != " + before + ".x || ImGui::GetCursorPos().y != " + before + ".y";
        if (measure) {
            std::string drawn = ctx.Local(parent, ("drawn" + i).c_str());
            ctx.w.Line("const bool " + drawn + " = " + moved + ";");
            moved = drawn;
            ctx.Include("<algorithm>");
            ctx.w.Line("ImGui::GetStateStorage()->SetFloat(" + WidthKey(ctx, child) + ", " + drawn + " ? std::max(ImGui::GetItemRectMax().x - " +
                       ctx.Local(parent, ("x" + i).c_str()) + ", 0.0f) : 0.0f);");
        }
        if (track) ctx.w.Line("if (" + moved + ") " + ctx.Local(parent, "any") + " = true;");
    }
    if (parent.type == "Grid") ctx.w.Line("ImGui::PopID();");
}

void EmitContainer(EmitCtx& ctx, const Node& n) {
    Writer& w = ctx.w;
    ContainerPlan plan = PlanContainer(ctx, n);
    const bool events = ctx.HasEvent(n, "onOpen") || ctx.HasEvent(n, "onClose");
    const bool modal_bound = n.type == "Modal" && ctx.Bind(n, "open").ok;
    std::string now = ctx.Local(n, "now"), prev = ctx.Local(n, "prev"), now_expr;
    bool track = false, flag = false;
    if (n.type == "Window") {
        BindInfo open = ctx.Bind(n, "open");
        if (open.ok) now_expr = open.cpp;
        else if (events) ctx.Diag(Diagnostic::Level::Info, &n, "events", "onOpen/onClose never fire for a Window without an open binding");
        track = events && open.ok;
    } else if (n.type == "Modal") {
        track = events || modal_bound;
        now_expr = "ImGui::IsPopupOpen(" + ctx.StrLit("###" + n.id) + ")";
    } else if (n.type == "Tab" || n.type == "CollapsingHeader" || n.type == "TreeNode" || n.type == "Popup") {
        track = flag = events;
    }
    if (track && n.type != "Modal") w.Line("const int " + prev + " = ImGui::GetStateStorage()->GetInt(" + OpenKey(ctx, n) + ", -1);");
    if (flag) w.Line("bool " + now + " = false;");
    for (const std::string& l : plan.guard_pre) w.Line(l);
    if (!plan.guard.empty()) {
        w.Line("if (" + plan.guard + ") {");
        w.Indent();
    }
    for (const std::string& l : plan.pre) w.Line(l);
    bool conditional = !plan.cond.empty();
    if (conditional) {
        w.Line("if (" + plan.cond + ") {");
        w.Indent();
    }
    if (flag && conditional) w.Line(now + " = true;");
    if (n.type == "Window") EmitWindowFx(ctx, n);
    for (const std::string& l : plan.body_pre) w.Line(l);
    if (n.type == "Tab" && ctx.parent && ctx.parent->type == "TabBar" && ctx.HasEvent(*ctx.parent, "onChange")) {
        w.Line("if (ImGui::IsItemActivated()) {");
        w.Indent();
        ctx.FireEvent(*ctx.parent, "onChange");
        w.Unindent();
        w.Line("}");
    }
    EmitChildren(ctx, n);
    for (const std::string& l : plan.after_children) w.Line(l);
    for (const std::string& l : plan.close_inside) w.Line(l);
    if (conditional) {
        w.Unindent();
        w.Line("}");
    }
    for (const std::string& l : plan.close_outside) w.Line(l);
    if (!plan.guard.empty()) {
        w.Unindent();
        w.Line("}");
    }
    if (!track) return;
    if (!flag) w.Line("const bool " + now + " = " + now_expr + ";");
    if (modal_bound) {
        BindInfo b = ctx.Bind(n, "open");
        w.Line("if (" + ctx.Local(n, "shown") + " && !" + now + ") " + b.cpp + " = false;");
    }
    const char* const kEvents[] = {"onOpen", "onClose"};
    for (const char* ev : kEvents) {
        if (!ctx.HasEvent(n, ev)) continue;
        bool open_ev = std::strcmp(ev, "onOpen") == 0;
        w.Line("if (" + std::string(open_ev ? "" : "!") + now + " && " + prev + " == " + (open_ev ? "0" : "1") + ") {");
        w.Indent();
        ctx.FireEvent(n, ev);
        w.Unindent();
        w.Line("}");
    }
    w.Line("ImGui::GetStateStorage()->SetInt(" + OpenKey(ctx, n) + ", " + now + " ? 1 : 0);");
}

}  // namespace codegen
}  // namespace imforge
