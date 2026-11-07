#include "tree_actions.h"

#include <algorithm>
#include <cstring>
#include <type_traits>
#include <utility>

#include "../builder_fonts.h"
#include "editor_flags.h"
#include "tree_ops.h"
#include "ui_helpers.h"
#include "widget_descriptions.h"

#if __has_include("icon_table.h")
#include "icon_table.h"
#define IMFORGE_TREE_HAS_ICON_TABLE 1
#else
#define IMFORGE_TREE_HAS_ICON_TABLE 0
#endif

namespace imforge::builder::tree_actions {

namespace {

template <class Ctx>
auto RequestViaHook(Ctx& ctx, const std::vector<std::string>& ids, int)
    -> decltype(ctx.RequestCreateComponent(ids), bool()) {
    ctx.RequestCreateComponent(ids);
    return true;
}
template <class Ctx>
bool RequestViaHook(Ctx&, const std::vector<std::string>&, long) {
    return false;
}

std::string PascalCase(const std::string& s) {
    std::string out;
    bool upper = true;
    for (char c : s) {
        bool alnum = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
        if (!alnum) {
            upper = true;
            continue;
        }
        if (upper && c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
        out.push_back(c);
        upper = false;
    }
    if (out.empty() || (out[0] >= '0' && out[0] <= '9')) out = "Component" + out;
    return out;
}

bool AllInSameList(Node& root, const std::vector<std::string>& ids, std::vector<Node>** list_out, size_t* min_index) {
    std::vector<Node>* list = nullptr;
    size_t lo = (size_t)-1;
    for (const std::string& id : ids) {
        tree::NodeLocation loc;
        if (!tree::LocateNode(root, id, loc)) return false;
        if (list && loc.list != list) return false;
        list = loc.list;
        lo = std::min(lo, loc.index);
    }
    if (!list) return false;
    if (list_out) *list_out = list;
    if (min_index) *min_index = lo;
    return true;
}

template <class Ctx>
bool ConvertFallback(Ctx& ctx, const std::vector<std::string>& ids_in, const std::string& name, std::string* err) {
    auto fail = [&](const char* m) {
        if (err) *err = m;
        return false;
    };
    Node* root = ctx.ActiveRoot();
    if (!root) return fail("no document open");
    std::vector<std::string> ids = tree::TopLevelIds(*root, ids_in);
    if (ids.empty()) return fail("nothing selected");
    for (const std::string& id : ids)
        if (id == root->id) return fail("the root node cannot become a component");
    if (!core_detail::IsIdentifier(name)) return fail("component name must be an identifier");
    if (ctx.project.FindComponent(name)) return fail("a component with this name already exists");
    std::vector<Node>* list = nullptr;
    size_t min_index = 0;
    if (!AllInSameList(*root, ids, &list, &min_index)) return fail("select sibling nodes");

    Component comp;
    comp.name = name;
    if (ids.size() == 1) {
        comp.root = *FindNode(*root, ids[0]);
    } else {
        Node wrap;
        wrap.type = "VStack";
        for (const Node& n : *list)
            if (std::find(ids.begin(), ids.end(), n.id) != ids.end()) wrap.children.push_back(n);
        wrap.id = GenerateId(wrap, "VStack");
        comp.root = std::move(wrap);
    }
    std::vector<std::string> doc_panels;
    if (ctx.active_kind == DocKind::Panel) doc_panels.push_back(ctx.active_panel);
    ctx.BeginComponentsEdit("Convert to component", doc_panels);
    for (const std::string& id : ids) tree::RemoveNode(*root, id, nullptr);
    Node inst = tree::MakeComponentInstance(comp, *root);
    std::string inst_id = inst.id;
    if (min_index > list->size()) min_index = list->size();
    list->insert(list->begin() + (long)min_index, std::move(inst));
    ctx.project.components.push_back(std::move(comp));
    ctx.EndEdit();
    ctx.Select(inst_id);
    ctx.SetStatus("Created component " + name);
    return true;
}

}  // namespace

bool ParseNewPayload(const ImGuiPayload* payload, std::string& type_out, std::string& component_out) {
    if (!payload || !payload->Data || payload->DataSize <= 0) return false;
    std::string s((const char*)payload->Data, (size_t)payload->DataSize);
    while (!s.empty() && s.back() == '\0') s.pop_back();
    component_out.clear();
    size_t plen = std::strlen(kComponentPayloadPrefix);
    if (s.compare(0, plen, kComponentPayloadPrefix) == 0) {
        component_out = s.substr(plen);
        type_out = "Component";
        return !component_out.empty();
    }
    type_out = s;
    return !type_out.empty();
}

std::string NewPayloadFor(const std::string& type, const std::string& component) {
    return component.empty() ? type : std::string(kComponentPayloadPrefix) + component;
}

std::vector<std::string> DraggedIds(EditorContext& ctx, const std::string& dragged_id) {
    Node* root = ctx.ActiveRoot();
    if (!root) return {};
    if (ctx.IsSelected(dragged_id)) {
        std::vector<std::string> ids = tree::TopLevelIds(*root, ctx.selection);
        if (!ids.empty()) return ids;
    }
    return {dragged_id};
}

bool IsLockedOrInherited(const Node& root, const std::string& id) {
    for (const std::string& pid : tree::PathToNode(root, id)) {
        const Node* n = FindNode(root, pid);
        if (n && n->locked) return true;
    }
    return false;
}

bool CanDropNodes(EditorContext& ctx, const std::vector<std::string>& ids, const std::string& parent_id,
                  const std::string& anchor_id) {
    Node* root = ctx.ActiveRoot();
    if (!root || ids.empty()) return false;
    Node* parent = parent_id.empty() ? root : FindNode(*root, parent_id);
    if (!parent || IsLockedOrInherited(*root, parent->id)) return false;
    for (const std::string& id : ids) {
        if (id == anchor_id || IsLockedOrInherited(*root, id)) return false;
        if (!tree::CanReparent(*root, id, parent->id, &ctx.catalog)) return false;
        if (!anchor_id.empty() && tree::IsAncestor(*root, id, anchor_id)) return false;
    }
    return true;
}

bool CanDropNew(EditorContext& ctx, const std::string& type, const std::string& parent_id) {
    Node* root = ctx.ActiveRoot();
    if (!root) return false;
    Node* parent = parent_id.empty() ? root : FindNode(*root, parent_id);
    if (!parent || IsLockedOrInherited(*root, parent->id)) return false;
    return tree::AcceptsChild(&ctx.catalog, *parent, type);
}

Node* AddNode(EditorContext& ctx, const std::string& type, const std::string& component,
              const std::string& parent_id, int index, const char* undo_label) {
    Node* root = ctx.ActiveRoot();
    if (!root) return nullptr;
    Node* parent = parent_id.empty() ? root : FindNode(*root, parent_id);
    if (!parent) return nullptr;
    if (!tree::AcceptsChild(&ctx.catalog, *parent, type)) {
        ctx.SetStatus(parent->type + " cannot contain " + type);
        return nullptr;
    }
    if (IsLockedOrInherited(*root, parent->id)) {
        ctx.SetStatus(parent->id + " is locked");
        return nullptr;
    }
    Node n;
    if (type == "Component") {
        const Component* def = ctx.project.FindComponent(component);
        if (!def) {
            ctx.SetStatus("Unknown component: " + component);
            return nullptr;
        }
        if (Component* active = ctx.ActiveComponent(); active && active->name == component) {
            ctx.SetStatus("A component cannot contain itself");
            return nullptr;
        }
        n = tree::MakeComponentInstance(*def, *root);
    } else {
        n = tree::MakeDefaultNode(type, *root, &ctx.catalog);
    }
    std::string id = n.id;
    ctx.BeginEdit(undo_label);
    tree::InsertChild(*parent, std::move(n), index);
    ctx.EndEdit();
    ctx.Select(id);
    return ctx.FindActiveNode(id);
}

Node* AddAtSelection(EditorContext& ctx, const std::string& type, const std::string& component) {
    Node* root = ctx.ActiveRoot();
    if (!root) {
        ctx.SetStatus("Open a panel or component first");
        return nullptr;
    }
    std::string parent;
    int index = -1;
    if (!tree::FindInsertPoint(*root, ctx.SelectedId(), type, &ctx.catalog, parent, index)) {
        ctx.SetStatus("No place for " + type + " near the selection");
        return nullptr;
    }
    return AddNode(ctx, type, component, parent, index);
}

bool DropNodes(EditorContext& ctx, const std::vector<std::string>& ids, const std::string& parent_id,
               const std::string& anchor_id, bool after) {
    if (!CanDropNodes(ctx, ids, parent_id, anchor_id)) return false;
    Node* root = ctx.ActiveRoot();
    ctx.BeginEdit("Move");
    bool ok = tree::MoveNodes(*root, ids, parent_id.empty() ? root->id : parent_id, anchor_id, after);
    ctx.EndEdit();
    return ok;
}

bool DropNew(EditorContext& ctx, const std::string& type, const std::string& component,
             const std::string& parent_id, const std::string& anchor_id, bool after) {
    Node* root = ctx.ActiveRoot();
    if (!root) return false;
    Node* parent = parent_id.empty() ? root : FindNode(*root, parent_id);
    if (!parent) return false;
    int index = -1;
    for (size_t i = 0; i < parent->children.size() && !anchor_id.empty(); ++i) {
        if (parent->children[i].id == anchor_id) {
            index = (int)i + (after ? 1 : 0);
            break;
        }
    }
    return AddNode(ctx, type, component, parent->id, index) != nullptr;
}

bool WrapSelection(EditorContext& ctx, const std::string& container_type) {
    Node* root = ctx.ActiveRoot();
    if (!root) return false;
    std::vector<std::string> ids = tree::TopLevelIds(*root, ctx.selection);
    if (ids.empty()) return false;
    for (const std::string& id : ids)
        if (IsLockedOrInherited(*root, id)) return false;
    if (!tree::CanWrap(*root, ids, container_type, &ctx.catalog)) {
        ctx.SetStatus("Cannot wrap the selection in " + container_type);
        return false;
    }
    std::string wid;
    ctx.BeginEdit(("Wrap in " + container_type).c_str());
    bool ok = tree::WrapNodes(*root, ids, container_type, &wid);
    ctx.EndEdit();
    if (ok) ctx.Select(wid);
    return ok;
}

bool UnwrapNode(EditorContext& ctx, const std::string& id) {
    Node* root = ctx.ActiveRoot();
    if (!root || IsLockedOrInherited(*root, id)) return false;
    if (!tree::CanUnwrap(*root, id, &ctx.catalog)) {
        ctx.SetStatus("Cannot unwrap " + id + " here");
        return false;
    }
    std::vector<std::string> kids;
    if (Node* n = FindNode(*root, id))
        for (const Node& c : n->children) kids.push_back(c.id);
    ctx.BeginEdit("Unwrap");
    bool ok = tree::UnwrapNode(*root, id, &ctx.catalog);
    ctx.EndEdit();
    if (ok) {
        ctx.ClearSelection();
        for (const std::string& k : kids) ctx.Select(k, true);
    }
    return ok;
}

bool SetLocked(EditorContext& ctx, const std::vector<std::string>& ids, bool locked) {
    Node* root = ctx.ActiveRoot();
    if (!root || ids.empty()) return false;
    ctx.BeginEdit(locked ? "Lock" : "Unlock");
    for (const std::string& id : ids)
        if (Node* n = FindNode(*root, id)) n->locked = locked;
    ctx.EndEdit();
    return true;
}

bool MoveSelection(EditorContext& ctx, int delta) {
    Node* root = ctx.ActiveRoot();
    if (!root || delta == 0) return false;
    std::vector<std::string> ids = tree::TopLevelIds(*root, ctx.selection);
    if (ids.empty()) return false;
    std::vector<std::pair<size_t, std::string>> order;
    for (const std::string& id : ids) {
        tree::NodeLocation loc;
        if (!tree::LocateNode(*root, id, loc) || IsLockedOrInherited(*root, id)) return false;
        long target = (long)loc.index + delta;
        if (target < 0 || target >= (long)loc.list->size()) return false;
        order.emplace_back(loc.index, id);
    }
    std::sort(order.begin(), order.end());
    if (delta > 0) std::reverse(order.begin(), order.end());
    ctx.BeginEdit("Move");
    bool any = false;
    for (const auto& o : order) any = tree::MoveSibling(*root, o.second, delta) || any;
    ctx.EndEdit();
    return any;
}

bool ConvertToComponent(EditorContext& ctx, const std::vector<std::string>& ids, const std::string& name,
                        std::string* err) {
    if (RequestViaHook(ctx, ids, 0)) return true;
    return ConvertFallback(ctx, ids, name, err);
}

std::string SuggestComponentName(EditorContext& ctx, const std::vector<std::string>& ids) {
    std::string base = "NewComponent";
    if (Node* root = ctx.ActiveRoot(); root && !ids.empty())
        if (const Node* n = FindNode(*root, ids.front())) base = PascalCase(n->name.empty() ? n->id : n->name);
    std::string cand = base;
    for (int i = 2; ctx.project.FindComponent(cand); ++i) cand = base + std::to_string(i);
    return cand;
}

void DrawAddMenu(EditorContext& ctx, const std::string& parent_id, int index) {
    Node* root = ctx.ActiveRoot();
    if (!root) return;
    Node* parent = parent_id.empty() ? root : FindNode(*root, parent_id);
    if (!parent) return;
    const bool in_component = ctx.ActiveComponent() != nullptr;
    std::vector<const WidgetDef*> all = ctx.catalog.All();
    std::vector<std::string> categories;
    for (const WidgetDef* w : all)
        if (std::find(categories.begin(), categories.end(), w->category) == categories.end())
            categories.push_back(w->category);
    for (const std::string& cat : categories) {
        if (!ImGui::BeginMenu(cat.c_str())) continue;
        for (const WidgetDef* w : all) {
            if (cat != w->category || !std::strcmp(w->type, "Window") || !std::strcmp(w->type, "Component")) continue;
            if (!std::strcmp(w->type, "Slot") && !in_component) continue;
            bool ok = tree::AcceptsChild(&ctx.catalog, *parent, w->type);
            const char* icon = Icon(w->icon, nullptr);
            std::string label = icon ? std::string(icon) + "  " + w->type : std::string(w->type);
            if (ImGui::MenuItem(label.c_str(), nullptr, false, ok)) AddNode(ctx, w->type, "", parent_id, index);
            const char* desc = WidgetDescription(w->type);
            if (desc && *desc) ui::Tooltip(desc);
        }
        ImGui::EndMenu();
    }
    if (ctx.project.components.empty()) return;
    if (!ImGui::BeginMenu("Components")) return;
    bool accepts = tree::AcceptsChild(&ctx.catalog, *parent, "Component");
    Component* active = ctx.ActiveComponent();
    for (const Component& c : ctx.project.components) {
        bool ok = accepts && !(active && active->name == c.name);
        const char* icon = Icon("fa-cube", nullptr);
        std::string label = icon ? std::string(icon) + "  " + c.name : c.name;
        if (ImGui::MenuItem(label.c_str(), nullptr, false, ok)) AddNode(ctx, "Component", c.name, parent_id, index);
    }
    ImGui::EndMenu();
}

const char* Icon(const char* name, const char* fallback) {
#if IMFORGE_TREE_HAS_ICON_TABLE
    if (name && GetFonts().has_icons)
        if (const char* u = IconUtf8(name)) return u;
#else
    (void)name;
#endif
    return fallback;
}

const char* WidgetIcon(EditorContext& ctx, const Node& node) {
    if (node.type == "Component") return Icon("fa-cube", nullptr);
    const WidgetDef* def = ctx.catalog.Find(node.type.c_str());
    return Icon(def ? def->icon : "fa-circle-question", nullptr);
}

}  // namespace imforge::builder::tree_actions
