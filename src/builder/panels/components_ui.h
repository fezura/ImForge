#pragma once

#include <string>

#include "../editor_context.h"
#include "components_ops.h"

namespace imforge::builder::components {

template <class F>
void EditDoc(EditorContext& ctx, Component& c, const char* label, F&& f) {
    ctx.BeginEdit(label, DocKind::Component, c.name);
    f();
    ctx.EndEdit();
}

bool CheckNewComponentName(EditorContext& ctx, const std::string& name, const std::string& allow, std::string* err);
bool NewComponent(EditorContext& ctx, const std::string& name, bool with_slot, std::string* err);
bool DuplicateComponent(EditorContext& ctx, const std::string& source, const std::string& name, std::string* err);
bool RenameComponent(EditorContext& ctx, const std::string& old_name, const std::string& new_name, std::string* err);
bool DeleteComponent(EditorContext& ctx, const std::string& name);
bool ExtractComponent(EditorContext& ctx, const ExtractPlan& plan, std::string* instance_id, std::string* err);

void DrawParamsEditor(EditorContext& ctx, Component& c);
void DrawSlotsEditor(EditorContext& ctx, Component& c);
void DrawInstancesList(EditorContext& ctx, const std::string& component);

}  // namespace imforge::builder::components
