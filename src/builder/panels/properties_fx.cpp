#include <algorithm>
#include <cstdio>

#include "imgui.h"

#include "properties_shared.h"

namespace imforge::builder::props {

namespace {

struct FxType {
    const char* type;
    const char* note;
    bool shader;
};

const FxType kFxTypes[] = {
    {"gradient", "Linear gradient fill behind the widget", false},
    {"radialGradient", "Radial gradient fill behind the widget", false},
    {"shadow", "Soft drop shadow", false},
    {"glow", "Outer glow", false},
    {"border", "Animated border drawn over the widget", false},
    {"particles", "Floating particles behind the widget", false},
    {"noise", "Grain / noise texture", false},
    {"cursorHighlight", "Radial highlight that follows the mouse", false},
    {"blur", "Background blur (shader fx)", true},
};
const int kFxTypeCount = 9;

int FxIndex(const std::string& t) {
    for (int i = 0; i < kFxTypeCount; ++i)
        if (t == kFxTypes[i].type) return i;
    return -1;
}

bool FloatParam(Json& p, const char* key, const char* label, float def, float speed, float mn, float mx, const char* fmt = "%.1f") {
    PropRow(label, nullptr, 0);
    float v = (float)p[key].AsDouble(def);
    if (ImGui::DragFloat("##f", &v, speed, mn, mx, fmt)) {
        p.Set(key, FloatJson(v));
        return true;
    }
    return false;
}

bool IntParam(Json& p, const char* key, const char* label, int def, int mn, int mx) {
    PropRow(label, nullptr, 0);
    int v = p[key].AsInt(def);
    if (ImGui::DragInt("##i", &v, 1.0f, mn, mx)) {
        p.Set(key, Json(v));
        return true;
    }
    return false;
}

bool BoolParam(Json& p, const char* key, const char* label, bool def) {
    PropRow(label, nullptr, 0);
    bool v = p[key].AsBool(def);
    if (ImGui::Checkbox("##b", &v)) {
        p.Set(key, Json(v));
        return true;
    }
    return false;
}

bool ColorParam(Json& p, const char* key, const char* label, const ImVec4& def) {
    PropRow(label, nullptr, 0);
    ImVec4 c = JsonColor(p[key], def);
    if (ColorEditHex("##c", c)) {
        p.Set(key, ColorJson(c));
        return true;
    }
    return false;
}

bool Vec2Param(Json& p, const char* key, const char* label, const ImVec2& def) {
    PropRow(label, nullptr, 0);
    ImVec2 v = JsonVec2(p[key], def);
    if (ImGui::DragFloat2("##v2", &v.x, 0.5f, 0, 0, "%.1f")) {
        p.Set(key, Vec2Json(v));
        return true;
    }
    return false;
}

bool EnumParam(Json& p, const char* key, const char* label, const char* const* items, int n, const char* def) {
    PropRow(label, nullptr, 0);
    std::string cur = p[key].AsString(def);
    std::vector<std::string> vec(items, items + n);
    if (ComboStrings("##e", cur, vec, nullptr)) {
        p.Set(key, Json(cur));
        return true;
    }
    return false;
}

bool OptionalFloatParam(Json& p, const char* key, const char* label, float def, float speed, float mn, float mx,
                        const char* fmt = "%.1f") {
    ImGui::PushID(key);
    PropRow(label, "Unchecked = inherit from the widget's rounding", 0);
    bool has = p.Has(key);
    bool changed = false;
    if (ImGui::Checkbox("##has", &has)) {
        if (has) p.Set(key, FloatJson(def));
        else p.Erase(key);
        changed = true;
    }
    if (has) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-FLT_MIN);
        float v = (float)p[key].AsDouble(def);
        if (ImGui::DragFloat("##f", &v, speed, mn, mx, fmt)) {
            p.Set(key, FloatJson(v));
            changed = true;
        }
    }
    ImGui::PopID();
    return changed;
}

const char* const kBorderStyles[] = {"Solid", "Rainbow", "Sweep", "Pulse"};

bool DrawFxParams(const std::string& type, Json& p) {
    bool c = false;
    if (type == "gradient") {
        c |= ColorParam(p, "from", "from", ImVec4(1, 1, 1, 0.125f));
        c |= ColorParam(p, "to", "to", ImVec4(0, 0, 0, 0.125f));
        c |= BoolParam(p, "vertical", "vertical", true);
        c |= OptionalFloatParam(p, "rounding", "rounding", 0.0f, 0.5f, 0.0f, 64.0f);
    } else if (type == "radialGradient") {
        c |= ColorParam(p, "inner", "inner", ImVec4(1, 1, 1, 0.25f));
        c |= ColorParam(p, "outer", "outer", ImVec4(0, 0, 0, 0));
        c |= OptionalFloatParam(p, "radius", "radius", 64.0f, 1.0f, 1.0f, 2000.0f, "%.0f px");
    } else if (type == "shadow") {
        c |= FloatParam(p, "size", "size", 8.0f, 0.5f, 0.0f, 128.0f, "%.1f px");
        c |= ColorParam(p, "color", "color", ImVec4(0, 0, 0, 0.5f));
        c |= Vec2Param(p, "offset", "offset", ImVec2(0, 2));
        c |= OptionalFloatParam(p, "rounding", "rounding", 0.0f, 0.5f, 0.0f, 64.0f);
    } else if (type == "glow") {
        c |= FloatParam(p, "size", "size", 8.0f, 0.5f, 0.0f, 128.0f, "%.1f px");
        c |= ColorParam(p, "color", "color", ImVec4(1, 1, 1, 0.375f));
        c |= OptionalFloatParam(p, "rounding", "rounding", 0.0f, 0.5f, 0.0f, 64.0f);
    } else if (type == "border") {
        c |= EnumParam(p, "style", "style", kBorderStyles, 4, "Solid");
        c |= FloatParam(p, "thickness", "thickness", 1.0f, 0.1f, 0.1f, 16.0f, "%.1f px");
        c |= ColorParam(p, "color", "color", ImVec4(1, 1, 1, 1));
        c |= FloatParam(p, "speed", "speed", 1.0f, 0.05f, 0.0f, 10.0f, "%.2f");
        c |= OptionalFloatParam(p, "rounding", "rounding", 0.0f, 0.5f, 0.0f, 64.0f);
    } else if (type == "particles") {
        c |= IntParam(p, "count", "count", 40, 1, 500);
        c |= FloatParam(p, "speed", "speed", 20.0f, 0.5f, 0.0f, 500.0f, "%.0f px/s");
        c |= FloatParam(p, "size", "size", 2.0f, 0.1f, 0.5f, 32.0f, "%.1f px");
        c |= ColorParam(p, "color", "color", ImVec4(1, 1, 1, 0.25f));
        c |= BoolParam(p, "connect", "connect", false);
        if (p["connect"].AsBool(false)) c |= FloatParam(p, "connectDist", "connect dist", 80.0f, 1.0f, 1.0f, 500.0f, "%.0f px");
    } else if (type == "noise") {
        c |= ColorParam(p, "color", "color", ImVec4(1, 1, 1, 0.094f));
        c |= FloatParam(p, "density", "density", 0.3f, 0.01f, 0.0f, 1.0f, "%.2f");
    } else if (type == "cursorHighlight") {
        c |= ColorParam(p, "color", "color", ImVec4(1, 1, 1, 0.157f));
        c |= FloatParam(p, "radius", "radius", 80.0f, 1.0f, 1.0f, 1000.0f, "%.0f px");
        c |= OptionalFloatParam(p, "rounding", "rounding", 0.0f, 0.5f, 0.0f, 64.0f);
    } else if (type == "blur") {
        c |= FloatParam(p, "radius", "radius", 8.0f, 0.25f, 0.0f, 64.0f, "%.1f px");
        c |= OptionalFloatParam(p, "rounding", "rounding", 0.0f, 0.5f, 0.0f, 64.0f);
        c |= ColorParam(p, "tint", "tint", ImVec4(1, 1, 1, 1));
        c |= FloatParam(p, "saturation", "saturation", 1.0f, 0.01f, 0.0f, 3.0f, "%.2f");
        c |= FloatParam(p, "brightness", "brightness", 1.0f, 0.01f, 0.0f, 3.0f, "%.2f");
        c |= FloatParam(p, "noise", "noise", 0.0f, 0.005f, 0.0f, 1.0f, "%.3f");
    } else {
        PropRow("params", nullptr, 0);
        ImGui::TextDisabled("%s", DumpJson(p, 0, true).c_str());
    }
    return c;
}

Json DefaultParams(const std::string& type) {
    Json p = Json::Object();
    if (type == "shadow") {
        p.Set("size", FloatJson(12));
        p.Set("color", Json("#00000080"));
    } else if (type == "glow") {
        p.Set("size", FloatJson(8));
        p.Set("color", Json("#4f8cff60"));
    } else if (type == "gradient") {
        p.Set("from", Json("#ffffff20"));
        p.Set("to", Json("#00000020"));
        p.Set("vertical", Json(true));
    } else if (type == "border") {
        p.Set("style", Json("Sweep"));
        p.Set("thickness", FloatJson(1.5f));
        p.Set("color", Json("#4f8cffaa"));
    } else if (type == "blur") {
        p.Set("radius", FloatJson(8));
    } else if (type == "particles") {
        p.Set("count", Json(40));
    } else if (type == "cursorHighlight") {
        p.Set("radius", FloatJson(80));
    }
    return p;
}

void DrawFx(PropsUI& ui, size_t i, bool drawlist_on, bool shader_on) {
    Node& node = ui.node;
    FxSpec f = node.fx[i];
    size_t count = node.fx.size();
    bool changed = false;
    ImGui::PushID((int)i);
    float avail = ImGui::GetContentRegionAvail().x;
    float bw = SmallButtonWidth() + ImGui::GetStyle().ItemInnerSpacing.x;
    ImGui::SetNextItemWidth(std::max(avail - bw * 3, 80.0f));
    int ti = FxIndex(f.type);
    if (ImGui::BeginCombo("##type", ti >= 0 ? kFxTypes[ti].type : f.type.c_str())) {
        for (int k = 0; k < kFxTypeCount; ++k) {
            bool sel = k == ti;
            if (ImGui::Selectable(kFxTypes[k].type, sel) && !sel) {
                f.type = kFxTypes[k].type;
                f.params = DefaultParams(f.type);
                changed = true;
            }
            ui::Tooltip(kFxTypes[k].note);
        }
        ImGui::EndCombo();
    }
    TrailingSameLine();
    bool up = SmallIconButton("up", PP_ICON_UP, "Move up (drawn first)", i > 0);
    TrailingSameLine();
    bool down = SmallIconButton("down", PP_ICON_DOWN, "Move down (drawn last)", i + 1 < count);
    TrailingSameLine();
    bool remove = SmallIconButton("rm", PP_ICON_REMOVE, "Remove effect");
    if (up || down || remove) {
        ui.Edit("Edit effects", [&] {
            if (up) std::swap(node.fx[i], node.fx[i - 1]);
            else if (down) std::swap(node.fx[i], node.fx[i + 1]);
            else node.fx.erase(node.fx.begin() + (long)i);
        });
        ImGui::PopID();
        return;
    }
    bool shader = ti >= 0 && kFxTypes[ti].shader;
    if (shader && !shader_on) {
        ImGui::PushStyleColor(ImGuiCol_Text, Colors().warning);
        ImGui::TextWrapped("blur requires the shaderFx feature (Project Settings > Features) and an imb_fx backend in the app.");
        ImGui::PopStyleColor();
    } else if (!shader && !drawlist_on) {
        ImGui::PushStyleColor(ImGuiCol_Text, Colors().warning);
        ImGui::TextWrapped("DrawList effects require the drawlistFx feature (Project Settings > Features).");
        ImGui::PopStyleColor();
    }
    if (!f.params.IsObject()) f.params = Json::Object();
    if (ui::BeginPropertyTable("fxparams")) {
        if (DrawFxParams(f.type, f.params)) changed = true;
        ui::EndPropertyTable();
    }
    if (changed) ui.Edit("Edit effects", [&] { node.fx[i] = f; });
    ImGui::PopID();
}

}  // namespace

void DrawFxSection(PropsUI& ui) {
    Node& node = ui.node;
    if (!SectionHeader("Effects", ui.multi || !node.fx.empty())) return;
    bool drawlist_on = ui.ctx.project.Feature("drawlistFx", true);
    bool shader_on = ui.ctx.project.Feature("shaderFx", false);
    for (size_t i = 0; i < node.fx.size(); ++i) {
        DrawFx(ui, i, drawlist_on, shader_on);
        if (i >= node.fx.size()) break;
        ImGui::Separator();
    }
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::BeginCombo("##addfx", PP_ICON_ADD " Add effect")) {
        for (int k = 0; k < kFxTypeCount; ++k) {
            bool unavailable = kFxTypes[k].shader ? !shader_on : !drawlist_on;
            if (ImGui::Selectable(kFxTypes[k].type, false)) {
                std::string type = kFxTypes[k].type;
                ui.Edit("Add effect", [&] {
                    FxSpec f;
                    f.type = type;
                    f.params = DefaultParams(type);
                    node.fx.push_back(f);
                });
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s%s", kFxTypes[k].note,
                                  unavailable ? (kFxTypes[k].shader ? "\n(needs the shaderFx feature)" : "\n(needs the drawlistFx feature)") : "");
        }
        ImGui::EndCombo();
    }
}

}  // namespace imforge::builder::props
