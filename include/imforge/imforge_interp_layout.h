#ifndef IMFORGE_INTERP_PARTS
#include "imforge_interp.h"
#else
#pragma once

namespace imforge {
namespace interp_layout {

inline bool Placeholder(DrawCtx& ctx, const char* text, ImU32 color);

inline void RectFromItem(DrawCtx& ctx) {
    ctx.has_rect = true;
    ctx.rect_min = ImGui::GetItemRectMin();
    ctx.rect_max = ImGui::GetItemRectMax();
}

inline void RectFromWindow(DrawCtx& ctx) {
    ctx.has_rect = true;
    ctx.rect_min = ImGui::GetWindowPos();
    ImVec2 sz = ImGui::GetWindowSize();
    ctx.rect_max = ImVec2(ctx.rect_min.x + sz.x, ctx.rect_min.y + sz.y);
}

inline std::string StableLabel(const DrawCtx& ctx, const char* prop) {
    return ctx.GetString(prop) + "###" + ctx.node.id;
}

inline void OpenEvents(DrawCtx& ctx, bool open) {
    Interpreter::NodeRt& rt = ctx.interp.Rt(ctx.id);
    if (rt.seen && open != rt.open) ctx.FireEvent(open ? "onOpen" : "onClose");
    rt.open = open;
    rt.seen = true;
}

inline bool OptionalSpacing(const DrawCtx& ctx, float& out) {
    Value v;
    if (!ctx.GetValue("spacing", v, Value::Kind::Float) || v.IsNull()) return false;
    const PropValue* pv = ctx.node.Prop("spacing");
    if (pv && pv->raw.IsNull()) return false;
    out = static_cast<float>(v.ToDouble()) * ctx.S();
    return true;
}

inline void BeginChildCompat(const char* id, const ImVec2& size, bool border, int child_flags, int window_flags) {
#if IMGUI_VERSION_NUM >= 19000
#if IMGUI_VERSION_NUM >= 19110
    if (border) child_flags |= ImGuiChildFlags_Borders;
#else
    if (border) child_flags |= ImGuiChildFlags_Border;
#endif
    ImGui::BeginChild(id, size, child_flags, window_flags);
#else
    (void)child_flags;
    ImGui::BeginChild(id, size, border, window_flags);
#endif
}

inline bool VStack(DrawCtx& ctx) {
    float spacing = 0;
    bool has_spacing = OptionalSpacing(ctx, spacing);
    float indent = ctx.GetFloat("indent", 0) * ctx.S();
    if (has_spacing) ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, spacing));
    if (indent > 0) ImGui::Indent(indent);
    ImGui::BeginGroup();
    ctx.DrawChildren();
    ImGui::EndGroup();
    if (indent > 0) ImGui::Unindent(indent);
    if (has_spacing) ImGui::PopStyleVar();
    RectFromItem(ctx);
    return true;
}

inline bool Group(DrawCtx& ctx) {
    ImGui::BeginGroup();
    ctx.DrawChildren();
    ImGui::EndGroup();
    RectFromItem(ctx);
    return true;
}

inline bool HStack(DrawCtx& ctx) {
    float spacing = ImGui::GetStyle().ItemSpacing.x;
    OptionalSpacing(ctx, spacing);
    const float S = ctx.S();
    const float avail = ImGui::GetContentRegionAvail().x;
    const int n = static_cast<int>(ctx.node.children.size());
    float fixed = 0;
    int fills = 0;
    std::vector<ImGuiID> auto_ids(static_cast<size_t>(n), 0);
    for (int i = 0; i < n; ++i) {
        const Node& child = ctx.node.children[static_cast<size_t>(i)];
        DrawCtx tmp(ctx, child);
        SizeSpec w, h;
        tmp.GetSizeSpec("size", w, h);
        switch (w.mode) {
        case SizeSpec::Mode::Fill: ++fills; break;
        case SizeSpec::Mode::Px: fixed += w.value * S; break;
        case SizeSpec::Mode::Percent: fixed += avail * w.value / 100.0f; break;
        case SizeSpec::Mode::FillMinus: fixed += interp_detail::MaxF(avail - w.value * S, 1.0f); break;
        default:
            ImGui::PushID(child.id.c_str());
            auto_ids[static_cast<size_t>(i)] = ImGui::GetID("##imforge_node");
            ImGui::PopID();
            fixed += ctx.interp.Rt(auto_ids[static_cast<size_t>(i)]).width;
            break;
        }
    }
    float fill_w = -1.0f;
    if (fills > 0) fill_w = interp_detail::MaxF((avail - fixed - spacing * static_cast<float>(n > 0 ? n - 1 : 0)) / static_cast<float>(fills), 1.0f);
    Interpreter::NodeRt& rt = ctx.interp.Rt(ctx.id);
    int align = ctx.GetEnum("align", "Start|Center|End", 0);
    if (align > 0 && rt.width > 0 && rt.width < avail)
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail - rt.width) * (align == 1 ? 0.5f : 1.0f));
    ImGui::BeginGroup();
    bool any_drawn = false;
    for (int i = 0; i < n; ++i) {
        if (any_drawn) ImGui::SameLine(0, spacing);
        const ImVec2 before = ImGui::GetCursorPos();
        const float screen_x = ImGui::GetCursorScreenPos().x;
        ctx.next_sibling = i;
        ctx.child_fill_width = fill_w;
        ctx.child_stagger = ctx.node.anim.stagger > 0 ? ctx.node.anim.stagger * static_cast<float>(i) : 0.0f;
        ctx.interp.DrawNode(ctx.node.children[static_cast<size_t>(i)], ctx);
        const ImVec2 after = ImGui::GetCursorPos();
        const bool drawn = after.x != before.x || after.y != before.y;
        if (drawn) any_drawn = true;
        if (auto_ids[static_cast<size_t>(i)] != 0)
            ctx.interp.Rt(auto_ids[static_cast<size_t>(i)]).width = drawn ? interp_detail::MaxF(ImGui::GetItemRectMax().x - screen_x, 0.0f) : 0.0f;
    }
    ctx.child_fill_width = -1.0f;
    ImGui::EndGroup();
    RectFromItem(ctx);
    rt.width = ctx.rect_max.x - ctx.rect_min.x;
    return true;
}

inline bool Child(DrawCtx& ctx) {
    ImVec2 size = ctx.GetSize();
    int flags = ctx.GetFlags("flags", "WindowFlags");
    int child_flags = 0;
#if IMGUI_VERSION_NUM >= 19000
    child_flags = ctx.GetFlags("childFlags", "ChildFlags");
#endif
    BeginChildCompat(ctx.node.id.c_str(), size, ctx.GetBool("border", false), child_flags, flags);
    ctx.DrawChildren();
    ImGui::EndChild();
    RectFromItem(ctx);
    return true;
}

inline bool Grid(DrawCtx& ctx) {
    int cols = ctx.GetInt("columns", 2);
    if (cols < 1) cols = 1;
    if (cols > interp_detail::MaxGridColumns()) {
        ctx.Diag(Diagnostic::Level::Warning, "columns clamped to the ImGui table limit", "columns");
        cols = interp_detail::MaxGridColumns();
    }
    int flags = ctx.GetFlags("flags", "TableFlags");
    std::vector<std::string> headers = ctx.GetStringList("headers");
    Value widths;
    bool has_widths = ctx.GetValue("columnWidths", widths, Value::Kind::List) && !widths.list.empty();
    if (!ImGui::BeginTable(ctx.node.id.c_str(), cols, flags)) {
        ctx.has_rect = false;
        return true;
    }
    if (has_widths || !headers.empty()) {
        for (int c = 0; c < cols; ++c) {
            const char* label = c < static_cast<int>(headers.size()) ? headers[static_cast<size_t>(c)].c_str() : "";
            int cflags = 0;
            float w = 0;
            if (has_widths && c < static_cast<int>(widths.list.size())) {
                SizeSpec s;
                if (SizeSpec::Parse(widths.list[static_cast<size_t>(c)].ToJson(), s)) {
                    if (s.mode == SizeSpec::Mode::Px) cflags = ImGuiTableColumnFlags_WidthFixed, w = s.value * ctx.S();
                    else if (s.mode == SizeSpec::Mode::Fill) cflags = ImGuiTableColumnFlags_WidthStretch, w = 1.0f;
                    else if (s.mode == SizeSpec::Mode::Percent) cflags = ImGuiTableColumnFlags_WidthStretch, w = s.value;
                }
            }
            ImGui::TableSetupColumn(label, cflags, w);
        }
        if (!headers.empty()) ImGui::TableHeadersRow();
    }
    ctx.DrawChildrenInCells();
    ImGui::EndTable();
    RectFromItem(ctx);
    return true;
}

inline bool TabBar(DrawCtx& ctx) {
    int flags = ctx.GetFlags("flags", "TabBarFlags");
    ctx.selected_child = -1;
    ImGui::BeginGroup();
    if (ImGui::BeginTabBar(ctx.node.id.c_str(), flags)) {
        ++ctx.interp.tab_bar_depth;
        ctx.DrawChildren();
        --ctx.interp.tab_bar_depth;
        ImGui::EndTabBar();
    }
    ImGui::EndGroup();
    RectFromItem(ctx);
    Interpreter::NodeRt& rt = ctx.interp.Rt(ctx.id);
    if (rt.seen && ctx.selected_child >= 0 && ctx.selected_child != rt.tab) ctx.FireEvent("onChange");
    if (ctx.selected_child >= 0) rt.tab = ctx.selected_child;
    rt.seen = true;
    return true;
}

inline bool Tab(DrawCtx& ctx) {
    const bool custom_host = ctx.parent && ctx.interp.FindWidgetEnd(ctx.parent->node.type) != nullptr;
    if (ctx.interp.tab_bar_depth <= 0 && !custom_host) {
        ctx.Diag(Diagnostic::Level::Error, "Tab must be a child of TabBar", "type");
        return Placeholder(ctx, "<Tab outside TabBar>", IM_COL32(255, 80, 80, 255));
    }
    int flags = ctx.GetFlags("flags", "TabItemFlags");
    std::string label = StableLabel(ctx, "label");
    if (ctx.parent) {
        for (size_t i = 0; i < ctx.inst.pending_tab.size(); ++i) {
            const auto& pt = ctx.inst.pending_tab[i];
            if (pt.first == ctx.parent->node.id && pt.second == ctx.node.id) {
                flags |= ImGuiTabItemFlags_SetSelected;
                ctx.inst.pending_tab.erase(ctx.inst.pending_tab.begin() + static_cast<std::ptrdiff_t>(i));
                break;
            }
        }
    }
    Interpreter::NodeRt& rt = ctx.interp.Rt(ctx.id);
    const bool closable = ctx.GetBool("closable", false);
    bool* open = nullptr;
    if (ctx.HasBinding("open")) open = ctx.BindBool("open");
    else if (closable) {
        if (!rt.seen) rt.open = true;
        open = &rt.open;
    }
    bool selected = false;
    if (!open || *open) {
        selected = ImGui::BeginTabItem(label.c_str(), closable ? open : nullptr, flags);
        RectFromItem(ctx);
        if (selected) {
            if (ctx.parent) ctx.parent->selected_child = ctx.sibling_index;
            ctx.DrawChildren();
            ImGui::EndTabItem();
        }
    } else {
        ctx.has_rect = false;
    }
    if (rt.seen && selected != rt.shown) ctx.FireEvent(selected ? "onOpen" : "onClose");
    rt.shown = selected;
    rt.seen = true;
    return true;
}

inline void ForceOpen(DrawCtx& ctx) {
    bool forced = false;
    if (ctx.interp.ForcedTrigger(ctx.node, "open", forced)) ImGui::SetNextItemOpen(forced);
}

inline bool CollapsingHeader(DrawCtx& ctx) {
    int flags = ctx.GetFlags("flags", "TreeNodeFlags");
    if (ctx.GetBool("defaultOpen", false)) flags |= ImGuiTreeNodeFlags_DefaultOpen;
    ForceOpen(ctx);
    bool open = ImGui::CollapsingHeader(StableLabel(ctx, "label").c_str(), flags);
    RectFromItem(ctx);
    if (open) ctx.DrawChildren();
    OpenEvents(ctx, open);
    return true;
}

inline bool TreeNode(DrawCtx& ctx) {
    int flags = ctx.GetFlags("flags", "TreeNodeFlags");
    if (ctx.GetBool("defaultOpen", false)) flags |= ImGuiTreeNodeFlags_DefaultOpen;
    ForceOpen(ctx);
    bool open = ImGui::TreeNodeEx(StableLabel(ctx, "label").c_str(), flags);
    RectFromItem(ctx);
    if (open) {
        ctx.DrawChildren();
        if (!(flags & ImGuiTreeNodeFlags_NoTreePushOnOpen)) ImGui::TreePop();
    }
    OpenEvents(ctx, open);
    return true;
}

inline bool ConsumePendingPopup(DrawCtx& ctx) {
    for (size_t i = 0; i < ctx.inst.pending_popups.size(); ++i) {
        if (ctx.inst.pending_popups[i] == ctx.node.id) {
            ctx.inst.pending_popups.erase(ctx.inst.pending_popups.begin() + static_cast<std::ptrdiff_t>(i));
            return true;
        }
    }
    return false;
}

inline bool Popup(DrawCtx& ctx) {
    int flags = ctx.GetFlags("flags", "WindowFlags");
    if (ConsumePendingPopup(ctx)) ImGui::OpenPopup(ctx.node.id.c_str());
    ctx.has_rect = false;
    bool open = ImGui::BeginPopup(ctx.node.id.c_str(), flags);
    if (open) {
        RectFromWindow(ctx);
        ctx.DrawChildren();
        ImGui::EndPopup();
    }
    OpenEvents(ctx, open);
    return true;
}

inline bool Modal(DrawCtx& ctx) {
    int flags = ctx.GetFlags("flags", "WindowFlags");
    std::string title = StableLabel(ctx, "title");
    Interpreter::NodeRt& rt = ctx.interp.Rt(ctx.id);
    const bool closable = ctx.GetBool("closable", false);
    bool* open = nullptr;
    if (ctx.HasBinding("open")) open = ctx.BindBool("open");
    else if (closable) {
        rt.open = true;
        open = &rt.open;
    }
    const bool bound = open && open != &rt.open;
    if (ConsumePendingPopup(ctx) && !ImGui::IsPopupOpen(title.c_str())) ImGui::OpenPopup(title.c_str());
    if (bound) {
        const bool popup = ImGui::IsPopupOpen(title.c_str());
        if (popup && !rt.shown) *open = true;
        else if (!popup && rt.shown) *open = false;
        else if (!popup && *open) ImGui::OpenPopup(title.c_str());
    }
    ctx.has_rect = false;
    const bool shown = ImGui::BeginPopupModal(title.c_str(), closable ? open : nullptr, flags);
    if (shown) {
        if (bound && !*open) {
            ImGui::CloseCurrentPopup();
        } else {
            RectFromWindow(ctx);
            ctx.DrawChildren();
        }
        ImGui::EndPopup();
        if (bound) open = ctx.BindBool("open");
    }
    const bool now = ImGui::IsPopupOpen(title.c_str());
    if (bound && open && shown && !now) *open = false;
    if (rt.seen && now != rt.shown) ctx.FireEvent(now ? "onOpen" : "onClose");
    rt.shown = now;
    rt.seen = true;
    return true;
}

inline bool MenuBar(DrawCtx& ctx) {
    if (ImGui::BeginMenuBar()) {
        ctx.has_rect = true;
        ctx.rect_min = ImGui::GetCursorScreenPos();
        ctx.DrawChildren();
        ctx.rect_max = ImVec2(ctx.rect_min.x + ImGui::GetWindowWidth(), ctx.rect_min.y + ImGui::GetFrameHeight());
        ImGui::EndMenuBar();
        return true;
    }
    ImVec2 p = ImGui::GetCursorScreenPos();
    float w = ImGui::GetContentRegionAvail().x, h = ImGui::GetFrameHeight();
    ImGui::GetWindowDrawList()->AddRectFilled(p, ImVec2(p.x + w, p.y + h), ImGui::GetColorU32(ImGuiCol_MenuBarBg));
    ImGui::BeginGroup();
    const int n = static_cast<int>(ctx.node.children.size());
    bool any_drawn = false;
    for (int i = 0; i < n; ++i) {
        if (any_drawn) ImGui::SameLine();
        const ImVec2 before = ImGui::GetCursorPos();
        ctx.next_sibling = i;
        ctx.interp.DrawNode(ctx.node.children[static_cast<size_t>(i)], ctx);
        const ImVec2 after = ImGui::GetCursorPos();
        if (after.x != before.x || after.y != before.y) any_drawn = true;
    }
    if (!any_drawn) ImGui::Dummy(ImVec2(w, h));
    ImGui::EndGroup();
    RectFromItem(ctx);
    return true;
}

inline bool Menu(DrawCtx& ctx) {
    std::string label = StableLabel(ctx, "label");
    bool open = ImGui::BeginMenu(label.c_str(), ctx.GetBool("enabled", true));
    RectFromItem(ctx);
    if (open) {
        ctx.DrawChildren();
        ImGui::EndMenu();
    }
    OpenEvents(ctx, open);
    return true;
}

inline bool Canvas(DrawCtx& ctx) {
    ImVec2 size = ctx.GetSize();
    BeginChildCompat(ctx.node.id.c_str(), size, ctx.GetBool("border", false), 0, 0);
    const ImVec2 cmin = ImGui::GetCursorScreenPos();
    const ImVec2 csize = ImGui::GetContentRegionAvail();
    const float S = ctx.S();
    const int n = static_cast<int>(ctx.node.children.size());
    bool pending_set_pos = false;
    for (int i = 0; i < n; ++i) {
        const Node& child = ctx.node.children[static_cast<size_t>(i)];
        DrawCtx tmp(ctx, child);
        SizeSpec w, h;
        tmp.GetSizeSpec("size", w, h);
        ImVec2 csz(0, 0);
        if (w.mode == SizeSpec::Mode::Px) csz.x = w.value * S;
        else if (w.mode == SizeSpec::Mode::Percent) csz.x = csize.x * w.value / 100.0f;
        if (h.mode == SizeSpec::Mode::Px) csz.y = h.value * S;
        else if (h.mode == SizeSpec::Mode::Percent) csz.y = csize.y * h.value / 100.0f;
        ImVec2 frac = interp_detail::AnchorFraction(child.layout.anchor);
        ImVec2 pos(cmin.x + frac.x * csize.x + child.layout.offset.x * S - frac.x * csz.x,
                   cmin.y + frac.y * csize.y + child.layout.offset.y * S - frac.y * csz.y);
        ImGui::SetCursorScreenPos(pos);
        ctx.next_sibling = i;
        ctx.interp.DrawNode(child, ctx);
        const ImVec2 after = ImGui::GetCursorScreenPos();
        pending_set_pos = after.x == pos.x && after.y == pos.y;
    }
    if (pending_set_pos) ImGui::Dummy(ImVec2(0, 0));
    ImGui::EndChild();
    RectFromItem(ctx);
    return true;
}

inline void FakeTitleBar(DrawCtx& ctx, const std::string& title) {
    ImVec2 p = ImGui::GetCursorScreenPos();
    float w = ImGui::GetContentRegionAvail().x, h = ImGui::GetFrameHeight();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), ImGui::GetColorU32(ImGuiCol_TitleBgActive));
    ImVec2 pad = ImGui::GetStyle().FramePadding;
    dl->AddText(ImVec2(p.x + pad.x, p.y + pad.y), ImGui::GetColorU32(ImGuiCol_Text), title.c_str());
    ImGui::Dummy(ImVec2(w, h));
    (void)ctx;
}

inline bool WindowNode(DrawCtx& ctx) {
    int flags = ctx.GetFlags("flags", "WindowFlags");
    if (ctx.GetBool("menuBar", false)) flags |= ImGuiWindowFlags_MenuBar;
    std::string title = ctx.GetString("title", "Window");
    if (ctx.opts.design_mode) {
        if (!(flags & ImGuiWindowFlags_NoTitleBar)) FakeTitleBar(ctx, title);
        ctx.DrawChildren();
        RectFromWindow(ctx);
        return true;
    }
    const float S = ctx.S();
    ImGuiCond cond = ImGuiCond_FirstUseEver;
    switch (ctx.GetEnum("sizeCond", "Always|Once|FirstUseEver|Appearing", 2)) {
    case 0: cond = ImGuiCond_Always; break;
    case 1: cond = ImGuiCond_Once; break;
    case 3: cond = ImGuiCond_Appearing; break;
    default: break;
    }
    if (ctx.HasProp("initialSize") || ctx.interp.PropDefault(ctx.node.type, "initialSize")) {
        ImVec2 sz = ctx.GetVec2("initialSize", ImVec2(0, 0));
        if (sz.x > 0 && sz.y > 0) ImGui::SetNextWindowSize(ImVec2(sz.x * S, sz.y * S), cond);
    }
    if (ctx.HasProp("initialPos")) {
        Value v;
        if (ctx.GetValue("initialPos", v) && (v.IsVec2() || v.IsList())) {
            Vec2 p = v.ToVec2();
            ImGui::SetNextWindowPos(ImVec2(p.x * S, p.y * S), cond);
        }
    }
    bool* open = ctx.HasBinding("open") ? ctx.BindBool("open") : nullptr;
    Interpreter::NodeRt& rt = ctx.interp.Rt(ctx.id);
    bool shown = false;
    bool is_open = !open || *open;
    if (is_open) {
        // top level window ids ignore the id stack, so the panel name keeps "window" roots apart
        std::string label = title + "###" + ctx.panel.name + "/" + ctx.node.id;
        shown = ImGui::Begin(label.c_str(), open, flags);
        RectFromWindow(ctx);
        if (shown) ctx.DrawChildren();
        ImGui::End();
        if (open) open = ctx.BindBool("open");
        is_open = !open || *open;
    } else {
        ctx.has_rect = false;
    }
    if (rt.seen && is_open != rt.open) ctx.FireEvent(is_open ? "onOpen" : "onClose");
    rt.open = is_open;
    rt.seen = true;
    return true;
}

inline bool Placeholder(DrawCtx& ctx, const char* text, ImU32 color) {
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImVec2 ts = ImGui::CalcTextSize(text);
    ImVec2 pad = ImGui::GetStyle().FramePadding;
    ImVec2 sz(ts.x + pad.x * 2, ts.y + pad.y * 2);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, ImVec2(p.x + sz.x, p.y + sz.y), ImGui::GetColorU32(ImGuiCol_FrameBg));
    dl->AddRect(p, ImVec2(p.x + sz.x, p.y + sz.y), color);
    dl->AddText(ImVec2(p.x + pad.x, p.y + pad.y), color, text);
    ImGui::Dummy(sz);
    RectFromItem(ctx);
    return true;
}

inline bool ComponentInstance(DrawCtx& ctx) {
    const imforge::Component* def = ctx.interp.FindComponent(ctx.node.component);
    if (!def) {
        ctx.Diag(Diagnostic::Level::Error, "unknown component '" + ctx.node.component + "'", "component");
        return Placeholder(ctx, ("<" + ctx.node.component + ">").c_str(), IM_COL32(255, 80, 80, 255));
    }
    for (const DrawCtx* p = ctx.outer; p; p = p->outer) {
        if (p->component == def) {
            ctx.Diag(Diagnostic::Level::Error, "recursive component '" + def->name + "'", "component");
            return Placeholder(ctx, "<recursion>", IM_COL32(255, 80, 80, 255));
        }
    }
    DrawCtx c(ctx, def->root);
    c.component = def;
    c.instance_node = &ctx.node;
    c.outer = &ctx;
    c.fill_width = ctx.fill_width;
    c.stagger_delay = ctx.stagger_delay;
    ImGui::BeginGroup();
    ctx.interp.DrawNodeImpl(c);
    ImGui::EndGroup();
    RectFromItem(ctx);
    if (c.has_rect) {
        ctx.rect_min = c.rect_min;
        ctx.rect_max = c.rect_max;
    }
    return true;
}

inline bool SlotNode(DrawCtx& ctx) {
    std::string name = ctx.GetString("name", "content");
    ctx.has_rect = false;
    if (!ctx.instance_node || !ctx.outer) {
        if (ctx.Design()) return Placeholder(ctx, ("[slot " + name + "]").c_str(), ImGui::GetColorU32(ImGuiCol_TextDisabled));
        return true;
    }
    for (const auto& s : ctx.instance_node->slots) {
        if (s.first != name) continue;
        DrawCtx& host = *ctx.outer;
        for (size_t i = 0; i < s.second.size(); ++i) {
            host.next_sibling = static_cast<int>(i);
            host.child_fill_width = -1.0f;
            host.child_stagger = 0.0f;
            ctx.interp.DrawNode(s.second[i], host);
        }
        return true;
    }
    return true;
}

inline bool CodeNode(DrawCtx& ctx) {
    if (BlockFn fn = ctx.interp.FindBlock(ctx.panel.name + "." + ctx.node.id)) {
        ImGui::BeginGroup();
        fn(ctx);
        ImGui::EndGroup();
        RectFromItem(ctx);
        return true;
    }
    ctx.has_rect = false;
    if (!ctx.opts.design_mode) return true;
    return Placeholder(ctx, ("</> " + ctx.node.id).c_str(), ImGui::GetColorU32(ImGuiCol_TextDisabled));
}

}  // namespace interp_layout

inline bool IsContainerType(const std::string& t) {
    static const char* const kTypes[] = {"Window", "VStack", "HStack", "Group", "Child", "Grid", "TabBar", "Tab",
                                         "CollapsingHeader", "TreeNode", "Popup", "Modal", "MenuBar", "Menu",
                                         "Canvas", "Component", "Slot", "Code"};
    for (const char* k : kTypes)
        if (t == k) return true;
    return false;
}

inline bool DrawContainer(DrawCtx& ctx) {
    using namespace interp_layout;
    const std::string& t = ctx.node.type;
    if (t == "VStack") return VStack(ctx);
    if (t == "HStack") return HStack(ctx);
    if (t == "Group") return Group(ctx);
    if (t == "Child") return Child(ctx);
    if (t == "Grid") return Grid(ctx);
    if (t == "TabBar") return TabBar(ctx);
    if (t == "Tab") return Tab(ctx);
    if (t == "CollapsingHeader") return CollapsingHeader(ctx);
    if (t == "TreeNode") return TreeNode(ctx);
    if (t == "Popup") return Popup(ctx);
    if (t == "Modal") return Modal(ctx);
    if (t == "MenuBar") return MenuBar(ctx);
    if (t == "Menu") return Menu(ctx);
    if (t == "Canvas") return Canvas(ctx);
    if (t == "Window") return WindowNode(ctx);
    if (t == "Component") return ComponentInstance(ctx);
    if (t == "Slot") return SlotNode(ctx);
    if (t == "Code") return CodeNode(ctx);
    return false;
}

inline void DrawCtx::DrawChildrenInCells() {
    const int n = static_cast<int>(node.children.size());
    for (int i = 0; i < n; ++i) {
        ImGui::TableNextColumn();
        next_sibling = i;
        child_fill_width = -1.0f;
        child_stagger = node.anim.stagger > 0 ? node.anim.stagger * static_cast<float>(i) : 0.0f;
        interp.DrawNode(node.children[static_cast<size_t>(i)], *this);
    }
}

}  // namespace imforge

#endif
