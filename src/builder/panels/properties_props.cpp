#include <algorithm>
#include <cstdio>

#include "imgui.h"

#include "../ui/expr_editor.h"
#include "../ui/flags_editor.h"
#include "../ui/size_editor.h"
#include "properties_shared.h"

namespace imforge::builder::props {

namespace {

bool IsInterpProp(const char* name) {
    return std::strcmp(name, "text") == 0 || std::strcmp(name, "label") == 0 || std::strcmp(name, "title") == 0 ||
           std::strcmp(name, "overlay") == 0;
}

bool SupportsExpr(PropType t) {
    return t == PropType::String || t == PropType::Float || t == PropType::Int || t == PropType::Bool;
}

expr::Type ExpectedFor(PropType t) {
    switch (t) {
    case PropType::Float: return expr::Type::Float;
    case PropType::Int: return expr::Type::Int;
    case PropType::Bool: return expr::Type::Bool;
    default: return expr::Type::Error;
    }
}

bool NullDefaultButton(const PropDef& pd) {
    float w = ImGui::CalcItemWidth();
    bool pressed = ImGui::Button("default##null", ImVec2(w, 0));
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
        ImGui::SetTooltip("%s\nClick to set an explicit value.", pd.hint ? pd.hint : "null = ImGui default");
    return pressed;
}

bool ListButtons(size_t i, size_t count, bool& up, bool& down, bool& remove) {
    TrailingSameLine();
    up = SmallIconButton("up", PP_ICON_UP, "Move up", i > 0);
    TrailingSameLine();
    down = SmallIconButton("down", PP_ICON_DOWN, "Move down", i + 1 < count);
    TrailingSameLine();
    remove = SmallIconButton("rm", PP_ICON_REMOVE, "Remove");
    return up || down || remove;
}

bool StringListEditor(const char* id, std::vector<std::string>& items) {
    ImGui::PushID(id);
    bool changed = false;
    float w = ImGui::CalcItemWidth();
    float bw = (SmallButtonWidth() + ImGui::GetStyle().ItemInnerSpacing.x) * 3;
    for (size_t i = 0; i < items.size(); ++i) {
        ImGui::PushID((int)i);
        ImGui::SetNextItemWidth(std::max(w - bw, 40.0f));
        if (ui::InputTextStd("##s", items[i])) changed = true;
        bool up = false, down = false, remove = false;
        if (ListButtons(i, items.size(), up, down, remove)) {
            if (up) std::swap(items[i], items[i - 1]);
            else if (down) std::swap(items[i], items[i + 1]);
            else if (remove) items.erase(items.begin() + (long)i);
            changed = true;
            ImGui::PopID();
            break;
        }
        ImGui::PopID();
    }
    if (ImGui::Button(PP_ICON_ADD " Add##add", ImVec2(w, 0))) {
        items.emplace_back();
        changed = true;
    }
    ImGui::PopID();
    return changed;
}

bool SizeListEditor(const char* id, Json& arr) {
    ImGui::PushID(id);
    bool changed = false;
    std::vector<SizeSpec> items;
    if (arr.IsArray()) {
        for (size_t i = 0; i < arr.Size(); ++i) {
            SizeSpec s;
            SizeSpec::Parse(arr[i], s);
            items.push_back(s);
        }
    }
    float w = ImGui::CalcItemWidth();
    float bw = (SmallButtonWidth() + ImGui::GetStyle().ItemInnerSpacing.x) * 3;
    for (size_t i = 0; i < items.size(); ++i) {
        ImGui::PushID((int)i);
        ImGui::SetNextItemWidth(std::max(w - bw, 60.0f));
        if (SizeSpecEditor("##s", items[i])) changed = true;
        bool up = false, down = false, remove = false;
        if (ListButtons(i, items.size(), up, down, remove)) {
            if (up) std::swap(items[i], items[i - 1]);
            else if (down) std::swap(items[i], items[i + 1]);
            else if (remove) items.erase(items.begin() + (long)i);
            changed = true;
            ImGui::PopID();
            break;
        }
        ImGui::PopID();
    }
    if (ImGui::Button(PP_ICON_ADD " Add##add", ImVec2(w, 0))) {
        items.push_back(SizeSpec());
        changed = true;
    }
    if (changed) {
        Json a = Json::Array();
        for (const SizeSpec& s : items) a.Push(s.ToJson());
        arr = std::move(a);
    }
    ImGui::PopID();
    return changed;
}

bool LiteralEditor(PropsUI& ui, const PropDef& pd, Json& value) {
    bool ranged = !(pd.min == 0 && pd.max == 0);
    switch (pd.type) {
    case PropType::Bool: {
        bool b = value.AsBool(false);
        if (ImGui::Checkbox("##v", &b)) {
            value = Json(b);
            return true;
        }
        return false;
    }
    case PropType::Int: {
        if (value.IsNull()) {
            if (NullDefaultButton(pd)) {
                value = Json(ranged ? (int)pd.min : 0);
                return true;
            }
            return false;
        }
        int v = value.AsInt(0);
        if (ImGui::DragInt("##v", &v, 1.0f, ranged ? (int)pd.min : 0, ranged ? (int)pd.max : 0)) {
            if (ranged) v = std::clamp(v, (int)pd.min, (int)pd.max);
            value = Json(v);
            return true;
        }
        return false;
    }
    case PropType::Float: {
        if (value.IsNull()) {
            if (NullDefaultButton(pd)) {
                value = FloatJson(ranged ? pd.min : 0.0f);
                return true;
            }
            return false;
        }
        float v = (float)value.AsDouble(0);
        float speed = ranged ? std::max((pd.max - pd.min) / 200.0f, 0.001f) : 0.1f;
        if (ImGui::DragFloat("##v", &v, speed, ranged ? pd.min : 0.0f, ranged ? pd.max : 0.0f, "%.3g")) {
            if (ranged) v = std::clamp(v, pd.min, pd.max);
            value = FloatJson(v);
            return true;
        }
        return false;
    }
    case PropType::String: {
        std::string s = value.AsString("");
        bool changed = IsInterpProp(pd.name) ? InterpStringEditor("##v", s, &ui.env) : ui::InputTextStd("##v", s);
        if (changed) value = Json(s);
        return changed;
    }
    case PropType::Code: {
        std::string s = value.AsString("");
        ImFont* mono = MonoFont();
        if (mono) ImGui::PushFont(mono);
        bool changed = ui::InputTextMultilineStd("##v", s, ImVec2(ImGui::CalcItemWidth(), ImGui::GetTextLineHeight() * 8),
                                                 ImGuiInputTextFlags_AllowTabInput);
        if (mono) ImGui::PopFont();
        if (changed) value = Json(s);
        return changed;
    }
    case PropType::Color: {
        if (value.IsNull()) {
            if (NullDefaultButton(pd)) {
                value = Json("#ffffffff");
                return true;
            }
            return false;
        }
        ImVec4 c = JsonColor(value, ImVec4(1, 1, 1, 1));
        if (ColorEditHex("##v", c)) {
            value = ColorJson(c);
            return true;
        }
        return false;
    }
    case PropType::Vec2: {
        if (value.IsNull()) {
            if (NullDefaultButton(pd)) {
                value = Vec2Json(ImVec2(0, 0));
                return true;
            }
            return false;
        }
        ImVec2 v = JsonVec2(value, ImVec2(0, 0));
        if (ImGui::DragFloat2("##v", &v.x, 1.0f, 0, 0, "%.3g")) {
            value = Vec2Json(v);
            return true;
        }
        return false;
    }
    case PropType::Vec4: {
        if (value.IsNull()) {
            if (NullDefaultButton(pd)) {
                value = Vec4Json(ImVec4(0, 0, 0, 0));
                return true;
            }
            return false;
        }
        ImVec4 v = JsonVec4(value, ImVec4(0, 0, 0, 0));
        if (ImGui::DragFloat4("##v", &v.x, 1.0f, 0, 0, "%.3g")) {
            value = Vec4Json(v);
            return true;
        }
        return false;
    }
    case PropType::Size: {
        if (!value.IsObject()) {
            Json o = Json::Object();
            o.Set("w", Json("auto"));
            o.Set("h", Json("auto"));
            value = o;
        }
        return SizePairEditor("##v", value);
    }
    case PropType::Enum: {
        std::vector<std::string> items = EnumValues(pd);
        std::string cur = value.AsString("");
        if (ComboStrings("##v", cur, items, nullptr)) {
            value = Json(cur);
            return true;
        }
        return false;
    }
    case PropType::Flags: {
        std::vector<std::string> flags = JsonStringList(value);
        if (FlagsEditor("##v", flags, pd.enum_values, ui.version)) {
            value = StringListJson(flags);
            return true;
        }
        return false;
    }
    case PropType::StringList: {
        if (value.IsNull()) {
            if (NullDefaultButton(pd)) {
                value = Json::Array();
                return true;
            }
            return false;
        }
        std::vector<std::string> items = JsonStringList(value);
        if (StringListEditor("##v", items)) {
            value = StringListJson(items);
            return true;
        }
        return false;
    }
    case PropType::SizeList: {
        if (value.IsNull()) {
            if (NullDefaultButton(pd)) {
                value = Json::Array();
                return true;
            }
            return false;
        }
        return SizeListEditor("##v", value);
    }
    case PropType::Expr: {
        std::string s = value.IsString() ? value.AsString() : std::string();
        if (ExprEditor("##v", s, &ui.env, expr::Type::Error)) {
            value = Json(s);
            return true;
        }
        return false;
    }
    }
    return false;
}

void DrawProp(PropsUI& ui, const PropDef& pd) {
    Node& node = ui.node;
    ImGui::PushID(pd.name);
    const PropValue* pv = node.Prop(pd.name);
    Json def = PropDefaultJson(pd);
    bool is_set = pv != nullptr;
    bool is_expr = pv && pv->IsExpr();
    bool is_param = pv && pv->IsParam();
    bool differs = is_set && !(pv->raw == def);
    bool unsupported = pd.min_version > ui.version;
    bool show_fx = SupportsExpr(pd.type);
    bool show_param = ui.in_component;
    int trailing = (show_fx ? 1 : 0) + (show_param ? 1 : 0) + 1;

    std::string help = pd.hint ? std::string(pd.hint) + "\n" : std::string();
    help += std::string("type: ") + PropTypeName(pd.type);
    if (pd.min_version > 18500) help += ", ImGui >= " + VersionText(pd.min_version);
    PropRow(pd.name, help.c_str(), trailing);

    std::string edit_label = std::string("Edit ") + pd.name;
    if (unsupported) ImGui::BeginDisabled();
    if (is_param) {
        std::vector<std::string> pnames;
        if (ui.component_doc)
            for (const Param& cp : ui.component_doc->params) pnames.push_back(cp.name);
        std::string cur = pv->Param();
        if (ComboStrings("##param", cur, pnames, nullptr))
            ui.Edit(edit_label.c_str(), [&] { node.SetProp(pd.name, ParamJson(cur)); });
    } else if (is_expr) {
        std::string e = pv->Expr();
        if (ExprEditor("##expr", e, &ui.env, ExpectedFor(pd.type)))
            ui.Edit(edit_label.c_str(), [&] { node.SetProp(pd.name, ExprJson(e)); });
    } else {
        Json value = pv ? pv->raw : def;
        if (LiteralEditor(ui, pd, value)) ui.Edit(edit_label.c_str(), [&] { node.SetProp(pd.name, value); });
    }
    if (unsupported) {
        ImGui::EndDisabled();
        UnsupportedNote(pd.min_version, ui.version);
    }

    if (show_fx) {
        TrailingSameLine();
        if (SmallIconButton("fx", "fx", is_expr ? "Back to a literal value" : "Compute this value with an expression",
                            !unsupported, is_expr)) {
            if (is_expr) {
                ui.Edit(edit_label.c_str(), [&] { node.SetProp(pd.name, def); });
            } else {
                std::string text = LiteralToExprText(pv ? pv->raw : def);
                ui.Edit(edit_label.c_str(), [&] { node.SetProp(pd.name, ExprJson(text)); });
            }
        }
    }
    if (show_param) {
        TrailingSameLine();
        if (SmallIconButton("param", PP_ICON_PARAM, is_param ? "Back to a literal value" : "Use a component parameter",
                            !unsupported, is_param)) {
            if (is_param) {
                ui.Edit(edit_label.c_str(), [&] { node.RemoveProp(pd.name); });
            } else if (ui.component_doc && !ui.component_doc->params.empty()) {
                std::string first = ui.component_doc->params.front().name;
                const char* want = PropTypeName(pd.type);
                for (const Param& cp : ui.component_doc->params) {
                    if (cp.type == want) {
                        first = cp.name;
                        break;
                    }
                }
                ui.Edit(edit_label.c_str(), [&] { node.SetProp(pd.name, ParamJson(first)); });
            }
        }
    }
    TrailingSameLine();
    if (differs) {
        if (ResetButton()) ui.Edit("Reset property", [&] { node.RemoveProp(pd.name); });
    } else {
        ImGui::Dummy(ImVec2(SmallButtonWidth(), SmallButtonWidth()));
    }
    ImGui::PopID();
}

}  // namespace

void DrawPropertiesSection(PropsUI& ui) {
    if (!SectionHeader("Properties", true)) return;
    Node& node = ui.node;
    if (!ui.def) {
        ui::TextDim("Unknown widget type \"%s\": no property schema. Raw values:", node.type.c_str());
    }
    if (!ui::BeginPropertyTable("props")) return;
    if (ui.def) {
        for (int i = 0; i < ui.def->prop_count; ++i) DrawProp(ui, ui.def->props[i]);
    }
    for (size_t i = 0; i < node.props.size(); ++i) {
        const std::string& key = node.props[i].first;
        if (ui.def && FindProp(*ui.def, key.c_str())) continue;
        ImGui::PushID(key.c_str());
        PropRow(key.c_str(), "Not part of this widget's schema", 1);
        std::string dump = DumpJson(node.props[i].second.raw, 0, true);
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s", dump.c_str());
        TrailingSameLine();
        if (SmallIconButton("rm", PP_ICON_REMOVE, "Remove this unknown property")) {
            std::string k = key;
            ui.Edit("Remove property", [&] { node.RemoveProp(k.c_str()); });
            ImGui::PopID();
            break;
        }
        ImGui::PopID();
    }
    ui::EndPropertyTable();
}

}  // namespace imforge::builder::props
