#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <string>
#include <vector>

#include "imgui.h"

#include "../builder_style.h"
#include "../editor_context.h"
#include "../imblib.h"
#include "../ui/icon_table.h"
#include "../ui/ui_helpers.h"
#include "components_ops.h"
#include "components_ui.h"
#include "editor_panel.h"

namespace imforge::builder {

namespace {

using namespace components;

class ComponentsPanel final : public EditorPanel {
public:
    const char* Name() const override { return "Components"; }
    const char* DefaultDock() const override { return "left"; }

    void OnProjectChanged(EditorContext& ctx) override {
        libs_dirty_ = true;
        if (!ctx.FindComponentDoc(selected_)) selected_.clear();
        if (!ctx.FindPanelDoc(return_doc_)) return_doc_.clear();
    }

    void Draw(EditorContext& ctx) override {
        if (!ctx.has_project) {
            ui::EmptyState(IconUtf8("fa-cubes"), "No project open", "Reusable widget groups with parameters and slots live here.");
            return;
        }
        if (!ctx.component_extract_request.empty()) {
            ctx.component_extract_request.clear();
            StartExtract(ctx);
        }
        if (Component* active = ctx.ActiveComponent()) {
            if (last_active_ != active->name) selected_ = active->name;
            last_active_ = active->name;
        } else {
            last_active_.clear();
        }
        DrawEditBanner(ctx);
        DrawToolbar(ctx);
        DrawComponentList(ctx);
        DrawLibraries(ctx);
        if (Component* c = ctx.FindComponentDoc(selected_)) DrawDetails(ctx, *c);
        DrawPrompt(ctx);
        DrawImblibDialogs(ctx);
    }

private:
    enum class Prompt { None, New, Rename, Duplicate, Delete, Extract };

    void DrawEditBanner(EditorContext& ctx) {
        Component* c = ctx.ActiveComponent();
        if (!c) return;
        const BuilderColors& col = Colors();
        ImGui::PushStyleColor(ImGuiCol_ChildBg, WithAlpha(col.accent, 0.12f));
        if (ImGui::BeginChild("edit_banner", ImVec2(0, ImGui::GetFrameHeightWithSpacing() + 4), ImGuiChildFlags_None)) {
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Editing component");
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Text, col.accent);
            ImGui::TextUnformatted(c->name.c_str());
            ImGui::PopStyleColor();
            ImGui::SameLine();
            if (ui::AccentButton("Done editing")) ExitEditMode(ctx);
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();
    }

    void DrawToolbar(EditorContext& ctx) {
        if (ImGui::Button("New...")) OpenPrompt(Prompt::New, UniqueComponentName(ctx.project, "NewComponent"));
        ui::Tooltip("Create an empty component");
        ImGui::SameLine();
        bool can_extract = ctx.ActiveRoot() != nullptr && !ctx.selection.empty();
        ImGui::BeginDisabled(!can_extract);
        if (ImGui::Button("From selection...")) StartExtract(ctx);
        ImGui::EndDisabled();
        ui::Tooltip("Move the selected nodes into a new component and replace them with an instance");
        ImGui::SameLine();
        if (ImGui::Button("Import...")) OpenImblibImportDialog(ctx);
        ui::Tooltip("Import components and widgets from an .imblib library");
        ImGui::SameLine();
        if (ImGui::Button("Export...")) {
            std::vector<std::string> pre;
            if (!selected_.empty()) pre.push_back(selected_);
            OpenImblibExportDialog(ctx, pre);
        }
        ui::Tooltip("Export components and custom widgets to an .imblib library");
        ImGui::Separator();
    }

    void DrawComponentList(EditorContext& ctx) {
        char title[96];
        std::snprintf(title, sizeof(title), "Project components (%d)###components", (int)ctx.project.components.size());
        if (!ui::Section(title)) return;
        if (ctx.project.components.empty()) {
            ui::TextDim("No components yet.");
            return;
        }
        ImGuiTableFlags tflags = ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_PadOuterX;
        if (!ImGui::BeginTable("component_list", 3, tflags)) return;
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Instances", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 4.5f);
        ImGui::TableSetupColumn("Source", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 3.5f);
        std::string to_edit, to_select;
        bool exit_edit = false;
        for (size_t i = 0; i < ctx.project.components.size(); ++i) {
            const Component& c = ctx.project.components[i];
            ImGui::PushID((int)i);
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            bool is_active = ctx.active_kind == DocKind::Component && ctx.active_panel == c.name;
            if (is_active) ImGui::PushStyleColor(ImGuiCol_Text, Colors().accent);
            if (ImGui::Selectable(c.name.c_str(), selected_ == c.name,
                                  ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick)) {
                to_select = c.name;
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) to_edit = c.name;
            }
            if (is_active) ImGui::PopStyleColor();
            if (ImGui::BeginPopupContextItem("component_ctx")) {
                to_select = c.name;
                if (ImGui::MenuItem(is_active ? "Done editing" : "Edit")) {
                    if (is_active) exit_edit = true;
                    else to_edit = c.name;
                }
                if (ImGui::MenuItem("Duplicate...")) OpenPrompt(Prompt::Duplicate, UniqueComponentName(ctx.project, c.name + "Copy"));
                if (ImGui::MenuItem("Rename...")) OpenPrompt(Prompt::Rename, c.name);
                if (ImGui::MenuItem("Delete...")) OpenDeletePrompt(ctx, c.name);
                ImGui::Separator();
                if (ImGui::MenuItem("Export...")) OpenImblibExportDialog(ctx, {c.name});
                ImGui::EndPopup();
            }
            ImGui::TableSetColumnIndex(1);
            size_t n = CountInstances(ctx.project, c.name);
            if (n > 0) {
                char buf[32];
                std::snprintf(buf, sizeof(buf), "%d", (int)n);
                ui::Badge(buf, Colors().info);
            } else {
                ui::TextDim("-");
            }
            ImGui::TableSetColumnIndex(2);
            if (IsLibraryComponent(c)) ui::Badge("lib", Colors().warning);
            ImGui::PopID();
        }
        ImGui::EndTable();
        if (!to_select.empty()) selected_ = to_select;
        if (exit_edit) ExitEditMode(ctx);
        else if (!to_edit.empty()) EnterEditMode(ctx, to_edit);
    }

    void DrawLibraries(EditorContext& ctx) {
        if (libs_dirty_) {
            libs_ = ListLibraries(ctx.project);
            libs_dirty_ = false;
        }
        char title[96];
        std::snprintf(title, sizeof(title), "Libraries (%d)###libraries", (int)libs_.size());
        if (!ui::Section(title, false)) return;
        if (libs_.empty()) {
            ui::TextDim("No libraries imported. Use Import... to add an .imblib.");
            return;
        }
        for (size_t i = 0; i < libs_.size(); ++i) {
            const LibraryInfo& lib = libs_[i];
            ImGui::PushID((int)i);
            std::string label = lib.name + (lib.version.empty() ? "" : " " + lib.version);
            bool node_open = ImGui::TreeNodeEx(label.c_str(), ImGuiTreeNodeFlags_SpanAvailWidth);
            ui::Tooltip(lib.file.c_str());
            if (node_open) {
                if (!lib.valid) {
                    ImGui::PushStyleColor(ImGuiCol_Text, Colors().error);
                    ImGui::TextWrapped("%s", lib.error.c_str());
                    ImGui::PopStyleColor();
                }
                if (!lib.components.empty()) ui::TextDim("Components");
                for (const std::string& name : lib.components) {
                    std::string resolved = ctx.FindComponentDoc(name) ? name : lib.name + "." + name;
                    bool present = ctx.FindComponentDoc(resolved) != nullptr;
                    ImGui::Bullet();
                    ImGui::SameLine();
                    if (ImGui::Selectable(resolved.c_str(), selected_ == resolved) && present) selected_ = resolved;
                    if (!present) ui::Tooltip("Not in the project (import again to add it)");
                }
                if (!lib.widgets.empty()) ui::TextDim("Widgets");
                for (const std::string& w : lib.widgets) {
                    ImGui::Bullet();
                    ImGui::SameLine();
                    ImGui::TextUnformatted(w.c_str());
                    ImGui::SameLine();
                    if (ctx.catalog.FindCustom(w.c_str())) ui::Badge("installed", Colors().success);
                    else ui::Badge("missing", Colors().error);
                }
                if (!lib.headers.empty()) ui::TextDim("Headers");
                for (const std::string& h : lib.headers) {
                    ImGui::Bullet();
                    ImGui::SameLine();
                    ImGui::TextUnformatted(h.c_str());
                }
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
    }

    void DrawDetails(EditorContext& ctx, Component& c) {
        ImGui::Spacing();
        ImGui::Separator();
        ui::HeadingText(ctx.Font(FontSlot::Heading), c.name.c_str());
        bool editing = ctx.active_kind == DocKind::Component && ctx.active_panel == c.name;
        if (editing) {
            if (ImGui::Button("Done editing")) ExitEditMode(ctx);
        } else {
            if (ui::AccentButton("Edit")) EnterEditMode(ctx, c.name);
        }
        ui::Tooltip("Open the component tree in the Canvas, Hierarchy and Properties panels");
        ImGui::SameLine();
        if (ImGui::Button("Duplicate")) OpenPrompt(Prompt::Duplicate, UniqueComponentName(ctx.project, c.name + "Copy"));
        ImGui::SameLine();
        if (ImGui::Button("Rename")) OpenPrompt(Prompt::Rename, c.name);
        ImGui::SameLine();
        if (ImGui::Button("Delete")) OpenDeletePrompt(ctx, c.name);
        ui::TextDim("%s", c.file.empty() ? "(unsaved)" : c.file.c_str());
        if (ui::Section("Parameters")) DrawParamsEditor(ctx, c);
        if (ui::Section("Slots")) DrawSlotsEditor(ctx, c);
        char title[64];
        std::snprintf(title, sizeof(title), "Instances (%d)###instances", (int)CountInstances(ctx.project, c.name));
        if (ui::Section(title)) DrawInstancesList(ctx, c.name);
    }

    void EnterEditMode(EditorContext& ctx, const std::string& name) {
        if (!ctx.FindComponentDoc(name)) return;
        if (ctx.active_kind == DocKind::Panel) return_doc_ = ctx.active_panel;
        ctx.SetActiveComponent(name);
        selected_ = name;
        ctx.SetStatus("Editing component " + name);
    }

    void ExitEditMode(EditorContext& ctx) {
        if (ctx.FindPanelDoc(return_doc_)) ctx.SetActivePanel(return_doc_);
        else if (!ctx.project.panels.empty()) ctx.SetActivePanel(ctx.project.panels.front().name);
        else ctx.CloseActiveDocument();
    }

    void OpenPrompt(Prompt kind, const std::string& name) {
        prompt_ = kind;
        prompt_pending_ = true;
        name_buf_ = name;
        error_.clear();
    }

    void OpenDeletePrompt(EditorContext& ctx, const std::string& name) {
        selected_ = name;
        delete_refs_ = FindInstances(ctx.project, name);
        OpenPrompt(Prompt::Delete, name);
    }

    void StartExtract(EditorContext& ctx) {
        Node* root = ctx.ActiveRoot();
        if (!root) return;
        plan_ = ExtractPlan();
        std::string err;
        if (!PlanExtract(*root, ctx.selection, &ctx.catalog, &ctx.project, plan_, &err)) {
            ctx.SetStatus("Cannot create component: " + err, 6.0);
            return;
        }
        std::string base = "NewComponent";
        if (plan_.node_ids.size() == 1)
            if (const Node* n = FindNode(*root, plan_.node_ids.front())) base = n->type + "Component";
        plan_.name = UniqueComponentName(ctx.project, base);
        OpenPrompt(Prompt::Extract, plan_.name);
    }

    static const char* PromptTitle(Prompt p) {
        switch (p) {
        case Prompt::New: return "New component";
        case Prompt::Rename: return "Rename component";
        case Prompt::Duplicate: return "Duplicate component";
        case Prompt::Delete: return "Delete component";
        case Prompt::Extract: return "Create component from selection";
        default: return "Component";
        }
    }

    void DrawPrompt(EditorContext& ctx) {
        if (prompt_ == Prompt::None) return;
        const char* title = PromptTitle(prompt_);
        if (prompt_pending_) {
            ImGui::OpenPopup(title);
            prompt_pending_ = false;
        }
        bool popup_open = true;
        ImGui::SetNextWindowSize(ImVec2(prompt_ == Prompt::Extract ? 640.0f : 420.0f, 0), ImGuiCond_Appearing);
        if (!ImGui::BeginPopupModal(title, &popup_open, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
            if (!ImGui::IsPopupOpen(title)) prompt_ = Prompt::None;
            return;
        }
        bool confirm = false, cancel = false;
        switch (prompt_) {
        case Prompt::Delete: confirm = DrawDeleteBody(ctx); break;
        case Prompt::Extract: confirm = DrawExtractBody(ctx); break;
        default: confirm = DrawNameBody(ctx); break;
        }
        if (!error_.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, Colors().error);
            ImGui::TextWrapped("%s", error_.c_str());
            ImGui::PopStyleColor();
        }
        ImGui::Separator();
        const char* ok_label = prompt_ == Prompt::Delete ? "Delete" : prompt_ == Prompt::Rename ? "Rename" : "Create";
        if (prompt_ == Prompt::Duplicate) ok_label = "Duplicate";
        if (ui::AccentButton(ok_label, ImVec2(110, 0))) confirm = true;
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(90, 0))) cancel = true;
        if (confirm && Confirm(ctx)) cancel = true;
        if (cancel) {
            ImGui::CloseCurrentPopup();
            prompt_ = Prompt::None;
        }
        ImGui::EndPopup();
        if (!popup_open) prompt_ = Prompt::None;
    }

    bool DrawNameBody(EditorContext& ctx) {
        (void)ctx;
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        ImGui::TextUnformatted("Name");
        ImGui::SetNextItemWidth(-FLT_MIN);
        bool enter = ui::InputTextStd("##name", name_buf_, ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
        if (prompt_ == Prompt::New) {
            ImGui::Checkbox("Add a \"content\" slot", &new_with_slot_);
            ImGui::Checkbox("Open in the editor", &new_open_editor_);
        }
        return enter;
    }

    bool DrawDeleteBody(EditorContext& ctx) {
        (void)ctx;
        ImGui::Text("Delete component \"%s\"?", name_buf_.c_str());
        if (!delete_refs_.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, Colors().warning);
            ImGui::Text("%d instance%s will keep referencing it and stop rendering:", (int)delete_refs_.size(),
                        delete_refs_.size() == 1 ? "" : "s");
            ImGui::PopStyleColor();
            size_t shown = std::min<size_t>(delete_refs_.size(), 8);
            for (size_t i = 0; i < shown; ++i) ImGui::BulletText("%s / %s", delete_refs_[i].doc.c_str(), delete_refs_[i].node_id.c_str());
            if (shown < delete_refs_.size()) ImGui::BulletText("... and %d more", (int)(delete_refs_.size() - shown));
            ui::TextDim("Tip: select an instance and use Unpack before deleting.");
        } else {
            ui::TextDim("The component is not used anywhere.");
        }
        return false;
    }

    bool DrawExtractBody(EditorContext& ctx) {
        (void)ctx;
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        ImGui::TextUnformatted("Component name");
        ImGui::SetNextItemWidth(-FLT_MIN);
        bool enter = ui::InputTextStd("##name", name_buf_, ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
        ImGui::Text("%d node%s selected", (int)plan_.node_ids.size(), plan_.node_ids.size() == 1 ? "" : "s");
        if (plan_.candidates.empty()) {
            ui::TextDim("No bindings or text literals found; the component will have no parameters.");
            return enter;
        }
        ImGui::TextUnformatted("Parameters");
        ImGui::SameLine();
        ui::HelpMarker("Bindings always become binding<T> parameters. Checked text literals become string parameters "
                       "with the literal as default; unchecked ones stay hard-coded inside the component.");
        ImGuiTableFlags tflags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp |
                                 ImGuiTableFlags_ScrollY;
        float h = std::min(ImGui::GetTextLineHeightWithSpacing() * ((float)plan_.candidates.size() + 1.5f), 300.0f);
        if (ImGui::BeginTable("candidates", 5, tflags, ImVec2(0, h))) {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("##on", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFrameHeight());
            ImGui::TableSetupColumn("Source", ImGuiTableColumnFlags_WidthStretch, 1.1f);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 1.2f);
            ImGui::TableSetupColumn("Parameter", ImGuiTableColumnFlags_WidthStretch, 1.0f);
            ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthStretch, 0.9f);
            ImGui::TableHeadersRow();
            for (size_t i = 0; i < plan_.candidates.size(); ++i) {
                ExtractCandidate& cand = plan_.candidates[i];
                bool is_binding = cand.kind == ExtractCandidate::Kind::Binding;
                ImGui::PushID((int)i);
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::BeginDisabled(is_binding);
                ImGui::Checkbox("##on", &cand.enabled);
                ImGui::EndDisabled();
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%s%s%s", cand.node_id.c_str(), is_binding ? " bind " : " . ", cand.key.c_str());
                ImGui::TableSetColumnIndex(2);
                ImGui::TextUnformatted(cand.value.c_str());
                ImGui::TableSetColumnIndex(3);
                std::string pname = cand.param_name;
                ImGui::SetNextItemWidth(-FLT_MIN);
                if (ui::InputTextStd("##param", pname)) {
                    std::string old = cand.param_name;
                    for (ExtractCandidate& other : plan_.candidates)
                        if (other.kind == cand.kind && other.value == cand.value && other.param_name == old) other.param_name = pname;
                }
                ImGui::TableSetColumnIndex(4);
                ui::TextDim("%s", cand.param_type.c_str());
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        return enter;
    }

    bool Confirm(EditorContext& ctx) {
        switch (prompt_) {
        case Prompt::New: return DoNew(ctx);
        case Prompt::Rename: return DoRename(ctx);
        case Prompt::Duplicate: return DoDuplicate(ctx);
        case Prompt::Delete: return DoDelete(ctx);
        case Prompt::Extract: return DoExtract(ctx);
        default: return true;
        }
    }

    bool DoNew(EditorContext& ctx) {
        if (!NewComponent(ctx, name_buf_, new_with_slot_, &error_)) return false;
        selected_ = name_buf_;
        if (new_open_editor_) EnterEditMode(ctx, name_buf_);
        return true;
    }

    bool DoDuplicate(EditorContext& ctx) {
        if (!DuplicateComponent(ctx, selected_, name_buf_, &error_)) return false;
        selected_ = name_buf_;
        return true;
    }

    bool DoRename(EditorContext& ctx) {
        if (!RenameComponent(ctx, selected_, name_buf_, &error_)) return false;
        selected_ = name_buf_;
        return true;
    }

    bool DoDelete(EditorContext& ctx) {
        if (!ctx.FindComponentDoc(name_buf_)) return true;
        if (ctx.active_kind == DocKind::Component && ctx.active_panel == name_buf_) ExitEditMode(ctx);
        DeleteComponent(ctx, name_buf_);
        if (selected_ == name_buf_) selected_.clear();
        return true;
    }

    bool DoExtract(EditorContext& ctx) {
        plan_.name = name_buf_;
        if (!ExtractComponent(ctx, plan_, nullptr, &error_)) return false;
        selected_ = name_buf_;
        ctx.SetStatus("Created component " + name_buf_ + " with " + std::to_string(plan_.candidates.size()) + " candidate parameters");
        return true;
    }

    Prompt prompt_ = Prompt::None;
    bool prompt_pending_ = false;
    bool new_with_slot_ = true, new_open_editor_ = true;
    bool libs_dirty_ = true;
    std::string selected_, return_doc_, last_active_, name_buf_, error_;
    ExtractPlan plan_;
    std::vector<InstanceRef> delete_refs_;
    std::vector<LibraryInfo> libs_;
};

}  // namespace

IMFORGE_REGISTER_PANEL(ComponentsPanel, 400);

}  // namespace imforge::builder
