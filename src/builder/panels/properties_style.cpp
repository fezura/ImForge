#include <algorithm>
#include <cstdio>

#include "imgui.h"

#include "../ui/anim_preview_state.h"
#include "../ui/flags_editor.h"
#include "properties_shared.h"

namespace imforge::builder::props {

namespace {

std::vector<std::string> ColNames(const PropsUI& ui) {
    std::vector<std::string> out;
    size_t n = 0;
    const char* const* names = ImGuiColNames(n);
    for (size_t i = 0; i < n; ++i) out.push_back(names[i]);
    if (ui.ctx.project.Feature("docking", false)) {
        names = ImGuiColNamesDocking(n);
        for (size_t i = 0; i < n; ++i) out.push_back(names[i]);
    }
    return out;
}

std::vector<std::string> VarNames(const PropsUI& ui) {
    std::vector<std::string> out;
    size_t n = 0;
    const char* const* names = ImGuiStyleVarNames(n);
    for (size_t i = 0; i < n; ++i) out.push_back(names[i]);
    if (ui.ctx.project.Feature("docking", false)) {
        names = ImGuiStyleVarNamesDocking(n);
        for (size_t i = 0; i < n; ++i) out.push_back(names[i]);
    }
    return out;
}

// only lists unused names; anything newer than the project's ImGui is greyed out
bool AddNameCombo(const char* label, const char* preview, const std::vector<std::string>& names,
                  const std::vector<std::string>& used, int version, int (*min_version)(const char*), std::string& out) {
    bool picked = false;
    if (ImGui::BeginCombo(label, preview)) {
        for (const std::string& n : names) {
            if (std::find(used.begin(), used.end(), n) != used.end()) continue;
            int mv = min_version(n.c_str());
            bool unavailable = mv > version;
            if (unavailable) ImGui::BeginDisabled();
            if (ImGui::Selectable(n.c_str(), false)) {
                out = n;
                picked = true;
            }
            if (unavailable) {
                ImGui::EndDisabled();
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                    ImGui::SetTooltip("Requires Dear ImGui >= %s", VersionText(mv).c_str());
            }
        }
        ImGui::EndCombo();
    }
    return picked;
}

ImVec4 ThemeColor(const PropsUI& ui, const std::string& name) {
    if (Theme* t = ui.ctx.ActiveTheme())
        for (const auto& c : t->colors)
            if (c.first == name) return ToImVec4(c.second);
    int idx = ImGuiColIndex(name.c_str());
    if (idx >= 0 && idx < ImGuiCol_COUNT) return ImGui::GetStyleColorVec4(idx);
    return ImVec4(1, 1, 1, 1);
}

Json ThemeVar(const PropsUI& ui, const std::string& name) {
    if (Theme* t = ui.ctx.ActiveTheme())
        for (const auto& v : t->vars)
            if (v.first == name) return v.second;
    return StyleVarIsVec2(name.c_str()) ? Vec2Json(ImVec2(0, 0)) : FloatJson(0);
}

const char* const kTransitionProps[] = {"alpha", "offset.x", "offset.y", "scale", "rounding", "color", "var"};
const char* const kTransitionOn[] = {"hover", "active", "open", "visible", "focus", "disabled"};
const char* const kEases[] = {"Linear",  "InQuad",     "OutQuad",   "InOutQuad", "InCubic", "OutCubic", "InOutCubic",
                              "OutBack", "InBack",     "OutElastic", "OutBounce", "OutExpo", "Spring"};
const char* const kPresets[] = {"None",      "Fade",         "SlideUp",      "SlideDown", "SlideLeft",
                                "SlideRight", "FadeSlideUp", "FadeSlideDown", "ScalePop",  "Stagger"};

int IndexOf(const char* const* items, int n, const std::string& v) {
    for (int i = 0; i < n; ++i)
        if (v == items[i]) return i;
    return -1;
}

Json DefaultTo(const std::string& prop) {
    if (prop == "alpha" || prop == "scale") return FloatJson(1.0f);
    if (prop.compare(0, 6, "color.") == 0) return Json("#ffffffff");
    if (prop.compare(0, 4, "var.") == 0) return StyleVarIsVec2(prop.c_str() + 4) ? Vec2Json(ImVec2(0, 0)) : FloatJson(0);
    return FloatJson(0);
}

bool TransitionToEditor(const std::string& prop, Json& to) {
    if (prop == "alpha") {
        float v = (float)to.AsDouble(1);
        if (ImGui::SliderFloat("##to", &v, 0.0f, 1.0f, "%.2f")) {
            to = FloatJson(v);
            return true;
        }
        return false;
    }
    if (prop == "scale") {
        float v = (float)to.AsDouble(1);
        if (ImGui::DragFloat("##to", &v, 0.01f, 0.0f, 4.0f, "%.2f")) {
            to = FloatJson(v);
            return true;
        }
        return false;
    }
    if (prop.compare(0, 6, "color.") == 0) {
        ImVec4 c = JsonColor(to, ImVec4(1, 1, 1, 1));
        if (ColorEditHex("##to", c)) {
            to = ColorJson(c);
            return true;
        }
        return false;
    }
    if (prop.compare(0, 4, "var.") == 0 && StyleVarIsVec2(prop.c_str() + 4)) {
        ImVec2 v = JsonVec2(to, ImVec2(0, 0));
        if (ImGui::DragFloat2("##to", &v.x, 0.5f, 0, 0, "%.1f")) {
            to = Vec2Json(v);
            return true;
        }
        return false;
    }
    float v = (float)to.AsDouble(0);
    if (ImGui::DragFloat("##to", &v, 0.5f, 0, 0, "%.1f")) {
        to = FloatJson(v);
        return true;
    }
    return false;
}

void DrawTransition(PropsUI& ui, size_t i) {
    Node& node = ui.node;
    Transition t = node.anim.transitions[i];
    size_t count = node.anim.transitions.size();
    bool changed = false;
    ImGui::PushID((int)i);
    const ImGuiStyle& st = ImGui::GetStyle();
    float avail = ImGui::GetContentRegionAvail().x;
    float bw = SmallButtonWidth() + st.ItemInnerSpacing.x;
    float w = std::max(avail - bw * 4, 80.0f);

    int cat = 0;
    std::string sub;
    if (t.prop.compare(0, 6, "color.") == 0) {
        cat = 5;
        sub = t.prop.substr(6);
    } else if (t.prop.compare(0, 4, "var.") == 0) {
        cat = 6;
        sub = t.prop.substr(4);
    } else {
        cat = IndexOf(kTransitionProps, 5, t.prop);
    }
    bool has_sub = cat == 5 || cat == 6;
    ImGui::SetNextItemWidth(has_sub ? w * 0.4f : w);
    if (ComboIndex("##cat", cat, kTransitionProps, 7)) {
        if (cat == 5) t.prop = "color.Button";
        else if (cat == 6) t.prop = "var.FrameRounding";
        else t.prop = cat >= 0 ? kTransitionProps[cat] : "alpha";
        t.to = DefaultTo(t.prop);
        changed = true;
    }
    if (has_sub) {
        ImGui::SameLine(0, st.ItemInnerSpacing.x);
        ImGui::SetNextItemWidth(w * 0.6f - st.ItemInnerSpacing.x);
        std::vector<std::string> names = cat == 5 ? ColNames(ui) : VarNames(ui);
        if (ComboStrings("##sub", sub, names, nullptr)) {
            t.prop = (cat == 5 ? "color." : "var.") + sub;
            t.to = DefaultTo(t.prop);
            changed = true;
        }
    }
    TrailingSameLine();
    if (SmallIconButton("prev", PP_ICON_PLAY, "Preview this transition on the Canvas"))
        RequestAnimPreview(ui.doc_name, node.id, AnimPreviewState::Kind::Transition, (int)i, ui.ctx.time);
    TrailingSameLine();
    bool up = SmallIconButton("up", PP_ICON_UP, "Move up", i > 0);
    TrailingSameLine();
    bool down = SmallIconButton("down", PP_ICON_DOWN, "Move down", i + 1 < count);
    TrailingSameLine();
    bool remove = SmallIconButton("rm", PP_ICON_REMOVE, "Remove transition");
    if (up || down || remove) {
        ui.Edit("Edit animation", [&] {
            auto& trs = node.anim.transitions;
            if (up) std::swap(trs[i], trs[i - 1]);
            else if (down) std::swap(trs[i], trs[i + 1]);
            else trs.erase(trs.begin() + (long)i);
        });
        ImGui::PopID();
        return;
    }

    ImGui::Indent(ImGui::GetFrameHeight() * 0.5f);
    float half = (ImGui::GetContentRegionAvail().x - st.ItemInnerSpacing.x) * 0.5f;
    ImGui::AlignTextToFramePadding();
    ui::TextDim("on");
    ImGui::SameLine();
    int on = IndexOf(kTransitionOn, 6, t.on);
    ImGui::SetNextItemWidth(half - ImGui::CalcTextSize("on ").x);
    if (ComboIndex("##on", on, kTransitionOn, 6)) {
        t.on = kTransitionOn[on];
        changed = true;
    }
    ImGui::SameLine(0, st.ItemInnerSpacing.x);
    ImGui::AlignTextToFramePadding();
    ui::TextDim("to");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (TransitionToEditor(t.prop, t.to)) changed = true;

    int ease = IndexOf(kEases, 13, t.ease);
    bool spring = t.ease == "Spring";
    ImGui::SetNextItemWidth(half);
    if (ComboIndex("##ease", ease, kEases, 13)) {
        t.ease = kEases[ease];
        if (t.ease == "Spring") {
            if (t.stiffness <= 0) t.stiffness = 170.0f;
            if (t.damping <= 0) t.damping = 26.0f;
        }
        changed = true;
    }
    ImGui::SameLine(0, st.ItemInnerSpacing.x);
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::DragFloat("##dur", &t.duration, 0.01f, 0.0f, 10.0f, "%.2f s")) changed = true;
    if (spring) {
        ImGui::SetNextItemWidth(half);
        if (ImGui::DragFloat("##stiff", &t.stiffness, 1.0f, 1.0f, 2000.0f, "k %.0f")) changed = true;
        ui::Tooltip("Spring stiffness");
        ImGui::SameLine(0, st.ItemInnerSpacing.x);
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::DragFloat("##damp", &t.damping, 0.5f, 0.0f, 200.0f, "c %.1f")) changed = true;
        ui::Tooltip("Spring damping");
    }
    ImGui::Unindent(ImGui::GetFrameHeight() * 0.5f);
    if (changed) ui.Edit("Edit animation", [&] { node.anim.transitions[i] = t; });
    ImGui::PopID();
}

}  // namespace

void DrawStyleSection(PropsUI& ui) {
    Node& node = ui.node;
    if (!SectionHeader("Style", ui.multi || !node.style.Empty())) return;
    if (!ui::BeginPropertyTable("style")) return;
    StyleOverride& s = node.style;
    int version = ui.version;

    PropRow("Colors", "Per-widget ImGuiCol overrides (PushStyleColor).", 0);
    {
        std::vector<std::string> used;
        for (const auto& c : s.colors) used.push_back(c.first);
        std::string pick;
        if (AddNameCombo("##addcol", PP_ICON_ADD " add color", ColNames(ui), used, version, ImGuiColMinVersion, pick)) {
            ImVec4 def = ThemeColor(ui, pick);
            ui.Edit("Add style color", [&] { s.colors.emplace_back(pick, ToVec4(def)); });
        }
    }
    for (size_t i = 0; i < s.colors.size(); ++i) {
        ImGui::PushID((int)i);
        std::string name = s.colors[i].first;
        std::string label = "  " + name;
        PropRow(label.c_str(), nullptr, 1);
        ImVec4 c = ToImVec4(s.colors[i].second);
        if (ColorEditHex("##c", c)) ui.Edit("Edit style color", [&] { s.colors[i].second = ToVec4(c); });
        int mv = ImGuiColMinVersion(name.c_str());
        if (mv == 0 || mv > version) {
            ImGui::SameLine();
            ui::Badge(mv == 0 ? "unknown" : "newer ImGui", Colors().warning);
        }
        TrailingSameLine();
        if (SmallIconButton("rm", PP_ICON_REMOVE, "Remove override")) {
            ui.Edit("Remove style color", [&] { s.colors.erase(s.colors.begin() + (long)i); });
            ImGui::PopID();
            break;
        }
        ImGui::PopID();
    }

    PropRow("Vars", "Per-widget ImGuiStyleVar overrides (PushStyleVar).", 0);
    {
        std::vector<std::string> used;
        for (const auto& v : s.vars) used.push_back(v.first);
        std::string pick;
        if (AddNameCombo("##addvar", PP_ICON_ADD " add var", VarNames(ui), used, version, ImGuiStyleVarMinVersion, pick)) {
            Json def = ThemeVar(ui, pick);
            ui.Edit("Add style var", [&] { s.vars.emplace_back(pick, def); });
        }
    }
    for (size_t i = 0; i < s.vars.size(); ++i) {
        ImGui::PushID((int)i);
        std::string name = s.vars[i].first;
        std::string label = "  " + name;
        PropRow(label.c_str(), nullptr, 1);
        if (StyleVarIsVec2(name.c_str())) {
            ImVec2 v = JsonVec2(s.vars[i].second, ImVec2(0, 0));
            if (ImGui::DragFloat2("##v", &v.x, 0.5f, 0, 0, "%.1f")) ui.Edit("Edit style var", [&] { s.vars[i].second = Vec2Json(v); });
        } else {
            float v = (float)s.vars[i].second.AsDouble(0);
            if (ImGui::DragFloat("##v", &v, 0.5f, 0, 0, "%.1f")) ui.Edit("Edit style var", [&] { s.vars[i].second = FloatJson(v); });
        }
        int mv = ImGuiStyleVarMinVersion(name.c_str());
        if (mv == 0 || mv > version) {
            ImGui::SameLine();
            ui::Badge(mv == 0 ? "unknown" : "newer ImGui", Colors().warning);
        }
        TrailingSameLine();
        if (SmallIconButton("rm", PP_ICON_REMOVE, "Remove override")) {
            ui.Edit("Remove style var", [&] { s.vars.erase(s.vars.begin() + (long)i); });
            ImGui::PopID();
            break;
        }
        ImGui::PopID();
    }

    PropRow("Classes", "Style classes from the project (Project Settings > Style classes).", 0);
    {
        std::vector<std::string> options;
        for (const auto& sc : ui.ctx.project.style_classes) options.push_back(sc.first);
        std::vector<std::string> classes = s.classes;
        if (MultiSelectEditor("##classes", classes, options)) ui.Edit("Edit style classes", [&] { s.classes = classes; });
    }
    ui::EndPropertyTable();
}

void DrawAnimSection(PropsUI& ui) {
    Node& node = ui.node;
    if (!SectionHeader("Animation", ui.multi || !node.anim.Empty())) return;
    AnimSpec& an = node.anim;
    if (!ui.ctx.project.Feature("animations", true)) {
        ImGui::PushStyleColor(ImGuiCol_Text, Colors().warning);
        ImGui::TextWrapped("The animations feature is disabled in Project Settings; these settings are kept but not emitted.");
        ImGui::PopStyleColor();
    }
    ui::TextDim("Transitions");
    for (size_t i = 0; i < an.transitions.size(); ++i) {
        DrawTransition(ui, i);
        if (i >= node.anim.transitions.size()) break;
        ImGui::Separator();
    }
    if (ImGui::SmallButton(PP_ICON_ADD " Add transition")) {
        ui.Edit("Edit animation", [&] {
            Transition t;
            t.prop = "alpha";
            t.on = "hover";
            t.to = FloatJson(1.0f);
            an.transitions.push_back(t);
        });
    }
    ImGui::Spacing();
    if (!ui::BeginPropertyTable("enterexit")) return;
    PropRow("Enter", "Preset played when the widget appears.", 1);
    {
        int idx = an.enter_preset.empty() ? 0 : IndexOf(kPresets, 10, an.enter_preset);
        if (ComboIndex("##enter", idx, kPresets, 10))
            ui.Edit("Edit animation", [&] { an.enter_preset = idx <= 0 ? std::string() : kPresets[idx]; });
        TrailingSameLine();
        if (SmallIconButton("prevEnter", PP_ICON_PLAY, "Preview the enter animation", !an.enter_preset.empty()))
            RequestAnimPreview(ui.doc_name, node.id, AnimPreviewState::Kind::Enter, -1, ui.ctx.time);
    }
    PropRow("  duration", nullptr, 0);
    {
        float d = an.enter_duration;
        if (ImGui::DragFloat("##enterdur", &d, 0.01f, 0.0f, 10.0f, "%.2f s")) ui.Edit("Edit animation", [&] { an.enter_duration = d; });
    }
    PropRow("  stagger", "Delay added per child when a container enters.", 0);
    {
        float sg = an.stagger;
        if (ImGui::DragFloat("##stagger", &sg, 0.005f, 0.0f, 2.0f, "%.3f s")) ui.Edit("Edit animation", [&] { an.stagger = sg; });
    }
    PropRow("Exit", "Preset played when the widget disappears.", 1);
    {
        int idx = an.exit_preset.empty() ? 0 : IndexOf(kPresets, 10, an.exit_preset);
        if (ComboIndex("##exit", idx, kPresets, 10))
            ui.Edit("Edit animation", [&] { an.exit_preset = idx <= 0 ? std::string() : kPresets[idx]; });
        TrailingSameLine();
        if (SmallIconButton("prevExit", PP_ICON_PLAY, "Preview the exit animation", !an.exit_preset.empty()))
            RequestAnimPreview(ui.doc_name, node.id, AnimPreviewState::Kind::Exit, -1, ui.ctx.time);
    }
    PropRow("  duration", nullptr, 0);
    {
        float d = an.exit_duration;
        if (ImGui::DragFloat("##exitdur", &d, 0.01f, 0.0f, 10.0f, "%.2f s")) ui.Edit("Edit animation", [&] { an.exit_duration = d; });
    }
    ui::EndPropertyTable();
}

}  // namespace imforge::builder::props
