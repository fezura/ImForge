#include <cfloat>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "imgui.h"

#include "../builder_fonts.h"
#include "../editor_context.h"
#include "../ui/color_utils.h"
#include "../ui/theme_apply.h"
#include "../ui/theme_preview.h"
#include "../ui/ui_helpers.h"
#include "editor_panel.h"

namespace imforge::builder {

namespace {

using ClassEntry = std::pair<std::string, StyleOverride>;

const ImGuiStyle& DefaultStyle() {
    static const ImGuiStyle s = [] {
        ImGuiStyle st;
        ImGui::StyleColorsDark(&st);
        return st;
    }();
    return s;
}

ClassEntry* FindEntry(Project& p, const std::string& name) {
    for (ClassEntry& e : p.style_classes)
        if (e.first == name) return &e;
    return nullptr;
}

std::string UniqueClassName(const Project& p, const std::string& base) {
    if (!FindStyleClass(p, base)) return base;
    for (int n = 2;; ++n) {
        std::string cand = base + "-" + std::to_string(n);
        if (!FindStyleClass(p, cand)) return cand;
    }
}

bool NodeUses(const Node& n, const std::string& cls) {
    for (const std::string& c : n.style.classes)
        if (c == cls) return true;
    return false;
}

int UsageCount(const Project& p, const std::string& cls, std::vector<std::string>* where) {
    int total = 0;
    for (const Panel& pn : p.panels) {
        int n = 0;
        Walk(pn.root, [&](const Node& node) { n += NodeUses(node, cls) ? 1 : 0; });
        if (n && where) where->push_back(pn.name + " (" + std::to_string(n) + ")");
        total += n;
    }
    for (const Component& c : p.components) {
        int n = 0;
        Walk(c.root, [&](const Node& node) { n += NodeUses(node, cls) ? 1 : 0; });
        if (n && where) where->push_back(c.name + " [component] (" + std::to_string(n) + ")");
        total += n;
    }
    return total;
}

void RenameReferences(EditorContext& ctx, const std::string& old_name, const std::string& new_name) {
    Project& p = ctx.project;
    auto rename_in = [&](Node& root) {
        Walk(root, [&](Node& node) {
            for (std::string& c : node.style.classes)
                if (c == old_name) c = new_name;
        });
    };
    for (Panel& pn : p.panels) {
        bool used = false;
        Walk(pn.root, [&](const Node& node) { used = used || NodeUses(node, old_name); });
        if (!used) continue;
        ctx.BeginEdit("Rename style class", DocKind::Panel, pn.name);
        rename_in(pn.root);
        ctx.EndEdit();
    }
    for (Component& c : p.components) {
        bool used = false;
        Walk(c.root, [&](const Node& node) { used = used || NodeUses(node, old_name); });
        if (!used) continue;
        ctx.BeginEdit("Rename style class", DocKind::Component, c.name);
        rename_in(c.root);
        ctx.EndEdit();
    }
}

class StyleClassesPanel : public EditorPanel {
public:
    const char* Name() const override { return "Style Classes"; }
    const char* DefaultDock() const override { return "right"; }
    const char* Icon() const override {
#ifdef ICON_FA_SWATCHBOOK
        return ICON_FA_SWATCHBOOK;
#else
        return nullptr;
#endif
    }
    void Draw(EditorContext& ctx) override;
    void OnProjectChanged(EditorContext&) override { selected_.clear(); }

private:
    std::string selected_, new_name_, rename_buf_;

    void DrawList(EditorContext& ctx);
    void DrawEditor(EditorContext& ctx, ClassEntry& entry);
    void DrawColors(EditorContext& ctx, ClassEntry& entry);
    void DrawVars(EditorContext& ctx, ClassEntry& entry);
    void AddClass(EditorContext& ctx);
};

void StyleClassesPanel::Draw(EditorContext& ctx) {
    if (!ctx.has_project) {
        ImGui::TextDisabled("Open a project to edit style classes.");
        return;
    }
    float list_w = ImGui::GetFontSize() * 12;
    if (ImGui::BeginChild("##class_list", ImVec2(list_w, 0), ImGuiChildFlags_Borders)) DrawList(ctx);
    ImGui::EndChild();
    ImGui::SameLine();
    if (ImGui::BeginChild("##class_editor", ImVec2(0, 0))) {
        ClassEntry* entry = FindEntry(ctx.project, selected_);
        if (entry) DrawEditor(ctx, *entry);
        else {
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextDisabled("Select a style class on the left or create one.");
            ImGui::PopTextWrapPos();
        }
    }
    ImGui::EndChild();
}

void StyleClassesPanel::AddClass(EditorContext& ctx) {
    std::string name = new_name_;
    for (char& c : name)
        if (c == ' ') c = '-';
    if (name.empty()) name = "class";
    name = UniqueClassName(ctx.project, name);
    ctx.EditProject("Add style class", [&] { ctx.project.style_classes.emplace_back(name, StyleOverride()); });
    selected_ = name;
    new_name_.clear();
}

void StyleClassesPanel::DrawList(EditorContext& ctx) {
    Project& p = ctx.project;
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - ImGui::GetFrameHeight() - ImGui::GetStyle().ItemSpacing.x);
    bool enter = ui::InputTextWithHintStd("##new_class", "new class", new_name_, ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    if (ImGui::Button("+", ImVec2(ImGui::GetFrameHeight(), 0)) || enter) AddClass(ctx);
    ui::Tooltip("Add a style class");
    ImGui::Separator();
    for (ClassEntry& e : p.style_classes) {
        int uses = UsageCount(p, e.first, nullptr);
        char label[160];
        std::snprintf(label, sizeof(label), "%s  (%d)", e.first.c_str(), uses);
        if (ImGui::Selectable(label, selected_ == e.first)) selected_ = e.first;
        if (ImGui::IsItemHovered() && uses == 0) ImGui::SetTooltip("Not used by any widget");
    }
    if (p.style_classes.empty()) ui::TextDim("No style classes yet");
}

void StyleClassesPanel::DrawEditor(EditorContext& ctx, ClassEntry& entry) {
    Project& p = ctx.project;
    ui::HeadingText(ctx.Font(FontSlot::Heading), entry.first.c_str());
    ImGui::SameLine();
    if (ImGui::SmallButton("Rename")) {
        rename_buf_ = entry.first;
        ImGui::OpenPopup("Rename class");
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Duplicate")) {
        ClassEntry copy = entry;
        copy.first = UniqueClassName(p, entry.first + "-copy");
        ctx.EditProject("Duplicate style class", [&] { p.style_classes.push_back(copy); });
        selected_ = copy.first;
        return;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Delete")) ImGui::OpenPopup("Delete class");

    if (ImGui::BeginPopupModal("Rename class", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ui::InputTextStd("Name", rename_buf_);
        if (ImGui::Button("Rename") && !rename_buf_.empty()) {
            std::string old_name = entry.first;
            std::string nm = rename_buf_ == old_name ? old_name : UniqueClassName(p, rename_buf_);
            if (nm != old_name) {
                ctx.EditProject("Rename style class", [&] { entry.first = nm; });
                RenameReferences(ctx, old_name, nm);
                selected_ = nm;
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    if (ImGui::BeginPopupModal("Delete class", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        int uses = UsageCount(p, entry.first, nullptr);
        ImGui::Text("Delete style class \"%s\"?", entry.first.c_str());
        if (uses) ui::TextDim("%d widget(s) reference it; they keep the name and validation will warn.", uses);
        if (ImGui::Button("Delete")) {
            std::string nm = entry.first;
            ctx.EditProject("Delete style class", [&] {
                for (size_t i = 0; i < p.style_classes.size(); ++i) {
                    if (p.style_classes[i].first == nm) {
                        p.style_classes.erase(p.style_classes.begin() + (long)i);
                        break;
                    }
                }
            });
            selected_.clear();
            ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
            return;
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    std::vector<std::string> where;
    int uses = UsageCount(p, entry.first, &where);
    if (uses == 0) {
        ui::TextDim("Not used by any widget yet");
    } else {
        std::string s = "Used by " + std::to_string(uses) + " widget(s): ";
        for (size_t i = 0; i < where.size(); ++i) s += (i ? ", " : "") + where[i];
        ImGui::PushTextWrapPos(0);
        ui::TextDim("%s", s.c_str());
        ImGui::PopTextWrapPos();
    }

    DrawColors(ctx, entry);
    DrawVars(ctx, entry);
    if (ui::Section("Preview")) {
        StyleScope scope = PushStyleOverride(entry.second);
        DrawThemePreviewWidgets("##class_preview", ImVec2(0, ImGui::GetFontSize() * 14), true);
        PopStyleScope(scope);
    }
}

void StyleClassesPanel::DrawColors(EditorContext& ctx, ClassEntry& entry) {
    if (!ui::Section("Colors")) return;
    StyleOverride& cls = entry.second;
    ImGuiColorEditFlags edit_flags =
        ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf;
    for (size_t i = 0; i < cls.colors.size(); ++i) {
        ImGui::PushID((int)i);
        ImVec4 value = ToImVec4(cls.colors[i].second);
        if (ImGui::ColorEdit4("##color", &value.x, edit_flags)) {
            Vec4 nv = ToVec4(value);
            ctx.EditProject("Style class color", [&] { cls.colors[i].second = nv; });
        }
        ImGui::SameLine();
        ImGui::TextUnformatted(cls.colors[i].first.c_str());
        ImGui::SameLine();
        ui::TextDim("%s", ColorToHex(value).c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("x")) {
            ctx.EditProject("Remove style class color", [&] { cls.colors.erase(cls.colors.begin() + (long)i); });
            ImGui::PopID();
            break;
        }
        ImGui::PopID();
    }
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12);
    if (ImGui::BeginCombo("##add_color", "Add color...", ImGuiComboFlags_HeightLarge)) {
        size_t n = 0;
        for (int pass = 0; pass < 2; ++pass) {
            const char* const* names = pass == 0 ? ImGuiColNames(n) : ImGuiColNamesDocking(n);
            for (size_t i = 0; i < n; ++i) {
                bool present = false;
                for (const auto& c : cls.colors)
                    if (c.first == names[i]) present = true;
                if (present) continue;
                if (ImGui::Selectable(names[i])) {
                    int idx = ImGuiColIndexByName(names[i]);
                    Vec4 def = idx >= 0 ? ToVec4(DefaultStyle().Colors[idx]) : Vec4{1, 1, 1, 1};
                    std::string nm = names[i];
                    ctx.EditProject("Add style class color", [&] { cls.colors.emplace_back(nm, def); });
                }
            }
        }
        ImGui::EndCombo();
    }
}

void StyleClassesPanel::DrawVars(EditorContext& ctx, ClassEntry& entry) {
    if (!ui::Section("Variables")) return;
    StyleOverride& cls = entry.second;
    for (size_t i = 0; i < cls.vars.size(); ++i) {
        ImGui::PushID((int)i);
        const std::string& name = cls.vars[i].first;
        bool vec2 = StyleVarIsVec2(name.c_str());
        float f = 0;
        ImVec2 v;
        StyleVarValue(cls.vars[i].second, vec2, f, v);
        bool align = name.find("Alpha") != std::string::npos || name.find("Align") != std::string::npos;
        float speed = align ? 0.005f : 0.1f, mx = align ? 1.0f : 64.0f;
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 9);
        bool changed = vec2 ? ImGui::DragFloat2("##var", &v.x, speed, 0, mx, "%.2f")
                            : ImGui::DragFloat("##var", &f, speed, 0, mx, "%.2f");
        if (changed) {
            Json j = StyleVarJson(vec2, f, v);
            ctx.EditProject("Style class variable", [&] { cls.vars[i].second = j; });
        }
        ImGui::SameLine();
        ImGui::TextUnformatted(name.c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("x")) {
            ctx.EditProject("Remove style class variable", [&] { cls.vars.erase(cls.vars.begin() + (long)i); });
            ImGui::PopID();
            break;
        }
        ImGui::PopID();
    }
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12);
    if (ImGui::BeginCombo("##add_var", "Add variable...", ImGuiComboFlags_HeightLarge)) {
        size_t n = 0;
        for (int pass = 0; pass < 2; ++pass) {
            const char* const* names = pass == 0 ? ImGuiStyleVarNames(n) : ImGuiStyleVarNamesDocking(n);
            for (size_t i = 0; i < n; ++i) {
                bool present = false;
                for (const auto& vv : cls.vars)
                    if (vv.first == names[i]) present = true;
                if (present) continue;
                if (ImGui::Selectable(names[i])) {
                    std::string nm = names[i];
                    Json def = DefaultStyleVarJson(names[i]);
                    ctx.EditProject("Add style class variable", [&] { cls.vars.emplace_back(nm, def); });
                }
            }
        }
        ImGui::EndCombo();
    }
}

}  // namespace

IMFORGE_REGISTER_PANEL(StyleClassesPanel, 62);

}  // namespace imforge::builder
