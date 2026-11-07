#include <algorithm>
#include <string>
#include <vector>

#include "components_ui.h"

namespace imforge::builder::components {

namespace {

bool ActionFail(std::string* err, const std::string& msg) {
    if (err) *err = msg;
    return false;
}

std::string ComponentFile(const std::string& name) { return "components/" + name + ".imc.json"; }

}  // namespace

bool CheckNewComponentName(EditorContext& ctx, const std::string& name, const std::string& allow, std::string* err) {
    if (!IsIdentifier(name)) return ActionFail(err, "Name must be an identifier: [A-Za-z_][A-Za-z0-9_]*");
    if (name != allow && ctx.FindComponentDoc(name)) return ActionFail(err, "A component named " + name + " already exists");
    if (err) err->clear();
    return true;
}

bool NewComponent(EditorContext& ctx, const std::string& name, bool with_slot, std::string* err) {
    if (!CheckNewComponentName(ctx, name, std::string(), err)) return false;
    ctx.BeginComponentsEdit("New component");
    ctx.project.components.push_back(MakeEmptyComponent(name, with_slot));
    ctx.EndEdit();
    ctx.Log(LogLevel::Info, "created component %s", name.c_str());
    return true;
}

bool DuplicateComponent(EditorContext& ctx, const std::string& source, const std::string& name, std::string* err) {
    const Component* src = ctx.FindComponentDoc(source);
    if (!src) return ActionFail(err, "No component selected");
    if (!CheckNewComponentName(ctx, name, std::string(), err)) return false;
    Component copy = *src;
    copy.name = name;
    copy.file = ComponentFile(name);
    ctx.BeginComponentsEdit("Duplicate component");
    ctx.project.components.push_back(std::move(copy));
    ctx.EndEdit();
    return true;
}

bool RenameComponent(EditorContext& ctx, const std::string& old_name, const std::string& new_name, std::string* err) {
    if (!ctx.FindComponentDoc(old_name)) return ActionFail(err, "No component selected");
    if (new_name == old_name) return true;
    if (!CheckNewComponentName(ctx, new_name, old_name, err)) return false;
    std::vector<std::string> panels;
    for (const InstanceRef& r : FindInstances(ctx.project, old_name))
        if (!r.in_component) panels.push_back(r.doc);
    ctx.BeginComponentsEdit("Rename component", panels);
    Component* c = ctx.FindComponentDoc(old_name);
    c->name = new_name;
    c->file = ComponentFile(new_name);
    size_t refs = 0;
    for (Panel& p : ctx.project.panels) refs += RenameInstanceRefs(p.root, old_name, new_name);
    for (Component& other : ctx.project.components) refs += RenameInstanceRefs(other.root, old_name, new_name);
    if (ctx.active_kind == DocKind::Component && ctx.active_panel == old_name) ctx.active_panel = new_name;
    ctx.EndEdit();
    ctx.Log(LogLevel::Info, "renamed component %s to %s (%d instances updated)", old_name.c_str(), new_name.c_str(), (int)refs);
    return true;
}

bool DeleteComponent(EditorContext& ctx, const std::string& name) {
    auto& comps = ctx.project.components;
    auto it = std::find_if(comps.begin(), comps.end(), [&](const Component& c) { return c.name == name; });
    if (it == comps.end()) return false;
    ctx.BeginComponentsEdit("Delete component");
    comps.erase(it);
    ctx.EndEdit();
    ctx.Log(LogLevel::Info, "deleted component %s", name.c_str());
    return true;
}

bool ExtractComponent(EditorContext& ctx, const ExtractPlan& plan, std::string* instance_id, std::string* err) {
    Node* root = ctx.ActiveRoot();
    if (!root) return ActionFail(err, "No active document");
    if (!CheckNewComponentName(ctx, plan.name, std::string(), err)) return false;
    std::vector<std::string> panels;
    if (ctx.active_kind == DocKind::Panel) panels.push_back(ctx.active_panel);
    Component comp;
    std::string inst;
    ctx.BeginComponentsEdit("Create component from selection", panels);
    bool ok = ApplyExtract(*root, plan, comp, &inst, err);
    if (ok) ctx.project.components.push_back(std::move(comp));
    ctx.EndEdit();
    if (!ok) return false;
    ctx.Select(inst);
    if (instance_id) *instance_id = inst;
    return true;
}

}  // namespace imforge::builder::components
