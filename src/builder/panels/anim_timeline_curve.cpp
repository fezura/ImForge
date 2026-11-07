#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "imgui.h"

#include "imforge/imb_runtime.h"

#include "../builder_style.h"
#include "../editor_context.h"
#include "../ui/anim_preview_driver.h"
#include "../ui/color_utils.h"
#include "../ui/ease_curves.h"
#include "../ui/ui_helpers.h"
#include "anim_timeline_editors.h"

namespace imforge::builder::anim_timeline {

namespace {

const float kPlotHeight = 150.0f;
const float kPreviewHeight = 72.0f;
const float kPresetListWidth = 120.0f;
const float kMinStiffness = 10.0f;
const float kMaxStiffness = 1000.0f;
const float kMinDamping = 1.0f;
const float kMaxDamping = 100.0f;
const float kExitHoldSeconds = 0.6f;
const float kExitEnterSeconds = 0.12f;

std::string ToText(const Json& to) {
    if (to.IsString()) return to.AsString();
    if (to.IsNumber()) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%g", to.AsDouble());
        return buf;
    }
    if (to.IsObject() && to.Has("x")) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%g, %g", to["x"].AsDouble(), to["y"].AsDouble());
        return buf;
    }
    return std::string();
}

Json FromText(const std::string& text, const Json& previous) {
    if (text.empty()) return previous;
    if (text[0] == '#') return Json(text);
    char* end = nullptr;
    double v = std::strtod(text.c_str(), &end);
    if (end && *end == '\0') return Json(v);
    if (end && *end == ',') {
        double y = std::strtod(end + 1, nullptr);
        Json o = Json::Object();
        o.Set("x", Json(v));
        o.Set("y", Json(y));
        return o;
    }
    return Json(text);
}

void DrawPlayheadDot(ImDrawList* dl, const ImVec2& min, const ImVec2& max, const Node& node, float length, float value_at_t,
                     float t) {
    const AnimPreviewDriver& d = GetAnimPreviewDriver();
    if (length <= 0.0f || d.node_id != node.id) return;
    (void)t;
    ImVec2 p = ui::CurvePoint(min, max, std::min(d.time / length, 1.0f), value_at_t);
    dl->AddCircleFilled(p, 4.0f, ToU32(Colors().accent));
    dl->AddCircle(p, 4.0f, ToU32(Colors().text), 0, 1.0f);
}

void DrawTransitionControls(EditorContext& ctx, Node& node, int index, int& selected_transition) {
    Transition& tr = node.anim.transitions[(size_t)index];
    if (!ui::BeginPropertyTable("transition_props", ImGui::GetFontSize() * 5.0f)) return;

    ui::PropertyLabel("prop", "alpha, offset.x, offset.y, scale, rounding, border, color.<ImGuiCol>, var.<StyleVar>");
    std::string prop = tr.prop;
    if (ui::InputTextStd("##prop", prop, ImGuiInputTextFlags_EnterReturnsTrue) && !prop.empty())
        ctx.Edit("Change transition prop", [&] { tr.prop = prop; });

    ui::PropertyLabel("on");
    int trig = TransitionTriggerIndex(tr.on.c_str());
    if (ImGui::BeginCombo("##on", tr.on.empty() ? "hover" : tr.on.c_str())) {
        for (int i = 0; i < kTransitionTriggerCount; ++i) {
            if (ImGui::Selectable(TransitionTriggerName(i), trig == i))
                ctx.Edit("Change transition trigger", [&] { tr.on = TransitionTriggerName(i); });
        }
        ImGui::EndCombo();
    }

    ui::PropertyLabel("to", "Number, #rrggbbaa color or \"x, y\"");
    std::string to = ToText(tr.to);
    if (ui::InputTextStd("##to", to, ImGuiInputTextFlags_EnterReturnsTrue))
        ctx.Edit("Change transition target", [&] { tr.to = FromText(to, tr.to); });

    ui::PropertyLabel("ease");
    bool spring = IsSpring(tr);
    const char* current = spring ? "Spring" : EaseName(EaseFromName(tr.ease.c_str()));
    if (ImGui::BeginCombo("##ease", current)) {
        for (int i = 0; i < kEaseCount; ++i) {
            imb::Ease e = static_cast<imb::Ease>(i);
            bool sel = !spring && EaseFromName(tr.ease.c_str()) == e;
            ImVec2 p = ImGui::GetCursorScreenPos();
            if (ImGui::Selectable(EaseName(e), sel, 0, ImVec2(0, ImGui::GetTextLineHeight())))
                ctx.Edit("Change transition ease", [&] {
                    tr.ease = EaseName(e);
                    tr.stiffness = tr.damping = 0.0f;
                });
            float w = ImGui::GetContentRegionAvail().x;
            ImVec2 tmin(p.x + w - 30.0f, p.y + 1.0f), tmax(p.x + w - 2.0f, p.y + ImGui::GetTextLineHeight() - 1.0f);
            ui::DrawEaseCurve(ImGui::GetWindowDrawList(), tmin, tmax, e, ToU32(Colors().text_dim), 1.0f, 20);
        }
        if (ImGui::Selectable("Spring", spring))
            ctx.Edit("Change transition ease", [&] {
                tr.ease = "Spring";
                if (tr.stiffness <= 0.0f) tr.stiffness = 170.0f;
                if (tr.damping <= 0.0f) tr.damping = 26.0f;
            });
        ImGui::EndCombo();
    }

    if (spring) {
        ui::PropertyLabel("stiffness");
        float k = tr.stiffness > 0 ? tr.stiffness : 170.0f;
        if (ImGui::SliderFloat("##stiff", &k, kMinStiffness, kMaxStiffness, "%.0f", ImGuiSliderFlags_Logarithmic))
            ctx.Edit("Change spring stiffness", [&] { tr.stiffness = k; });
        ui::PropertyLabel("damping");
        float dmp = tr.damping > 0 ? tr.damping : 26.0f;
        if (ImGui::SliderFloat("##damp", &dmp, kMinDamping, kMaxDamping, "%.1f"))
            ctx.Edit("Change spring damping", [&] { tr.damping = dmp; });
        ui::PropertyLabel("settles");
        ui::SpringParams sp;
        sp.stiffness = k;
        sp.damping = dmp;
        ImGui::Text("~%.2f s", (double)ui::SpringSettleTime(sp));
    } else {
        ui::PropertyLabel("duration");
        float dur = tr.duration;
        if (ImGui::DragFloat("##dur", &dur, 0.005f, 0.0f, 10.0f, "%.2f s"))
            ctx.Edit("Change animation duration", [&] { tr.duration = std::max(0.0f, dur); });
    }
    ui::EndPropertyTable();

    if (ImGui::SmallButton("Remove transition")) {
        ctx.Edit("Remove transition", [&] { node.anim.transitions.erase(node.anim.transitions.begin() + index); });
        selected_transition = node.anim.transitions.empty() ? -1 : std::min(index, (int)node.anim.transitions.size() - 1);
    }
}

// Enter uses a fresh id per replay so the preset starts at t = 0. For exit the sample enters, holds, then gets
// hidden (BeginEnter with visible = false), that way the exit plays from a settled state.
void DrawPresetPreview(const Node& node, bool exit, unsigned replay_generation, double replay_time) {
    const BuilderColors& c = Colors();
    const std::string& preset_name = exit ? node.anim.exit_preset : node.anim.enter_preset;
    float duration = exit ? node.anim.exit_duration : node.anim.enter_duration;
    imb::Preset preset = PresetFromName(preset_name.c_str());
    ImGui::PushStyleColor(ImGuiCol_ChildBg, c.bg_frame);
    if (ImGui::BeginChild("preset_preview", ImVec2(0, kPreviewHeight), ImGuiChildFlags_Borders)) {
        ImGuiID id = ImGui::GetID("sample") ^ (ImGuiID)(replay_generation * 0x9E3779B1u);
        ImVec2 size(std::min(ImGui::GetContentRegionAvail().x - 16.0f, 140.0f), 28.0f);
        ImGui::SetCursorPos(ImVec2((ImGui::GetWindowWidth() - size.x) * 0.5f, (kPreviewHeight - size.y) * 0.5f));
        bool visible = true;
        float dur = duration;
        if (exit) {
            double elapsed = ImGui::GetTime() - replay_time;
            visible = elapsed < (double)(kExitHoldSeconds + duration);
            if (visible) dur = kExitEnterSeconds;
        }
        if (imb::BeginEnter(id, visible, dur, preset)) {
            ImGui::Button(preset_name.empty() ? "Sample" : preset_name.c_str(), size);
            imb::EndEnter();
        } else if (exit) {
            const char* msg = "hidden - click to replay";
            ImVec2 ts = ImGui::CalcTextSize(msg);
            ImGui::SetCursorPos(ImVec2((ImGui::GetWindowWidth() - ts.x) * 0.5f, (kPreviewHeight - ts.y) * 0.5f));
            ui::TextDim("%s", msg);
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
}

}  // namespace

void DrawCurveEditor(EditorContext& ctx, Node& node, int& selected_transition) {
    if (selected_transition < 0 || selected_transition >= (int)node.anim.transitions.size()) return;
    const BuilderColors& c = Colors();
    const Transition& tr = node.anim.transitions[(size_t)selected_transition];
    ImGui::TextUnformatted("Transition");
    ImGui::SameLine();
    ui::Badge(tr.on.c_str(), TriggerColor(tr.on));
    ImGui::SameLine();
    ui::TextDim("%s", tr.prop.c_str());

    ImVec2 min = ImGui::GetCursorScreenPos();
    ImVec2 max(min.x + ImGui::GetContentRegionAvail().x, min.y + kPlotHeight);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ui::DrawCurveGrid(dl, min, max, ToU32(c.separator, 0.6f), ToU32(c.bg_frame));
    float length = TransitionLength(tr);
    float t = length > 0.0f ? std::min(GetAnimPreviewDriver().time / length, 1.0f) : 1.0f;
    if (IsSpring(tr)) {
        ui::SpringParams sp;
        sp.stiffness = tr.stiffness > 0 ? tr.stiffness : 170.0f;
        sp.damping = tr.damping > 0 ? tr.damping : 26.0f;
        ui::DrawSpringCurve(dl, min, max, sp, ToU32(c.accent), 2.0f);
        std::vector<float> samples;
        int n = ui::SampleSpring(sp, samples);
        int idx = std::min((int)(t * (float)(n - 1)), n - 1);
        DrawPlayheadDot(dl, min, max, node, length, samples[(size_t)std::max(idx, 0)], t);
    } else {
        imb::Ease e = EaseFromName(tr.ease.c_str());
        ui::DrawEaseCurve(dl, min, max, e, ToU32(c.accent), 2.0f, 96);
        DrawPlayheadDot(dl, min, max, node, length, imb::EaseApply(e, t), t);
    }
    dl->AddText(ImVec2(min.x + 6.0f, min.y + 4.0f), ToU32(c.text_dim), "1");
    dl->AddText(ImVec2(min.x + 6.0f, max.y - ImGui::GetTextLineHeight() - 4.0f), ToU32(c.text_dim), "0");
    ImGui::Dummy(ImVec2(max.x - min.x, kPlotHeight + 4.0f));

    DrawTransitionControls(ctx, node, selected_transition, selected_transition);
}

void DrawPresetEditor(EditorContext& ctx, Node& node) {
    static bool edit_exit = false;
    static unsigned replay_generation = 0;
    static unsigned seen_generation = ~0u;
    static double replay_time = 0.0;
    ImGui::TextUnformatted("Preset");
    ImGui::SameLine();
    if (ImGui::RadioButton("Enter", !edit_exit)) {
        if (edit_exit) ++replay_generation;
        edit_exit = false;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Exit", edit_exit)) {
        if (!edit_exit) ++replay_generation;
        edit_exit = true;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Replay preview")) ++replay_generation;
    if (seen_generation != replay_generation) {
        seen_generation = replay_generation;
        replay_time = ImGui::GetTime();
    }

    std::string& preset_ref = edit_exit ? node.anim.exit_preset : node.anim.enter_preset;
    float& duration_ref = edit_exit ? node.anim.exit_duration : node.anim.enter_duration;
    const char* label = edit_exit ? "Set exit preset" : "Set enter preset";

    if (ImGui::BeginTable("preset_split", 2, ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableSetupColumn("list", ImGuiTableColumnFlags_WidthFixed, kPresetListWidth);
        ImGui::TableSetupColumn("preview", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        if (ImGui::BeginChild("preset_list", ImVec2(kPresetListWidth, kPreviewHeight + 70.0f), ImGuiChildFlags_None)) {
            if (ImGui::Selectable("(none)", preset_ref.empty()))
                ctx.Edit(label, [&] { preset_ref.clear(); });
            for (int i = 0; i < kPresetCount; ++i) {
                const char* name = PresetName(static_cast<imb::Preset>(i));
                if (ImGui::Selectable(name, preset_ref == name)) {
                    ctx.Edit(label, [&] { preset_ref = name; });
                    ++replay_generation;
                }
            }
        }
        ImGui::EndChild();
        ImGui::TableSetColumnIndex(1);
        DrawPresetPreview(node, edit_exit, replay_generation, replay_time);
        if (ImGui::IsItemClicked()) ++replay_generation;
        ui::Tooltip("Click to replay");
        if (ui::BeginPropertyTable("preset_props", ImGui::GetFontSize() * 5.0f)) {
            ui::PropertyLabel("duration");
            float dur = duration_ref;
            if (ImGui::DragFloat("##pdur", &dur, 0.005f, 0.0f, 10.0f, "%.2f s"))
                ctx.Edit("Change animation duration", [&] { duration_ref = std::max(0.0f, dur); });
            if (!edit_exit) {
                ui::PropertyLabel("stagger", "Delay added per child (containers): child i starts at i * stagger");
                float st = node.anim.stagger;
                if (ImGui::DragFloat("##stagger", &st, 0.002f, 0.0f, 2.0f, "%.3f s"))
                    ctx.Edit("Change enter stagger", [&] { node.anim.stagger = std::max(0.0f, st); });
            }
            ui::EndPropertyTable();
        }
        ImGui::EndTable();
    }
    if (!node.anim.transitions.empty()) ui::TextDim("Click a transition row in the timeline to edit its curve.");
}

void DrawQuickAdd(EditorContext& ctx, Node& node) {
    ui::TextDim("Quick add");
    ImGui::SameLine();
    if (ImGui::SmallButton("Hover color fade")) {
        ctx.Edit("Add transition", [&] {
            Transition tr;
            tr.prop = "color.Button";
            tr.on = "hover";
            tr.to = Json(ColorToHex(ImGui::GetStyleColorVec4(ImGuiCol_ButtonHovered)));
            tr.duration = 0.15f;
            tr.ease = "OutCubic";
            node.anim.transitions.push_back(tr);
        });
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Press scale")) {
        ctx.Edit("Add transition", [&] {
            Transition tr;
            tr.prop = "scale";
            tr.on = "active";
            tr.to = Json(0.95);
            tr.duration = 0.08f;
            tr.ease = "OutQuad";
            node.anim.transitions.push_back(tr);
        });
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Enter FadeSlideUp")) {
        ctx.Edit("Set enter preset", [&] {
            node.anim.enter_preset = "FadeSlideUp";
            node.anim.enter_duration = 0.25f;
        });
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Rainbow border")) {
        ctx.Edit("Add fx", [&] {
            FxSpec fx;
            fx.type = "border";
            fx.params = Json::Object();
            fx.params.Set("style", Json("Rainbow"));
            fx.params.Set("thickness", Json(2.0));
            fx.params.Set("speed", Json(1.0));
            node.fx.push_back(fx);
        });
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Soft shadow")) {
        ctx.Edit("Add fx", [&] {
            FxSpec fx;
            fx.type = "shadow";
            fx.params = Json::Object();
            fx.params.Set("size", Json(12.0));
            fx.params.Set("color", Json("#00000080"));
            node.fx.push_back(fx);
        });
    }
}

}  // namespace imforge::builder::anim_timeline
