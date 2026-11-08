#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

#include "imgui.h"

#include "../builder_style.h"
#include "../bundled_packs.h"
#include "../editor_context.h"
#include "../ui/tree_actions.h"
#include "../ui/tree_ops.h"
#include "../ui/ui_helpers.h"
#include "../ui/widget_descriptions.h"
#include "editor_panel.h"

namespace imforge::builder {

namespace {

struct PaletteItem {
    std::string type, component, category, label, icon, description;
    int min_version = 18500;
    bool container = false;
};

struct PaletteGroup {
    std::string name;
    std::vector<PaletteItem> items;
};

bool ContainsCI(const std::string& hay, const std::string& needle) {
    if (needle.empty()) return true;
    auto it = std::search(hay.begin(), hay.end(), needle.begin(), needle.end(), [](char a, char b) {
        return std::tolower((unsigned char)a) == std::tolower((unsigned char)b);
    });
    return it != hay.end();
}

std::string VersionText(int num) {
    return Project::VersionString(num);
}

std::vector<PaletteGroup> BuildGroups(EditorContext& ctx) {
    std::vector<PaletteGroup> groups;
    auto group_for = [&](const std::string& name) -> PaletteGroup& {
        for (PaletteGroup& g : groups)
            if (g.name == name) return g;
        groups.push_back(PaletteGroup{name, {}});
        return groups.back();
    };
    const bool in_component = ctx.ActiveComponent() != nullptr;
    for (const WidgetDef* w : ctx.catalog.All()) {
        if (!std::strcmp(w->type, "Window") || !std::strcmp(w->type, "Component")) continue;
        if (!std::strcmp(w->type, "Slot") && !in_component) continue;
        PaletteItem it;
        it.type = w->type;
        it.label = w->type;
        it.category = w->category ? w->category : "Other";
        it.icon = w->icon ? w->icon : "fa-puzzle-piece";
        it.min_version = w->min_version;
        it.container = w->container;
        it.description = WidgetDescription(w->type);
        if (const CustomWidgetDef* cw = ctx.catalog.FindCustom(w->type)) {
            const Json& m = cw->manifest;
            if (m["description"].IsString()) it.description = m["description"].AsString();
            else if (m["hint"].IsString()) it.description = m["hint"].AsString();
            else if (it.description.empty()) it.description = "Custom widget from " + (cw->include.empty() ? std::string("a widget manifest") : cw->include);
            if (const BundledPack* pack = BundledPackForWidget(w->type)) it.category = pack->title;
        }
        group_for(it.category).items.push_back(std::move(it));
    }
    Component* active = ctx.ActiveComponent();
    for (const Component& c : ctx.project.components) {
        if (active && active->name == c.name) continue;
        PaletteItem it;
        it.type = "Component";
        it.component = c.name;
        it.label = c.name;
        it.category = "Components";
        it.icon = "fa-cube";
        it.container = !c.slots.empty();
        it.description = "Component with " + std::to_string(c.params.size()) + (c.params.size() == 1 ? " param" : " params") +
                         " and " + std::to_string(c.slots.size()) + (c.slots.size() == 1 ? " slot" : " slots") +
                         (c.file.empty() ? std::string() : " (" + c.file + ")");
        group_for("Components").items.push_back(std::move(it));
    }
    return groups;
}

class PalettePanel final : public EditorPanel {
public:
    const char* Name() const override { return "Palette"; }
    const char* DefaultDock() const override { return "left"; }
    const char* Icon() const override { return tree_actions::Icon("fa-shapes", nullptr); }

    void Draw(EditorContext& ctx) override {
        DrawToolbar(ctx);
        std::vector<PaletteGroup> groups = BuildGroups(ctx);
        const int project_version = ctx.has_project ? ctx.project.ImGuiVersionNum() : 0;
        if (!ImGui::BeginChild("palette_scroll", ImVec2(0, 0), ImGuiChildFlags_None)) {
            ImGui::EndChild();
            return;
        }
        bool any = false;
        for (const PaletteGroup& g : groups) {
            std::vector<const PaletteItem*> items;
            for (const PaletteItem& it : g.items)
                if (Matches(it)) items.push_back(&it);
            if (items.empty()) continue;
            any = true;
            if (!search_.empty()) ImGui::SetNextItemOpen(true);
            char header[128];
            std::snprintf(header, sizeof(header), "%s (%d)###pal_%s", g.name.c_str(), (int)items.size(), g.name.c_str());
            if (!ui::Section(header, true)) continue;
            if (grid_) DrawGrid(ctx, items, project_version);
            else DrawList(ctx, items, project_version);
            ImGui::Spacing();
        }
        if (!any) ui::TextDim(search_.empty() ? "No widgets available." : "No widget matches the search.");
        ImGui::EndChild();
    }

private:
    std::string search_;
    bool grid_ = true;

    bool Matches(const PaletteItem& it) const {
        return ContainsCI(it.label, search_) || ContainsCI(it.type, search_) || ContainsCI(it.category, search_) ||
               ContainsCI(it.description, search_);
    }

    void DrawToolbar(EditorContext& ctx) {
        const BuilderColors& c = Colors();
        float btn = ImGui::GetFrameHeight();
        float spacing = ImGui::GetStyle().ItemSpacing.x;
        ImGui::SetNextItemWidth(std::max(ImGui::GetContentRegionAvail().x - (btn + spacing) * 2.0f, 60.0f));
        ui::InputTextWithHintStd("##pal_search", "Search widgets", search_);
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, grid_ ? c.accent : c.text_dim);
        if (ui::IconButton(tree_actions::Icon("fa-table-cells", "#"), "Grid view", ImVec2(btn, btn))) grid_ = true;
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, grid_ ? c.text_dim : c.accent);
        if (ui::IconButton(tree_actions::Icon("fa-list", "="), "List view", ImVec2(btn, btn))) grid_ = false;
        ImGui::PopStyleColor();
        ImGui::PushTextWrapPos(0.0f);
        if (Node* sel = ctx.SelectedNode()) ui::TextDim("Adds near %s (%s)", sel->id.c_str(), sel->type.c_str());
        else if (ctx.ActiveRoot()) ui::TextDim("Click to add, drag to place.");
        else ui::TextDim("Open a panel or component to add widgets.");
        ImGui::PopTextWrapPos();
        ImGui::Separator();
    }

    void DrawGrid(EditorContext& ctx, const std::vector<const PaletteItem*>& items, int project_version) {
        const float gap = std::floor(ImGui::GetStyle().ItemSpacing.x * 0.75f);
        const float min_w = ImGui::GetFontSize() * 4.6f;
        const float avail = ImGui::GetContentRegionAvail().x;
        const int columns = std::max(1, (int)((avail + gap) / (min_w + gap)));
        const float cell_w = std::floor((avail - gap * (float)(columns - 1)) / (float)columns);
        const float cell_h = std::floor(ImGui::GetFontSize() * 4.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(gap, gap));
        int col = 0;
        for (const PaletteItem* it : items) {
            ImGui::PushID(it->label.c_str());
            ImVec2 pos = ImGui::GetCursorScreenPos();
            bool clicked = ImGui::InvisibleButton("##cell", ImVec2(cell_w, cell_h));
            DrawCellContent(ctx, *it, pos, ImVec2(cell_w, cell_h), project_version);
            AfterItem(ctx, *it, clicked, project_version);
            ImGui::PopID();
            if (++col < columns) ImGui::SameLine();
            else col = 0;
        }
        ImGui::PopStyleVar();
        if (col != 0) ImGui::NewLine();
    }

    void DrawList(EditorContext& ctx, const std::vector<const PaletteItem*>& items, int project_version) {
        const BuilderColors& c = Colors();
        const ImGuiStyle& st = ImGui::GetStyle();
        for (const PaletteItem* it : items) {
            ImGui::PushID(it->label.c_str());
            ImVec2 pos = ImGui::GetCursorScreenPos();
            bool clicked = ImGui::Selectable("##row", false, 0, ImVec2(0, ImGui::GetFrameHeight()));
            ImVec2 rmin = ImGui::GetItemRectMin(), rmax = ImGui::GetItemRectMax();
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const bool unsupported = project_version > 0 && it->min_version > project_version;
            ImU32 col_text = ToU32(unsupported ? c.text_disabled : c.text);
            float x = pos.x + st.FramePadding.x, y = pos.y + st.FramePadding.y;
            if (const char* icon = tree_actions::Icon(it->icon.c_str(), nullptr)) {
                dl->AddText(ImVec2(x, y), ToU32(unsupported ? c.text_disabled : c.accent), icon);
                x += ImGui::GetFontSize() * 1.4f;
            }
            dl->AddText(ImVec2(x, y), col_text, it->label.c_str());
            float desc_x = std::max(x + ImGui::CalcTextSize(it->label.c_str()).x + st.ItemSpacing.x * 2.0f,
                                    rmin.x + (rmax.x - rmin.x) * 0.42f);
            if (desc_x < rmax.x - 20.0f && !it->description.empty()) {
                dl->PushClipRect(ImVec2(desc_x, rmin.y), ImVec2(rmax.x - st.FramePadding.x, rmax.y), true);
                dl->AddText(ImVec2(desc_x, y), ToU32(c.text_dim), it->description.c_str());
                dl->PopClipRect();
            }
            AfterItem(ctx, *it, clicked, project_version);
            ImGui::PopID();
        }
    }

    void DrawCellContent(EditorContext& ctx, const PaletteItem& it, const ImVec2& pos, const ImVec2& size,
                         int project_version) {
        (void)ctx;
        const BuilderColors& c = Colors();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const bool unsupported = project_version > 0 && it.min_version > project_version;
        const bool hovered = ImGui::IsItemHovered();
        const bool held = ImGui::IsItemActive();
        const ImVec2 end(pos.x + size.x, pos.y + size.y);
        const float rounding = ImGui::GetStyle().FrameRounding + 2.0f;
        dl->AddRectFilled(pos, end, ToU32(held ? c.bg_active : hovered ? c.bg_hover : WithAlpha(c.bg_frame, 0.55f)), rounding);
        if (hovered) dl->AddRect(pos, end, ToU32(c.accent, held ? 0.9f : 0.55f), rounding, 0, 1.0f);

        const char* icon = tree_actions::Icon(it.icon.c_str(), nullptr);
        if (!icon) icon = tree_actions::Icon(it.container ? "fa-vector-square" : "fa-puzzle-piece", nullptr);
        ImFont* big = IconFontLarge();
        const float line = ImGui::GetTextLineHeight();
        const float icon_sz = big ? std::floor(big->FontSize * 0.92f) : ImGui::GetFontSize();
        const float block_h = (icon ? icon_sz + 6.0f : 0.0f) + line;
        const float top = std::floor(pos.y + (size.y - block_h) * 0.5f);
        if (icon) {
            ImFont* f = big ? big : ImGui::GetFont();
            ImVec2 is = f->CalcTextSizeA(icon_sz, FLT_MAX, 0.0f, icon);
            ImVec4 ic = unsupported ? c.text_disabled : hovered ? c.accent_hover : c.accent;
            dl->AddText(f, icon_sz, ImVec2(std::floor(pos.x + (size.x - is.x) * 0.5f), top), ToU32(ic), icon);
        }
        const char* label = it.label.c_str();
        const char* label_end = label + it.label.size();
        const float max_w = size.x - 8.0f;
        ImVec2 ls = ImGui::CalcTextSize(label, label_end);
        const float ty = icon ? top + icon_sz + 6.0f : top;
        const ImU32 tc = ToU32(unsupported ? c.text_disabled : hovered ? c.text : WithAlpha(c.text, 0.86f));
        if (ls.x <= max_w) {
            dl->AddText(ImVec2(std::floor(pos.x + (size.x - ls.x) * 0.5f), ty), tc, label, label_end);
        } else {
            const float dots = ImGui::CalcTextSize("..").x;
            while (label_end > label + 1 && ImGui::CalcTextSize(label, label_end).x + dots > max_w) --label_end;
            float w = ImGui::CalcTextSize(label, label_end).x + dots;
            float tx = std::floor(pos.x + (size.x - w) * 0.5f);
            dl->AddText(ImVec2(tx, ty), tc, label, label_end);
            dl->AddText(ImVec2(tx + w - dots, ty), tc, "..");
        }
        if (it.container) {
            const float k = std::floor(ImGui::GetFontSize() * 0.45f);
            const ImVec2 a(end.x - 4.0f, pos.y + 4.0f);
            dl->AddTriangleFilled(ImVec2(a.x - k, a.y), a, ImVec2(a.x, a.y + k), ToU32(c.accent, hovered ? 0.8f : 0.45f));
        }
    }

    void AfterItem(EditorContext& ctx, const PaletteItem& it, bool clicked, int project_version) {
        if (ImGui::BeginDragDropSource()) {
            std::string payload = tree_actions::NewPayloadFor(it.type, it.component);
            ImGui::SetDragDropPayload(tree_actions::kPayloadNew, payload.c_str(), payload.size() + 1);
            const char* icon = tree_actions::Icon(it.icon.c_str(), nullptr);
            ImGui::Text("%s %s", icon ? icon : "+", it.label.c_str());
            ImGui::EndDragDropSource();
        } else if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort) && ImGui::BeginTooltip()) {
            DrawTooltip(ctx, it, project_version);
            ImGui::EndTooltip();
        }
        if (clicked) tree_actions::AddAtSelection(ctx, it.type, it.component);
    }

    void DrawTooltip(EditorContext& ctx, const PaletteItem& it, int project_version) {
        const BuilderColors& c = Colors();
        const char* icon = tree_actions::Icon(it.icon.c_str(), nullptr);
        ImGui::Text("%s%s%s", icon ? icon : "", icon ? "  " : "", it.label.c_str());
        ImGui::SameLine();
        ui::TextDim("%s", it.category.c_str());
        if (!it.description.empty()) {
            ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28.0f);
            ImGui::TextUnformatted(it.description.c_str());
            ImGui::PopTextWrapPos();
        }
        if (it.container) ui::TextDim(it.type == "Component" ? "Has slots for child content" : "Container: accepts children");
        if (project_version > 0 && it.min_version > project_version)
            ImGui::TextColored(c.warning, "Requires Dear ImGui %s (project targets %s); codegen falls back",
                               VersionText(it.min_version).c_str(), ctx.project.imgui_version.c_str());
        ui::TextDim("Click to add near the selection, drag onto the canvas or hierarchy");
    }
};

}  // namespace

IMFORGE_REGISTER_PANEL(PalettePanel, 20);

}  // namespace imforge::builder
