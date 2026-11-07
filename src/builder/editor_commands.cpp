#include <algorithm>
#include <cstring>

#include "editor_context.h"

namespace imforge::builder {

namespace {

struct Located {
    Node* parent;
    size_t index;
    std::vector<Node>* list;
};

bool Locate(Node& root, const std::string& id, Located& out) {
    size_t index = 0;
    Node* parent = FindParent(root, id, &index);
    if (!parent) return false;
    if (index < parent->children.size() && parent->children[index].id == id) {
        out = {parent, index, &parent->children};
        return true;
    }
    for (auto& s : parent->slots) {
        if (index < s.second.size() && s.second[index].id == id) {
            out = {parent, index, &s.second};
            return true;
        }
    }
    return false;
}

bool IsAncestorOf(const Node& maybe_ancestor, const std::string& id) {
    if (maybe_ancestor.id == id) return false;
    return FindNode(maybe_ancestor, id) != nullptr;
}

size_t CountId(const Node& root, const std::string& id) {
    size_t c = 0;
    Walk(root, [&](const Node& n) { c += n.id == id ? 1 : 0; });
    return c;
}

void ReassignIds(Node& root, Node& inserted) {
    Walk(inserted, [&](Node& n) {
        if (n.id.empty() || CountId(root, n.id) > 1) n.id = GenerateId(root, n.type);
    });
}

bool LockedPath(const Node& n, const std::string& id, bool locked) {
    locked = locked || n.locked;
    if (n.id == id) return locked;
    for (const Node& c : n.children)
        if (FindNode(c, id)) return LockedPath(c, id, locked);
    for (const auto& s : n.slots)
        for (const Node& c : s.second)
            if (FindNode(c, id)) return LockedPath(c, id, locked);
    return false;
}

std::vector<std::string> TopLevelSelection(Node& root, const std::vector<std::string>& selection, bool skip_locked = false) {
    std::vector<std::string> out;
    for (const std::string& id : selection) {
        Node* n = FindNode(root, id);
        if (!n || n == &root) continue;
        if (skip_locked && LockedPath(root, id, false)) continue;
        bool nested = false;
        for (const std::string& other : selection) {
            if (other == id) continue;
            Node* o = FindNode(root, other);
            if (o && o != &root && IsAncestorOf(*o, id)) nested = true;
        }
        if (!nested) out.push_back(id);
    }
    return out;
}

}  // namespace

bool EditorContext::CanAcceptChild(const Node& parent, const std::string& child_type) const {
    const WidgetDef* pdef = catalog.Find(parent.type.c_str());
    const WidgetDef* cdef = catalog.Find(child_type.c_str());
    if (parent.type == "Component") return false;
    if (pdef && !pdef->container) return false;
    if (pdef && pdef->child_types && *pdef->child_types) {
        std::string allowed = pdef->child_types;
        size_t start = 0;
        bool found = false;
        while (start <= allowed.size()) {
            size_t bar = allowed.find('|', start);
            std::string t = allowed.substr(start, bar == std::string::npos ? std::string::npos : bar - start);
            if (t == child_type) found = true;
            if (bar == std::string::npos) break;
            start = bar + 1;
        }
        if (!found) return false;
    }
    if (cdef && child_type == "Tab" && parent.type != "TabBar") return false;
    return true;
}

Node* EditorContext::InsertNode(const std::string& type, const std::string& parent_id, int index, const char* undo_label) {
    Node* root = ActiveRoot();
    if (!root) return nullptr;
    Node* parent = parent_id.empty() ? root : FindNode(*root, parent_id);
    if (!parent) return nullptr;
    if (!CanAcceptChild(*parent, type)) {
        Node* alt = parent;
        Located loc;
        if (Locate(*root, parent->id, loc) && CanAcceptChild(*loc.parent, type)) {
            alt = loc.parent;
            index = (int)loc.index + 1;
        } else {
            Log(LogLevel::Warning, "%s cannot contain %s", parent->type.c_str(), type.c_str());
            return nullptr;
        }
        parent = alt;
    }
    BeginEdit(undo_label);
    Node n;
    n.type = type;
    n.id = GenerateId(*root, type);
    if (const WidgetDef* def = catalog.Find(type.c_str())) {
        for (int i = 0; i < def->prop_count; ++i) {
            const PropDef& pd = def->props[i];
            if (std::strcmp(pd.name, "label") == 0 || std::strcmp(pd.name, "text") == 0 || std::strcmp(pd.name, "title") == 0)
                n.SetProp(pd.name, Json(type));
        }
    }
    size_t at = (index < 0 || (size_t)index > parent->children.size()) ? parent->children.size() : (size_t)index;
    parent->children.insert(parent->children.begin() + (long)at, std::move(n));
    std::string new_id = parent->children[at].id;
    EndEdit();
    Select(new_id);
    return FindActiveNode(new_id);
}

bool EditorContext::MoveNode(const std::string& id, const std::string& new_parent_id, int index) {
    Node* root = ActiveRoot();
    if (!root || id == root->id) return false;
    Node* node = FindNode(*root, id);
    Node* new_parent = new_parent_id.empty() ? root : FindNode(*root, new_parent_id);
    if (!node || !new_parent || IsAncestorOf(*node, new_parent->id) || node == new_parent) return false;
    if (!CanAcceptChild(*new_parent, node->type)) return false;
    Located loc;
    if (!Locate(*root, id, loc)) return false;
    BeginEdit("Move");
    Node moved = std::move((*loc.list)[loc.index]);
    bool same_list = loc.parent == new_parent && loc.list == &new_parent->children;
    loc.list->erase(loc.list->begin() + (long)loc.index);
    size_t at = (index < 0) ? new_parent->children.size() : (size_t)index;
    if (same_list && index >= 0 && (size_t)index > loc.index) --at;
    if (at > new_parent->children.size()) at = new_parent->children.size();
    new_parent->children.insert(new_parent->children.begin() + (long)at, std::move(moved));
    EndEdit();
    return true;
}

bool EditorContext::RenameNode(const std::string& id, const std::string& new_id) {
    Node* root = ActiveRoot();
    if (!root || new_id.empty() || id == new_id) return false;
    if (!core_detail::IsIdentifier(new_id)) {
        SetStatus("Invalid id: use [A-Za-z_][A-Za-z0-9_]*");
        return false;
    }
    if (FindNode(*root, new_id)) {
        SetStatus("Id already used: " + new_id);
        return false;
    }
    Node* n = FindNode(*root, id);
    if (!n) return false;
    BeginEdit("Rename");
    n->id = new_id;
    EndEdit();
    if (IsSelected(id)) {
        Deselect(id);
        Select(new_id, true);
    }
    return true;
}

bool EditorContext::DeleteSelection() {
    Node* root = ActiveRoot();
    if (!root) return false;
    std::vector<std::string> ids = TopLevelSelection(*root, selection, true);
    if (ids.empty()) {
        if (!selection.empty()) SetStatus("Selection is locked or contains the root");
        return false;
    }
    BeginEdit("Delete");
    std::string select_after;
    for (const std::string& id : ids) {
        Located loc;
        if (!Locate(*root, id, loc)) continue;
        if (loc.index > 0) select_after = (*loc.list)[loc.index - 1].id;
        else if (loc.index + 1 < loc.list->size()) select_after = (*loc.list)[loc.index + 1].id;
        else select_after = loc.parent->id;
        loc.list->erase(loc.list->begin() + (long)loc.index);
    }
    ClearSelection();
    EndEdit();
    if (!select_after.empty() && FindNode(*root, select_after)) Select(select_after);
    return true;
}

bool EditorContext::CopySelection() {
    Node* root = ActiveRoot();
    if (!root) return false;
    std::vector<std::string> ids = TopLevelSelection(*root, selection);
    if (ids.empty()) return false;
    Json arr = Json::Array();
    for (const std::string& id : ids)
        if (Node* n = FindNode(*root, id)) arr.Push(NodeToJson(*n));
    clipboard_ = arr;
    Json wrapper = Json::Object();
    wrapper.Set("kind", Json("imforge-nodes"));
    wrapper.Set("nodes", arr);
    ImGui::SetClipboardText(DumpJson(wrapper).c_str());
    SetStatus("Copied " + std::to_string(ids.size()) + (ids.size() == 1 ? " node" : " nodes"));
    return true;
}

bool EditorContext::CutSelection() {
    if (!CopySelection()) return false;
    Node* root = ActiveRoot();
    std::vector<std::string> ids = TopLevelSelection(*root, selection, true);
    if (ids.empty()) {
        SetStatus("Selection is locked");
        return false;
    }
    BeginEdit("Cut");
    for (const std::string& id : ids) {
        Located loc;
        if (Locate(*root, id, loc)) loc.list->erase(loc.list->begin() + (long)loc.index);
    }
    ClearSelection();
    EndEdit();
    return true;
}

bool EditorContext::CanPaste() const {
    if (clipboard_.IsArray() && clipboard_.Size() > 0) return true;
    const char* text = ImGui::GetClipboardText();
    return text && std::strstr(text, "imforge-nodes") != nullptr;
}

bool EditorContext::Paste() {
    Node* root = ActiveRoot();
    if (!root) return false;
    Json nodes = clipboard_;
    const char* text = ImGui::GetClipboardText();
    if (text && *text) {
        Json j;
        if (ParseJson(text, std::strlen(text), j) && j["kind"].AsString("") == "imforge-nodes" && j["nodes"].IsArray())
            nodes = j["nodes"];
    }
    if (!nodes.IsArray() || nodes.Size() == 0) return false;
    std::vector<Node> parsed;
    for (const Json& nj : nodes.arr) {
        Node n;
        Error err;
        if (NodeFromJson(nj, n, &err)) parsed.push_back(std::move(n));
        else Log(LogLevel::Warning, "paste: %s", err.message.c_str());
    }
    if (parsed.empty()) return false;
    Node* target_parent = root;
    size_t at = root->children.size();
    if (Node* sel = SelectedNode()) {
        Located loc{};
        bool located = sel != root && Locate(*root, sel->id, loc);
        if (CanAcceptChild(*sel, parsed.front().type)) {
            target_parent = sel;
            at = sel->children.size();
        } else if (located && loc.list == &loc.parent->children) {
            target_parent = loc.parent;
            at = loc.index + 1;
        }
    }
    BeginEdit("Paste");
    std::vector<std::string> new_ids;
    for (Node& n : parsed) {
        if (!CanAcceptChild(*target_parent, n.type)) {
            Log(LogLevel::Warning, "paste: %s cannot contain %s", target_parent->type.c_str(), n.type.c_str());
            continue;
        }
        if (at > target_parent->children.size()) at = target_parent->children.size();
        target_parent->children.insert(target_parent->children.begin() + (long)at, std::move(n));
        ReassignIds(*root, target_parent->children[at]);
        new_ids.push_back(target_parent->children[at].id);
        ++at;
    }
    EndEdit();
    ClearSelection();
    for (const std::string& id : new_ids) Select(id, true);
    return !new_ids.empty();
}

bool EditorContext::DuplicateSelection() {
    Node* root = ActiveRoot();
    if (!root) return false;
    std::vector<std::string> ids = TopLevelSelection(*root, selection);
    if (ids.empty()) return false;
    BeginEdit("Duplicate");
    std::vector<std::string> new_ids;
    for (const std::string& id : ids) {
        Located loc;
        if (!Locate(*root, id, loc)) continue;
        Node copy = (*loc.list)[loc.index];
        Walk(copy, [&](Node& n) { n.id.clear(); });
        loc.list->insert(loc.list->begin() + (long)loc.index + 1, std::move(copy));
        ReassignIds(*root, (*loc.list)[loc.index + 1]);
        new_ids.push_back((*loc.list)[loc.index + 1].id);
    }
    EndEdit();
    ClearSelection();
    for (const std::string& id : new_ids) Select(id, true);
    return !new_ids.empty();
}

}  // namespace imforge::builder
