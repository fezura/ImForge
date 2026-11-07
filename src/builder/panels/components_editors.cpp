#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <string>
#include <vector>

#include "imgui.h"

#include "../builder_style.h"
#include "../ui/ui_helpers.h"
#include "components_ui.h"

namespace imforge::builder::components {

namespace {

std::vector<std::string> SplitComma(const std::string& s) {
    std::vector<std::string> out;
    size_t start = 0;
    while (start <= s.size()) {
        size_t comma = s.find(',', start);
        std::string part = s.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
        while (!part.empty() && part.front() == ' ') part.erase(0, 1);
        while (!part.empty() && part.back() == ' ') part.pop_back();
        if (!part.empty()) out.push_back(part);
        if (comma == std::string::npos) break;
        start = comma + 1;
    }
    return out;
}

bool ParamNameTaken(const Component& c, const std::string& name, const Param* except) {
    for (const Param& p : c.params)
        if (&p != except && p.name == name) return true;
    return false;
}

void DrawTypeEditor(EditorContext& ctx, Component& c, Param& p) {
    ParamType t = ParseParamType(p.type);
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::BeginCombo("##type", t.base.c_str())) {
        for (const std::string& base : ParamBaseTypes()) {
            if (ImGui::Selectable(base.c_str(), base == t.base) && base != t.base) {
                ParamType nt;
                nt.base = base;
                nt.arg = base == "binding" ? "any" : "";
                EditDoc(ctx, c, "Change parameter type", [&] {
                    p.type = MakeParamType(nt);
                    p.def = DefaultForParamType(p.type);
                });
            }
        }
        ImGui::EndCombo();
    }
    if (t.base == "binding") {
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::BeginCombo("##target", t.arg.c_str())) {
            for (const std::string& target : BindingTargetTypes()) {
                if (ImGui::Selectable(target.c_str(), target == t.arg) && target != t.arg) {
                    ParamType nt = t;
                    nt.arg = target;
                    EditDoc(ctx, c, "Change parameter type", [&] { p.type = MakeParamType(nt); });
                }
            }
            ImGui::EndCombo();
        }
    } else if (t.base == "enum") {
        std::string values = t.arg;
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ui::InputTextWithHintStd("##enumvalues", "A,B,C", values)) {
            ParamType nt = t;
            nt.arg = values;
            EditDoc(ctx, c, "Change enum values", [&] {
                p.type = MakeParamType(nt);
                std::vector<std::string> opts = SplitComma(values);
                if (std::find(opts.begin(), opts.end(), p.def.AsString()) == opts.end()) p.def = DefaultForParamType(p.type);
            });
        }
    }
}

void DrawDefaultEditor(EditorContext& ctx, Component& c, Param& p) {
    ParamType t = ParseParamType(p.type);
    const char* label = "Parameter default";
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (t.base == "binding") {
        ui::TextDim("bound per instance");
    } else if (t.base == "string") {
        std::string s = p.def.AsString();
        if (ui::InputTextStd("##def", s)) EditDoc(ctx, c, label, [&] { p.def = Json(s); });
    } else if (t.base == "int") {
        int v = p.def.AsInt();
        if (ImGui::InputInt("##def", &v)) EditDoc(ctx, c, label, [&] { p.def = Json(v); });
    } else if (t.base == "float") {
        float v = (float)p.def.AsDouble();
        if (ImGui::InputFloat("##def", &v, 0.0f, 0.0f, "%.4g")) EditDoc(ctx, c, label, [&] { p.def = Json(core_detail::FloatToNum(v)); });
    } else if (t.base == "bool") {
        bool v = p.def.AsBool();
        if (ImGui::Checkbox("##def", &v)) EditDoc(ctx, c, label, [&] { p.def = Json(v); });
    } else if (t.base == "color") {
        Vec4 col = ParseColor(p.def.AsString("#ffffffff"));
        float f[4] = {col.x, col.y, col.z, col.w};
        if (ImGui::ColorEdit4("##def", f, ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf)) {
            Vec4 nc;
            nc.x = f[0];
            nc.y = f[1];
            nc.z = f[2];
            nc.w = f[3];
            EditDoc(ctx, c, label, [&] { p.def = Json(ColorToHex(nc)); });
        }
    } else if (t.base == "vec2" || t.base == "vec4") {
        const bool four = t.base == "vec4";
        float v[4] = {(float)p.def["x"].AsDouble(), (float)p.def["y"].AsDouble(), (float)p.def["z"].AsDouble(),
                      (float)p.def["w"].AsDouble()};
        bool changed = four ? ImGui::InputFloat4("##def", v, "%.4g") : ImGui::InputFloat2("##def", v, "%.4g");
        if (changed) {
            EditDoc(ctx, c, label, [&] {
                Json o = Json::Object();
                o.Set("x", Json(core_detail::FloatToNum(v[0])));
                o.Set("y", Json(core_detail::FloatToNum(v[1])));
                if (four) {
                    o.Set("z", Json(core_detail::FloatToNum(v[2])));
                    o.Set("w", Json(core_detail::FloatToNum(v[3])));
                }
                p.def = o;
            });
        }
    } else if (t.base == "enum") {
        std::vector<std::string> opts = SplitComma(t.arg);
        std::string cur = p.def.AsString();
        if (ImGui::BeginCombo("##def", cur.empty() ? "(none)" : cur.c_str())) {
            for (const std::string& o : opts)
                if (ImGui::Selectable(o.c_str(), o == cur) && o != cur) EditDoc(ctx, c, label, [&] { p.def = Json(o); });
            ImGui::EndCombo();
        }
    } else {
        ui::TextDim("unknown type");
    }
}

}  // namespace

void DrawParamsEditor(EditorContext& ctx, Component& c) {
    if (ImGui::SmallButton("+ Add parameter")) {
        EditDoc(ctx, c, "Add parameter", [&] { c.params.push_back(Param{UniqueParamName(c, "param"), "string", Json("")}); });
    }
    ImGui::SameLine();
    ui::HelpMarker("Parameters are passed by instances. Use {\"param\": name} on any prop or binding inside the "
                   "component; binding<T> parameters bind to a variable of the instance's panel.");
    if (c.params.empty()) {
        ui::TextDim("No parameters.");
        return;
    }
    ImGuiTableFlags flags = ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
                            ImGuiTableFlags_PadOuterX;
    if (!ImGui::BeginTable("params", 4, flags)) return;
    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 1.1f);
    ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthStretch, 1.2f);
    ImGui::TableSetupColumn("Default", ImGuiTableColumnFlags_WidthStretch, 1.4f);
    ImGui::TableSetupColumn("##actions", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFrameHeight() * 3.3f);
    ImGui::TableHeadersRow();
    int remove = -1, move_from = -1, move_delta = 0;
    for (int i = 0; i < (int)c.params.size(); ++i) {
        Param& p = c.params[(size_t)i];
        ImGui::PushID(i);
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        std::string name = p.name;
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ui::InputTextStd("##name", name) && IsIdentifier(name) && !ParamNameTaken(c, name, &p)) {
            EditDoc(ctx, c, "Rename parameter", [&] {
                RenameParamRefs(c.root, p.name, name);
                p.name = name;
            });
        }
        ImGui::TableSetColumnIndex(1);
        DrawTypeEditor(ctx, c, p);
        ImGui::TableSetColumnIndex(2);
        DrawDefaultEditor(ctx, c, p);
        ImGui::TableSetColumnIndex(3);
        ImGui::BeginDisabled(i == 0);
        if (ImGui::ArrowButton("up", ImGuiDir_Up)) {
            move_from = i;
            move_delta = -1;
        }
        ImGui::EndDisabled();
        ImGui::SameLine(0, 2);
        ImGui::BeginDisabled(i + 1 >= (int)c.params.size());
        if (ImGui::ArrowButton("down", ImGuiDir_Down)) {
            move_from = i;
            move_delta = 1;
        }
        ImGui::EndDisabled();
        ImGui::SameLine(0, 2);
        if (ImGui::Button("x", ImVec2(ImGui::GetFrameHeight(), 0))) remove = i;
        ui::Tooltip("Remove parameter (references inside the component keep the name)");
        ImGui::PopID();
    }
    ImGui::EndTable();
    if (remove >= 0) EditDoc(ctx, c, "Remove parameter", [&] { c.params.erase(c.params.begin() + remove); });
    if (move_from >= 0) {
        EditDoc(ctx, c, "Reorder parameters", [&] {
            std::swap(c.params[(size_t)move_from], c.params[(size_t)(move_from + move_delta)]);
        });
    }
}

void DrawSlotsEditor(EditorContext& ctx, Component& c) {
    if (ImGui::SmallButton("+ Add slot")) {
        EditDoc(ctx, c, "Add slot", [&] { c.slots.push_back(UniqueSlotName(c, c.slots.empty() ? "content" : "slot")); });
    }
    ImGui::SameLine();
    ui::HelpMarker("Slots are placeholders filled by each instance. A Slot node with the same name marks where the "
                   "instance children are drawn.");
    if (c.slots.empty()) {
        ui::TextDim("No slots.");
        return;
    }
    const WidgetDef* root_def = ctx.catalog.Find(c.root.type.c_str());
    const bool root_is_container = !root_def || root_def->container;
    ImGuiTableFlags flags = ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
                            ImGuiTableFlags_PadOuterX;
    if (!ImGui::BeginTable("slots", 3, flags)) return;
    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 1.0f);
    ImGui::TableSetupColumn("Node", ImGuiTableColumnFlags_WidthStretch, 1.2f);
    ImGui::TableSetupColumn("##actions", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFrameHeight() * 1.2f);
    ImGui::TableHeadersRow();
    int remove = -1;
    for (int i = 0; i < (int)c.slots.size(); ++i) {
        std::string& slot = c.slots[(size_t)i];
        ImGui::PushID(i);
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        std::string name = slot;
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ui::InputTextStd("##name", name) && IsIdentifier(name) &&
            std::find(c.slots.begin(), c.slots.end(), name) == c.slots.end()) {
            EditDoc(ctx, c, "Rename slot", [&] {
                RenameSlotRefs(c.root, slot, name);
                slot = name;
            });
        }
        ImGui::TableSetColumnIndex(1);
        if (HasSlotNode(c.root, slot)) {
            ui::TextDim("Slot node placed");
        } else {
            ImGui::BeginDisabled(!root_is_container);
            if (ImGui::SmallButton("Insert Slot node")) {
                EditDoc(ctx, c, "Insert slot node", [&] { c.root.children.push_back(MakeSlotNode(c.root, slot)); });
            }
            ImGui::EndDisabled();
            ui::Tooltip(root_is_container ? "Append a Slot node for this slot to the component root"
                                          : "The component root is not a container; wrap it first");
        }
        ImGui::TableSetColumnIndex(2);
        if (ImGui::Button("x", ImVec2(ImGui::GetFrameHeight(), 0))) remove = i;
        ui::Tooltip("Remove slot (existing Slot nodes stay in the tree)");
        ImGui::PopID();
    }
    ImGui::EndTable();
    if (remove >= 0) EditDoc(ctx, c, "Remove slot", [&] { c.slots.erase(c.slots.begin() + remove); });
}

void DrawInstancesList(EditorContext& ctx, const std::string& component) {
    std::vector<InstanceRef> refs = FindInstances(ctx.project, component);
    if (refs.empty()) {
        ui::TextDim("No instances. Drag the component from the Palette or Components list onto the canvas.");
        return;
    }
    for (size_t i = 0; i < refs.size(); ++i) {
        const InstanceRef& r = refs[i];
        char label[256];
        std::snprintf(label, sizeof(label), "%s / %s%s##inst%d", r.doc.c_str(), r.node_id.c_str(),
                      r.in_component ? "  (component)" : "", (int)i);
        bool current = ctx.active_panel == r.doc && ctx.IsSelected(r.node_id);
        if (ImGui::Selectable(label, current)) {
            if (r.in_component) ctx.SetActiveComponent(r.doc);
            else ctx.SetActivePanel(r.doc);
            ctx.Select(r.node_id);
        }
    }
}

}  // namespace imforge::builder::components
