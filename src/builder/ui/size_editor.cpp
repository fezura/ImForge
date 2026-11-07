#include "size_editor.h"

#include <cstring>

#include "imgui.h"
#include "imgui_internal.h"

namespace imforge::builder {

namespace {

const char* const kModeNames[] = {"auto", "px", "fill", "%", "fill-N"};

bool ModeTakesValue(SizeSpec::Mode m) {
    return m == SizeSpec::Mode::Px || m == SizeSpec::Mode::Percent || m == SizeSpec::Mode::FillMinus;
}

}  // namespace

bool SizeSpecEditor(const char* label, SizeSpec& spec) {
    ImGui::PushID(label);
    bool changed = false;
    float total = ImGui::CalcItemWidth();
    const ImGuiStyle& st = ImGui::GetStyle();
    bool takes_value = ModeTakesValue(spec.mode);
    float combo_w = takes_value ? total * 0.5f - st.ItemInnerSpacing.x * 0.5f : total;
    if (combo_w < 50.0f) combo_w = 50.0f;

    int mode = static_cast<int>(spec.mode);
    ImGui::SetNextItemWidth(combo_w);
    if (ImGui::Combo("##mode", &mode, kModeNames, 5)) {
        SizeSpec::Mode m = static_cast<SizeSpec::Mode>(mode);
        if (m != spec.mode) {
            spec.mode = m;
            if (!ModeTakesValue(m)) spec.value = 0;
            else if (m == SizeSpec::Mode::Percent && (spec.value <= 0 || spec.value > 100)) spec.value = 100;
            else if (m == SizeSpec::Mode::Px && spec.value <= 0) spec.value = 100;
            changed = true;
        }
    }
    if (ModeTakesValue(spec.mode)) {
        ImGui::SameLine(0, st.ItemInnerSpacing.x);
        ImGui::SetNextItemWidth(total - combo_w - st.ItemInnerSpacing.x);
        const char* fmt = spec.mode == SizeSpec::Mode::Percent ? "%.0f%%" : (spec.mode == SizeSpec::Mode::FillMinus ? "-%.0f px" : "%.0f px");
        float mx = spec.mode == SizeSpec::Mode::Percent ? 100.0f : 100000.0f;
        if (ImGui::DragFloat("##value", &spec.value, 1.0f, 0.0f, mx, fmt)) changed = true;
    }
    const char* vis_end = ImGui::FindRenderedTextEnd(label);
    if (vis_end != label) {
        ImGui::SameLine(0, st.ItemInnerSpacing.x);
        ImGui::TextUnformatted(label, vis_end);
    }
    ImGui::PopID();
    return changed;
}

bool SizePairEditor(const char* id, Json& size_json) {
    ImGui::PushID(id);
    SizeSpec w, h;
    if (size_json.IsObject()) {
        SizeSpec::Parse(size_json["w"], w);
        SizeSpec::Parse(size_json["h"], h);
    }
    bool changed = false;
    float label_w = ImGui::CalcTextSize("W").x + ImGui::GetStyle().ItemInnerSpacing.x * 2;
    float item_w = ImGui::CalcItemWidth();

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("W");
    ImGui::SameLine(label_w);
    ImGui::SetNextItemWidth(item_w - label_w);
    if (SizeSpecEditor("##w", w)) changed = true;

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("H");
    ImGui::SameLine(label_w);
    ImGui::SetNextItemWidth(item_w - label_w);
    if (SizeSpecEditor("##h", h)) changed = true;

    if (changed) {
        Json o = Json::Object();
        o.Set("w", w.ToJson());
        o.Set("h", h.ToJson());
        size_json = std::move(o);
    }
    ImGui::PopID();
    return changed;
}

}  // namespace imforge::builder
