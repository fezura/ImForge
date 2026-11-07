#include "tree_ops.h"

#include <algorithm>
#include <cstring>
#include <map>
#include <set>

namespace imforge::builder::tree {

namespace {

const WidgetDef* Def(const WidgetCatalog* catalog, const std::string& type) {
    return catalog ? catalog->Find(type.c_str()) : FindWidget(type.c_str());
}

std::string FreshId(const std::string& type, const std::set<std::string>& used) {
    std::string base;
    for (char c : type) {
        bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
        base.push_back(ok ? c : '_');
    }
    if (base.empty() || (base[0] >= '0' && base[0] <= '9')) base = "node" + base;
    if (base[0] >= 'A' && base[0] <= 'Z') base[0] = (char)(base[0] - 'A' + 'a');
    for (int n = 1;; ++n) {
        std::string cand = base + "_" + std::to_string(n);
        if (!used.count(cand)) return cand;
    }
}

bool SameList(const std::vector<NodeLocation>& locs) {
    for (const NodeLocation& l : locs)
        if (l.list != locs.front().list) return false;
    return true;
}

bool LocateAll(Node& root, const std::vector<std::string>& ids, std::vector<NodeLocation>& out) {
    out.clear();
    for (const std::string& id : ids) {
        NodeLocation loc;
        if (id == root.id || !LocateNode(root, id, loc)) return false;
        out.push_back(loc);
    }
    return !out.empty();
}

std::vector<Node> ExtractSorted(std::vector<NodeLocation>& locs, size_t& min_index) {
    std::sort(locs.begin(), locs.end(), [](const NodeLocation& a, const NodeLocation& b) { return a.index < b.index; });
    min_index = locs.front().index;
    std::vector<Node> moved;
    for (const NodeLocation& l : locs) moved.push_back(std::move((*l.list)[l.index]));
    for (size_t i = locs.size(); i-- > 0;) locs[i].list->erase(locs[i].list->begin() + (long)locs[i].index);
    return moved;
}

bool PathRec(const Node& n, const std::string& id, std::vector<std::string>& path) {
    path.push_back(n.id);
    if (n.id == id) return true;
    for (const Node& c : n.children)
        if (PathRec(c, id, path)) return true;
    for (const auto& s : n.slots)
        for (const Node& c : s.second)
            if (PathRec(c, id, path)) return true;
    path.pop_back();
    return false;
}

std::vector<const Node*> UnwrapCandidates(const Node& n) {
    std::vector<const Node*> out;
    for (const Node& c : n.children) out.push_back(&c);
    if (out.empty())
        for (const auto& s : n.slots)
            for (const Node& c : s.second) out.push_back(&c);
    return out;
}

}  // namespace

bool LocateNode(Node& root, const std::string& id, NodeLocation& out) {
    size_t index = 0;
    Node* parent = FindParent(root, id, &index);
    if (!parent) return false;
    if (index < parent->children.size() && parent->children[index].id == id) {
        out = {parent, &parent->children, index};
        return true;
    }
    for (auto& s : parent->slots) {
        if (index < s.second.size() && s.second[index].id == id) {
            out = {parent, &s.second, index};
            return true;
        }
    }
    return false;
}

bool IsAncestor(const Node& root, const std::string& ancestor_id, const std::string& id) {
    if (ancestor_id == id) return false;
    const Node* a = FindNode(root, ancestor_id);
    return a && FindNode(*a, id) != nullptr;
}

bool AcceptsChild(const WidgetCatalog* catalog, const Node& parent, const std::string& child_type) {
    if (parent.type == "Component" || child_type.empty()) return false;
    const WidgetDef* pdef = Def(catalog, parent.type);
    if (!pdef) return child_type != "Window";
    return WidgetAcceptsChild(*pdef, child_type.c_str());
}

bool CanReparent(const Node& root, const std::string& id, const std::string& new_parent_id,
                 const WidgetCatalog* catalog) {
    if (id.empty() || id == root.id) return false;
    const Node* node = FindNode(root, id);
    const Node* parent = new_parent_id.empty() ? &root : FindNode(root, new_parent_id);
    if (!node || !parent || node == parent) return false;
    if (FindNode(*node, parent->id)) return false;
    return AcceptsChild(catalog, *parent, node->type);
}

Node* InsertChild(Node& parent, Node child, int index) {
    size_t at = (index < 0 || (size_t)index > parent.children.size()) ? parent.children.size() : (size_t)index;
    parent.children.insert(parent.children.begin() + (long)at, std::move(child));
    return &parent.children[at];
}

bool MoveNode(Node& root, const std::string& id, const std::string& new_parent_id, int index) {
    if (id == root.id) return false;
    Node* node = FindNode(root, id);
    Node* new_parent = new_parent_id.empty() ? &root : FindNode(root, new_parent_id);
    if (!node || !new_parent || node == new_parent || FindNode(*node, new_parent->id)) return false;
    NodeLocation loc;
    if (!LocateNode(root, id, loc)) return false;
    Node moved = std::move((*loc.list)[loc.index]);
    bool same_list = loc.list == &new_parent->children;
    loc.list->erase(loc.list->begin() + (long)loc.index);
    size_t at = index < 0 ? new_parent->children.size() : (size_t)index;
    if (same_list && index >= 0 && (size_t)index > loc.index) --at;
    if (at > new_parent->children.size()) at = new_parent->children.size();
    new_parent->children.insert(new_parent->children.begin() + (long)at, std::move(moved));
    return true;
}

bool MoveNodes(Node& root, const std::vector<std::string>& ids, const std::string& new_parent_id,
               const std::string& anchor_id, bool after) {
    std::vector<std::string> top = TopLevelIds(root, ids);
    if (top.empty()) return false;
    const std::string parent_id = new_parent_id.empty() ? root.id : new_parent_id;
    for (const std::string& id : top) {
        if (id == root.id || id == anchor_id) return false;
        const Node* n = FindNode(root, id);
        if (!n || n->id == parent_id || FindNode(*n, parent_id)) return false;
        if (!anchor_id.empty() && FindNode(*n, anchor_id)) return false;
    }
    std::vector<Node> moved;
    for (const std::string& id : top) {
        Node n;
        if (!RemoveNode(root, id, &n)) return false;
        moved.push_back(std::move(n));
    }
    Node* parent = FindNode(root, parent_id);
    if (!parent) return false;
    size_t at = parent->children.size();
    if (!anchor_id.empty()) {
        for (size_t i = 0; i < parent->children.size(); ++i) {
            if (parent->children[i].id == anchor_id) {
                at = after ? i + 1 : i;
                break;
            }
        }
    }
    for (Node& n : moved) {
        parent->children.insert(parent->children.begin() + (long)at, std::move(n));
        ++at;
    }
    return true;
}

bool MoveSibling(Node& root, const std::string& id, int delta) {
    NodeLocation loc;
    if (delta == 0 || id == root.id || !LocateNode(root, id, loc)) return false;
    long target = (long)loc.index + delta;
    if (target < 0 || target >= (long)loc.list->size()) return false;
    Node moved = std::move((*loc.list)[loc.index]);
    loc.list->erase(loc.list->begin() + (long)loc.index);
    loc.list->insert(loc.list->begin() + target, std::move(moved));
    return true;
}

bool RemoveNode(Node& root, const std::string& id, Node* out) {
    NodeLocation loc;
    if (id == root.id || !LocateNode(root, id, loc)) return false;
    if (out) *out = std::move((*loc.list)[loc.index]);
    loc.list->erase(loc.list->begin() + (long)loc.index);
    return true;
}

Node CloneWithFreshIds(const Node& src, const Node& root) {
    std::set<std::string> used;
    Walk(root, [&](const Node& n) { used.insert(n.id); });
    Node clone = src;
    std::map<std::string, std::string> remap;
    Walk(clone, [&](Node& n) {
        std::string fresh = FreshId(n.type == "Component" && !n.component.empty() ? n.component : n.type, used);
        used.insert(fresh);
        remap[n.id] = fresh;
        n.id = fresh;
    });
    Walk(clone, [&](Node& n) {
        for (auto& ev : n.events) {
            for (Action& a : ev.second.actions) {
                if (a.type != "openPopup" && a.type != "setTab") continue;
                auto t = remap.find(a.target);
                if (t != remap.end()) a.target = t->second;
                if (a.type == "setTab" && a.value.IsString()) {
                    auto v = remap.find(a.value.AsString());
                    if (v != remap.end()) a.value = Json(v->second);
                }
            }
        }
    });
    return clone;
}

bool CanWrap(const Node& root, const std::vector<std::string>& ids, const std::string& container_type,
             const WidgetCatalog* catalog) {
    std::vector<NodeLocation> locs;
    if (!LocateAll(const_cast<Node&>(root), ids, locs) || !SameList(locs)) return false;
    if (!AcceptsChild(catalog, *locs.front().parent, container_type)) return false;
    Node container;
    container.type = container_type == "TabBar" ? "Tab" : container_type;
    for (const NodeLocation& l : locs)
        if (!AcceptsChild(catalog, container, (*l.list)[l.index].type)) return false;
    return true;
}

bool WrapNode(Node& root, const std::string& id, const std::string& container_type) {
    return WrapNodes(root, std::vector<std::string>{id}, container_type, nullptr);
}

bool WrapNodes(Node& root, const std::vector<std::string>& ids, const std::string& container_type,
               std::string* wrapper_id_out) {
    std::vector<NodeLocation> locs;
    if (container_type.empty() || !LocateAll(root, ids, locs) || !SameList(locs)) return false;
    Node wrapper = MakeDefaultNode(container_type, root, nullptr);
    Node* host = &wrapper;
    if (container_type == "TabBar") {
        wrapper.children.push_back(MakeDefaultNode("Tab", root, nullptr));
        host = &wrapper.children.back();
    }
    std::vector<Node>* list = locs.front().list;
    size_t min_index = 0;
    std::vector<Node> moved = ExtractSorted(locs, min_index);
    for (Node& n : moved) host->children.push_back(std::move(n));
    list->insert(list->begin() + (long)min_index, std::move(wrapper));
    if (wrapper_id_out) *wrapper_id_out = (*list)[min_index].id;
    return true;
}

bool CanUnwrap(const Node& root, const std::string& id, const WidgetCatalog* catalog) {
    NodeLocation loc;
    if (id == root.id || !LocateNode(const_cast<Node&>(root), id, loc)) return false;
    const Node& node = (*loc.list)[loc.index];
    std::vector<const Node*> kids = UnwrapCandidates(node);
    if (kids.empty()) return false;
    for (const Node* k : kids)
        if (!AcceptsChild(catalog, *loc.parent, k->type)) return false;
    return true;
}

bool UnwrapNode(Node& root, const std::string& id, const WidgetCatalog* catalog) {
    NodeLocation loc;
    if (id == root.id || !LocateNode(root, id, loc)) return false;
    if (catalog && !CanUnwrap(root, id, catalog)) return false;
    Node node = std::move((*loc.list)[loc.index]);
    std::vector<Node> kids = std::move(node.children);
    if (kids.empty())
        for (auto& s : node.slots)
            for (Node& c : s.second) kids.push_back(std::move(c));
    if (kids.empty()) {
        (*loc.list)[loc.index] = std::move(node);
        return false;
    }
    loc.list->erase(loc.list->begin() + (long)loc.index);
    loc.list->insert(loc.list->begin() + (long)loc.index, std::make_move_iterator(kids.begin()),
                     std::make_move_iterator(kids.end()));
    return true;
}

Node MakeDefaultNode(const std::string& type, const Node& root, const WidgetCatalog* catalog) {
    Node n;
    n.type = type;
    n.id = GenerateId(root, type);
    const WidgetDef* def = Def(catalog, type);
    if (!def) return n;
    for (int i = 0; i < def->prop_count; ++i) {
        const PropDef& p = def->props[i];
        const bool text_like = p.type == PropType::String &&
                               (!std::strcmp(p.name, "label") || !std::strcmp(p.name, "text") ||
                                !std::strcmp(p.name, "title") || !std::strcmp(p.name, "desc") ||
                                !std::strcmp(p.name, "name"));
        const bool items = p.type == PropType::StringList && !std::strcmp(p.name, "items");
        const bool columns = p.type == PropType::Int && !std::strcmp(p.name, "columns");
        if (!text_like && !items && !columns) continue;
        Json v = PropDefaultJson(p);
        if (text_like && !v.IsString()) v = Json(type);
        if (items && v.Size() == 0) {
            v = Json::Array();
            v.Push(Json("Item 1")).Push(Json("Item 2")).Push(Json("Item 3"));
        }
        if (columns && !v.IsNumber()) v = Json(2);
        n.SetProp(p.name, std::move(v));
    }
    return n;
}

Node MakeComponentInstance(const Component& def, const Node& root) {
    Node n;
    n.type = "Component";
    n.component = def.name;
    n.id = GenerateId(root, def.name);
    for (const Param& p : def.params) {
        if (p.type.compare(0, 8, "binding<") == 0 || p.def.IsNull()) continue;
        n.SetProp(p.name.c_str(), p.def);
    }
    for (const std::string& s : def.slots) n.slots.emplace_back(s, std::vector<Node>());
    return n;
}

std::vector<std::string> PathToNode(const Node& root, const std::string& id) {
    std::vector<std::string> path;
    if (!PathRec(root, id, path)) path.clear();
    return path;
}

std::vector<std::string> TopLevelIds(const Node& root, const std::vector<std::string>& ids) {
    std::vector<std::string> out;
    for (const std::string& id : ids) {
        if (!FindNode(root, id)) continue;
        bool nested = false;
        for (const std::string& other : ids)
            if (other != id && IsAncestor(root, other, id)) nested = true;
        if (!nested && std::find(out.begin(), out.end(), id) == out.end()) out.push_back(id);
    }
    return out;
}

bool FindInsertPoint(Node& root, const std::string& selected_id, const std::string& type,
                     const WidgetCatalog* catalog, std::string& parent_id_out, int& index_out) {
    Node* sel = selected_id.empty() ? nullptr : FindNode(root, selected_id);
    if (!sel) sel = &root;
    if (AcceptsChild(catalog, *sel, type)) {
        parent_id_out = sel->id;
        index_out = -1;
        return true;
    }
    Node* cur = sel;
    while (cur != &root) {
        NodeLocation loc;
        if (!LocateNode(root, cur->id, loc)) return false;
        if (loc.list == &loc.parent->children && AcceptsChild(catalog, *loc.parent, type)) {
            parent_id_out = loc.parent->id;
            index_out = (int)loc.index + 1;
            return true;
        }
        cur = loc.parent;
    }
    return false;
}

}  // namespace imforge::builder::tree
