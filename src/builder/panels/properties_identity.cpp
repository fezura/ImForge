#include <algorithm>
#include <cstdio>
#include <functional>
#include <set>

#include "imgui.h"

#include "../ui/expr_editor.h"
#include "../ui/icon_table.h"
#include "properties_shared.h"

namespace imforge::builder::props {

namespace {

void RenameWithReferences(PropsUI& ui, const std::string& new_id) {
    std::string old_id = ui.node.id;
    ui.Edit("Rename", [&] {
        ui.node.id = new_id;
        Walk(*ui.root, [&](Node& n) {
            for (auto& ev : n.events) {
                for (Action& a : ev.second.actions) {
                    if ((a.type == "openPopup" || a.type == "setTab") && a.target == old_id) a.target = new_id;
                    if (a.type == "setTab" && a.value.IsString() && a.value.AsString() == old_id) a.value = Json(new_id);
                }
            }
        });
    });
    ui.ctx.Deselect(old_id);
    ui.ctx.Select(new_id, true);
    ui.ctx.SetStatus("Renamed " + old_id + " to " + new_id);
}

void DrawIdRow(PropsUI& ui) {
    static std::string buf;
    static ImGuiID editing = 0;
    PropRow("Id", "Unique identifier: [A-Za-z_][A-Za-z0-9_]*. Used in code markers, PushID and event targets.", 0);
    ImGuiID wid = ImGui::GetID("##id");
    if (editing != wid) buf = ui.node.id;
    bool valid = core_detail::IsIdentifier(buf) && (buf == ui.node.id || FindNode(*ui.root, buf) == nullptr);
    if (!valid) {
        ImGui::PushStyleColor(ImGuiCol_Border, Colors().error);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
    }
    ui::InputTextStd("##id", buf);
    if (!valid) {
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
    }
    if (ImGui::IsItemActive()) editing = wid;
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        if (valid && buf != ui.node.id) RenameWithReferences(ui, buf);
        editing = 0;
    } else if (ImGui::IsItemDeactivated()) {
        editing = 0;
    }
    if (!valid) {
        ImGui::PushStyleColor(ImGuiCol_Text, Colors().error);
        ImGui::TextUnformatted(core_detail::IsIdentifier(buf) ? "id already used" : "invalid identifier");
        ImGui::PopStyleColor();
    }
}

const char* const kAnchors[9] = {"TopLeft", "Top", "TopRight", "Left", "Center", "Right", "BottomLeft", "Bottom", "BottomRight"};

Json ParamValue(const PropsUI& ui, const Component& comp, const std::string& pname) {
    if (const PropValue* pv = ui.node.Prop(pname.c_str())) return pv->raw;
    for (const Param& p : comp.params)
        if (p.name == pname) return p.def;
    return Json();
}

bool UnpackComponent(PropsUI& ui) {
    const Component* comp = ui.ctx.project.FindComponent(ui.node.component);
    if (!comp) {
        ui.ctx.SetStatus("Unknown component " + ui.node.component);
        return false;
    }
    Node expanded = comp->root;
    Walk(expanded, [&](Node& n) {
        for (auto& p : n.props)
            if (p.second.IsParam()) p.second.raw = ParamValue(ui, *comp, p.second.Param());
        for (auto& b : n.bind)
            if (b.second.IsParam()) b.second.raw = ParamValue(ui, *comp, b.second.Param());
    });
    std::function<void(Node&)> fill = [&](Node& n) {
        for (size_t i = 0; i < n.children.size();) {
            Node& c = n.children[i];
            if (c.type == "Slot") {
                std::string sname = c.Prop("name") ? c.Prop("name")->raw.AsString("content") : "content";
                std::vector<Node> ins;
                for (const auto& s : ui.node.slots)
                    if (s.first == sname) ins = s.second;
                n.children.erase(n.children.begin() + (long)i);
                n.children.insert(n.children.begin() + (long)i, ins.begin(), ins.end());
                i += ins.size();
            } else {
                fill(c);
                ++i;
            }
        }
    };
    fill(expanded);

    std::set<std::string> used;
    std::vector<std::string> all;
    CollectIds(*ui.root, all);
    std::vector<std::string> mine;
    CollectIds(ui.node, mine);
    for (const std::string& id : all) used.insert(id);
    for (const std::string& id : mine) used.erase(id);
    for (const auto& s : ui.node.slots)
        for (const Node& c : s.second) Walk(c, [&](const Node& n) { used.insert(n.id); });
    std::string instance_id = ui.node.id;
    Walk(expanded, [&](Node& n) {
        if (&n == &expanded) {
            n.id = instance_id;
            return;
        }
        if (used.count(n.id)) {
            std::string base = instance_id + "_" + n.id;
            std::string cand = base;
            for (int k = 2; used.count(cand); ++k) cand = base + "_" + std::to_string(k);
            n.id = cand;
        }
        used.insert(n.id);
    });
    used.insert(instance_id);

    const Node& inst = ui.node;
    if (expanded.name.empty()) expanded.name = inst.name;
    if (expanded.visible_if.empty()) expanded.visible_if = inst.visible_if;
    if (expanded.enabled_if.empty()) expanded.enabled_if = inst.enabled_if;
    if (expanded.tooltip.empty()) expanded.tooltip = inst.tooltip;
    if (expanded.font.empty()) expanded.font = inst.font;
    if (expanded.style.Empty()) expanded.style = inst.style;
    if (expanded.anim.Empty()) expanded.anim = inst.anim;
    if (expanded.fx.empty()) expanded.fx = inst.fx;
    expanded.layout = inst.layout;
    expanded.locked = inst.locked;

    bool done = false;
    ui.Edit("Unpack component", [&] {
        if (&ui.node == ui.root) {
            *ui.root = expanded;
            done = true;
            return;
        }
        size_t idx = 0;
        Node* parent = FindParent(*ui.root, instance_id, &idx);
        if (!parent) return;
        if (idx < parent->children.size() && parent->children[idx].id == instance_id) {
            parent->children[idx] = expanded;
            done = true;
            return;
        }
        for (auto& s : parent->slots) {
            if (idx < s.second.size() && s.second[idx].id == instance_id) {
                s.second[idx] = expanded;
                done = true;
                return;
            }
        }
    });
    if (done) ui.ctx.Select(instance_id);
    return done;
}

bool ParamEditor(PropsUI& ui, const Param& p, Json& value) {
    std::string t = p.type;
    bool changed = false;
    if (t == "string") {
        std::string s = value.AsString(p.def.AsString(""));
        if (ui::InputTextStd("##v", s)) {
            value = Json(s);
            changed = true;
        }
    } else if (t == "int") {
        int v = value.AsInt(p.def.AsInt(0));
        if (ImGui::DragInt("##v", &v)) {
            value = Json(v);
            changed = true;
        }
    } else if (t == "float") {
        float v = (float)value.AsDouble(p.def.AsDouble(0));
        if (ImGui::DragFloat("##v", &v, 0.1f)) {
            value = FloatJson(v);
            changed = true;
        }
    } else if (t == "bool") {
        bool v = value.AsBool(p.def.AsBool(false));
        if (ImGui::Checkbox("##v", &v)) {
            value = Json(v);
            changed = true;
        }
    } else if (t == "color") {
        ImVec4 c = JsonColor(value, JsonColor(p.def, ImVec4(1, 1, 1, 1)));
        if (ColorEditHex("##v", c)) {
            value = ColorJson(c);
            changed = true;
        }
    } else if (t == "vec2") {
        ImVec2 v = JsonVec2(value, JsonVec2(p.def, ImVec2(0, 0)));
        if (ImGui::DragFloat2("##v", &v.x)) {
            value = Vec2Json(v);
            changed = true;
        }
    } else if (t == "vec4") {
        ImVec4 v = JsonVec4(value, JsonVec4(p.def, ImVec4(0, 0, 0, 0)));
        if (ImGui::DragFloat4("##v", &v.x)) {
            value = Vec4Json(v);
            changed = true;
        }
    } else if (t.compare(0, 5, "enum:") == 0) {
        std::vector<std::string> items;
        std::string rest = t.substr(5);
        size_t start = 0;
        while (start <= rest.size()) {
            size_t comma = rest.find(',', start);
            std::string item = rest.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
            if (!item.empty()) items.push_back(item);
            if (comma == std::string::npos) break;
            start = comma + 1;
        }
        std::string cur = value.AsString(p.def.AsString(items.empty() ? "" : items[0]));
        if (ComboStrings("##v", cur, items, nullptr)) {
            value = Json(cur);
            changed = true;
        }
    } else if (t.compare(0, 8, "binding<") == 0) {
        std::string inner = t.size() > 9 ? t.substr(8, t.size() - 9) : "any";
        std::string path = value.AsString("");
        PathStatus st;
        if (PathEditor("##v", path, &ui.env, inner == "any" ? nullptr : inner.c_str(), &st)) {
            value = Json(path);
            changed = true;
        }
        PathStatusBadge(st);
    } else {
        ui::TextDim("unsupported param type %s", t.c_str());
    }
    return changed;
}

}  // namespace

void DrawIdentitySection(PropsUI& ui) {
    if (!SectionHeader("Identity", true)) return;
    if (!ui::BeginPropertyTable("identity")) return;
    Node& node = ui.node;

    PropRow("Type", nullptr, 0);
    const char* glyph = ui.def && ui.def->icon ? IconUtf8(ui.def->icon) : nullptr;
    if (glyph && *glyph) {
        ImGui::TextUnformatted(glyph);
        ImGui::SameLine();
    }
    ImGui::TextUnformatted(node.type.c_str());
    if (ui.custom) {
        ImGui::SameLine();
        ui::TextDim("(%s)", ui.custom->category.c_str());
    }
    if (!ui.def) {
        ImGui::SameLine();
        ui::Badge("unknown type", Colors().error);
    } else if (ui.def->min_version > ui.version) {
        ImGui::SameLine();
        ui::Badge("needs newer ImGui", Colors().warning);
        UnsupportedNote(ui.def->min_version, ui.version);
    }

    DrawIdRow(ui);

    PropRow("Name", "Display name used in the Hierarchy; not emitted in code.", 0);
    {
        std::string name = node.name;
        if (ui::InputTextStd("##name", name)) ui.Edit("Edit name", [&] { node.name = name; });
    }

    PropRow("Locked", nullptr, 0);
    {
        bool locked = node.locked;
        if (ImGui::Checkbox(locked ? PP_ICON_LOCK " locked" : PP_ICON_UNLOCK " unlocked", &locked))
            ui.Edit("Toggle lock", [&] { node.locked = locked; });
        ImGui::SameLine();
        ui::HelpMarker("Locked widgets cannot be moved, resized or deleted on the Canvas or in the Hierarchy; "
                       "their properties stay editable here. Round-trip merge treats Code blocks as locked too.");
    }

    PropRow("Tooltip", "Shown when the widget is hovered (ImGui::SetTooltip).", 0);
    {
        std::string tip = node.tooltip;
        if (ui::InputTextStd("##tooltip", tip)) ui.Edit("Edit tooltip", [&] { node.tooltip = tip; });
    }

    PropRow("Font", "Project font pushed around this widget (PushFont/PopFont).", 0);
    {
        std::vector<std::string> fonts;
        for (const FontSpec& f : ui.ctx.project.fonts) fonts.push_back(f.name);
        std::string font = node.font;
        bool missing = !font.empty() && std::find(fonts.begin(), fonts.end(), font) == fonts.end();
        if (missing) fonts.push_back(font + " (missing)");
        if (ComboStrings("##font", font, fonts, "(default)")) {
            size_t sp = font.find(" (missing)");
            if (sp != std::string::npos) font = font.substr(0, sp);
            ui.Edit("Edit font", [&] { node.font = font; });
        }
    }
    ui::EndPropertyTable();
}

void DrawConditionsSection(PropsUI& ui) {
    Node& node = ui.node;
    if (!SectionHeader("Conditions", !node.visible_if.empty() || !node.enabled_if.empty())) return;
    if (!ui::BeginPropertyTable("conditions")) return;
    PropRow("Visible if", "Boolean expression; the widget is skipped when false.", 0);
    {
        std::string e = node.visible_if;
        if (ExprEditor("##visibleIf", e, &ui.env, expr::Type::Bool)) ui.Edit("Edit visibleIf", [&] { node.visible_if = e; });
    }
    PropRow("Enabled if", "Boolean expression; the widget is wrapped in BeginDisabled when false.", 0);
    {
        std::string e = node.enabled_if;
        if (ExprEditor("##enabledIf", e, &ui.env, expr::Type::Bool)) ui.Edit("Edit enabledIf", [&] { node.enabled_if = e; });
    }
    ui::EndPropertyTable();
}

void DrawLayoutSection(PropsUI& ui) {
    size_t idx = 0;
    Node* parent = FindParent(*ui.root, ui.node.id, &idx);
    if (!parent || parent->type != "Canvas") return;
    if (!SectionHeader("Layout", true)) return;
    Node& node = ui.node;
    if (!ui::BeginPropertyTable("layout")) return;
    PropRow("Anchor", "Point of the Canvas the widget is positioned from.", 0);
    {
        float cell = ImGui::GetFrameHeight();
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(2, 2));
        for (int r = 0; r < 3; ++r) {
            for (int c = 0; c < 3; ++c) {
                int i = r * 3 + c;
                if (c) ImGui::SameLine();
                bool sel = node.layout.anchor == kAnchors[i];
                ImGui::PushID(i);
                const BuilderColors& col = Colors();
                ImGui::PushStyleColor(ImGuiCol_Button, sel ? col.accent : col.bg_frame);
                if (ImGui::Button(sel ? "o" : "", ImVec2(cell, cell)) && !sel)
                    ui.Edit("Edit anchor", [&] { node.layout.anchor = kAnchors[i]; });
                ImGui::PopStyleColor();
                ui::Tooltip(kAnchors[i]);
                ImGui::PopID();
            }
        }
        ImGui::PopStyleVar();
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ui::TextDim("%s", node.layout.anchor.c_str());
    }
    PropRow("Offset", "Offset from the anchor point in DPI-scaled pixels.", 1);
    {
        ImVec2 off(node.layout.offset.x, node.layout.offset.y);
        if (ImGui::DragFloat2("##offset", &off.x, 1.0f, 0, 0, "%.0f"))
            ui.Edit("Edit offset", [&] { node.layout.offset = Vec2{off.x, off.y}; });
        TrailingSameLine();
        if (ResetButton()) ui.Edit("Reset offset", [&] { node.layout.offset = Vec2{}; });
    }
    ui::EndPropertyTable();
}

bool DrawComponentSection(PropsUI& ui) {
    if (!SectionHeader("Component instance", true)) return false;
    Node& node = ui.node;
    const Component* comp = ui.ctx.project.FindComponent(node.component);
    if (!ui::BeginPropertyTable("component")) return false;
    PropRow("Component", "Component definition this instance expands to.", 0);
    {
        std::vector<std::string> names = ComponentNames(ui.ctx.project);
        if (ui.component_doc) names.erase(std::remove(names.begin(), names.end(), ui.component_doc->name), names.end());
        std::string cur = node.component;
        if (ComboStrings("##component", cur, names, nullptr)) ui.Edit("Change component", [&] { node.component = cur; });
        if (!comp) {
            ui::Badge("unknown component", Colors().error);
        }
    }
    if (comp) {
        for (const Param& p : comp->params) {
            ImGui::PushID(p.name.c_str());
            const PropValue* pv = node.Prop(p.name.c_str());
            bool is_set = pv != nullptr;
            bool is_param = pv && pv->IsParam();
            int trailing = (ui.in_component ? 1 : 0) + 1;
            std::string help = "type: " + p.type;
            PropRow(p.name.c_str(), help.c_str(), trailing);
            Json value = pv ? pv->raw : p.def;
            if (is_param) {
                std::vector<std::string> pnames;
                if (ui.component_doc)
                    for (const Param& cp : ui.component_doc->params) pnames.push_back(cp.name);
                std::string cur = pv->Param();
                if (ComboStrings("##param", cur, pnames, nullptr))
                    ui.Edit("Edit param", [&] { node.SetProp(p.name.c_str(), ParamJson(cur)); });
            } else if (ParamEditor(ui, p, value)) {
                ui.Edit("Edit param", [&] { node.SetProp(p.name.c_str(), value); });
            }
            if (ui.in_component) {
                TrailingSameLine();
                if (SmallIconButton("param", PP_ICON_PARAM, "Forward a parameter of the component being edited", true, is_param)) {
                    if (is_param) ui.Edit("Edit param", [&] { node.RemoveProp(p.name.c_str()); });
                    else if (ui.component_doc && !ui.component_doc->params.empty()) {
                        std::string first = ui.component_doc->params.front().name;
                        for (const Param& cp : ui.component_doc->params)
                            if (cp.type == p.type) {
                                first = cp.name;
                                break;
                            }
                        ui.Edit("Edit param", [&] { node.SetProp(p.name.c_str(), ParamJson(first)); });
                    }
                }
            }
            TrailingSameLine();
            if (is_set) {
                if (ResetButton("Reset to the component default")) ui.Edit("Reset param", [&] { node.RemoveProp(p.name.c_str()); });
            } else {
                ImGui::Dummy(ImVec2(SmallButtonWidth(), SmallButtonWidth()));
            }
            ImGui::PopID();
        }
        if (!comp->slots.empty()) {
            PropRow("Slots", "Children placed into the component's Slot nodes (edit them in the Hierarchy).", 0);
            std::string s;
            for (const std::string& slot : comp->slots) {
                size_t n = 0;
                for (const auto& kv : node.slots)
                    if (kv.first == slot) n = kv.second.size();
                if (!s.empty()) s += ", ";
                s += slot + " (" + std::to_string(n) + ")";
            }
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(s.c_str());
        }
    }
    ui::EndPropertyTable();
    for (const auto& p : node.props) {
        bool known = false;
        if (comp)
            for (const Param& cp : comp->params) known = known || cp.name == p.first;
        if (known) continue;
        ImGui::PushID(p.first.c_str());
        if (SmallIconButton("rm", PP_ICON_REMOVE, "Remove this value (no such parameter)")) {
            std::string key = p.first;
            ui.Edit("Remove param", [&] { node.RemoveProp(key.c_str()); });
            ImGui::PopID();
            break;
        }
        ImGui::SameLine();
        ui::TextDim("%s: %s (unknown parameter)", p.first.c_str(), DumpJson(p.second.raw, 0, true).c_str());
        ImGui::PopID();
    }
    ImGui::Spacing();
    bool can_unpack = comp != nullptr;
    if (!can_unpack) ImGui::BeginDisabled();
    bool unpack = ImGui::Button("Unpack");
    if (!can_unpack) ImGui::EndDisabled();
    ui::Tooltip("Replace this instance with a copy of the component tree (parameters substituted, slots inserted)");
    return unpack && can_unpack && UnpackComponent(ui);
}

}  // namespace imforge::builder::props
