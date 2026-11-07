#include <cfloat>
#include <cstdio>
#include <string>

#include "imgui.h"

#include "../builder_style.h"
#include "../editor_context.h"
#include "../ui/ui_helpers.h"
#include "editor_panel.h"

namespace imforge::builder {

namespace {

int LevelIndex(Diagnostic::Level l) {
    switch (l) {
    case Diagnostic::Level::Error: return 3;
    case Diagnostic::Level::Warning: return 2;
    default: return 1;
    }
}

class DiagnosticsPanel final : public EditorPanel {
public:
    const char* Name() const override { return "Diagnostics"; }
    const char* DefaultDock() const override { return "bottom"; }

    void Draw(EditorContext& ctx) override {
        int errors = 0, warnings = 0, infos = 0;
        for (const Diagnostic& d : ctx.diagnostics) {
            if (d.level == Diagnostic::Level::Error) ++errors;
            else if (d.level == Diagnostic::Level::Warning) ++warnings;
            else ++infos;
        }
        if (ImGui::Button("Revalidate")) ctx.Revalidate();
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, ui::LevelColor(3));
        ImGui::Text("%s %d", ui::LevelIcon(3), errors);
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, ui::LevelColor(2));
        ImGui::Text("%s %d", ui::LevelIcon(2), warnings);
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, ui::LevelColor(1));
        ImGui::Text("%s %d", ui::LevelIcon(1), infos);
        ImGui::PopStyleColor();
        ImGui::SameLine();
        if (ctx.active_kind == DocKind::None) ui::TextDim("no document");
        else ui::TextDim("%s", ctx.active_panel.c_str());
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::InputTextWithHint("##diag_filter", "Filter messages or ids", filter_.InputBuf, IM_ARRAYSIZE(filter_.InputBuf)))
            filter_.Build();
        ImGui::Separator();

        if (ctx.diagnostics.empty()) {
            ImGui::Spacing();
            ui::TextDim(ctx.has_project ? "No issues in the active document." : "Open a project to see diagnostics.");
            return;
        }
        ImGuiTableFlags table_flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable |
                                      ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp;
        if (!ImGui::BeginTable("diagnostics", 4, table_flags)) return;
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 1.6f);
        ImGui::TableSetupColumn("Panel", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 7.0f);
        ImGui::TableSetupColumn("Node", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 8.0f);
        ImGui::TableSetupColumn("Message", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();
        for (size_t i = 0; i < ctx.diagnostics.size(); ++i) {
            const Diagnostic& d = ctx.diagnostics[i];
            if (filter_.IsActive() && !filter_.PassFilter(d.message.c_str()) && !filter_.PassFilter(d.node_id.c_str())) continue;
            int lvl = LevelIndex(d.level);
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::PushStyleColor(ImGuiCol_Text, ui::LevelColor(lvl));
            ImGui::TextUnformatted(ui::LevelIcon(lvl));
            ImGui::PopStyleColor();
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(d.panel.c_str());
            ImGui::TableSetColumnIndex(2);
            std::string node_label = d.node_id + (d.prop.empty() ? "" : "." + d.prop);
            ImGui::TextUnformatted(node_label.c_str());
            ImGui::TableSetColumnIndex(3);
            char id[32];
            std::snprintf(id, sizeof(id), "##diag_%d", (int)i);
            bool selected = !d.node_id.empty() && ctx.IsSelected(d.node_id);
            if (ImGui::Selectable(id, selected, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap)) {
                if (!d.panel.empty() && ctx.FindPanelDoc(d.panel)) ctx.SetActivePanel(d.panel);
                else if (!d.panel.empty() && ctx.FindComponentDoc(d.panel)) ctx.SetActiveComponent(d.panel);
                if (!d.node_id.empty()) {
                    ctx.Select(d.node_id);
                    ctx.OpenPanel("Properties", false);
                }
            }
            ImGui::SameLine(0, 0);
            ImGui::TextWrapped("%s", d.message.c_str());
        }
        ImGui::EndTable();
    }

private:
    ImGuiTextFilter filter_;
};

}  // namespace

IMFORGE_REGISTER_PANEL(DiagnosticsPanel, 910);

}  // namespace imforge::builder
