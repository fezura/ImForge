#include <cstdio>
#include <map>

#include "imgui.h"
#include "imgui_internal.h"

#include "../ui/icon_table.h"
#include "editor_panel.h"
#include "properties_shared.h"

namespace imforge::builder {

namespace props {

void PropsUI::SyncMulti() {
    for (Node* n : selected) {
        if (n == &node) continue;
        n->style = node.style;
        n->anim = node.anim;
        n->fx = node.fx;
    }
}

bool SectionHeader(const char* label, bool default_open) {
    static std::map<std::string, bool> remembered;
    auto it = remembered.find(label);
    bool open = it != remembered.end() ? it->second : default_open;
    ImGui::SetNextItemOpen(open, ImGuiCond_Always);
    bool now = ui::Section(label, open);
    if (now != open) remembered[label] = now;
    return now;
}

float SmallButtonWidth() { return ImGui::GetFrameHeight(); }

bool SmallIconButton(const char* id, const char* icon, const char* tooltip, bool enabled, bool highlighted) {
    char label[96];
    std::snprintf(label, sizeof(label), "%s##%s", icon, id);
    if (!enabled) ImGui::BeginDisabled();
    const BuilderColors& c = Colors();
    ImGui::PushStyleColor(ImGuiCol_Button, highlighted ? c.accent : ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_Text, highlighted ? ImVec4(1, 1, 1, 1) : c.text_dim);
    float w = SmallButtonWidth();
    bool pressed = ImGui::Button(label, ImVec2(w, w));
    ImGui::PopStyleColor(2);
    if (!enabled) ImGui::EndDisabled();
    if (tooltip && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayShort))
        ImGui::SetTooltip("%s", tooltip);
    return pressed;
}

bool ResetButton(const char* tooltip) { return SmallIconButton("reset", PP_ICON_RESET, tooltip); }

float PropRow(const char* label, const char* help, int trailing_buttons) {
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::AlignTextToFramePadding();
    ImGui::PushStyleColor(ImGuiCol_Text, Colors().text_dim);
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();
    if (help && *help && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("%s", help);
    ImGui::TableSetColumnIndex(1);
    float avail = ImGui::GetContentRegionAvail().x;
    float w = avail - (SmallButtonWidth() + ImGui::GetStyle().ItemInnerSpacing.x) * (float)trailing_buttons;
    if (w < 40.0f) w = 40.0f;
    ImGui::SetNextItemWidth(w);
    return w;
}

void TrailingSameLine() { ImGui::SameLine(0, ImGui::GetStyle().ItemInnerSpacing.x); }

bool ColorEditHex(const char* id, ImVec4& color) {
    ImGui::PushID(id);
    const ImGuiStyle& st = ImGui::GetStyle();
    float total = ImGui::CalcItemWidth();
    float hex_w = ImGui::CalcTextSize("#ffffffff").x + st.FramePadding.x * 2 + 4.0f;
    float edit_w = total - hex_w - st.ItemInnerSpacing.x;
    if (edit_w < ImGui::GetFrameHeight()) edit_w = ImGui::GetFrameHeight();
    ImGui::SetNextItemWidth(edit_w);
    bool changed = ImGui::ColorEdit4("##col", &color.x, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaPreviewHalf |
                                                       ImGuiColorEditFlags_AlphaBar);
    ImGui::SameLine(0, st.ItemInnerSpacing.x);
    static std::string buf;
    static ImGuiID active_id = 0;
    ImGuiID hid = ImGui::GetID("##hex");
    if (active_id != hid) buf = ColorToHex(color);
    ImGui::SetNextItemWidth(hex_w);
    ui::InputTextStd("##hex", buf);
    if (ImGui::IsItemActive()) active_id = hid;
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        ImVec4 parsed;
        if (HexToColor(buf, parsed)) {
            color = parsed;
            changed = true;
        }
        active_id = 0;
    } else if (ImGui::IsItemDeactivated()) {
        active_id = 0;
    }
    ImGui::PopID();
    return changed;
}

bool ComboStrings(const char* label, std::string& value, const std::vector<std::string>& items, const char* none_label) {
    bool changed = false;
    const char* preview = value.empty() ? (none_label ? none_label : "") : value.c_str();
    if (ImGui::BeginCombo(label, preview)) {
        if (none_label) {
            if (ImGui::Selectable(none_label, value.empty())) {
                if (!value.empty()) changed = true;
                value.clear();
            }
        }
        for (const std::string& it : items) {
            bool sel = it == value;
            if (ImGui::Selectable(it.c_str(), sel) && !sel) {
                value = it;
                changed = true;
            }
            if (sel) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    return changed;
}

bool ComboIndex(const char* label, int& index, const char* const* items, int count) {
    const char* preview = (index >= 0 && index < count) ? items[index] : "";
    bool changed = false;
    if (ImGui::BeginCombo(label, preview)) {
        for (int i = 0; i < count; ++i) {
            bool sel = i == index;
            if (ImGui::Selectable(items[i], sel) && !sel) {
                index = i;
                changed = true;
            }
            if (sel) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    return changed;
}

std::string VersionText(int version_num) { return Project::VersionString(version_num); }

void UnsupportedNote(int min_version, int version) {
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayShort))
        ImGui::SetTooltip("Requires Dear ImGui >= %s (project targets %s)", VersionText(min_version).c_str(),
                          VersionText(version).c_str());
}

std::string HandlerName(const PropsUI& ui, const char* event) {
    std::string ev = event ? event : "";
    if (!ev.empty() && ev[0] >= 'a' && ev[0] <= 'z') ev[0] = (char)(ev[0] - 'a' + 'A');
    return ui.doc_name + "_" + ui.node.id + "_" + ev;
}

std::vector<std::string> NodeIdsOfType(const Node& root, const char* types_pipe) {
    std::vector<std::string> out;
    Walk(root, [&](const Node& n) {
        if (PipeListContains(types_pipe, n.type.c_str())) out.push_back(n.id);
    });
    return out;
}

std::vector<std::string> ComponentNames(const Project& project) {
    std::vector<std::string> out;
    for (const Component& c : project.components) out.push_back(c.name);
    return out;
}

void DrawMotionsSection(PropsUI& ui);

}  // namespace props

namespace {

using namespace props;

class PropertiesPanel final : public EditorPanel {
public:
    const char* Name() const override { return "Properties"; }
    const char* DefaultDock() const override { return "right"; }
    const char* Icon() const override {
#ifdef ICON_FA_SLIDERS
        return ICON_FA_SLIDERS;
#else
        return nullptr;
#endif
    }

    void Draw(EditorContext& ctx) override {
        if (!ctx.has_project) {
            ui::EmptyState(IconUtf8("fa-sliders"), "No project open", "Properties of the selected widget appear here.");
            return;
        }
        Node* root = ctx.ActiveRoot();
        if (!root) {
            ui::EmptyState(IconUtf8("fa-window-maximize"), "No document open", "Open a panel or a component to edit its widgets.");
            return;
        }
        std::vector<Node*> selected = ctx.SelectedNodes();
        if (selected.empty()) {
            DrawNoSelection(ctx, *root);
            return;
        }
        Node& node = *selected.back();
        if (Panel* p = ctx.ActivePanel()) {
            env_.db = &ctx.types;
            env_.SetPanel(*p);
        } else if (Component* c = ctx.ActiveComponent()) {
            env_.db = &ctx.types;
            env_.SetComponent(*c);
        }
        PropsUI ui{ctx, node, root, selected, env_, nullptr, nullptr, 0, false, false, nullptr, std::string()};
        ui.def = ctx.catalog.Find(node.type.c_str());
        ui.custom = ctx.catalog.FindCustom(node.type.c_str());
        ui.version = ctx.project.ImGuiVersionNum();
        ui.multi = selected.size() > 1;
        ui.in_component = ctx.active_kind == DocKind::Component;
        ui.component_doc = ctx.ActiveComponent();
        ui.doc_name = ctx.active_panel;

        ImGui::PushID(node.id.c_str());
        if (ui.multi) DrawMulti(ui);
        else DrawSingle(ui);
        ImGui::PopID();
    }

private:
    EditorTypeEnv env_;

    static void DrawNoSelection(EditorContext& ctx, const Node& root) {
        int count = 0;
        Walk(root, [&](const Node&) { ++count; });
        const ImGuiStyle& st = ImGui::GetStyle();
        const float extra = ImGui::GetFontSize() * 1.2f + ImGui::GetTextLineHeight() + ImGui::GetFrameHeight() + st.ItemSpacing.y * 2.0f;
        ui::EmptyState(IconUtf8("fa-arrow-pointer"), "Nothing selected",
                       "Select a widget in the Hierarchy or on the Canvas to edit its properties.", extra);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + ImGui::GetFontSize() * 1.2f - st.ItemSpacing.y);
        char info[256];
        std::snprintf(info, sizeof(info), "%s %s  \xc2\xb7  %d widgets, root \"%s\"",
                      ctx.active_kind == DocKind::Component ? "Component" : "Panel", ctx.active_panel.c_str(), count, root.id.c_str());
        ui::CenteredTextWrapped(info, ImGui::GetContentRegionAvail().x, ToU32(Colors().text_disabled));
        ui::CenteredRowStart(ui::ButtonWidth("Select root"));
        if (ImGui::Button("Select root")) ctx.Select(root.id);
    }

    static void DrawSingle(PropsUI& ui) {
        DrawIdentitySection(ui);
        if (ui.node.type == "Component") {
            DrawComponentSection(ui);
            return;
        }
        DrawPropertiesSection(ui);
        DrawBindingsSection(ui);
        DrawConditionsSection(ui);
        DrawEventsSection(ui);
        DrawStyleSection(ui);
        DrawAnimSection(ui);
        DrawMotionsSection(ui);
        DrawFxSection(ui);
        DrawLayoutSection(ui);
    }

    static void DrawMulti(PropsUI& ui) {
        ImGui::Text("%d widgets selected", (int)ui.selected.size());
        std::string ids;
        for (size_t i = 0; i < ui.selected.size() && i < 6; ++i) {
            if (i) ids += ", ";
            ids += ui.selected[i]->id;
        }
        if (ui.selected.size() > 6) ids += ", ...";
        ui::TextDim("%s", ids.c_str());
        ui::TextDim("Style, animation and effects of \"%s\" are shown; edits apply to every selected widget.",
                    ui.node.id.c_str());
        if (ui::AccentButton("Apply to all now")) {
            ui.Edit("Apply style to selection", [] {});
        }
        ui::Tooltip("Copy this widget's style, animation and effects to the other selected widgets");
        ImGui::Spacing();
        DrawStyleSection(ui);
        DrawAnimSection(ui);
        DrawMotionsSection(ui);
        DrawFxSection(ui);
    }
};

}  // namespace

IMFORGE_REGISTER_PANEL(PropertiesPanel, 30);

}  // namespace imforge::builder
