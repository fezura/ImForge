#include "canvas_panel.h"

#if IMFORGE_HAS_INTERP

#include <cfloat>
#include <cmath>
#include <cstring>

#include "../builder_fonts.h"
#include "../ui/color_utils.h"
#include "../ui/project_fonts.h"
#include "../ui/ui_helpers.h"
#include "theme_state.h"

namespace imforge::builder {

namespace {

bool IsBindingParam(const Param& p) { return p.type.compare(0, 8, "binding<") == 0; }

std::vector<std::string> EnumChoices(const Param& p) {
    std::vector<std::string> out;
    if (p.type.compare(0, 5, "enum:") != 0) return out;
    std::string rest = p.type.substr(5);
    size_t start = 0;
    while (start <= rest.size()) {
        size_t comma = rest.find(',', start);
        std::string v = rest.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
        if (!v.empty()) out.push_back(v);
        if (comma == std::string::npos) break;
        start = comma + 1;
    }
    return out;
}

Json DefaultForParam(const Param& p) {
    if (!p.def.IsNull()) return p.def;
    const std::string& t = p.type;
    if (t == "string") return Json(p.name);
    if (t == "int") return Json(0);
    if (t == "float") return Json(0.0);
    if (t == "bool") return Json(false);
    if (t == "color") return Json("#ffffffff");
    if (t == "vec2") {
        Json o = Json::Object();
        o.Set("x", Json(0.0));
        o.Set("y", Json(0.0));
        return o;
    }
    if (t == "vec4") {
        Json o = Json::Object();
        o.Set("x", Json(0.0));
        o.Set("y", Json(0.0));
        o.Set("z", Json(0.0));
        o.Set("w", Json(0.0));
        return o;
    }
    std::vector<std::string> choices = EnumChoices(p);
    if (!choices.empty()) return Json(choices.front());
    return Json(p.name);
}

bool SameFontSource(const ImFont* a, const ImFont* b) {
#if IMGUI_VERSION_NUM >= 19200
    (void)a;
    (void)b;
    return false;
#else
#if IMGUI_VERSION_NUM >= 19190
    const ImFontConfig* ac_ = a ? a->Sources : nullptr;
    const ImFontConfig* bc_ = b ? b->Sources : nullptr;
#else
    const ImFontConfig* ac_ = a ? a->ConfigData : nullptr;
    const ImFontConfig* bc_ = b ? b->ConfigData : nullptr;
#endif
    if (!ac_ || !bc_) return false;
    const char* an = ac_->Name;
    const char* bn = bc_->Name;
    const char* ac = std::strchr(an, ',');
    const char* bc = std::strchr(bn, ',');
    size_t al = ac ? (size_t)(ac - an) : std::strlen(an);
    size_t bl = bc ? (size_t)(bc - bn) : std::strlen(bn);
    return al == bl && al > 0 && std::strncmp(an, bn, al) == 0;
#endif
}

}  // namespace

const Panel* CanvasPanel::ResolvePanel(EditorContext& ctx, std::string& instance_key) {
    instance_key.clear();
    if (Panel* p = ctx.ActivePanel()) {
        instance_key = "panel:" + p->name;
        return p;
    }
    if (Component* c = ctx.ActiveComponent()) {
        BuildComponentPreview(ctx, *c);
        instance_key = "component:" + c->name;
        return &preview_panel_;
    }
    return nullptr;
}

Json CanvasPanel::MockParamValue(const Component& comp, const Param& p) {
    auto ci = mock_params_.find(comp.name);
    if (ci != mock_params_.end()) {
        auto pi = ci->second.find(p.name);
        if (pi != ci->second.end()) return pi->second;
    }
    return DefaultForParam(p);
}

void CanvasPanel::BuildComponentPreview(EditorContext& ctx, const Component& comp) {
    (void)ctx;
    Panel& pv = preview_panel_;
    pv = Panel();
    pv.name = comp.name;
    Node& root = pv.root;
    root.id = "__preview";
    root.type = "Window";
    root.SetProp("title", Json(comp.name));
    Json window_flags = Json::Array();
    window_flags.Push(Json("NoTitleBar"));
    root.SetProp("flags", std::move(window_flags));
    Json size = Json::Object();
    size.Set("x", Json(480));
    size.Set("y", Json(320));
    root.SetProp("initialSize", std::move(size));
    Node inst;
    inst.id = "__inst";
    inst.type = "Component";
    inst.component = comp.name;
    for (const Param& p : comp.params) {
        if (IsBindingParam(p)) continue;
        inst.SetProp(p.name.c_str(), MockParamValue(comp, p));
    }
    for (const std::string& slot : comp.slots) {
        Node ph;
        ph.id = "__slot_" + slot;
        ph.type = "Text";
        ph.SetProp("text", Json("[slot: " + slot + "]"));
        ph.SetProp("disabled", Json(true));
        std::vector<Node> content;
        content.push_back(std::move(ph));
        inst.slots.emplace_back(slot, std::move(content));
    }
    root.children.push_back(std::move(inst));
}

void CanvasPanel::DrawMockParamsPopup(EditorContext& ctx, const Component& comp) {
    (void)ctx;
    if (!ImGui::BeginPopup("##canvas_mock_params")) return;
    ImGui::TextUnformatted("Mock parameters");
    ImGui::SameLine();
    if (ImGui::SmallButton("Reset")) mock_params_.erase(comp.name);
    ImGui::Separator();
    if (comp.params.empty()) ui::TextDim("This component has no parameters.");
    std::map<std::string, Json>& values = mock_params_[comp.name];
    if (ui::BeginPropertyTable("##mock_params", ImGui::GetFontSize() * 7.0f)) {
        for (const Param& p : comp.params) {
            ui::PropertyLabel(p.name.c_str(), p.type.c_str());
            ImGui::PushID(p.name.c_str());
            if (IsBindingParam(p)) {
                ui::TextDim("mock storage (%s)", p.type.c_str());
            } else {
                Json v = MockParamValue(comp, p);
                bool changed = false;
                std::vector<std::string> choices = EnumChoices(p);
                if (p.type == "bool") {
                    bool b = v.AsBool(false);
                    if (ImGui::Checkbox("##v", &b)) {
                        v = Json(b);
                        changed = true;
                    }
                } else if (p.type == "int") {
                    int i = v.AsInt(0);
                    if (ImGui::InputInt("##v", &i)) {
                        v = Json(i);
                        changed = true;
                    }
                } else if (p.type == "float") {
                    float f = (float)v.AsDouble(0);
                    if (ImGui::InputFloat("##v", &f)) {
                        v = Json((double)f);
                        changed = true;
                    }
                } else if (p.type == "color") {
                    ImVec4 col(1, 1, 1, 1);
                    HexToColor(v.AsString(""), col);
                    if (ImGui::ColorEdit4("##v", &col.x, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar)) {
                        v = Json(ColorToHex(col));
                        changed = true;
                    }
                } else if (p.type == "vec2" || p.type == "vec4") {
                    float f[4] = {(float)v["x"].AsDouble(0), (float)v["y"].AsDouble(0), (float)v["z"].AsDouble(0),
                                  (float)v["w"].AsDouble(0)};
                    bool is4 = p.type == "vec4";
                    if (is4 ? ImGui::InputFloat4("##v", f) : ImGui::InputFloat2("##v", f)) {
                        Json o = Json::Object();
                        o.Set("x", Json((double)f[0]));
                        o.Set("y", Json((double)f[1]));
                        if (is4) {
                            o.Set("z", Json((double)f[2]));
                            o.Set("w", Json((double)f[3]));
                        }
                        v = std::move(o);
                        changed = true;
                    }
                } else if (!choices.empty()) {
                    std::string cur = v.AsString(choices.front());
                    if (ImGui::BeginCombo("##v", cur.c_str())) {
                        for (const std::string& c : choices) {
                            if (ImGui::Selectable(c.c_str(), c == cur)) {
                                v = Json(c);
                                changed = true;
                            }
                        }
                        ImGui::EndCombo();
                    }
                } else {
                    std::string s = v.IsString() ? v.AsString() : DumpJson(v);
                    if (ui::InputTextStd("##v", s)) {
                        v = Json(s);
                        changed = true;
                    }
                }
                if (changed) values[p.name] = v;
            }
            ImGui::PopID();
        }
        ui::EndPropertyTable();
    }
    ImGui::EndPopup();
}

StyleScope CanvasPanel::PushCanvasStyle(EditorContext& ctx, float S) {
    StyleScope scope;
    const Theme* theme = GetThemePreviewState().apply_to_canvas ? ctx.ActiveTheme() : nullptr;
    if (theme) {
        for (const auto& c : theme->colors) {
            int idx = ImGuiColIndexByName(c.first.c_str());
            if (idx < 0) continue;
            ImGui::PushStyleColor(idx, ImVec4(c.second.x, c.second.y, c.second.z, c.second.w));
            ++scope.colors;
        }
    }
    size_t n = 0;
    const char* const* names = ImGuiStyleVarNames(n);
    for (size_t i = 0; i < n; ++i) {
        interp_detail::StyleVarRef r = interp_detail::StyleVar(ImGui::GetStyle(), names[i]);
        if (r.var < 0) continue;
        const Json* tv = nullptr;
        if (theme)
            for (const auto& v : theme->vars)
                if (v.first == names[i]) tv = &v.second;
        float k = r.scaled ? S : 1.0f;
        if (r.v) {
            ImVec2 v = *r.v;
            if (tv) {
                float f = 0;
                ImVec2 v2;
                if (StyleVarValue(*tv, true, f, v2)) v = v2;
            }
            ImGui::PushStyleVar(r.var, ImVec2(v.x * k, v.y * k));
        } else {
            float f = *r.f;
            if (tv) {
                ImVec2 v2;
                float tf = 0;
                if (StyleVarValue(*tv, false, tf, v2)) f = tf;
            }
            ImGui::PushStyleVar(r.var, f * k);
        }
        ++scope.vars;
    }
    return scope;
}

ImFont* CanvasPanel::BaseFont(EditorContext& ctx) const {
    for (const FontSpec& fs : ctx.project.fonts)
        if (ImFont* f = FindProjectFont(fs.name)) return f;
    if (ImFont* f = ctx.Font(FontSlot::Ui)) return f;
    return ImGui::GetFont();
}

ImFont* CanvasPanel::PickFont(ImFont* base, float target_px) const {
    if (!base || target_px <= 0.0f) return nullptr;
#if IMGUI_VERSION_NUM >= 19200
    return base;
#else
    if (std::fabs(base->FontSize - target_px) < 0.5f) return base;
    ImFontAtlas* atlas = ImGui::GetIO().Fonts;
    ImFont* best = base;
    float best_err = std::fabs(base->FontSize - target_px);
    for (int i = 0; i < atlas->Fonts.Size; ++i) {
        ImFont* f = atlas->Fonts[i];
        if (f == base || !SameFontSource(f, base)) continue;
        float err = std::fabs(f->FontSize - target_px);
        if (err < best_err) {
            best = f;
            best_err = err;
        }
    }
    return best;
#endif
}

}  // namespace imforge::builder

#endif
