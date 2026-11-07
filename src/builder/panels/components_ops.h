#pragma once

#include <string>
#include <vector>

#include "imforge/imforge_core.h"

namespace imforge::builder::components {

struct InstanceRef {
    std::string doc;
    bool in_component = false;
    std::string node_id;
};

struct ParamType {
    std::string base;
    std::string arg;
};

struct ExtractCandidate {
    enum class Kind { Binding, Literal };
    Kind kind = Kind::Literal;
    std::string node_id, key, value;
    std::string param_name, param_type;
    bool enabled = true;
};

struct ExtractPlan {
    std::string name;
    std::vector<std::string> node_ids;
    std::vector<ExtractCandidate> candidates;
};

const std::vector<std::string>& ParamBaseTypes();
const std::vector<std::string>& BindingTargetTypes();
ParamType ParseParamType(const std::string& type);
std::string MakeParamType(const ParamType& t);
Json DefaultForParamType(const std::string& type);
bool IsIdentifier(const std::string& s);
bool IsValidComponentName(const std::string& s);
std::string UniqueComponentName(const Project& project, const std::string& base);
std::string UniqueParamName(const Component& c, const std::string& base);
std::string UniqueSlotName(const Component& c, const std::string& base);
bool IsLibraryComponent(const Component& c);

Component MakeEmptyComponent(const std::string& name, bool with_slot);
std::vector<InstanceRef> FindInstances(const Project& project, const std::string& component);
size_t CountInstances(const Project& project, const std::string& component);
size_t RenameInstanceRefs(Node& root, const std::string& old_name, const std::string& new_name);
size_t RenameParamRefs(Node& root, const std::string& old_name, const std::string& new_name);
size_t RenameSlotRefs(Node& root, const std::string& old_name, const std::string& new_name);
bool HasSlotNode(const Node& root, const std::string& slot);
Node MakeSlotNode(const Node& root, const std::string& slot);

bool PlanExtract(const Node& root, const std::vector<std::string>& selection, const WidgetCatalog* catalog,
                 const Project* project, ExtractPlan& out, std::string* err);
bool ApplyExtract(Node& root, const ExtractPlan& plan, Component& out_component, std::string* instance_id,
                  std::string* err);

}  // namespace imforge::builder::components
