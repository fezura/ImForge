#pragma once

#include <string>
#include <vector>

#include "imforge/imforge_core.h"

namespace imforge::builder::tree {

struct NodeLocation {
    Node* parent = nullptr;
    std::vector<Node>* list = nullptr;
    size_t index = 0;
};

bool LocateNode(Node& root, const std::string& id, NodeLocation& out);
bool IsAncestor(const Node& root, const std::string& ancestor_id, const std::string& id);
bool AcceptsChild(const WidgetCatalog* catalog, const Node& parent, const std::string& child_type);
bool CanReparent(const Node& root, const std::string& id, const std::string& new_parent_id,
                 const WidgetCatalog* catalog);

Node* InsertChild(Node& parent, Node child, int index);
bool MoveNode(Node& root, const std::string& id, const std::string& new_parent_id, int index);
bool MoveNodes(Node& root, const std::vector<std::string>& ids, const std::string& new_parent_id,
               const std::string& anchor_id, bool after);
bool MoveSibling(Node& root, const std::string& id, int delta);
bool RemoveNode(Node& root, const std::string& id, Node* out);

Node CloneWithFreshIds(const Node& src, const Node& root);

bool CanWrap(const Node& root, const std::vector<std::string>& ids, const std::string& container_type,
             const WidgetCatalog* catalog);
bool WrapNode(Node& root, const std::string& id, const std::string& container_type);
bool WrapNodes(Node& root, const std::vector<std::string>& ids, const std::string& container_type,
               std::string* wrapper_id_out);
bool CanUnwrap(const Node& root, const std::string& id, const WidgetCatalog* catalog);
bool UnwrapNode(Node& root, const std::string& id, const WidgetCatalog* catalog = nullptr);

Node MakeDefaultNode(const std::string& type, const Node& root, const WidgetCatalog* catalog);
Node MakeComponentInstance(const Component& def, const Node& root);

std::vector<std::string> PathToNode(const Node& root, const std::string& id);
std::vector<std::string> TopLevelIds(const Node& root, const std::vector<std::string>& ids);
bool FindInsertPoint(Node& root, const std::string& selected_id, const std::string& type,
                     const WidgetCatalog* catalog, std::string& parent_id_out, int& index_out);

}  // namespace imforge::builder::tree
