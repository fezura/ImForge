#include "theme_preview.h"

#include <cmath>
#include <cstdio>

namespace imforge::builder {

namespace {

struct PreviewState {
    bool check = true;
    bool check2 = false;
    int radio = 0;
    float slider = 0.42f;
    int drag = 7;
    int combo = 0;
    char text[64] = "Edit me";
    int selected = 1;
    float values[12] = {0.2f, 0.5f, 0.3f, 0.8f, 0.6f, 0.9f, 0.4f, 0.7f, 0.5f, 0.3f, 0.6f, 0.8f};
};

PreviewState& State() {
    static PreviewState s;
    return s;
}

void DrawMenuBar() {
    if (!ImGui::BeginMenuBar()) return;
    if (ImGui::BeginMenu("File")) {
        ImGui::MenuItem("New", "Ctrl+N");
        ImGui::MenuItem("Open", "Ctrl+O");
        ImGui::Separator();
        ImGui::MenuItem("Quit");
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Edit")) {
        ImGui::MenuItem("Undo", "Ctrl+Z");
        ImGui::MenuItem("Redo", "Ctrl+Y", false, false);
        ImGui::EndMenu();
    }
    ImGui::EndMenuBar();
}

void DrawControls(PreviewState& s) {
    ImGui::TextUnformatted("Text");
    ImGui::SameLine();
    ImGui::TextDisabled("Disabled");
#if IMGUI_VERSION_NUM >= 19100
    ImGui::SameLine();
    ImGui::TextLink("Link");
#endif
    ImGui::Button("Button");
    ImGui::SameLine();
    ImGui::SmallButton("Small");
    ImGui::SameLine();
    ImGui::Checkbox("Check", &s.check);
    ImGui::SameLine();
    ImGui::RadioButton("A", &s.radio, 0);
    ImGui::SameLine();
    ImGui::RadioButton("B", &s.radio, 1);
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.5f);
    ImGui::SliderFloat("Slider", &s.slider, 0.0f, 1.0f);
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.5f);
    ImGui::DragInt("Drag", &s.drag, 0.2f, 0, 100);
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.5f);
    ImGui::InputText("Input", s.text, sizeof(s.text));
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.5f);
    ImGui::Combo("Combo", &s.combo, "Alpha\0Beta\0Gamma\0");
    float t = (float)(std::fmod(ImGui::GetTime() * 0.25, 1.0));
    ImGui::ProgressBar(t, ImVec2(ImGui::GetContentRegionAvail().x * 0.5f, 0));
    ImGui::SameLine();
    if (ImGui::Button("Popup")) ImGui::OpenPopup("##preview_popup");
    if (ImGui::BeginPopup("##preview_popup")) {
        ImGui::MenuItem("Item 1");
        ImGui::MenuItem("Item 2", nullptr, &s.check2);
        if (ImGui::BeginMenu("Submenu")) {
            ImGui::MenuItem("Nested");
            ImGui::EndMenu();
        }
        ImGui::EndPopup();
    }
}

void DrawTabs(PreviewState& s) {
    if (!ImGui::BeginTabBar("##preview_tabs")) return;
    if (ImGui::BeginTabItem("Tree")) {
        if (ImGui::CollapsingHeader("Header", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (ImGui::TreeNodeEx("Node", ImGuiTreeNodeFlags_DefaultOpen)) {
                for (int i = 0; i < 3; ++i) {
                    char label[32];
                    std::snprintf(label, sizeof(label), "Selectable %d", i + 1);
                    if (ImGui::Selectable(label, s.selected == i)) s.selected = i;
                }
                ImGui::TreePop();
            }
        }
        ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Table")) {
        if (ImGui::BeginTable("##preview_table", 3,
                              ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable)) {
            ImGui::TableSetupColumn("Name");
            ImGui::TableSetupColumn("Value");
            ImGui::TableSetupColumn("State");
            ImGui::TableHeadersRow();
            const char* rows[3][3] = {{"Health", "100", "ok"}, {"Armor", "50", "low"}, {"Ammo", "12", "ok"}};
            for (int r = 0; r < 3; ++r) {
                ImGui::TableNextRow();
                for (int c = 0; c < 3; ++c) {
                    ImGui::TableSetColumnIndex(c);
                    ImGui::TextUnformatted(rows[r][c]);
                }
            }
            ImGui::EndTable();
        }
        ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Plot")) {
        ImGui::PlotLines("##lines", s.values, 12, 0, "Lines", 0.0f, 1.0f, ImVec2(ImGui::GetContentRegionAvail().x, 48));
        ImGui::PlotHistogram("##hist", s.values, 12, 0, "Histogram", 0.0f, 1.0f,
                             ImVec2(ImGui::GetContentRegionAvail().x, 48));
        ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
}

}  // namespace

void DrawThemePreviewWidgets(const char* id, const ImVec2& size, bool compact) {
    PreviewState& s = State();
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetStyleColorVec4(ImGuiCol_WindowBg));
    ImGuiWindowFlags flags = compact ? 0 : ImGuiWindowFlags_MenuBar;
    bool open = ImGui::BeginChild(id, size, ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding, flags);
    ImGui::PopStyleColor();
    if (open) {
        if (!compact) DrawMenuBar();
        DrawControls(s);
        if (!compact) {
#if IMGUI_VERSION_NUM >= 18950
            ImGui::SeparatorText("Containers");
#else
            ImGui::Separator();
#endif
            DrawTabs(s);
        }
    }
    ImGui::EndChild();
}

}  // namespace imforge::builder
