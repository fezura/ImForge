#include <algorithm>
#include <cstdio>

#include "imgui.h"

#include "../ui/expr_editor.h"
#include "properties_shared.h"

namespace imforge::builder::props {

namespace {

void SetBind(Node& node, const char* slot, Json v) {
    for (auto& b : node.bind) {
        if (b.first == slot) {
            b.second.raw = std::move(v);
            return;
        }
    }
    node.bind.emplace_back(std::string(slot), PropValue(std::move(v)));
}

void RemoveBind(Node& node, const char* slot) {
    node.bind.erase(std::remove_if(node.bind.begin(), node.bind.end(),
                                   [&](const std::pair<std::string, PropValue>& b) { return b.first == slot; }),
                    node.bind.end());
}

std::vector<std::string> BindingParams(const PropsUI& ui) {
    std::vector<std::string> out;
    if (!ui.component_doc) return out;
    for (const Param& p : ui.component_doc->params)
        if (p.type.compare(0, 8, "binding<") == 0) out.push_back(p.name);
    return out;
}

EventSpec* FindEvent(Node& node, const char* name) {
    for (auto& e : node.events)
        if (e.first == name) return &e.second;
    return nullptr;
}

EventSpec& EnsureEvent(Node& node, const char* name) {
    if (EventSpec* e = FindEvent(node, name)) return *e;
    node.events.emplace_back(std::string(name), EventSpec());
    return node.events.back().second;
}

void PruneEvent(Node& node, const char* name) {
    node.events.erase(std::remove_if(node.events.begin(), node.events.end(),
                                     [&](const std::pair<std::string, EventSpec>& e) {
                                         return e.first == name && !e.second.handler && e.second.actions.empty();
                                     }),
                      node.events.end());
}

const char* const kActionTypes[] = {"set", "toggle", "openPopup", "closePopup", "setTab", "copyClipboard", "handler"};
const int kActionTypeCount = 7;

int ActionTypeIndex(const std::string& t) {
    for (int i = 0; i < kActionTypeCount; ++i)
        if (t == kActionTypes[i]) return i;
    return -1;
}

bool SetValueEditor(const PathStatus& st, Json& v) {
    CKind k = st.kind == PathStatus::Kind::Ok ? st.type.kind : CKind::Unknown;
    switch (k) {
    case CKind::Bool: {
        bool b = v.AsBool(false);
        if (ImGui::Checkbox("##val", &b)) {
            v = Json(b);
            return true;
        }
        return false;
    }
    case CKind::I8: case CKind::U8: case CKind::I16: case CKind::U16: case CKind::I32: case CKind::U32:
    case CKind::I64: case CKind::U64: {
        int i = v.AsInt(0);
        if (ImGui::InputInt("##val", &i)) {
            v = Json(i);
            return true;
        }
        return false;
    }
    case CKind::Float: case CKind::Double: {
        float f = (float)v.AsDouble(0);
        if (ImGui::InputFloat("##val", &f, 0, 0, "%.3g")) {
            v = FloatJson(f);
            return true;
        }
        return false;
    }
    case CKind::Enum: {
        const EnumInfo* e = st.type.enm;
        std::string ename = e && e->name ? e->name : st.type.name;
        std::vector<std::string> items;
        if (e)
            for (const auto& kv : e->values) items.push_back(ename + "::" + kv.first);
        std::string cur = v.IsObject() ? v["expr"].AsString("") : v.AsString("");
        if (ComboStrings("##val", cur, items, "(choose)")) {
            v = ExprJson(cur);
            return true;
        }
        return false;
    }
    case CKind::Vec2: {
        ImVec2 p = JsonVec2(v, ImVec2(0, 0));
        if (ImGui::DragFloat2("##val", &p.x)) {
            v = Vec2Json(p);
            return true;
        }
        return false;
    }
    case CKind::Vec4: {
        ImVec4 p = JsonVec4(v, ImVec4(0, 0, 0, 0));
        if (ImGui::DragFloat4("##val", &p.x)) {
            v = Vec4Json(p);
            return true;
        }
        return false;
    }
    default: {
        std::string s = v.IsString() ? v.AsString() : (v.IsNull() ? std::string() : DumpJson(v, 0, true));
        if (ui::InputTextStd("##val", s)) {
            v = Json(s);
            return true;
        }
        return false;
    }
    }
}

// value field with an "fx" toggle, `expected` narrows the expression type when we know it
bool ValueWithExprToggle(PropsUI& ui, const char* id, Json& v, const PathStatus* target_status, expr::Type expected) {
    ImGui::PushID(id);
    bool changed = false;
    float w = ImGui::GetContentRegionAvail().x - SmallButtonWidth() - ImGui::GetStyle().ItemInnerSpacing.x;
    ImGui::SetNextItemWidth(std::max(w, 40.0f));
    bool is_expr = v.IsObject() && v["expr"].IsString();
    if (is_expr) {
        std::string e = v["expr"].AsString();
        if (ExprEditor("##expr", e, &ui.env, expected)) {
            v = ExprJson(e);
            changed = true;
        }
    } else if (target_status) {
        changed = SetValueEditor(*target_status, v);
    } else {
        std::string s = v.IsString() ? v.AsString() : std::string();
        if (ui::InputTextStd("##lit", s)) {
            v = Json(s);
            changed = true;
        }
    }
    TrailingSameLine();
    if (SmallIconButton("fx", "fx", is_expr ? "Back to a literal value" : "Compute the value with an expression", true, is_expr)) {
        if (is_expr) v = Json();
        else v = ExprJson(LiteralToExprText(v));
        changed = true;
    }
    ImGui::PopID();
    return changed;
}

expr::Type ExpectedForTarget(const PathStatus& st) {
    if (st.kind != PathStatus::Kind::Ok) return expr::Type::Error;
    switch (st.type.kind) {
    case CKind::Bool: return expr::Type::Bool;
    case CKind::I8: case CKind::U8: case CKind::I16: case CKind::U16: case CKind::I32: case CKind::U32:
    case CKind::I64: case CKind::U64: return expr::Type::Int;
    case CKind::Float: case CKind::Double: return expr::Type::Float;
    case CKind::StdString: case CKind::CharArray: return expr::Type::String;
    default: return expr::Type::Error;
    }
}

void DrawAction(PropsUI& ui, const char* ev, size_t i, size_t count) {
    Node& node = ui.node;
    Action a = FindEvent(node, ev)->actions[i];
    bool changed = false;
    ImGui::PushID((int)i);
    float avail = ImGui::GetContentRegionAvail().x;
    float bw = SmallButtonWidth() + ImGui::GetStyle().ItemInnerSpacing.x;
    ImGui::SetNextItemWidth(std::max(avail - bw * 3, 60.0f));
    int ti = ActionTypeIndex(a.type);
    if (ComboIndex("##type", ti, kActionTypes, kActionTypeCount)) {
        a.type = kActionTypes[ti];
        a.target.clear();
        a.value = Json();
        changed = true;
    }
    TrailingSameLine();
    bool up = SmallIconButton("up", PP_ICON_UP, "Move up", i > 0);
    TrailingSameLine();
    bool down = SmallIconButton("down", PP_ICON_DOWN, "Move down", i + 1 < count);
    TrailingSameLine();
    bool remove = SmallIconButton("rm", PP_ICON_REMOVE, "Remove action");
    if (up || down || remove) {
        ui.Edit("Edit event", [&] {
            EventSpec& es = EnsureEvent(node, ev);
            if (up) std::swap(es.actions[i], es.actions[i - 1]);
            else if (down) std::swap(es.actions[i], es.actions[i + 1]);
            else es.actions.erase(es.actions.begin() + (long)i);
            PruneEvent(node, ev);
        });
        ImGui::PopID();
        return;
    }

    ImGui::Indent(ImGui::GetFrameHeight() * 0.5f);
    if (a.type == "set" || a.type == "toggle") {
        ImGui::SetNextItemWidth(-FLT_MIN);
        PathStatus st;
        if (PathEditor("##target", a.target, &ui.env, a.type == "toggle" ? "bool" : nullptr, &st)) changed = true;
        if (st.kind == PathStatus::Kind::Empty) ui::TextDim("target path");
        else PathStatusBadge(st);
        if (a.type == "set") {
            if (ValueWithExprToggle(ui, "value", a.value, &st, ExpectedForTarget(st))) changed = true;
        }
    } else if (a.type == "openPopup") {
        ImGui::SetNextItemWidth(-FLT_MIN);
        std::vector<std::string> ids = NodeIdsOfType(*ui.root, "Popup|Modal");
        if (ComboStrings("##target", a.target, ids, "(choose a Popup or Modal)")) changed = true;
        if (!a.target.empty() && std::find(ids.begin(), ids.end(), a.target) == ids.end())
            ui::Badge("no such popup", Colors().error);
    } else if (a.type == "setTab") {
        ImGui::SetNextItemWidth(-FLT_MIN);
        std::vector<std::string> bars = NodeIdsOfType(*ui.root, "TabBar");
        if (ComboStrings("##target", a.target, bars, "(choose a TabBar)")) {
            a.value = Json();
            changed = true;
        }
        std::vector<std::string> tabs;
        if (const Node* bar = FindNode(*ui.root, a.target))
            for (const Node& c : bar->children)
                if (c.type == "Tab") tabs.push_back(c.id);
        std::string tab = a.value.AsString("");
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ComboStrings("##tab", tab, tabs, "(choose a Tab)")) {
            a.value = Json(tab);
            changed = true;
        }
    } else if (a.type == "copyClipboard") {
        if (ValueWithExprToggle(ui, "value", a.value, nullptr, expr::Type::String)) changed = true;
    } else if (a.type == "closePopup") {
        ui::TextDim("ImGui::CloseCurrentPopup()");
    } else if (a.type == "handler") {
        ui::TextDim("calls %s()", HandlerName(ui, ev).c_str());
    } else {
        ui::TextDim("unknown action type \"%s\"", a.type.c_str());
    }
    ImGui::Unindent(ImGui::GetFrameHeight() * 0.5f);
    if (changed) {
        ui.Edit("Edit event", [&] {
            EventSpec& es = EnsureEvent(node, ev);
            if (i < es.actions.size()) es.actions[i] = a;
        });
    }
    ImGui::PopID();
}

}  // namespace

void DrawBindingsSection(PropsUI& ui) {
    if (!ui.def || ui.def->bind_count == 0) return;
    if (!SectionHeader("Bindings", true)) return;
    if (!ui::BeginPropertyTable("bindings")) return;
    Node& node = ui.node;
    for (int i = 0; i < ui.def->bind_count; ++i) {
        const BindDef& bd = ui.def->binds[i];
        ImGui::PushID(bd.name);
        const PropValue* pv = node.Bind(bd.name);
        bool is_param = pv && pv->IsParam();
        std::string label = std::string(bd.name) + (bd.required ? " *" : "");
        std::string help = std::string("Accepts: ") + (bd.types ? bd.types : "any") + (bd.required ? "\nRequired" : "");
        int trailing = (ui.in_component ? 1 : 0) + 1;
        PropRow(label.c_str(), help.c_str(), trailing);
        std::string edit_label = std::string("Bind ") + bd.name;
        PathStatus st;
        bool empty = !pv || (!is_param && pv->raw.AsString("").empty());
        if (is_param) {
            std::vector<std::string> pnames = BindingParams(ui);
            std::string cur = pv->Param();
            if (ComboStrings("##param", cur, pnames, nullptr))
                ui.Edit(edit_label.c_str(), [&] { SetBind(node, bd.name, ParamJson(cur)); });
        } else {
            std::string path = pv ? pv->raw.AsString("") : std::string();
            if (PathEditor("##path", path, &ui.env, bd.types, &st)) {
                ui.Edit(edit_label.c_str(), [&] {
                    if (path.empty()) RemoveBind(node, bd.name);
                    else SetBind(node, bd.name, Json(path));
                });
            }
        }
        if (ui.in_component) {
            TrailingSameLine();
            if (SmallIconButton("param", PP_ICON_PARAM, is_param ? "Back to a path" : "Forward a binding<T> parameter", true, is_param)) {
                if (is_param) {
                    ui.Edit(edit_label.c_str(), [&] { RemoveBind(node, bd.name); });
                } else {
                    std::vector<std::string> pnames = BindingParams(ui);
                    if (!pnames.empty()) {
                        std::string first = pnames.front();
                        ui.Edit(edit_label.c_str(), [&] { SetBind(node, bd.name, ParamJson(first)); });
                    } else {
                        ui.ctx.SetStatus("The component has no binding<T> parameter");
                    }
                }
            }
        }
        TrailingSameLine();
        if (SmallIconButton("clear", PP_ICON_REMOVE, "Clear binding", pv != nullptr))
            ui.Edit(edit_label.c_str(), [&] { RemoveBind(node, bd.name); });
        if (is_param) {
            ui::Badge("component parameter", Colors().info);
        } else if (!empty) {
            PathStatusBadge(st);
        } else if (bd.required) {
            ui::Badge("required", Colors().warning);
        }
        ImGui::PopID();
    }
    ui::EndPropertyTable();
}

void DrawEventsSection(PropsUI& ui) {
    if (!ui.def || ui.def->event_count == 0) return;
    Node& node = ui.node;
    if (!SectionHeader("Events", !node.events.empty())) return;
    for (int e = 0; e < ui.def->event_count; ++e) {
        const char* ev = ui.def->events[e];
        ImGui::PushID(ev);
        EventSpec* es = FindEvent(node, ev);
        bool handler = es && es->handler;
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(ev);
        ImGui::SameLine();
        if (ImGui::Checkbox("handler", &handler)) {
            ui.Edit("Edit event", [&] {
                EnsureEvent(node, ev).handler = handler;
                PruneEvent(node, ev);
            });
        }
        std::string fn = HandlerName(ui, ev);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            ImGui::SetTooltip("Generate a call to a user handler:\nvoid %s();", fn.c_str());
        ImGui::SameLine();
        ImFont* mono = MonoFont();
        if (mono) ImGui::PushFont(mono);
        ui::TextDim("%s", fn.c_str());
        if (mono) ImGui::PopFont();

        es = FindEvent(node, ev);
        size_t count = es ? es->actions.size() : 0;
        for (size_t i = 0; i < count; ++i) {
            DrawAction(ui, ev, i, count);
            es = FindEvent(node, ev);
            if (!es || es->actions.size() != count) break;
        }
        if (ImGui::SmallButton(PP_ICON_ADD " Add action")) {
            ui.Edit("Edit event", [&] {
                Action a;
                a.type = "set";
                EnsureEvent(node, ev).actions.push_back(a);
            });
        }
        ImGui::Spacing();
        ImGui::PopID();
    }
    for (const auto& kv : node.events) {
        if (WidgetHasEvent(*ui.def, kv.first.c_str())) continue;
        ImGui::PushID(kv.first.c_str());
        if (SmallIconButton("rm", PP_ICON_REMOVE, "Remove: this widget has no such event")) {
            std::string name = kv.first;
            ui.Edit("Remove event", [&] {
                node.events.erase(std::remove_if(node.events.begin(), node.events.end(),
                                                 [&](const std::pair<std::string, EventSpec>& x) { return x.first == name; }),
                                  node.events.end());
            });
            ImGui::PopID();
            break;
        }
        ImGui::SameLine();
        ui::TextDim("%s (unknown event)", kv.first.c_str());
        ImGui::PopID();
    }
}

}  // namespace imforge::builder::props
